#include "SSRT.h"

#include "Deferred.h"
#include "State.h"
#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	SSRT::Settings,
	Enabled,
	EnableVanillaSSAO,
	GIIntensity,
	AOIntensity,
	ProbeSpawnTileSize,
	MaxHiZSteps,
	HiZThickness,
	HiZMaxDistance)

////////////////////////////////////////////////////////////////////////////////////

void SSRT::RestoreDefaultSettings()
{
	settings = {};
	recompileFlag = true;
}

void SSRT::DrawSettings()
{
	if (!ShadersOK())
		ImGui::TextColored({ 1, 0, 0, 1 }, "Compute shaders failed to compile!");

	///////////////////////////////
	ImGui::SeparatorText("Toggles");

	if (ImGui::BeginTable("Toggles", 2)) {
		ImGui::TableNextColumn();
		ImGui::Checkbox("Enabled", &settings.Enabled);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Enable screen-space probe GI.");
		}

		ImGui::TableNextColumn();
		ImGui::Checkbox("Vanilla SSAO", &settings.EnableVanillaSSAO);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Enable Skyrim's built-in SSAO.");
		}

		ImGui::EndTable();
	}

	///////////////////////////////
	ImGui::SeparatorText("GI");

	{
		auto giGuard = Util::DisableGuard(!settings.Enabled);

		ImGui::SliderFloat("GI Intensity", &settings.GIIntensity, 0.f, 10.f, "%.2f");
		ImGui::SliderFloat("AO Intensity", &settings.AOIntensity, 0.f, 4.f, "%.2f");
	}

	///////////////////////////////
	ImGui::SeparatorText("Hi-Z Tracing");

	{
		auto hizGuard = Util::DisableGuard(!settings.Enabled);

		ImGui::SliderInt("Max Steps", (int*)&settings.MaxHiZSteps, 16, 128);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Maximum number of Hi-Z tracing steps per ray.");
		}

		ImGui::SliderFloat("Thickness", &settings.HiZThickness, 0.001f, 0.5f, "%.3f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Depth thickness bias for hit detection.");
		}

		ImGui::SliderFloat("Max Distance", &settings.HiZMaxDistance, 50.f, 2000.f, "%.0f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Maximum world-space trace distance.\n");
		}
	}

	///////////////////////////////
	ImGui::SeparatorText("Debug");

	ImGui::Checkbox("Show Raw Probes", &debugProbes);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Output raw probe buffer to deferred composite, bypassing interpolation and denoising.");
	}

	if (ImGui::TreeNode("Buffer Viewer")) {
		static float debugRescale = .3f;
		ImGui::SliderFloat("View Resize", &debugRescale, 0.f, 1.f);

		BUFFER_VIEWER_NODE(texGIOcclusion, debugRescale)
		BUFFER_VIEWER_NODE_TITLE(texProbeBuffer[0], "ProbeBuffer", debugRescale)
		BUFFER_VIEWER_NODE_TITLE(texProbeMask[0], "ProbeMask", debugRescale)
		BUFFER_VIEWER_NODE(texDepthPyramid, debugRescale)
		BUFFER_VIEWER_NODE(texRadiancePyramid, debugRescale)
		BUFFER_VIEWER_NODE_TITLE(texGIDenoiserColor[0], "DenoiserColor", debugRescale)

		ImGui::TreePop();
	}
}

void SSRT::LoadSettings(json& o_json)
{
	settings = o_json;
	recompileFlag = true;
}

void SSRT::SaveSettings(json& o_json)
{
	o_json = settings;
}

////////////////////////////////////////////////////////////////////////////////////
// Helper: create a structured buffer with SRV + UAV
////////////////////////////////////////////////////////////////////////////////////

static eastl::unique_ptr<Buffer> CreateStructuredBuffer(uint stride, uint count, bool indirectArgs = false)
{
	D3D11_BUFFER_DESC desc{};
	desc.ByteWidth = stride * count;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	if (indirectArgs)
		desc.MiscFlags |= D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
	desc.StructureByteStride = stride;

	auto buf = eastl::make_unique<Buffer>(desc);

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = count;
	buf->CreateSRV(srvDesc);

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.NumElements = count;
	buf->CreateUAV(uavDesc);

	return buf;
}

static eastl::unique_ptr<Buffer> CreateRawIndirectBuffer(uint byteWidth)
{
	D3D11_BUFFER_DESC desc{};
	desc.ByteWidth = byteWidth;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;

	auto buf = eastl::make_unique<Buffer>(desc);

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.NumElements = byteWidth / 4;
	uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
	buf->CreateUAV(uavDesc);

	return buf;
}

////////////////////////////////////////////////////////////////////////////////////

void SSRT::SetupResources()
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	// Get screen dimensions from main render target
	D3D11_TEXTURE2D_DESC mainTexDesc{};
	auto mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	mainTex.texture->GetDesc(&mainTexDesc);

	uint screenWidth = mainTexDesc.Width;
	uint screenHeight = mainTexDesc.Height;
	DXGI_FORMAT radianceFormat = mainTexDesc.Format;

	// Compute probe grid dimensions
	probeCountX = (screenWidth + probeSize - 1) / probeSize;
	probeCountY = (screenHeight + probeSize - 1) / probeSize;
	maxProbeCount = probeCountX * probeCountY;

	uint probeBufferWidth = probeCountX * probeSize;
	uint probeBufferHeight = probeCountY * probeSize;

	uint spawnTileSize = settings.ProbeSpawnTileSize;
	spawnDimsX = (screenWidth + spawnTileSize - 1) / spawnTileSize;
	spawnDimsY = (screenHeight + spawnTileSize - 1) / spawnTileSize;
	maxSpawnCount = spawnDimsX * spawnDimsY;
	maxRayCount = maxSpawnCount * probeSize * probeSize;

	// Mip count for probe mask: floor(log2(max(probeCountX, probeCountY))) + 1
	uint maxDim = std::max(probeCountX, probeCountY);
	probeMaskMipCount = 1;
	while ((1u << probeMaskMipCount) <= maxDim)
		probeMaskMipCount++;

	logger::debug("SSRT: screen={}x{}, probeCount={}x{}, spawnCount={}, maxRays={}, maskMips={}",
		screenWidth, screenHeight, probeCountX, probeCountY, maxSpawnCount, maxRayCount, probeMaskMipCount);

	// Constant buffer
	ssrtCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<SSRTCB>());

	//////////////////////////////////////////////////////////////////////////////////
	// Textures
	//////////////////////////////////////////////////////////////////////////////////

	auto createTex2D = [&](uint w, uint h, DXGI_FORMAT fmt, uint mips, uint bindFlags, uint miscFlags = 0) -> eastl::unique_ptr<Texture2D> {
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = w;
		desc.Height = h;
		desc.MipLevels = mips;
		desc.ArraySize = 1;
		desc.Format = fmt;
		desc.SampleDesc = { 1, 0 };
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = bindFlags;
		desc.MiscFlags = miscFlags;

		auto tex = eastl::make_unique<Texture2D>(desc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = fmt;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = mips;
		tex->CreateSRV(srvDesc);

		if (bindFlags & D3D11_BIND_UNORDERED_ACCESS) {
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
			uavDesc.Format = fmt;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;
			tex->CreateUAV(uavDesc);
		}

		return tex;
	};

	constexpr uint SRV_UAV = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	// Final output
	texGIOcclusion = createTex2D(screenWidth, screenHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, 1, SRV_UAV);

	// Probe buffers (ping-pong)
	for (int i = 0; i < 2; i++) {
		texProbeBuffer[i] = createTex2D(probeBufferWidth, probeBufferHeight,
			DXGI_FORMAT_R16G16B16A16_FLOAT, 1, SRV_UAV);
	}

	// Probe mask with mip chain (ping-pong)
	for (int i = 0; i < 2; i++) {
		texProbeMask[i] = createTex2D(probeCountX, probeCountY,
			DXGI_FORMAT_R32_UINT, probeMaskMipCount, SRV_UAV);

		// Create per-mip UAVs
		probeMaskMipUAVs[i].clear();
		for (uint m = 0; m < probeMaskMipCount; m++) {
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
			uavDesc.Format = DXGI_FORMAT_R32_UINT;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = m;

			winrt::com_ptr<ID3D11UnorderedAccessView> mipUav;
			DX::ThrowIfFailed(device->CreateUnorderedAccessView(
				texProbeMask[i]->resource.get(), &uavDesc, mipUav.put()));
			probeMaskMipUAVs[i].push_back(mipUav);
		}
	}

	// Depth pyramid for Hi-Z tracing (R32_FLOAT, MIN downsample)
	{
		depthPyramidMipCount = 1;
		uint w = screenWidth, h = screenHeight;
		while (w > 1 || h > 1) {
			w = std::max(1u, w >> 1);
			h = std::max(1u, h >> 1);
			depthPyramidMipCount++;
		}
		depthPyramidMipCount = std::min(depthPyramidMipCount, 10u);

		texDepthPyramid = createTex2D(screenWidth, screenHeight,
			DXGI_FORMAT_R32_FLOAT, depthPyramidMipCount, SRV_UAV);

		// Per-mip SRVs and UAVs for pyramid build
		depthPyramidMipSRVs.clear();
		depthPyramidMipUAVs.clear();
		for (uint m = 0; m < depthPyramidMipCount; m++) {
			// SRV for single mip
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = m;
			srvDesc.Texture2D.MipLevels = 1;
			winrt::com_ptr<ID3D11ShaderResourceView> mipSrv;
			DX::ThrowIfFailed(device->CreateShaderResourceView(
				texDepthPyramid->resource.get(), &srvDesc, mipSrv.put()));
			depthPyramidMipSRVs.push_back(mipSrv);

			// UAV for single mip
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
			uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = m;
			winrt::com_ptr<ID3D11UnorderedAccessView> mipUav;
			DX::ThrowIfFailed(device->CreateUnorderedAccessView(
				texDepthPyramid->resource.get(), &uavDesc, mipUav.put()));
			depthPyramidMipUAVs.push_back(mipUav);
		}
	}

	// Radiance pyramid (for reading scene color at Hi-Z hit points)
	{
		constexpr uint RADIANCE_MIPS = 5;
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = screenWidth;
		desc.Height = screenHeight;
		desc.MipLevels = RADIANCE_MIPS;
		desc.ArraySize = 1;
		desc.Format = radianceFormat;
		desc.SampleDesc = { 1, 0 };
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
		texRadiancePyramid = eastl::make_unique<Texture2D>(desc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = radianceFormat;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = RADIANCE_MIPS;
		texRadiancePyramid->CreateSRV(srvDesc);
	}

	// GI denoiser textures
	for (int i = 0; i < 2; i++) {
		texGIDenoiserColor[i] = createTex2D(screenWidth, screenHeight,
			DXGI_FORMAT_R16G16B16A16_FLOAT, 1, SRV_UAV);
		texGIDenoiserColorDelta[i] = createTex2D(screenWidth, screenHeight,
			DXGI_FORMAT_R32_FLOAT, 1, SRV_UAV);
	}
	texGIDenoiserBlurMask = createTex2D(screenWidth, screenHeight,
		DXGI_FORMAT_R32_FLOAT, 1, SRV_UAV);

	// Previous frame depth + normals
	texPrevDepth = createTex2D(screenWidth, screenHeight,
		DXGI_FORMAT_R32_FLOAT, 1, D3D11_BIND_SHADER_RESOURCE);
	texPrevNormals = createTex2D(screenWidth, screenHeight,
		DXGI_FORMAT_R10G10B10A2_UNORM, 1, D3D11_BIND_SHADER_RESOURCE);

	//////////////////////////////////////////////////////////////////////////////////
	// Structured Buffers
	//////////////////////////////////////////////////////////////////////////////////

	for (int i = 0; i < 2; i++) {
		bufProbeSH[i] = CreateStructuredBuffer(8, 9 * maxProbeCount);    // uint2 = 8 bytes
		bufProbeSpawn[i] = CreateStructuredBuffer(4, maxSpawnCount);      // uint = 4 bytes
	}

	bufProbeSpawnScan = CreateStructuredBuffer(4, maxSpawnCount);
	bufProbeSpawnIndex = CreateStructuredBuffer(4, maxSpawnCount);
	bufProbeSpawnSample = CreateStructuredBuffer(8, maxRayCount);         // uint2
	bufProbeSpawnRadiance = CreateStructuredBuffer(8, maxRayCount);       // uint2

	bufEmptyTile = CreateStructuredBuffer(4, maxProbeCount);
	bufEmptyTileCount = CreateStructuredBuffer(4, 1);
	bufOverrideTile = CreateStructuredBuffer(4, maxSpawnCount);
	bufOverrideTileCount = CreateStructuredBuffer(4, 1);

	uint prefixBlockCount = (maxSpawnCount + 127) / 128;
	bufPrefixSumBlockTotals = CreateStructuredBuffer(4, std::max(prefixBlockCount, 1u));

	// Indirect dispatch args: 3 uints (12 bytes), needs DRAWINDIRECT_ARGS flag
	bufDispatchIndirectArgs = CreateRawIndirectBuffer(12);

	//////////////////////////////////////////////////////////////////////////////////
	// Samplers
	//////////////////////////////////////////////////////////////////////////////////

	{
		D3D11_SAMPLER_DESC samplerDesc = {
			.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
			.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
			.MaxAnisotropy = 1,
			.MinLOD = 0,
			.MaxLOD = D3D11_FLOAT32_MAX
		};
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, linearClampSampler.put()));

		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, pointClampSampler.put()));
	}

	CompileComputeShaders();
}

void SSRT::ClearShaderCache()
{
	static const std::vector<winrt::com_ptr<ID3D11ComputeShader>*> shaderPtrs = {
		&pyramidCopyCS, &buildDepthPyramidCS,
		&clearCountersCS, &clearProbeMaskCS,
		&reprojectScreenProbesCS, &filterProbeMaskCS,
		&spawnScreenProbesCS, &compactScreenProbesCS,
		&patchScreenProbesCS, &sampleScreenProbesCS,
		&populateScreenProbesCS, &blendScreenProbesCS,
		&filterScreenProbesCS, &projectScreenProbesCS,
		&interpolateScreenProbesCS,
		&prefixSumLocalCS, &prefixSumBlockCS, &prefixSumFinalizeCS,
		&prepareDispatchIndirectCS,
		&reprojectGICS, &filterGICS
	};

	for (auto shader : shaderPtrs)
		*shader = nullptr;

	CompileComputeShaders();
}

void SSRT::CompileComputeShaders()
{
	struct ShaderCompileInfo
	{
		winrt::com_ptr<ID3D11ComputeShader>* programPtr;
		std::string_view filename;
		std::vector<std::pair<const char*, const char*>> defines;
	};

	std::vector<ShaderCompileInfo> shaderInfos = {
		{ &pyramidCopyCS, "PyramidCopyCS.cs.hlsl", {} },
		{ &buildDepthPyramidCS, "BuildDepthPyramidCS.cs.hlsl", {} },
		{ &clearCountersCS, "ClearCountersCS.cs.hlsl", {} },
		{ &clearProbeMaskCS, "ClearProbeMaskCS.cs.hlsl", {} },
		{ &reprojectScreenProbesCS, "ReprojectScreenProbesCS.cs.hlsl", {} },
		{ &filterProbeMaskCS, "FilterProbeMaskCS.cs.hlsl", {} },
		{ &spawnScreenProbesCS, "SpawnScreenProbesCS.cs.hlsl", {} },
		{ &prefixSumLocalCS, "PrefixSumLocalCS.cs.hlsl", {} },
		{ &prefixSumBlockCS, "PrefixSumBlockCS.cs.hlsl", {} },
		{ &prefixSumFinalizeCS, "PrefixSumFinalizeCS.cs.hlsl", {} },
		{ &compactScreenProbesCS, "CompactScreenProbesCS.cs.hlsl", {} },
		{ &prepareDispatchIndirectCS, "PrepareDispatchIndirectCS.cs.hlsl", {} },
		{ &patchScreenProbesCS, "PatchScreenProbesCS.cs.hlsl", {} },
		{ &sampleScreenProbesCS, "SampleScreenProbesCS.cs.hlsl", {} },
		{ &populateScreenProbesCS, "PopulateScreenProbesCS.cs.hlsl", {} },
		{ &blendScreenProbesCS, "BlendScreenProbesCS.cs.hlsl", {} },
		{ &filterScreenProbesCS, "FilterScreenProbesCS.cs.hlsl", {} },
		{ &projectScreenProbesCS, "ProjectScreenProbesCS.cs.hlsl", {} },
		{ &interpolateScreenProbesCS, "InterpolateScreenProbesCS.cs.hlsl", {} },
		{ &reprojectGICS, "ReprojectGICS.cs.hlsl", {} },
		{ &filterGICS, "FilterGICS.cs.hlsl", {} },
	};

	for (auto& info : shaderInfos) {
		auto path = std::filesystem::path("Data\\Shaders\\SSRT") / info.filename;
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0")))
			info.programPtr->attach(rawPtr);
	}

	recompileFlag = false;
}

bool SSRT::ShadersOK()
{
	return pyramidCopyCS && buildDepthPyramidCS &&
	       clearCountersCS && clearProbeMaskCS &&
	       reprojectScreenProbesCS && filterProbeMaskCS &&
	       spawnScreenProbesCS && compactScreenProbesCS &&
	       patchScreenProbesCS && sampleScreenProbesCS &&
	       populateScreenProbesCS && blendScreenProbesCS &&
	       filterScreenProbesCS && projectScreenProbesCS &&
	       interpolateScreenProbesCS &&
	       prefixSumLocalCS && prefixSumBlockCS && prefixSumFinalizeCS &&
	       prepareDispatchIndirectCS &&
	       reprojectGICS && filterGICS;
}

void SSRT::UpdateCB()
{
	// Use full render target size (not dynamic resolution) to match camera matrices
	// Camera ViewProj/ViewProjInverse correspond to the full frustum covering the full RT
	float2 res = globals::state->screenSize;

	auto eye = Util::GetCameraData(0);

	SSRTCB data{};

	// Reprojection matrix: PrevViewProjUnjittered * ViewProjInverse
	{
		auto viewProj = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&eye.viewProjMat));
		auto viewProjInv = DirectX::XMMatrixInverse(nullptr, viewProj);
		auto prevViewProj = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&eye.previousViewProjMatrixUnjittered));
		auto reprojection = DirectX::XMMatrixMultiply(viewProjInv, prevViewProj);
		DirectX::XMStoreFloat4x4(&data.Reprojection, reprojection);

		auto prevViewProjInv = DirectX::XMMatrixInverse(nullptr, prevViewProj);
		DirectX::XMStoreFloat4x4(&data.PrevViewProjInverse, prevViewProjInv);
	}

	auto eyePos = Util::GetEyePosition(0);
	data.Eye = { eyePos.x, eyePos.y, eyePos.z };
	data.FrameIndex = globals::state->frameCount;

	data.BufferDimensions = res;
	data.RcpBufferDimensions = float2(1.0f) / res;

	// Near/far from projection matrix
	float nearPlane = eye.projMat(3, 2) / eye.projMat(2, 2);
	float farPlane = eye.projMat(3, 2) / (eye.projMat(2, 2) + 1.0f);
	data.NearFar = { nearPlane, farPlane };

	// CellSize: angular size of one probe cell
	float tanHalfFov = 1.0f / eye.projMat(1, 1);
	data.CellSize = 2.0f * tanHalfFov / (float)probeSize;

	data.ProbeSize = probeSize;
	data.ProbeCountX = probeCountX;
	data.ProbeCountY = probeCountY;
	data.ProbeMaskMipCount = probeMaskMipCount;
	data.ProbeSpawnTileSize = settings.ProbeSpawnTileSize;

	data.BlurDirectionX = 0;
	data.BlurDirectionY = 0;

	data.MaxHiZSteps = settings.MaxHiZSteps;
	data.HiZThickness = settings.HiZThickness;
	data.HiZMaxDistance = settings.HiZMaxDistance;
	data.GIIntensity = settings.GIIntensity;
	data.AOIntensity = settings.AOIntensity;
	data.MaxSpawnCount = maxSpawnCount;
	data.MaxRayCount = maxRayCount;
	data.DepthPyramidMipCount = depthPyramidMipCount;

	cachedCB = data;
	ssrtCB->Update(data);
}

void SSRT::DrawSSRT()
{
	auto context = globals::d3d::context;

	auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
	GET_INSTANCE_MEMBER(BSImagespaceShaderISSAOBlurH, imageSpaceManager);

	// Toggle vanilla SSAO
	static bool* enableSSAO = reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(BSImagespaceShaderISSAOBlurH.get()) + 0x50LL);
	*enableSSAO = settings.EnableVanillaSSAO;

	if (!(settings.Enabled && ShadersOK())) {
		FLOAT clr[4] = { 0.f, 0.f, 0.f, 1.f };
		context->ClearUnorderedAccessViewFloat(texGIOcclusion->uav.get(), clr);
		return;
	}

	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "SSRT");

	if (recompileFlag)
		ClearShaderCache();

	UpdateCB();

	//////////////////////////////////////////////////////

	auto renderer = globals::game::renderer;
	auto rts = renderer->GetRuntimeData().renderTargets;
	auto deferred = globals::deferred;

	// Use full render target size to match g_BufferDimensions and texture allocations
	float2 size = globals::state->screenSize;
	uint resX = (uint)size.x;
	uint resY = (uint)size.y;

	auto cb = ssrtCB->CB();
	auto* sharedDataBuf = globals::state->sharedDataCB->CB();
	std::array<ID3D11SamplerState*, 2> samplers = { pointClampSampler.get(), linearClampSampler.get() };

	// Bind constant buffers shared across all passes
	context->CSSetConstantBuffers(1, 1, &cb);
	context->CSSetConstantBuffers(5, 1, &sharedDataBuf);
	context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());

	uint pp = pingPong;
	uint prevPP = 1 - pp;

	// Helper to unbind SRVs/UAVs
	auto unbindCS = [&](uint srvCount, uint uavCount) {
		std::array<ID3D11ShaderResourceView*, 16> nullSrvs = {};
		std::array<ID3D11UnorderedAccessView*, 8> nullUavs = {};
		if (srvCount > 0)
			context->CSSetShaderResources(0, std::min(srvCount, (uint)nullSrvs.size()), nullSrvs.data());
		if (uavCount > 0)
			context->CSSetUnorderedAccessViews(0, std::min(uavCount, (uint)nullUavs.size()), nullUavs.data(), nullptr);
	};

	//////////////////////////////////////////////////////
	// Phase 1: Depth pyramid generation
	//////////////////////////////////////////////////////
	{
		TracyD3D11Zone(globals::state->tracyCtx, "SSRT - Depth Pyramid");

		// Copy depth to pyramid mip 0
		{
			ID3D11ShaderResourceView* srvs[] = { Util::GetCurrentSceneDepthSRV() };
			ID3D11UnorderedAccessView* uavs[] = { depthPyramidMipUAVs[0].get() };
			context->CSSetShaderResources(0, 1, srvs);
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			context->CSSetShader(pyramidCopyCS.get(), nullptr, 0);
			context->Dispatch((resX + 7) >> 3, (resY + 7) >> 3, 1);
			unbindCS(1, 1);
		}

		// Build mip chain with MAX downsample (reversed-Z: max = nearest)
		for (uint m = 1; m < depthPyramidMipCount; m++) {
			ID3D11ShaderResourceView* srvs[] = { depthPyramidMipSRVs[m - 1].get() };
			ID3D11UnorderedAccessView* uavs[] = { depthPyramidMipUAVs[m].get() };
			context->CSSetShaderResources(0, 1, srvs);
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			context->CSSetShader(buildDepthPyramidCS.get(), nullptr, 0);

			uint mipW = std::max(1u, resX >> m);
			uint mipH = std::max(1u, resY >> m);
			context->Dispatch((mipW + 7) >> 3, (mipH + 7) >> 3, 1);
			unbindCS(1, 1);
		}

		// Radiance pyramid: copy mip 0 + GenerateMips
		context->CopySubresourceRegion(texRadiancePyramid->resource.get(), 0, 0, 0, 0,
			rts[deferred->forwardRenderTargets[0]].texture, 0, nullptr);
		context->GenerateMips(texRadiancePyramid->srv.get());
	}

	//////////////////////////////////////////////////////
	// Phase 2: Clear counters and probe mask
	//////////////////////////////////////////////////////
	{
		TracyD3D11Zone(globals::state->tracyCtx, "SSRT - Clear");

		// Clear counters
		{
			ID3D11UnorderedAccessView* uavs[] = {
				bufEmptyTileCount->uav.get(),
				bufOverrideTileCount->uav.get()
			};
			context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
			context->CSSetShader(clearCountersCS.get(), nullptr, 0);
			context->Dispatch(1, 1, 1);
			unbindCS(0, 2);
		}

		// Clear probe mask
		{
			ID3D11UnorderedAccessView* uavs[] = { probeMaskMipUAVs[pp][0].get() };
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			context->CSSetShader(clearProbeMaskCS.get(), nullptr, 0);
			context->Dispatch((probeCountX + 7) >> 3, (probeCountY + 7) >> 3, 1);
			unbindCS(0, 1);
		}
	}

	//////////////////////////////////////////////////////
	// Phase 3: Reproject screen probes
	//////////////////////////////////////////////////////
	{
		TracyD3D11Zone(globals::state->tracyCtx, "SSRT - Reproject");

		ID3D11ShaderResourceView* srvs[] = {
			Util::GetCurrentSceneDepthSRV(),                   // t0: depth
			rts[NORMALROUGHNESS].SRV,                          // t1: normals
			rts[RE::RENDER_TARGETS::kMOTION_VECTOR].SRV,      // t2: velocity
			texPrevDepth->srv.get(),                           // t3: prev depth
			texPrevNormals->srv.get(),                         // t4: prev normals
			texProbeMask[prevPP]->srv.get(),                   // t5: prev probe mask
			texProbeBuffer[prevPP]->srv.get(),                 // t6: prev probe buffer
		};
		ID3D11UnorderedAccessView* uavs[] = {
			probeMaskMipUAVs[pp][0].get(),                     // u0: current probe mask
			texProbeBuffer[pp]->uav.get(),                     // u1: current probe buffer
			bufEmptyTile->uav.get(),                           // u2: empty tiles
			bufEmptyTileCount->uav.get(),                      // u3: empty tile count
			bufProbeSH[pp]->uav.get(),                         // u4: current probe SH (write)
			bufProbeSH[prevPP]->uav.get(),                     // u5: previous probe SH (read)
		};
		context->CSSetShaderResources(0, _countof(srvs), srvs);
		context->CSSetUnorderedAccessViews(0, _countof(uavs), uavs, nullptr);
		context->CSSetShader(reprojectScreenProbesCS.get(), nullptr, 0);

		uint pbw = probeCountX * probeSize;
		uint pbh = probeCountY * probeSize;
		context->Dispatch((pbw + 7) >> 3, (pbh + 7) >> 3, 1);
		unbindCS(_countof(srvs), _countof(uavs));
	}

	//////////////////////////////////////////////////////
	// Phase 4: Filter probe mask mip pyramid
	//////////////////////////////////////////////////////
	{
		TracyD3D11Zone(globals::state->tracyCtx, "SSRT - FilterProbeMask");

		for (uint m = 1; m < probeMaskMipCount; m++) {
			// Read from mip m-1 (u1), write to mip m (u0)
			ID3D11UnorderedAccessView* uavs[] = {
				probeMaskMipUAVs[pp][m].get(),       // u0: output mip
				probeMaskMipUAVs[pp][m - 1].get(),   // u1: input mip (read)
			};
			context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
			context->CSSetShader(filterProbeMaskCS.get(), nullptr, 0);

			uint mipW = std::max(1u, probeCountX >> m);
			uint mipH = std::max(1u, probeCountY >> m);
			context->Dispatch((mipW + 7) >> 3, (mipH + 7) >> 3, 1);
			unbindCS(0, 2);
		}
	}

	//////////////////////////////////////////////////////
	// Phase 5: Spawn + prefix sum + compact
	//////////////////////////////////////////////////////
	{
		TracyD3D11Zone(globals::state->tracyCtx, "SSRT - Spawn/Compact");

		// Spawn
		{
			ID3D11ShaderResourceView* srvs[] = {
				rts[NORMALROUGHNESS].SRV,                      // t0: normals (sky detection)
			};
			ID3D11UnorderedAccessView* uavs[] = {
				bufProbeSpawnScan->uav.get(),                  // u0: scan buffer (0 or 1)
				bufProbeSpawn[prevPP]->uav.get(),              // u1: prev probe spawn (seeds)
			};
			context->CSSetShaderResources(0, _countof(srvs), srvs);
			context->CSSetUnorderedAccessViews(0, _countof(uavs), uavs, nullptr);
			context->CSSetShader(spawnScreenProbesCS.get(), nullptr, 0);
			context->Dispatch((maxSpawnCount + 63) >> 6, 1, 1);
			unbindCS(_countof(srvs), _countof(uavs));
		}

		// Prefix sum (3-pass)
		{
			// Pass 1: Local scan (reads ScanBuffer flags, writes IndexBuffer + BlockTotals)
			{
				ID3D11UnorderedAccessView* uavs[] = {
					bufProbeSpawnScan->uav.get(),              // u0: scan flags (read only)
					bufPrefixSumBlockTotals->uav.get(),        // u1: block totals (output)
					bufProbeSpawnIndex->uav.get(),             // u2: local prefix sums (output)
				};
				context->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);
				context->CSSetShader(prefixSumLocalCS.get(), nullptr, 0);
				uint blockCount = (maxSpawnCount + 127) / 128;
				context->Dispatch(blockCount, 1, 1);
				unbindCS(0, 3);
			}

			// Pass 2: Block totals scan
			{
				ID3D11UnorderedAccessView* uavs[] = {
					bufPrefixSumBlockTotals->uav.get(),        // u0: block totals in/out
				};
				context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
				context->CSSetShader(prefixSumBlockCS.get(), nullptr, 0);
				context->Dispatch(1, 1, 1);
				unbindCS(0, 1);
			}

			// Pass 3: Finalize (add block offsets to IndexBuffer)
			{
				ID3D11UnorderedAccessView* uavs[] = {
					bufProbeSpawnIndex->uav.get(),             // u0: prefix sums (in/out)
					bufPrefixSumBlockTotals->uav.get(),        // u1: scanned block totals
				};
				context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
				context->CSSetShader(prefixSumFinalizeCS.get(), nullptr, 0);
				uint blockCount = (maxSpawnCount + 127) / 128;
				context->Dispatch(blockCount, 1, 1);
				unbindCS(0, 2);
			}
		}

		// Compact
		{
			ID3D11ShaderResourceView* srvs[] = {
				texProbeMask[pp]->srv.get(),                   // t0: probe mask
				bufProbeSpawn[prevPP]->srv.get(),              // t1: uncompacted seeds (from Spawn)
			};
			ID3D11UnorderedAccessView* uavs[] = {
				bufProbeSpawn[pp]->uav.get(),                  // u0: probe spawn (compacted output)
				bufProbeSpawnScan->uav.get(),                  // u1: scan buffer
				bufProbeSpawnIndex->uav.get(),                 // u2: index buffer
				bufOverrideTile->uav.get(),                    // u3: override tiles
				bufOverrideTileCount->uav.get(),               // u4: override tile count
			};
			context->CSSetShaderResources(0, _countof(srvs), srvs);
			context->CSSetUnorderedAccessViews(0, _countof(uavs), uavs, nullptr);
			context->CSSetShader(compactScreenProbesCS.get(), nullptr, 0);
			context->Dispatch((maxSpawnCount + 63) >> 6, 1, 1);
			unbindCS(_countof(srvs), _countof(uavs));
		}
	}

	//////////////////////////////////////////////////////
	// Phase 6: Patch disoccluded regions
	//////////////////////////////////////////////////////
	{
		TracyD3D11Zone(globals::state->tracyCtx, "SSRT - Patch");

		// Prepare indirect dispatch args
		{
			ID3D11UnorderedAccessView* uavs[] = {
				bufDispatchIndirectArgs->uav.get(),            // u0: indirect args
			};
			ID3D11ShaderResourceView* srvs[] = {
				bufEmptyTileCount->srv.get(),                  // t0: empty tile count
			};
			context->CSSetShaderResources(0, 1, srvs);
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			context->CSSetShader(prepareDispatchIndirectCS.get(), nullptr, 0);
			context->Dispatch(1, 1, 1);
			unbindCS(1, 1);
		}

		// Patch via DispatchIndirect
		{
			ID3D11ShaderResourceView* srvs[] = {
				rts[NORMALROUGHNESS].SRV,                          // t0: normals
			};
			ID3D11UnorderedAccessView* uavs[] = {
				bufProbeSpawn[pp]->uav.get(),                  // u0: probe spawn
				bufEmptyTile->uav.get(),                       // u1: empty tiles
				bufOverrideTile->uav.get(),                    // u2: override tiles
				bufOverrideTileCount->uav.get(),               // u3: override count
				bufEmptyTileCount->uav.get(),                  // u4: empty tile count
			};
			context->CSSetShaderResources(0, _countof(srvs), srvs);
			context->CSSetUnorderedAccessViews(0, _countof(uavs), uavs, nullptr);
			context->CSSetShader(patchScreenProbesCS.get(), nullptr, 0);
			context->DispatchIndirect(bufDispatchIndirectArgs->resource.get(), 0);
			unbindCS(_countof(srvs), _countof(uavs));
		}
	}

	//////////////////////////////////////////////////////
	// Phase 7: Sample directions
	//////////////////////////////////////////////////////
	{
		TracyD3D11Zone(globals::state->tracyCtx, "SSRT - Sample");

		ID3D11ShaderResourceView* srvs[] = {
			Util::GetCurrentSceneDepthSRV(),                   // t0: depth
			rts[NORMALROUGHNESS].SRV,                          // t1: normals
			texProbeBuffer[pp]->srv.get(),                     // t2: probe buffer (neighbor radiance)
			texProbeMask[pp]->srv.get(),                       // t3: probe mask
		};
		ID3D11UnorderedAccessView* uavs[] = {
			bufProbeSpawnSample->uav.get(),                    // u0: sample directions
			bufProbeSpawnRadiance->uav.get(),                  // u1: radiance output (cleared)
			bufProbeSpawn[pp]->uav.get(),                      // u2: probe spawn (read)
			bufProbeSpawnScan->uav.get(),                      // u3: scan buffer (probe count)
			bufProbeSpawnIndex->uav.get(),                     // u4: index buffer
			texProbeBuffer[prevPP]->uav.get(),                 // u5: prev probe buffer (write reprojected radiance)
		};
		context->CSSetShaderResources(0, _countof(srvs), srvs);
		context->CSSetUnorderedAccessViews(0, _countof(uavs), uavs, nullptr);
		context->CSSetShader(sampleScreenProbesCS.get(), nullptr, 0);
		context->Dispatch((maxRayCount + 63) >> 6, 1, 1);
		unbindCS(_countof(srvs), _countof(uavs));
	}

	//////////////////////////////////////////////////////
	// Phase 8: Populate probes (Hi-Z tracing)
	//////////////////////////////////////////////////////
	{
		TracyD3D11Zone(globals::state->tracyCtx, "SSRT - Populate (Hi-Z)");

		ID3D11ShaderResourceView* srvs[] = {
			texDepthPyramid->srv.get(),                        // t0: depth pyramid (all mips)
			texRadiancePyramid->srv.get(),                     // t1: radiance pyramid
			rts[NORMALROUGHNESS].SRV,                          // t2: normals
		};
		ID3D11UnorderedAccessView* uavs[] = {
			bufProbeSpawnRadiance->uav.get(),                  // u0: output radiance
			bufProbeSpawn[pp]->uav.get(),                      // u1: probe spawn (seeds)
			bufProbeSpawnSample->uav.get(),                    // u2: sample directions
			bufProbeSpawnScan->uav.get(),                      // u3: scan (probe count)
			bufProbeSpawnIndex->uav.get(),                     // u4: index
		};
		context->CSSetShaderResources(0, _countof(srvs), srvs);
		context->CSSetUnorderedAccessViews(0, _countof(uavs), uavs, nullptr);
		context->CSSetShader(populateScreenProbesCS.get(), nullptr, 0);
		context->Dispatch((maxRayCount + 31) >> 5, 1, 1);
		unbindCS(_countof(srvs), _countof(uavs));
	}

	//////////////////////////////////////////////////////
	// Phase 9: Blend
	//////////////////////////////////////////////////////
	{
		TracyD3D11Zone(globals::state->tracyCtx, "SSRT - Blend");

		ID3D11ShaderResourceView* srvs[] = {
			texProbeBuffer[prevPP]->srv.get(),                 // t0: previous probe buffer
			rts[NORMALROUGHNESS].SRV,                          // t1: normals
		};
		ID3D11UnorderedAccessView* uavs[] = {
			texProbeBuffer[pp]->uav.get(),                     // u0: current probe buffer
			probeMaskMipUAVs[pp][0].get(),                     // u1: probe mask
			bufProbeSpawnRadiance->uav.get(),                  // u2: traced radiance
			bufProbeSpawnSample->uav.get(),                    // u3: sample directions
			bufProbeSpawn[pp]->uav.get(),                      // u4: probe spawn
			bufProbeSpawnScan->uav.get(),                      // u5: scan
			bufProbeSpawnIndex->uav.get(),                     // u6: index
		};
		context->CSSetShaderResources(0, _countof(srvs), srvs);
		context->CSSetUnorderedAccessViews(0, _countof(uavs), uavs, nullptr);
		context->CSSetShader(blendScreenProbesCS.get(), nullptr, 0);
		context->Dispatch((maxRayCount + 63) >> 6, 1, 1);
		unbindCS(_countof(srvs), _countof(uavs));
	}

	//////////////////////////////////////////////////////
	// Phase 10: Filter screen probes (2-pass separable)
	//////////////////////////////////////////////////////
	{
		TracyD3D11Zone(globals::state->tracyCtx, "SSRT - Filter Probes");

		// Pass X: blur direction (1,0)
		{
			cachedCB.BlurDirectionX = 1;
			cachedCB.BlurDirectionY = 0;
			ssrtCB->Update(cachedCB);

			ID3D11ShaderResourceView* srvs[] = {
				texProbeBuffer[pp]->srv.get(),                 // t0: input probe (just blended)
				Util::GetCurrentSceneDepthSRV(),               // t1: depth
				rts[NORMALROUGHNESS].SRV,                      // t2: normals
				texProbeMask[pp]->srv.get(),                   // t3: probe mask
			};
			ID3D11UnorderedAccessView* uavs[] = {
				texProbeBuffer[prevPP]->uav.get(),             // u0: output probe (temp)
				bufProbeSpawn[pp]->uav.get(),                  // u1: spawn (read seeds)
				bufProbeSpawnScan->uav.get(),                  // u2: scan (probe count)
				bufProbeSpawnIndex->uav.get(),                 // u3: index
			};
			context->CSSetShaderResources(0, _countof(srvs), srvs);
			context->CSSetUnorderedAccessViews(0, _countof(uavs), uavs, nullptr);
			context->CSSetShader(filterScreenProbesCS.get(), nullptr, 0);
			context->Dispatch((maxRayCount + 63) >> 6, 1, 1);
			unbindCS(_countof(srvs), _countof(uavs));
		}

		// Pass Y: blur direction (0,1) — read from prevPP, write to pp
		{
			cachedCB.BlurDirectionX = 0;
			cachedCB.BlurDirectionY = 1;
			ssrtCB->Update(cachedCB);

			ID3D11ShaderResourceView* srvs[] = {
				texProbeBuffer[prevPP]->srv.get(),             // t0: input (from pass X)
				Util::GetCurrentSceneDepthSRV(),               // t1: depth
				rts[NORMALROUGHNESS].SRV,                      // t2: normals
				texProbeMask[pp]->srv.get(),                   // t3: probe mask
			};
			ID3D11UnorderedAccessView* uavs[] = {
				texProbeBuffer[pp]->uav.get(),                 // u0: output (final filtered)
				bufProbeSpawn[pp]->uav.get(),                  // u1: spawn (read seeds)
				bufProbeSpawnScan->uav.get(),                  // u2: scan (probe count)
				bufProbeSpawnIndex->uav.get(),                 // u3: index
			};
			context->CSSetShaderResources(0, _countof(srvs), srvs);
			context->CSSetUnorderedAccessViews(0, _countof(uavs), uavs, nullptr);
			context->CSSetShader(filterScreenProbesCS.get(), nullptr, 0);
			context->Dispatch((maxRayCount + 63) >> 6, 1, 1);
			unbindCS(_countof(srvs), _countof(uavs));
		}

		// Reset blur direction
		cachedCB.BlurDirectionX = 0;
		cachedCB.BlurDirectionY = 0;
		ssrtCB->Update(cachedCB);
	}

	//////////////////////////////////////////////////////
	// Phase 11: Project to SH
	//////////////////////////////////////////////////////
	{
		TracyD3D11Zone(globals::state->tracyCtx, "SSRT - Project SH");

		ID3D11ShaderResourceView* srvs[] = {
			rts[NORMALROUGHNESS].SRV,                          // t0: normals
		};
		ID3D11UnorderedAccessView* uavs[] = {
			texProbeBuffer[pp]->uav.get(),                     // u0: probe buffer (read)
			bufProbeSH[pp]->uav.get(),                         // u1: SH output
			bufProbeSpawn[pp]->uav.get(),                      // u2: spawn (read seeds)
			bufProbeSpawnScan->uav.get(),                      // u3: scan (probe count)
			bufProbeSpawnIndex->uav.get(),                     // u4: index
		};
		context->CSSetShaderResources(0, _countof(srvs), srvs);
		context->CSSetUnorderedAccessViews(0, _countof(uavs), uavs, nullptr);
		context->CSSetShader(projectScreenProbesCS.get(), nullptr, 0);
		context->Dispatch((maxRayCount + 63) >> 6, 1, 1);
		unbindCS(_countof(srvs), _countof(uavs));
	}

	if (debugProbes) {
		//////////////////////////////////////////////////////
		// Debug: Copy raw probe buffer directly to output
		//////////////////////////////////////////////////////
		TracyD3D11Zone(globals::state->tracyCtx, "SSRT - Debug Probes");

		D3D11_BOX srcBox = { 0, 0, 0, resX, resY, 1 };
		context->CopySubresourceRegion(texGIOcclusion->resource.get(), 0, 0, 0, 0,
			texProbeBuffer[pp]->resource.get(), 0, &srcBox);
	} else {
		//////////////////////////////////////////////////////
		// Phase 12: Interpolate to full resolution
		//////////////////////////////////////////////////////
		{
			TracyD3D11Zone(globals::state->tracyCtx, "SSRT - Interpolate");

			ID3D11ShaderResourceView* srvs[] = {
				Util::GetCurrentSceneDepthSRV(),                   // t0: depth
				rts[NORMALROUGHNESS].SRV,                          // t1: normals
				texProbeMask[pp]->srv.get(),                       // t2: probe mask (with mips)
				texProbeBuffer[pp]->srv.get(),                     // t3: probe buffer
			};
			ID3D11UnorderedAccessView* uavs[] = {
				texGIDenoiserColor[pp]->uav.get(),                 // u0: denoiser color output
				bufProbeSH[pp]->uav.get(),                         // u1: probe SH (read)
			};
			context->CSSetShaderResources(0, _countof(srvs), srvs);
			context->CSSetUnorderedAccessViews(0, _countof(uavs), uavs, nullptr);
			context->CSSetShader(interpolateScreenProbesCS.get(), nullptr, 0);
			context->Dispatch((resX + 7) >> 3, (resY + 7) >> 3, 1);
			unbindCS(_countof(srvs), _countof(uavs));
		}

		//////////////////////////////////////////////////////
		// Phase 13: GI Denoiser - Temporal reproject
		//////////////////////////////////////////////////////
		{
			TracyD3D11Zone(globals::state->tracyCtx, "SSRT - Reproject GI");

			ID3D11ShaderResourceView* srvs[] = {
				Util::GetCurrentSceneDepthSRV(),                   // t0: depth
				rts[NORMALROUGHNESS].SRV,                          // t1: normals
				rts[RE::RENDER_TARGETS::kMOTION_VECTOR].SRV,      // t2: velocity
				texGIDenoiserColor[prevPP]->srv.get(),             // t3: prev denoised color
				texGIDenoiserColorDelta[prevPP]->srv.get(),        // t4: prev color delta
				texPrevDepth->srv.get(),                           // t5: prev depth
				texPrevNormals->srv.get(),                         // t6: prev normals
			};
			ID3D11UnorderedAccessView* uavs[] = {
				texGIDenoiserColor[pp]->uav.get(),                 // u0: denoiser color (accumulated)
				texGIDenoiserBlurMask->uav.get(),                  // u1: blur mask
				texGIDenoiserColorDelta[pp]->uav.get(),            // u2: color delta
			};
			context->CSSetShaderResources(0, _countof(srvs), srvs);
			context->CSSetUnorderedAccessViews(0, _countof(uavs), uavs, nullptr);
			context->CSSetShader(reprojectGICS.get(), nullptr, 0);
			context->Dispatch((resX + 7) >> 3, (resY + 7) >> 3, 1);
			unbindCS(_countof(srvs), _countof(uavs));
		}

		//////////////////////////////////////////////////////
		// Phase 14: GI Denoiser - Spatial filter (2-pass)
		//////////////////////////////////////////////////////
		{
			TracyD3D11Zone(globals::state->tracyCtx, "SSRT - Filter GI");

			// Pass X: direction (1,0), read from pp, write to prevPP
			{
				cachedCB.BlurDirectionX = 1;
				cachedCB.BlurDirectionY = 0;
				ssrtCB->Update(cachedCB);

				ID3D11ShaderResourceView* srvs[] = {
					texGIDenoiserColor[pp]->srv.get(),             // t0: input color
					Util::GetCurrentSceneDepthSRV(),               // t1: depth
					rts[NORMALROUGHNESS].SRV,                      // t2: normals
					texGIDenoiserBlurMask->srv.get(),              // t3: blur mask
				};
				ID3D11UnorderedAccessView* uavs[] = {
					texGIDenoiserColor[prevPP]->uav.get(),         // u0: output color
				};
				context->CSSetShaderResources(0, _countof(srvs), srvs);
				context->CSSetUnorderedAccessViews(0, _countof(uavs), uavs, nullptr);
				context->CSSetShader(filterGICS.get(), nullptr, 0);
				context->Dispatch((resX + 7) >> 3, (resY + 7) >> 3, 1);
				unbindCS(_countof(srvs), _countof(uavs));
			}

			// Pass Y: direction (0,1), read from prevPP, write to final output
			{
				cachedCB.BlurDirectionX = 0;
				cachedCB.BlurDirectionY = 1;
				ssrtCB->Update(cachedCB);

				ID3D11ShaderResourceView* srvs[] = {
					texGIDenoiserColor[prevPP]->srv.get(),         // t0: input color
					Util::GetCurrentSceneDepthSRV(),               // t1: depth
					rts[NORMALROUGHNESS].SRV,                      // t2: normals
					texGIDenoiserBlurMask->srv.get(),              // t3: blur mask
				};
				ID3D11UnorderedAccessView* uavs[] = {
					texGIOcclusion->uav.get(),                     // u0: FINAL output
				};
				context->CSSetShaderResources(0, _countof(srvs), srvs);
				context->CSSetUnorderedAccessViews(0, _countof(uavs), uavs, nullptr);
				context->CSSetShader(filterGICS.get(), nullptr, 0);
				context->Dispatch((resX + 7) >> 3, (resY + 7) >> 3, 1);
				unbindCS(_countof(srvs), _countof(uavs));
			}
		}
	}

	//////////////////////////////////////////////////////
	// Phase 15: Frame swap + copy history
	//////////////////////////////////////////////////////
	{
		// Copy depth from pyramid mip 0 (R32_FLOAT, avoids depth-stencil format issues)
		context->CopySubresourceRegion(texPrevDepth->resource.get(), 0, 0, 0, 0,
			texDepthPyramid->resource.get(), 0, nullptr);

		// Copy normals from NORMALROUGHNESS render target
		context->CopySubresourceRegion(texPrevNormals->resource.get(), 0, 0, 0, 0,
			rts[NORMALROUGHNESS].texture, 0, nullptr);

		pingPong = 1 - pingPong;
	}

	//////////////////////////////////////////////////////
	// Cleanup
	//////////////////////////////////////////////////////
	{
		unbindCS(16, 8);

		samplers.fill(nullptr);
		cb = nullptr;

		context->CSSetConstantBuffers(1, 1, &cb);
		context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());
		context->CSSetShader(nullptr, nullptr, 0);
	}
}
