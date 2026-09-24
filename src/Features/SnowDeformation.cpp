#include "SnowDeformation.h"

#include "Globals.h"
#include "Utils/D3D.h"
#include "Utils/Game.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	SnowDeformation::Settings,
	EnableSnowDeformation,
	StampRadius,
	RefillTime,
	RefillOnlyWhenSnowing,
	RangeTrenchesM)

void SnowDeformation::SetupResources()
{
	perFrame = new ConstantBuffer(ConstantBufferDesc<PerFrame>(), "SnowDeformation::PerFrame");

	D3D11_TEXTURE2D_DESC texDesc = {
		.Width = kTextureDim,
		.Height = kTextureDim,
		.MipLevels = 1,
		.ArraySize = 1,
		.Format = DXGI_FORMAT_R16_FLOAT,
		.SampleDesc = { .Count = 1 },
		.Usage = D3D11_USAGE_DEFAULT,
		.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS
	};

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
		.Format = texDesc.Format,
		.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
		.Texture2D = {
			.MostDetailedMip = 0,
			.MipLevels = 1 }
	};

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
		.Format = texDesc.Format,
		.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
		.Texture2D = { .MipSlice = 0 }
	};

	deformationTexture = new Texture2D(texDesc, "SnowDeformation::DeformationMap");
	deformationTexture->CreateSRV(srvDesc);
	deformationTexture->CreateUAV(uavDesc);

	auto device = globals::d3d::device;
	D3D11_BUFFER_DESC tileDesc{};
	tileDesc.ByteWidth = kStampTileCap * sizeof(uint32_t);
	tileDesc.Usage = D3D11_USAGE_DYNAMIC;
	tileDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	tileDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	tileDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	tileDesc.StructureByteStride = sizeof(uint32_t);
	DX::ThrowIfFailed(device->CreateBuffer(&tileDesc, nullptr, stampTileBuffer.put()));
	Util::SetResourceName(stampTileBuffer.get(), "SnowDeformation::StampTiles");

	D3D11_SHADER_RESOURCE_VIEW_DESC tileSrvDesc{};
	tileSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
	tileSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	tileSrvDesc.Buffer.NumElements = kStampTileCap;
	DX::ThrowIfFailed(device->CreateShaderResourceView(stampTileBuffer.get(), &tileSrvDesc, stampTileSRV.put()));
	Util::SetResourceName(stampTileSRV.get(), "SnowDeformation::StampTiles SRV");

	constexpr uint tilesPerAxis = kTextureDim / 8;
	stampTileBits.resize(tilesPerAxis * tilesPerAxis / 32);
	stampTiles.reserve(kStampTileCap);
}

SnowDeformation::SettingsGPU SnowDeformation::GetCommonBufferData(bool a_inWorld)
{
	// Advance the window once per frame and only from the in-world upload:
	// reflection/early uploads carry probe cameras that must not steer it.
	// Frozen while disabled so the map and its origin stay in step; the first
	// frame back scrolls by the whole gap.
	static Util::FrameChecker frameChecker;
	if (a_inWorld && settings.EnableSnowDeformation && frameChecker.IsNewFrame()) {
		// Snap to whole texels so scrolling never resamples the map. The
		// cached FrameBuffer camera position is what the lighting pixel
		// shader sees as CameraPosAdjust, so map and terrain agree.
		auto eyePosFB = globals::game::frameBufferCached.GetCameraPosAdjust();
		const float deformTexel = deformWorldSize / kTextureDim;
		const DirectX::XMINT2 originTexel = {
			(int)std::floor((eyePosFB.x - deformWorldSize * 0.5f) / deformTexel),
			(int)std::floor((eyePosFB.y - deformWorldSize * 0.5f) / deformTexel)
		};

		pendingScrollDelta.x += originTexel.x - windowOriginTexel.x;
		pendingScrollDelta.y += originTexel.y - windowOriginTexel.y;
		windowOriginTexel = originTexel;
		windowOrigin = { originTexel.x * deformTexel, originTexel.y * deformTexel };
		// A texel's physical address is its world texel index, masked.
		constexpr int mask = (int)kTextureDim - 1;
		mapOrigin = { originTexel.x & mask, originTexel.y & mask };
	}

	SettingsGPU data{};
	data.WindowOrigin = windowOrigin;
	data.InvWorldSize = 1.0f / deformWorldSize;
	data.EnableSnowDeformation = settings.EnableSnowDeformation;
	data.DebugTerrainOverlay = debugTerrainOverlay ? 1u : 0u;
	data.MapOrigin = mapOrigin;
	return data;
}

void SnowDeformation::ApplyRangeSettings()
{
	// Trench window: content is scale-relative, so a resize clears the map.
	if (trenchRangeDirty || !rangeInitApplied) {
		float newWorldSize = std::clamp(settings.RangeTrenchesM, 29.0f, 200.0f) * 2.0f * kUnitsPerMeter;
		if (std::abs(newWorldSize - deformWorldSize) > 1.0f) {
			deformWorldSize = newWorldSize;
			clearRequested = true;
		}
		trenchRangeDirty = false;
	}
	rangeInitApplied = true;
}

bool SnowDeformation::BuildStampTiles(const PerFrame& a_data)
{
	constexpr int dim = (int)kTextureDim;
	constexpr int mask = dim - 1;
	constexpr uint tilesPerAxis = kTextureDim / 8;
	std::fill(stampTileBits.begin(), stampTileBits.end(), 0u);
	stampTiles.clear();

	const int origin[2] = { a_data.MapOrigin.x, a_data.MapOrigin.y };
	const float windowMin[2] = { a_data.WindowOrigin.x, a_data.WindowOrigin.y };
	for (uint i = 0; i < a_data.StampCount; i++) {
		const auto& stamp = a_data.Stamps[i];
		const auto& end = a_data.StampEnds[i];
		const float radius = end.z;
		const float boxMin[2] = { std::min(stamp.x, end.x) - radius, std::min(stamp.y, end.y) - radius };
		const float boxMax[2] = { std::max(stamp.x, end.x) + radius, std::max(stamp.y, end.y) + radius };

		// Logical texel range per axis, then its physical run(s): a range can
		// wrap across the torus seam into two.
		int runs[2][2][2];
		int runCount[2];
		bool inside = true;
		for (int axis = 0; axis < 2 && inside; axis++) {
			const int l0 = std::max((int)std::floor((boxMin[axis] - windowMin[axis]) / a_data.TexelSize), 0);
			const int l1 = std::min((int)std::floor((boxMax[axis] - windowMin[axis]) / a_data.TexelSize), dim - 1);
			if (l0 > l1) {
				inside = false;
				break;
			}
			const int p = (l0 + origin[axis]) & mask;
			const int len = l1 - l0 + 1;
			const int run = std::min(len, dim - p);
			runs[axis][0][0] = p;
			runs[axis][0][1] = p + run - 1;
			runCount[axis] = 1;
			if (run < len) {
				runs[axis][1][0] = 0;
				runs[axis][1][1] = len - run - 1;
				runCount[axis] = 2;
			}
		}
		if (!inside)
			continue;

		for (int ry = 0; ry < runCount[1]; ry++)
			for (int rx = 0; rx < runCount[0]; rx++)
				for (int ty = runs[1][ry][0] >> 3; ty <= runs[1][ry][1] >> 3; ty++)
					for (int tx = runs[0][rx][0] >> 3; tx <= runs[0][rx][1] >> 3; tx++) {
						const uint32_t index = (uint32_t)ty * tilesPerAxis + (uint32_t)tx;
						uint32_t& word = stampTileBits[index >> 5];
						const uint32_t bit = 1u << (index & 31);
						if (word & bit)
							continue;
						word |= bit;
						if (stampTiles.size() >= kStampTileCap)
							return false;
						stampTiles.push_back((uint32_t)tx | ((uint32_t)ty << 16));
					}
	}
	return true;
}

void SnowDeformation::Prepass()
{
	ApplyRangeSettings();

	auto context = globals::d3d::context;

	// Keep t101 bound even while paused or disabled: the lighting shader
	// samples it whenever the feature is compiled in (and gates on
	// EnableSnowDeformation from FeatureData).
	ID3D11ShaderResourceView* deformationSRV = GetDeformationSRV();
	context->PSSetShaderResources(101, 1, &deformationSRV);

	if (!settings.EnableSnowDeformation)
		return;

	auto ui = globals::game::ui;
	if (ui && ui->GameIsPaused())
		return;

	// Before the scroll is consumed.
	if (!EnsureUpdateShaders())
		return;

	PerFrame perFrameData{};
	perFrameData.WindowOrigin = windowOrigin;
	perFrameData.MapOrigin = mapOrigin;
	perFrameData.TexelSize = deformWorldSize / kTextureDim;

	// The window origin was advanced in GetCommonBufferData (during
	// UpdateSharedData); here only the texels it reassigned are cleared. A
	// scroll of +n brings n new texels in at the high end of that axis.
	{
		constexpr int dim = (int)kTextureDim;
		const DirectX::XMINT2 scroll = pendingScrollDelta;
		pendingScrollDelta = { 0, 0 };
		uint rectCount = 0;
		if (clearRequested || std::abs(scroll.x) >= dim || std::abs(scroll.y) >= dim) {
			perFrameData.RingRects[rectCount++] = { 0, 0, dim, dim };
		} else {
			if (scroll.x != 0)
				perFrameData.RingRects[rectCount++] = { scroll.x > 0 ? dim - scroll.x : 0, 0, std::abs(scroll.x), dim };
			if (scroll.y != 0)
				perFrameData.RingRects[rectCount++] = { 0, scroll.y > 0 ? dim - scroll.y : 0, dim, std::abs(scroll.y) };
		}
		clearRequested = false;
		for (uint i = 0; i < rectCount; i++)
			perFrameData.RingTotalTexels += (uint)(perFrameData.RingRects[i].z * perFrameData.RingRects[i].w);
	}

	// Refill sweeps the map one band of rows per frame. A frame's refill
	// (~1e-5 of full depth) is below what the R16F map stores near full depth
	// and would round back, so it is banked and each sweep spends the bank.
	{
		float deltaTime = *globals::game::deltaTime;
		// With RefillOnlyWhenSnowing, snow only recovers while the current
		// weather carries snow; interiors have no sky and do not refill.
		bool refillActive = !settings.RefillOnlyWhenSnowing;
		if (!refillActive)
			if (auto* sky = RE::Sky::GetSingleton())
				if (auto* weather = sky->currentWeather)
					refillActive = weather->data.flags.any(RE::TESWeather::WeatherDataFlag::kSnow);
		refillBank += (refillActive && settings.RefillTime > 0.0f) ? deltaTime / settings.RefillTime : 0.0f;
		if (refillBand < 0 && refillBank >= kRefillStep) {
			refillSweepAmount = refillBank;
			refillBank = 0.0f;
			refillBand = 0;
		}
		if (refillBand >= 0) {
			constexpr uint rowsPerBand = kTextureDim / kRefillBands;
			perFrameData.RefillAmount = refillSweepAmount;
			perFrameData.RefillRowStart = (uint)refillBand * rowsPerBand;
			perFrameData.RefillRowCount = rowsPerBand;
			if (++refillBand >= (int)kRefillBands)
				refillBand = -1;
		}
	}

	GatherStamps(perFrameData);
	const bool tiled = BuildStampTiles(perFrameData);

	// Always timed, so an idle frame reads as zero instead of the last busy one.
	globals::profiler->BeginPass("SnowDeformation::DeformationUpdate");
	const bool ringWork = perFrameData.RingTotalTexels > 0;
	const bool refillWork = perFrameData.RefillRowCount > 0;
	const bool stampWork = perFrameData.StampCount > 0 && (!tiled || !stampTiles.empty());
	if (ringWork || refillWork || stampWork) {
		perFrame->Update(perFrameData);

		ID3D11ShaderResourceView* nullSRV = nullptr;
		context->PSSetShaderResources(101, 1, &nullSRV);

		ID3D11Buffer* buffers[1] = { perFrame->CB() };
		context->CSSetConstantBuffers(0, 1, buffers);
		ID3D11UnorderedAccessView* uav = deformationTexture->uav.get();
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

		if (ringWork) {
			context->CSSetShader(ringCS, nullptr, 0);
			context->Dispatch((perFrameData.RingTotalTexels + 255) / 256, 1, 1);
		}
		if (refillWork) {
			context->CSSetShader(refillCS, nullptr, 0);
			context->Dispatch(kTextureDim / 8, perFrameData.RefillRowCount / 8, 1);
		}
		if (stampWork) {
			D3D11_MAPPED_SUBRESOURCE mapped;
			if (tiled && SUCCEEDED(context->Map(stampTileBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
				memcpy(mapped.pData, stampTiles.data(), stampTiles.size() * sizeof(uint32_t));
				context->Unmap(stampTileBuffer.get(), 0);
				ID3D11ShaderResourceView* tileSRV = stampTileSRV.get();
				context->CSSetShaderResources(0, 1, &tileSRV);
				context->CSSetShader(stampCS, nullptr, 0);
				context->Dispatch((UINT)stampTiles.size(), 1, 1);
			} else if (!tiled) {
				context->CSSetShader(stampAllCS, nullptr, 0);
				context->Dispatch(kTextureDim / 8, kTextureDim / 8, 1);
			}
		}

		context->CSSetShader(nullptr, nullptr, 0);
		ID3D11Buffer* nullBuffer = nullptr;
		context->CSSetConstantBuffers(0, 1, &nullBuffer);
		context->CSSetShaderResources(0, 1, &nullSRV);
		ID3D11UnorderedAccessView* nullUAV = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

		context->PSSetShaderResources(101, 1, &deformationSRV);
	}
	globals::profiler->EndPass();
}

bool SnowDeformation::EnsureUpdateShaders()
{
	if (updateShadersFailed)
		return false;
	if (ringCS && refillCS && stampCS && stampAllCS)
		return true;

	auto compile = [](const char* a_entry) {
		logger::debug("Compiling DeformationUpdateCS:{}", a_entry);
		return static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\SnowDeformation\\DeformationUpdateCS.hlsl", {}, "cs_5_0", a_entry));
	};
	if (!ringCS)
		ringCS = compile("RingCS");
	if (!refillCS)
		refillCS = compile("RefillCS");
	if (!stampCS)
		stampCS = compile("StampCS");
	if (!stampAllCS)
		stampAllCS = compile("StampAllCS");

	if (ringCS && refillCS && stampCS && stampAllCS)
		return true;
	updateShadersFailed = true;
	logger::error("[SNOW DEFORMATION] DeformationUpdateCS failed to compile, deformation disabled until shaders reload");
	return false;
}

void SnowDeformation::ClearShaderCache()
{
	updateShadersFailed = false;
	for (auto* shader : { &ringCS, &refillCS, &stampCS, &stampAllCS }) {
		if (*shader)
			(*shader)->Release();
		*shader = nullptr;
	}
}

void SnowDeformation::LoadSettings(json& o_json)
{
	settings = o_json;
}

void SnowDeformation::SaveSettings(json& o_json)
{
	o_json = settings;
}

void SnowDeformation::RestoreDefaultSettings()
{
	settings = {};
	clearRequested = true;
}
