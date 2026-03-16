#include "VRStereoOptimizations.h"

#include "ExtendedMaterials.h"
#include "Globals.h"
#include "State.h"
#include "Utils/D3D.h"
#include "Utils/Game.h"

#include <imgui.h>

// JSON enum serialization for StereoMode
NLOHMANN_JSON_SERIALIZE_ENUM(VRStereoOptimizations::StereoMode, {
	{ VRStereoOptimizations::StereoMode::Off, "Off" },
	{ VRStereoOptimizations::StereoMode::Enable, "Enable" },
})

//=============================================================================
// SETTINGS MANAGEMENT
//=============================================================================

void VRStereoOptimizations::SaveSettings(json& o_json)
{
	o_json["StereoMode"] = settings.stereoMode;
	o_json["DisocclusionDepthThreshold"] = settings.disocclusionDepthThreshold;
	o_json["FullBlendDistance"] = settings.fullBlendDistance;
	o_json["QualityJitterOffset"] = settings.qualityJitterOffset;
	o_json["FoveatedRegionRadius"] = settings.foveatedRegionRadius;
	o_json["FoveatedRegionCenterX"] = settings.foveatedRegionCenterX;
	o_json["FoveatedRegionCenterY"] = settings.foveatedRegionCenterY;
	o_json["UseEyeTracking"] = settings.useEyeTracking;
	o_json["DebugVisualization"] = settings.debugVisualization;
	o_json["DebugSkipMerge"] = settings.debugSkipMerge;
	o_json["DebugForceAllStencil"] = settings.debugForceAllStencil;
	o_json["DebugForceAllReprojectCS"] = settings.debugForceAllReprojectCS;
	o_json["DebugDepthMap"] = settings.debugDepthMap;
	o_json["MipBiasMode"] = settings.mipBiasMode;
	o_json["MipLodBias"] = settings.mipLodBias;
	o_json["MipBiasNearDist"] = settings.mipBiasNearDist;
	o_json["MipBiasFarDist"] = settings.mipBiasFarDist;
	o_json["CASStrength"] = settings.casStrength;
	o_json["AlphaTestThreshold"] = settings.alphaTestThreshold;
}

void VRStereoOptimizations::LoadSettings(json& o_json)
{
	if (o_json.contains("StereoMode"))
		settings.stereoMode = o_json["StereoMode"].get<StereoMode>();
	if (o_json.contains("DisocclusionDepthThreshold"))
		settings.disocclusionDepthThreshold = o_json["DisocclusionDepthThreshold"].get<float>();
	if (o_json.contains("QualityJitterOffset"))
		settings.qualityJitterOffset = o_json["QualityJitterOffset"].get<float>();
	if (o_json.contains("FoveatedRegionRadius"))
		settings.foveatedRegionRadius = o_json["FoveatedRegionRadius"].get<float>();
	if (o_json.contains("FoveatedRegionCenterX"))
		settings.foveatedRegionCenterX = o_json["FoveatedRegionCenterX"].get<float>();
	if (o_json.contains("FoveatedRegionCenterY"))
		settings.foveatedRegionCenterY = o_json["FoveatedRegionCenterY"].get<float>();
	if (o_json.contains("UseEyeTracking"))
		settings.useEyeTracking = o_json["UseEyeTracking"].get<bool>();
	if (o_json.contains("DebugVisualization"))
		settings.debugVisualization = o_json["DebugVisualization"].get<bool>();
	if (o_json.contains("DebugSkipMerge"))
		settings.debugSkipMerge = o_json["DebugSkipMerge"].get<bool>();
	if (o_json.contains("DebugForceAllStencil"))
		settings.debugForceAllStencil = o_json["DebugForceAllStencil"].get<bool>();
	if (o_json.contains("DebugForceAllReprojectCS"))
		settings.debugForceAllReprojectCS = o_json["DebugForceAllReprojectCS"].get<bool>();
	if (o_json.contains("DebugDepthMap"))
		settings.debugDepthMap = o_json["DebugDepthMap"].get<bool>();
	if (o_json.contains("FullBlendDistance"))
		settings.fullBlendDistance = o_json["FullBlendDistance"].get<float>();
	if (o_json.contains("MipBiasMode"))
		settings.mipBiasMode = o_json["MipBiasMode"].get<int>();
	// Backwards compat: old bool EnableMipBias -> mode 2 (Distant Trees)
	else if (o_json.contains("EnableMipBias") && o_json["EnableMipBias"].get<bool>())
		settings.mipBiasMode = 2;
	if (o_json.contains("MipLodBias"))
		settings.mipLodBias = o_json["MipLodBias"].get<float>();
	if (o_json.contains("MipBiasNearDist"))
		settings.mipBiasNearDist = o_json["MipBiasNearDist"].get<float>();
	if (o_json.contains("MipBiasFarDist"))
		settings.mipBiasFarDist = o_json["MipBiasFarDist"].get<float>();
	// CAS disabled for now — ignore saved value
	// if (o_json.contains("CASStrength"))
	//	settings.casStrength = o_json["CASStrength"].get<float>();
	settings.casStrength = 0.0f;
	if (o_json.contains("AlphaTestThreshold"))
		settings.alphaTestThreshold = o_json["AlphaTestThreshold"].get<float>();
}

void VRStereoOptimizations::RestoreDefaultSettings()
{
	settings = {};
}

//=============================================================================
// RESOURCE SETUP
//=============================================================================

void VRStereoOptimizations::SetupResources()
{
	if (!REL::Module::IsVR())
		return;

	auto device = globals::d3d::device;
	auto renderer = globals::game::renderer;

	// Constant buffers
	paramsCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<VRStereoOptParams>());

	// Get main RT dimensions for per-eye calculations
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	D3D11_TEXTURE2D_DESC mainDesc;
	main.texture->GetDesc(&mainDesc);

	// Per-pixel mode texture (R8_UINT, full SBS resolution = both eyes)
	{
		D3D11_TEXTURE2D_DESC modeDesc{};
		modeDesc.Width = mainDesc.Width;
		modeDesc.Height = mainDesc.Height;
		modeDesc.MipLevels = 1;
		modeDesc.ArraySize = 1;
		modeDesc.Format = DXGI_FORMAT_R8_UINT;
		modeDesc.SampleDesc.Count = 1;
		modeDesc.SampleDesc.Quality = 0;
		modeDesc.Usage = D3D11_USAGE_DEFAULT;
		modeDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		modeDesc.CPUAccessFlags = 0;
		modeDesc.MiscFlags = 0;

		texPerPixelMode = eastl::make_unique<Texture2D>(modeDesc);
		texPerPixelMode->CreateSRV(D3D11_SHADER_RESOURCE_VIEW_DESC{
			.Format = DXGI_FORMAT_R8_UINT,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 } });
		texPerPixelMode->CreateUAV(D3D11_UNORDERED_ACCESS_VIEW_DESC{
			.Format = DXGI_FORMAT_R8_UINT,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 } });
	}

	// Depth-stencil state for stencil write pass:
	// Depth test OFF (not rendering geometry), stencil ALWAYS + REPLACE with ref=1
	{
		D3D11_DEPTH_STENCIL_DESC dssDesc{};
		dssDesc.DepthEnable = FALSE;
		dssDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		dssDesc.StencilEnable = TRUE;
		dssDesc.StencilReadMask = 0xFF;
		dssDesc.StencilWriteMask = 0xFF;
		dssDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		dssDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
		dssDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
		dssDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
		dssDesc.BackFace = dssDesc.FrontFace;

		DX::ThrowIfFailed(device->CreateDepthStencilState(&dssDesc, stencilWriteDSS.put()));
	}

	// Rasterizer state for stencil write: no culling, no depth clip
	{
		D3D11_RASTERIZER_DESC rsDesc{};
		rsDesc.FillMode = D3D11_FILL_SOLID;
		rsDesc.CullMode = D3D11_CULL_NONE;
		rsDesc.DepthClipEnable = FALSE;

		DX::ThrowIfFailed(device->CreateRasterizerState(&rsDesc, stencilWriteRS.put()));
	}

	// Read-only depth DSV for stencil write pass: allows simultaneous depth SRV binding.
	// We write stencil but never write depth, so D3D11_DSV_READ_ONLY_DEPTH is safe.
	{
		auto& depthData = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
		if (depthData.views[0] && depthData.texture) {
			D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
			depthData.views[0]->GetDesc(&dsvDesc);
			dsvDesc.Flags = D3D11_DSV_READ_ONLY_DEPTH;

			DX::ThrowIfFailed(device->CreateDepthStencilView(depthData.texture, &dsvDesc, stencilWriteReadOnlyDSV.put()));
		} else {
			logger::warn("[VRStereoOptimizations] Could not create read-only DSV: depth stencil data not available");
		}
	}

	// CAS sharpness parameter buffer (structured buffer SRV to avoid cbuffer conflicts)
	{
		D3D11_BUFFER_DESC bufDesc{};
		bufDesc.ByteWidth = sizeof(float);
		bufDesc.Usage = D3D11_USAGE_DYNAMIC;
		bufDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		bufDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bufDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bufDesc.StructureByteStride = sizeof(float);

		float initSharpness = settings.casStrength;
		D3D11_SUBRESOURCE_DATA initData{};
		initData.pSysMem = &initSharpness;

		DX::ThrowIfFailed(device->CreateBuffer(&bufDesc, &initData, casParamsBuf.put()));

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = 1;
		DX::ThrowIfFailed(device->CreateShaderResourceView(casParamsBuf.get(), &srvDesc, casParamsSRV.put()));
	}

	// CAS output texture (same format as main RT, with UAV capability)
	{
		D3D11_TEXTURE2D_DESC casDesc{};
		casDesc.Width = mainDesc.Width;
		casDesc.Height = mainDesc.Height;
		casDesc.MipLevels = 1;
		casDesc.ArraySize = 1;
		casDesc.Format = mainDesc.Format;
		casDesc.SampleDesc.Count = 1;
		casDesc.SampleDesc.Quality = 0;
		casDesc.Usage = D3D11_USAGE_DEFAULT;
		casDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		casDesc.CPUAccessFlags = 0;
		casDesc.MiscFlags = 0;

		casTex = eastl::make_unique<Texture2D>(casDesc);
		casTex->CreateSRV(D3D11_SHADER_RESOURCE_VIEW_DESC{
			.Format = mainDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 } });
		casTex->CreateUAV(D3D11_UNORDERED_ACCESS_VIEW_DESC{
			.Format = mainDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 } });
	}

	CompileShaders();

	logger::info("[VRStereoOptimizations] Resources created: mode tex {}x{} (full SBS)", mainDesc.Width, mainDesc.Height);
}

void VRStereoOptimizations::CompileShaders()
{
	std::vector<std::pair<const char*, const char*>> csDefines = {
		{ "VR", nullptr },
		{ "FRAMEBUFFER", nullptr }
	};

	std::vector<std::pair<const char*, const char*>> vspsDefines = {
		{ "VR", nullptr }
	};

	if (auto* ptr = Util::CompileShader(L"Data\\Shaders\\VRStereoOptimizations\\StencilCS.hlsl", csDefines, "cs_5_0"))
		stencilCS.attach(reinterpret_cast<ID3D11ComputeShader*>(ptr));
	else
		logger::error("[VRStereoOptimizations] Failed to compile StencilCS");

	{
		auto debugDefines = csDefines;
		debugDefines.push_back({ "DEBUG_DEPTH_MAP", nullptr });
		if (auto* ptr = Util::CompileShader(L"Data\\Shaders\\VRStereoOptimizations\\StencilCS.hlsl", debugDefines, "cs_5_0"))
			stencilDebugDepthMapCS.attach(reinterpret_cast<ID3D11ComputeShader*>(ptr));
		else
			logger::error("[VRStereoOptimizations] Failed to compile StencilCS (DEBUG_DEPTH_MAP)");
	}

	if (auto* ptr = Util::CompileShader(L"Data\\Shaders\\VRStereoOptimizations\\StencilWriteVS.hlsl", vspsDefines, "vs_5_0"))
		stencilWriteVS.attach(reinterpret_cast<ID3D11VertexShader*>(ptr));
	else
		logger::error("[VRStereoOptimizations] Failed to compile StencilWriteVS");

	if (auto* ptr = Util::CompileShader(L"Data\\Shaders\\VRStereoOptimizations\\StencilWritePS.hlsl", vspsDefines, "ps_5_0"))
		stencilWritePS.attach(reinterpret_cast<ID3D11PixelShader*>(ptr));
	else
		logger::error("[VRStereoOptimizations] Failed to compile StencilWritePS");

	if (auto* ptr = Util::CompileShader(L"Data\\Shaders\\VRStereoOptimizations\\ReprojectionCS.hlsl", csDefines, "cs_5_0"))
		reprojectionCS.attach(reinterpret_cast<ID3D11ComputeShader*>(ptr));
	else
		logger::error("[VRStereoOptimizations] Failed to compile ReprojectionCS");

	{
		std::vector<std::pair<const char*, const char*>> casDefines = {};
		if (auto* ptr = Util::CompileShader(L"Data\\Shaders\\VR\\CASCS.hlsl", casDefines, "cs_5_0"))
			casCS.attach(reinterpret_cast<ID3D11ComputeShader*>(ptr));
		else
			logger::error("[VRStereoOptimizations] Failed to compile CASCS");
	}
}

void VRStereoOptimizations::ClearShaderCache()
{
	stencilCS = nullptr;
	stencilDebugDepthMapCS = nullptr;
	stencilWriteVS = nullptr;
	stencilWritePS = nullptr;
	reprojectionCS = nullptr;
	casCS = nullptr;
	dssCache.clear();
}

void VRStereoOptimizations::Reset()
{
	stencilActive = false;
	stencilSwapCount = 0;
}

//=============================================================================
// IMGUI SETTINGS
//=============================================================================

void VRStereoOptimizations::DrawSettings()
{
	const char* modeNames[] = { "Off", "Enable" };
	int currentMode = static_cast<int>(settings.stereoMode);
	if (ImGui::Combo("Feature Enable", &currentMode, modeNames, IM_ARRAYSIZE(modeNames)))
		settings.stereoMode = static_cast<StereoMode>(currentMode);

	// MIP LOD Bias section (always shown, independent of stereo mode)
	ImGui::Separator();
	const char* mipBiasModes[] = { "Off", "All Textures", "Distant Trees" };
	ImGui::Combo("MIP LOD Bias", &settings.mipBiasMode, mipBiasModes, 3);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Off: No MIP bias\nAll Textures: Depth-gated sharpening for all textures\nDistant Trees: Depth-gated sharpening for foliage only");

	if (settings.mipBiasMode > 0) {
		ImGui::SliderFloat("MIP Bias Strength", &settings.mipLodBias, -3.0f, 0.0f, "%.2f");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Negative = sharper. -0.5 subtle, -1.0 moderate, -2.0 aggressive.");
		ImGui::SliderFloat("MIP Near Distance", &settings.mipBiasNearDist, 0.0f, 10000.0f, "%.0f");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Game units. No MIP bias closer than this distance.");
		ImGui::SliderFloat("MIP Far Distance", &settings.mipBiasFarDist, 0.0f, 20000.0f, "%.0f");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Game units. Full MIP bias beyond this distance.\nSmooth ramp between near and far.");
	}
	ImGui::Separator();


	// CAS slider hidden for now — forced to 0
	// ImGui::SliderFloat("CAS Sharpening", &settings.casStrength, 0.0f, 1.0f, "%.2f");
	// if (ImGui::IsItemHovered())
	//	ImGui::SetTooltip("Contrast Adaptive Sharpening (intended for use with TAA).\n0 = disabled, higher = sharper.");
	// ImGui::Separator();

	if (settings.stereoMode == StereoMode::Off)
		return;

	ImGui::SliderFloat("Disocclusion Depth Threshold", &settings.disocclusionDepthThreshold, 0.001f, 0.1f, "%.4f");

	if (globals::state->IsDeveloperMode()) {
		if (ImGui::TreeNode("Debug")) {
			ImGui::SliderFloat("Full Blend Distance", &settings.fullBlendDistance, 0.0f, 10000.0f, "%.0f");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Geometry closer than this distance (game units) is fully shaded in both eyes and bilaterally blended for 2x supersampling. 0 = disabled.");

			ImGui::SliderFloat("POM Depth Scale", &settings.pomDepthScale, 0.0f, 500.0f, "%.1f");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Scale factor for POM depth correction in stereo reprojection.\n1.0 = physical scale. Increase for more visible POM stereo depth.");
			ImGui::Checkbox("Skip Pixel Reprojection", &settings.debugSkipMerge);
			ImGui::Checkbox("Full Blend Depth View", &settings.debugFullBlendDepth);
			ImGui::Checkbox("Debug POM Depth", &settings.debugPOMDepth);
			if (settings.debugFullBlendDepth)
				ImGui::TextColored(ImVec4(0, 1, 1, 1), "  Cyan = full blend zone (closer = stronger tint)");
			ImGui::Text("Stencil swaps this frame: %u", stencilSwapCount);
			ImGui::TreePop();
		}
	}
}

//=============================================================================
// CONSTANT BUFFER UPDATE
//=============================================================================

void VRStereoOptimizations::UpdateConstantBuffer()
{
	float2 resolution = Util::ConvertToDynamic(globals::state->screenSize);

	VRStereoOptParams params{};
	params.FrameDim[0] = resolution.x;
	params.FrameDim[1] = resolution.y;
	params.RcpFrameDim[0] = 1.0f / resolution.x;
	params.RcpFrameDim[1] = 1.0f / resolution.y;
	params.StereoModeValue = static_cast<uint32_t>(settings.stereoMode);
	params.DisocclusionThreshold = settings.disocclusionDepthThreshold;
	params.EdgeDepthThreshold = settings.edgeDepthThreshold;
	params.EdgeWidth = static_cast<uint32_t>(settings.edgeWidth);
	params.QualityJitter[0] = settings.qualityJitterOffset;
	params.QualityJitter[1] = settings.qualityJitterOffset;
	params.FoveatedRadius = settings.foveatedRegionRadius;
	params.FoveatedCenter[0] = settings.foveatedRegionCenterX;
	params.FoveatedCenter[1] = settings.foveatedRegionCenterY;
	params.MinEdgeDistance = settings.minEdgeDistance;
	params.FullBlendDistance = settings.fullBlendDistance;

	paramsCB->Update(params);
}

//=============================================================================
// PHASE 1: STENCIL CLASSIFICATION + WRITE
//=============================================================================

void VRStereoOptimizations::DispatchStencil()
{
	if (!REL::Module::IsVR())
		return;
	if (settings.stereoMode == StereoMode::Off)
		return;
	if (!stencilCS || !stencilWriteVS || !stencilWritePS || !texPerPixelMode || !paramsCB)
		return;

	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "VR Stereo Opt - Stencil");

	if (globals::state->frameAnnotations)
		globals::state->BeginPerfEvent("VR Stereo Opt - Stencil");

	auto context = globals::d3d::context;

	UpdateConstantBuffer();
	auto cbPtr = paramsCB->CB();
	// Use live depth buffer (kMAIN) instead of kPOST_ZPREPASS_COPY — at StartDeferred time,
	// kPOST_ZPREPASS_COPY is stale (previous frame). kMAIN has fresh z-prepass depth so
	// StencilCS can correctly detect sky-vs-geometry edges in the current frame.
	auto renderer = globals::game::renderer;
	auto* depthSRV = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].depthSRV;

	// Dispatch classification CS over Eye 1 region
	// Input: t0 = depth, b1 = params CB
	// Output: u0 = per-pixel mode texture
	{
		ID3D11ShaderResourceView* srvs[1]{ depthSRV };
		ID3D11UnorderedAccessView* uavs[1]{ texPerPixelMode->uav.get() };

		context->CSSetConstantBuffers(1, 1, &cbPtr);
		context->CSSetShaderResources(0, 1, srvs);
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
		auto* activeStencilCS = (settings.debugDepthMap && stencilDebugDepthMapCS) ? stencilDebugDepthMapCS.get() : stencilCS.get();
		context->CSSetShader(activeStencilCS, nullptr, 0);

		uint32_t fullWidth = texPerPixelMode->desc.Width;
		uint32_t fullHeight = texPerPixelMode->desc.Height;
		context->Dispatch((fullWidth + 7) / 8, (fullHeight + 7) / 8, 1);

		// Cleanup CS bindings
		ID3D11ShaderResourceView* nullSRV = nullptr;
		ID3D11UnorderedAccessView* nullUAV = nullptr;
		ID3D11Buffer* nullCB = nullptr;
		context->CSSetShaderResources(0, 1, &nullSRV);
		context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
		context->CSSetConstantBuffers(1, 1, &nullCB);
		context->CSSetShader(nullptr, nullptr, 0);
	}

	// Transfer classification to hardware stencil buffer
	ExecuteStencilWritePass();

	stencilActive = true;
	stencilSwapCount = 0;

	if (globals::state->frameAnnotations)
		globals::state->EndPerfEvent();
}

void VRStereoOptimizations::ExecuteStencilWritePass()
{
	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;

	// ===== SAVE FULL D3D11 PIPELINE STATE =====

	ID3D11RenderTargetView* savedRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
	ID3D11DepthStencilView* savedDSV = nullptr;
	context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs, &savedDSV);

	ID3D11DepthStencilState* savedDSS = nullptr;
	UINT savedStencilRef = 0;
	context->OMGetDepthStencilState(&savedDSS, &savedStencilRef);

	ID3D11BlendState* savedBlendState = nullptr;
	FLOAT savedBlendFactor[4] = {};
	UINT savedSampleMask = 0;
	context->OMGetBlendState(&savedBlendState, savedBlendFactor, &savedSampleMask);

	ID3D11RasterizerState* savedRS = nullptr;
	context->RSGetState(&savedRS);

	D3D11_VIEWPORT savedViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
	UINT numViewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
	context->RSGetViewports(&numViewports, savedViewports);

	ID3D11VertexShader* savedVS = nullptr;
	context->VSGetShader(&savedVS, nullptr, nullptr);

	ID3D11PixelShader* savedPS = nullptr;
	context->PSGetShader(&savedPS, nullptr, nullptr);

	ID3D11GeometryShader* savedGS = nullptr;
	context->GSGetShader(&savedGS, nullptr, nullptr);

	ID3D11InputLayout* savedInputLayout = nullptr;
	context->IAGetInputLayout(&savedInputLayout);

	D3D11_PRIMITIVE_TOPOLOGY savedTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
	context->IAGetPrimitiveTopology(&savedTopology);

	ID3D11ShaderResourceView* savedPSSRVs[2] = {};
	context->PSGetShaderResources(0, 2, savedPSSRVs);

	ID3D11Buffer* savedPSCB = nullptr;
	context->PSGetConstantBuffers(1, 1, &savedPSCB);

	// ===== SET UP STENCIL WRITE PASS =====

	// Use our custom read-only-depth DSV to allow simultaneous depth SRV binding (t1).
	// D3D11_DSV_READ_ONLY_DEPTH permits depth SRV + stencil write simultaneously.
	// Using views[0] would cause D3D11 to silently NULL the depth SRV.
	// depthData.readOnlyViews[0] has BOTH read-only flags and doesn't allow stencil writes.
	context->OMSetRenderTargets(0, nullptr, stencilWriteReadOnlyDSV.get());
	context->OMSetDepthStencilState(stencilWriteDSS.get(), 1);
	context->RSSetState(stencilWriteRS.get());

	// Eye 1 viewport (right half of SBS buffer)
	{
		D3D11_TEXTURE2D_DESC mainDesc;
		renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].texture->GetDesc(&mainDesc);

		D3D11_VIEWPORT vp{};
		vp.TopLeftX = static_cast<float>(mainDesc.Width / 2);
		vp.TopLeftY = 0.0f;
		vp.Width = static_cast<float>(mainDesc.Width / 2);
		vp.Height = static_cast<float>(mainDesc.Height);
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		context->RSSetViewports(1, &vp);
	}

	// Bind shaders and mode texture
	context->VSSetShader(stencilWriteVS.get(), nullptr, 0);
	context->PSSetShader(stencilWritePS.get(), nullptr, 0);
	context->GSSetShader(nullptr, nullptr, 0);

	ID3D11ShaderResourceView* modeSRV = texPerPixelMode->srv.get();
	context->PSSetShaderResources(0, 1, &modeSRV);

	auto* depthSRV = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].depthSRV;
	context->PSSetShaderResources(1, 1, &depthSRV);

	// Bind params CB to pixel shader (CS and PS have separate CB bindings)
	auto cbPtr = paramsCB->CB();
	context->PSSetConstantBuffers(1, 1, &cbPtr);

	// Fullscreen triangle: no VB/IB, procedurally generated in VS
	context->IASetInputLayout(nullptr);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	context->Draw(3, 0);

	// ===== RESTORE FULL D3D11 PIPELINE STATE =====

	ID3D11ShaderResourceView* nullSRVs[2] = {};
	context->PSSetShaderResources(0, 2, nullSRVs);

	context->PSSetConstantBuffers(1, 1, &savedPSCB);

	context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs, savedDSV);
	context->OMSetDepthStencilState(savedDSS, savedStencilRef);
	context->OMSetBlendState(savedBlendState, savedBlendFactor, savedSampleMask);
	context->RSSetState(savedRS);
	context->RSSetViewports(numViewports, savedViewports);
	context->VSSetShader(savedVS, nullptr, 0);
	context->PSSetShader(savedPS, nullptr, 0);
	context->GSSetShader(savedGS, nullptr, 0);
	context->IASetInputLayout(savedInputLayout);
	context->IASetPrimitiveTopology(savedTopology);
	context->PSSetShaderResources(0, 2, savedPSSRVs);

	// Release COM references acquired by Get* calls
	for (auto& rtv : savedRTVs) {
		if (rtv)
			rtv->Release();
	}
	if (savedDSV)
		savedDSV->Release();
	if (savedDSS)
		savedDSS->Release();
	if (savedBlendState)
		savedBlendState->Release();
	if (savedRS)
		savedRS->Release();
	if (savedVS)
		savedVS->Release();
	if (savedPS)
		savedPS->Release();
	if (savedGS)
		savedGS->Release();
	if (savedInputLayout)
		savedInputLayout->Release();
	if (savedPSSRVs[0])
		savedPSSRVs[0]->Release();
	if (savedPSSRVs[1])
		savedPSSRVs[1]->Release();
	if (savedPSCB)
		savedPSCB->Release();
}

void VRStereoOptimizations::PerformLateStencilWrite()
{
	// Placeholder for future multi-pass stencil strategies
}

//=============================================================================
// DSS CACHE: CLONE + STENCIL NOT_EQUAL ENFORCEMENT
//=============================================================================

ID3D11DepthStencilState* VRStereoOptimizations::GetOrCreateModifiedDSS(ID3D11DepthStencilState* originalDSS)
{
	if (!originalDSS || !stencilActive)
		return originalDSS;

	stencilSwapCount++;

	auto it = dssCache.find(originalDSS);
	if (it != dssCache.end())
		return it->second.get();

	// Clone original desc and add read-only stencil NOT_EQUAL test
	D3D11_DEPTH_STENCIL_DESC desc{};
	originalDSS->GetDesc(&desc);

	desc.StencilEnable = TRUE;
	desc.StencilReadMask = 0xFF;
	desc.StencilWriteMask = 0x00;  // Read-only: game rendering must not modify our marks

	// NOT_EQUAL with ref=1: skip pixels where stencil == 1 (MODE_MAIN)
	desc.FrontFace.StencilFunc = D3D11_COMPARISON_NOT_EQUAL;
	desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
	desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	desc.BackFace = desc.FrontFace;

	winrt::com_ptr<ID3D11DepthStencilState> modifiedDSS;
	HRESULT hr = globals::d3d::device->CreateDepthStencilState(&desc, modifiedDSS.put());
	if (FAILED(hr)) {
		logger::warn("[VRStereoOptimizations] Failed to create modified DSS (HRESULT: {:#x})", static_cast<uint32_t>(hr));
		return originalDSS;
	}

	auto* result = modifiedDSS.get();
	dssCache[originalDSS] = std::move(modifiedDSS);

	return result;
}

//=============================================================================
// PHASE 3: REPROJECTION COMPUTE SHADER
//=============================================================================

void VRStereoOptimizations::DispatchReprojection()
{
	if (!REL::Module::IsVR())
		return;
	if (settings.stereoMode == StereoMode::Off)
		return;
	if (!reprojectionCS || !texPerPixelMode || !paramsCB)
		return;
	if (settings.debugSkipMerge)
		return;

	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "VR Stereo Opt - Reprojection");

	if (globals::state->frameAnnotations)
		globals::state->BeginPerfEvent("VR Stereo Opt - Reprojection");

	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	UpdateConstantBuffer();
	auto cbPtr = paramsCB->CB();
	auto* depthSRV = Util::GetCurrentSceneDepthSRV();

	// Bind: t0 = depth, t1 = mode texture, u0 = main UAV, b1 = params
	ID3D11ShaderResourceView* srvs[2]{
		depthSRV,
		texPerPixelMode->srv.get()
	};
	ID3D11UnorderedAccessView* uavs[1]{ main.UAV };

	context->CSSetConstantBuffers(1, 1, &cbPtr);
	context->CSSetShaderResources(0, 2, srvs);
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
	context->CSSetShader(reprojectionCS.get(), nullptr, 0);

	// Dispatch over full SBS texture
	uint32_t fullWidth = texPerPixelMode->desc.Width;
	uint32_t fullHeight = texPerPixelMode->desc.Height;
	context->Dispatch((fullWidth + 7) / 8, (fullHeight + 7) / 8, 1);

	// Cleanup
	ID3D11ShaderResourceView* nullSRVs[2] = {};
	ID3D11UnorderedAccessView* nullUAV = nullptr;
	ID3D11Buffer* nullCB = nullptr;
	context->CSSetShaderResources(0, 2, nullSRVs);
	context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	context->CSSetConstantBuffers(1, 1, &nullCB);
	context->CSSetShader(nullptr, nullptr, 0);

	// Stencil culling is done for this frame
	logger::trace("[VRStereoOptimizations] Frame: stencilSwapCount={}", stencilSwapCount);
	stencilActive = false;

	if (globals::state->frameAnnotations)
		globals::state->EndPerfEvent();
}

void VRStereoOptimizations::DeactivateStencil()
{
	if (!stencilActive)
		return;
	logger::trace("[VRStereoOptimizations] Frame: stencilSwapCount={}", stencilSwapCount);
	stencilActive = false;
}

//=============================================================================
// CAS (CONTRAST ADAPTIVE SHARPENING) - POST-TAA
//=============================================================================

void VRStereoOptimizations::ApplyCAS(RE::RENDER_TARGET a_target)
{
	logger::trace("[VRStereoOptimizations] CAS: entered (strength={}, casCS={}, casTex={}, casParamsBuf={})",
		settings.casStrength, (void*)casCS.get(), (void*)casTex.get(), (void*)casParamsBuf.get());

	if (settings.casStrength <= 0.0f || !casCS || !casTex || !casParamsBuf)
		return;

	if (!REL::Module::IsVR())
		return;

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	// Get the render target that post-processing just wrote to
	auto& target = renderer->GetRuntimeData().renderTargets[a_target];
	if (!target.texture || !target.SRV) {
		logger::trace("[VRStereoOptimizations] CAS: target RT has no texture/SRV, skipping");
		return;
	}

	D3D11_TEXTURE2D_DESC targetDesc;
	target.texture->GetDesc(&targetDesc);
	logger::trace("[VRStereoOptimizations] CAS: dispatching on RT {} ({}x{}, strength={})", (int)a_target, targetDesc.Width, targetDesc.Height, settings.casStrength);

	// Check for dimension/format mismatch with intermediate texture
	D3D11_TEXTURE2D_DESC casTexDesc;
	static_cast<ID3D11Texture2D*>(casTex->resource.get())->GetDesc(&casTexDesc);
	if (casTexDesc.Width != targetDesc.Width || casTexDesc.Height != targetDesc.Height || casTexDesc.Format != targetDesc.Format) {
		logger::info("[VRStereoOptimizations] CAS: recreating casTex to match target ({}x{} fmt={} -> {}x{} fmt={})",
			casTexDesc.Width, casTexDesc.Height, (int)casTexDesc.Format,
			targetDesc.Width, targetDesc.Height, (int)targetDesc.Format);

		D3D11_TEXTURE2D_DESC newDesc{};
		newDesc.Width = targetDesc.Width;
		newDesc.Height = targetDesc.Height;
		newDesc.MipLevels = 1;
		newDesc.ArraySize = 1;
		newDesc.Format = targetDesc.Format;
		newDesc.SampleDesc.Count = 1;
		newDesc.SampleDesc.Quality = 0;
		newDesc.Usage = D3D11_USAGE_DEFAULT;
		newDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		newDesc.CPUAccessFlags = 0;
		newDesc.MiscFlags = 0;

		casTex = eastl::make_unique<Texture2D>(newDesc);
		casTex->CreateSRV(D3D11_SHADER_RESOURCE_VIEW_DESC{
			.Format = targetDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 } });
		casTex->CreateUAV(D3D11_UNORDERED_ACCESS_VIEW_DESC{
			.Format = targetDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 } });
	}

	// Update sharpness parameter via Map/Unmap
	{
		D3D11_MAPPED_SUBRESOURCE mapped;
		if (SUCCEEDED(context->Map(casParamsBuf.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
			*static_cast<float*>(mapped.pData) = settings.casStrength;
			context->Unmap(casParamsBuf.get(), 0);
		}
	}

	// Unbind the RT so we can read from it
	context->OMSetRenderTargets(0, nullptr, nullptr);

	// Dispatch CAS: read from target SRV, write to casTex UAV
	{
		ID3D11ShaderResourceView* views[2] = { target.SRV, casParamsSRV.get() };
		context->CSSetShaderResources(0, 2, views);

		ID3D11UnorderedAccessView* uavs[1] = { casTex->uav.get() };
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

		context->CSSetShader(casCS.get(), nullptr, 0);

		context->Dispatch((targetDesc.Width + 7) / 8, (targetDesc.Height + 7) / 8, 1);
	}

	// Cleanup CS state
	ID3D11ShaderResourceView* nullSRV[2] = { nullptr, nullptr };
	context->CSSetShaderResources(0, 2, nullSRV);
	ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
	context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
	context->CSSetShader(nullptr, nullptr, 0);

	// Copy sharpened result back to the render target
	context->CopyResource(target.texture, casTex->resource.get());

	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
}
