#include "SnowDeformation.h"

#include "Globals.h"
#include "Utils/D3D.h"
#include "Utils/Game.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	SnowDeformation::Settings,
	EnableSnowDeformation,
	StampRadius,
	RefillTime,
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

	for (uint i = 0; i < 2; i++) {
		deformationTextures[i] = new Texture2D(texDesc, i == 0 ? "SnowDeformation::DeformationMap0" : "SnowDeformation::DeformationMap1");
		deformationTextures[i]->CreateSRV(srvDesc);
		deformationTextures[i]->CreateUAV(uavDesc);
	}
}

SnowDeformation::SettingsGPU SnowDeformation::GetCommonBufferData(bool a_inWorld)
{
	// Advance the window once per frame, and only from the in-world upload —
	// the reflections/early uploads can carry probe cameras whose position
	// must not steer the deformation window.
	static Util::FrameChecker frameChecker;
	if (a_inWorld && frameChecker.IsNewFrame()) {
		// Snap to whole texels so scrolling never resamples the map. Use the
		// cached FrameBuffer camera position: it is exactly what the lighting
		// pixel shader sees as CameraPosAdjust, so map and terrain agree.
		auto eyePosFB = globals::game::frameBufferCached.GetCameraPosAdjust();
		const float deformTexel = deformWorldSize / kTextureDim;
		float2 desiredOrigin = {
			std::floor((eyePosFB.x - deformWorldSize * 0.5f) / deformTexel) * deformTexel,
			std::floor((eyePosFB.y - deformWorldSize * 0.5f) / deformTexel) * deformTexel
		};

		pendingScrollDelta.x += (int)std::lround((desiredOrigin.x - windowOrigin.x) / deformTexel);
		pendingScrollDelta.y += (int)std::lround((desiredOrigin.y - windowOrigin.y) / deformTexel);
		windowOrigin = desiredOrigin;
	}

	SettingsGPU data{};
	data.WindowOrigin = windowOrigin;
	data.InvWorldSize = 1.0f / deformWorldSize;
	data.EnableSnowDeformation = settings.EnableSnowDeformation;
	data.DebugTerrainOverlay = debugTerrainOverlay ? 1u : 0u;
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

void SnowDeformation::Prepass()
{
	ApplyRangeSettings();

	auto context = globals::d3d::context;

	// The lighting shader samples t101 whenever the feature is compiled in, so
	// keep the SRV bound even while paused or disabled (the shader also checks
	// EnableSnowDeformation from FeatureData before using it).
	ID3D11ShaderResourceView* deformationSRV = GetDeformationSRV();
	context->PSSetShaderResources(101, 1, &deformationSRV);

	if (!settings.EnableSnowDeformation)
		return;

	auto ui = globals::game::ui;
	if (ui && ui->GameIsPaused())
		return;

	PerFrame perFrameData{};

	// The window origin was advanced in GetCommonBufferData (during
	// UpdateSharedData) so the FeatureData constant buffer and the texture we
	// scroll here describe the same frame. We only consume the stored state.
	perFrameData.ScrollDelta = pendingScrollDelta;
	pendingScrollDelta = { 0, 0 };

	perFrameData.WindowOrigin = windowOrigin;
	perFrameData.TexelSize = deformWorldSize / kTextureDim;

	float deltaTime = *globals::game::deltaTime;
	perFrameData.RefillAmount = settings.RefillTime > 0.0f ? deltaTime / settings.RefillTime : 0.0f;

	perFrameData.ClearMap = clearRequested;
	clearRequested = false;

	GatherStamps(perFrameData);

	perFrame->Update(perFrameData);

	uint previousTexture = currentTexture;
	currentTexture = 1 - currentTexture;

	{
		ID3D11Buffer* buffers[1] = { perFrame->CB() };
		context->CSSetConstantBuffers(0, 1, buffers);

		ID3D11ShaderResourceView* srvs[] = { deformationTextures[previousTexture]->srv.get() };
		context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11UnorderedAccessView* uavs[] = { deformationTextures[currentTexture]->uav.get() };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		context->CSSetShader(GetDeformationUpdateCS(), nullptr, 0);
		globals::profiler->BeginPass("SnowDeformation::DeformationUpdate");
		context->Dispatch(kTextureDim / 8, kTextureDim / 8, 1);
		globals::profiler->EndPass();
	}

	context->CSSetShader(nullptr, nullptr, 0);

	ID3D11Buffer* nullBuffer = nullptr;
	context->CSSetConstantBuffers(0, 1, &nullBuffer);

	ID3D11ShaderResourceView* nullSrvs[1] = { nullptr };
	context->CSSetShaderResources(0, 1, nullSrvs);

	ID3D11UnorderedAccessView* nullUavs[1] = { nullptr };
	context->CSSetUnorderedAccessViews(0, 1, nullUavs, nullptr);

	// Rebind: after the ping-pong flip this points at the freshly written map.
	deformationSRV = GetDeformationSRV();
	context->PSSetShaderResources(101, 1, &deformationSRV);
}

ID3D11ComputeShader* SnowDeformation::GetDeformationUpdateCS()
{
	if (!deformationUpdateCS) {
		logger::debug("Compiling DeformationUpdateCS");
		deformationUpdateCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\SnowDeformation\\DeformationUpdateCS.hlsl", {}, "cs_5_0"));
	}
	return deformationUpdateCS;
}

void SnowDeformation::ClearShaderCache()
{
	if (deformationUpdateCS)
		deformationUpdateCS->Release();
	deformationUpdateCS = nullptr;
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
