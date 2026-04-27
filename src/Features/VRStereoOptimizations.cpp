#include "VRStereoOptimizations.h"

#include "ExtendedMaterials.h"
#include "Globals.h"
#include "Menu.h"
#include "State.h"
#include "Utils/D3D.h"
#include "Utils/Game.h"
#include "Utils/UI.h"

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
	o_json["DebugFullBlendDepth"] = settings.debugFullBlendDepth;
	o_json["DebugPOMDepth"] = settings.debugPOMDepth;
	o_json["POMDepthScale"] = settings.pomDepthScale;
	o_json["ForwardOcclusionScale"] = settings.forwardOcclusionScale;
}

void VRStereoOptimizations::LoadSettings(json& o_json)
{
	auto loadClampedFloat = [&](const char* key, float& dst, float lo, float hi) {
		if (auto it = o_json.find(key); it != o_json.end() && it->is_number())
			dst = std::clamp(it->get<float>(), lo, hi);
	};
	auto loadBool = [&](const char* key, bool& dst) {
		if (auto it = o_json.find(key); it != o_json.end() && it->is_boolean())
			dst = it->get<bool>();
	};

	if (o_json.contains("StereoMode"))
		settings.stereoMode = o_json["StereoMode"].get<StereoMode>();

	loadClampedFloat("DisocclusionDepthThreshold", settings.disocclusionDepthThreshold, 0.001f, 0.1f);
	loadClampedFloat("QualityJitterOffset", settings.qualityJitterOffset, 0.0f, 1.0f);
	loadClampedFloat("FoveatedRegionRadius", settings.foveatedRegionRadius, 0.0f, 1.0f);
	loadClampedFloat("FoveatedRegionCenterX", settings.foveatedRegionCenterX, 0.0f, 1.0f);
	loadClampedFloat("FoveatedRegionCenterY", settings.foveatedRegionCenterY, 0.0f, 1.0f);
	loadClampedFloat("FullBlendDistance", settings.fullBlendDistance, 0.0f, 50000.0f);
	loadClampedFloat("POMDepthScale", settings.pomDepthScale, 0.0f, 500.0f);
	loadClampedFloat("ForwardOcclusionScale", settings.forwardOcclusionScale, 0.0f, 10.0f);

	loadBool("UseEyeTracking", settings.useEyeTracking);
	loadBool("DebugVisualization", settings.debugVisualization);
	loadBool("DebugSkipMerge", settings.debugSkipMerge);
	loadBool("DebugForceAllStencil", settings.debugForceAllStencil);
	loadBool("DebugForceAllReprojectCS", settings.debugForceAllReprojectCS);
	loadBool("DebugDepthMap", settings.debugDepthMap);
	loadBool("DebugFullBlendDepth", settings.debugFullBlendDepth);
	loadBool("DebugPOMDepth", settings.debugPOMDepth);
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
	paramsCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<VRStereoOptParams>(), "VRStereoOpt::ParamsCB");

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

		texPerPixelMode = eastl::make_unique<Texture2D>(modeDesc, "VRStereoOpt::PerPixelMode");
		texPerPixelMode->CreateSRV(D3D11_SHADER_RESOURCE_VIEW_DESC{
			.Format = DXGI_FORMAT_R8_UINT,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 } });
		texPerPixelMode->CreateUAV(D3D11_UNORDERED_ACCESS_VIEW_DESC{
			.Format = DXGI_FORMAT_R8_UINT,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 } });
	}

	// POM offset texture (R16_FLOAT, full SBS resolution)
	// Written by Lighting PS (u7) for POM-active pixels, read by StereoBlendCS for depth-aware reprojection.
	// Replaces the former overloading of Reflectance.w, so Reflectance stays R11G11B10 with no alpha.
	{
		D3D11_TEXTURE2D_DESC pomDesc{};
		pomDesc.Width = mainDesc.Width;
		pomDesc.Height = mainDesc.Height;
		pomDesc.MipLevels = 1;
		pomDesc.ArraySize = 1;
		pomDesc.Format = DXGI_FORMAT_R16_FLOAT;
		pomDesc.SampleDesc.Count = 1;
		pomDesc.SampleDesc.Quality = 0;
		pomDesc.Usage = D3D11_USAGE_DEFAULT;
		pomDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		pomDesc.CPUAccessFlags = 0;
		pomDesc.MiscFlags = 0;

		texPomOffset = eastl::make_unique<Texture2D>(pomDesc, "VRStereoOpt::PomOffset");
		texPomOffset->CreateSRV(D3D11_SHADER_RESOURCE_VIEW_DESC{
			.Format = DXGI_FORMAT_R16_FLOAT,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 } });
		texPomOffset->CreateUAV(D3D11_UNORDERED_ACCESS_VIEW_DESC{
			.Format = DXGI_FORMAT_R16_FLOAT,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 } });
	}

	// Depth-stencil state for stencil write pass:
	// Depth test OFF (not rendering geometry), depth writes OFF, stencil ALWAYS + REPLACE with ref=1.
	// We use the normal (writable) kMAIN DSV — no simultaneous SRV binding needed.
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
		Util::SetResourceName(stencilWriteDSS.get(), "VRStereoOpt::StencilWriteDSS");
	}

	// Rasterizer state for stencil write: no culling, no depth clip
	{
		D3D11_RASTERIZER_DESC rsDesc{};
		rsDesc.FillMode = D3D11_FILL_SOLID;
		rsDesc.CullMode = D3D11_CULL_NONE;
		rsDesc.DepthClipEnable = FALSE;

		DX::ThrowIfFailed(device->CreateRasterizerState(&rsDesc, stencilWriteRS.put()));
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
}

void VRStereoOptimizations::ClearShaderCache()
{
	stencilCS = nullptr;
	stencilDebugDepthMapCS = nullptr;
	stencilWriteVS = nullptr;
	stencilWritePS = nullptr;
	dssCache.clear();
}

void VRStereoOptimizations::Reset()
{
	stencilActive = false;
	stencilSwapCount = 0;
}

void VRStereoOptimizations::ClearPomOffsetTexture()
{
	if (!texPomOffset)
		return;
	const float clearValue[4] = { kPomOffsetNoData, kPomOffsetNoData, kPomOffsetNoData, kPomOffsetNoData };
	globals::d3d::context->ClearUnorderedAccessViewFloat(texPomOffset->uav.get(), clearValue);
}

//=============================================================================
// IMGUI SETTINGS
//=============================================================================

void VRStereoOptimizations::DrawSettings()
{
	const char* modeNames[] = { "Off", "Enable" };
	int currentMode = static_cast<int>(settings.stereoMode);
	if (ImGui::Combo("Enable Stereo Reprojection", &currentMode, modeNames, IM_ARRAYSIZE(modeNames)))
		settings.stereoMode = static_cast<StereoMode>(currentMode);
	Util::AddTooltip("Reprojects Eye 0 (left) pixels into Eye 1 (right) using depth and motion data,\nskipping redundant full shading where the views overlap.\nReduces GPU cost in VR by shading each pixel fewer times per frame.");

	if (globals::game::isVR && settings.stereoMode == StereoMode::Enable && !loaded) {
		const auto& themeSettings = Menu::GetSingleton()->GetTheme();
		ImGui::TextColored(themeSettings.StatusPalette.RestartNeeded,
			"Restart is required to enable VR stereo reprojection.");
	}
	if (settings.stereoMode == StereoMode::Off)
		return;

	ImGui::SliderFloat("Disocclusion Depth Threshold", &settings.disocclusionDepthThreshold, 0.001f, 0.1f, "%.4f");

	ImGui::SliderFloat("Forward Occlusion Scale", &settings.forwardOcclusionScale, 0.0f, 1.0f, "%.2f");
	Util::AddTooltip("Prevents Eye 0 silhouette edges from bleeding onto Eye 1 backgrounds.\nFires when Eye 0 depth is within this fraction of Eye 1 depth (e.g. 0.5 = Eye 0 less than 2x Eye 1 depth).\nLower = more aggressive. 0 = disabled.");

	if (globals::state->IsDeveloperMode()) {
		if (ImGui::TreeNode("Debug")) {
			ImGui::SliderFloat("Full Blend Distance", &settings.fullBlendDistance, 0.0f, 10000.0f, "%.0f");
			Util::AddTooltip("Geometry closer than this distance (game units) is fully shaded in both eyes and bilaterally blended for 2x supersampling. 0 = disabled.");

			ImGui::SliderFloat("POM Depth Scale", &settings.pomDepthScale, 0.0f, 500.0f, "%.1f");
			Util::AddTooltip("Scale factor for POM depth correction in stereo reprojection.\n1.0 = physical scale. Increase for more visible POM stereo depth.");
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
	params.EdgeWidth = 2;
	params.QualityJitter[0] = settings.qualityJitterOffset;
	params.QualityJitter[1] = settings.qualityJitterOffset;
	params.FoveatedRadius = settings.foveatedRegionRadius;
	params.FoveatedCenter[0] = settings.foveatedRegionCenterX;
	params.FoveatedCenter[1] = settings.foveatedRegionCenterY;
	params.MinEdgeDistance = settings.minEdgeDistance;
	params.FullBlendDistance = settings.fullBlendDistance;
	params.ForwardOcclusionScale = settings.forwardOcclusionScale;

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
	if (!stencilCS || !stencilWriteVS || !stencilWritePS || !texPerPixelMode || !paramsCB ||
		!stencilWriteDSS || !stencilWriteRS)
		return;

	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "VR Stereo Opt - Stencil");

	if (globals::state->frameAnnotations)
		globals::state->BeginPerfEvent("VR Stereo Opt - Stencil");

	auto context = globals::d3d::context;

	UpdateConstantBuffer();
	auto cbPtr = paramsCB->CB();
	// Use the same depth source as the rest of the deferred pipeline.
	// kMAIN.depthSRV is unpopulated at StartDeferred time (z-prepass has not written to it yet).
	// GetCurrentSceneDepthSRV() returns TerrainBlending's blended depth when active, or
	// kPOST_ZPREPASS_COPY otherwise — both have valid z-prepass data by this point.
	auto* depthSRV = Util::GetCurrentSceneDepthSRV();
	if (!depthSRV) {
		logger::warn("[VRStereoOptimizations] DispatchStencil: depthSRV is null, skipping");
		if (globals::state->frameAnnotations)
			globals::state->EndPerfEvent();
		return;
	}

	// Dispatch classification CS over Eye 1 region
	// Input: t0 = depth, b1 = params CB
	// Output: u0 = per-pixel mode texture
	{
		TracyD3D11Zone(globals::state->tracyCtx, "StereoOpt - Mode Classify");

		{
			TracyD3D11Zone(globals::state->tracyCtx, "StereoOpt - Mode Classify Bind");

			ID3D11ShaderResourceView* srvs[1]{ depthSRV };
			ID3D11UnorderedAccessView* uavs[1]{ texPerPixelMode->uav.get() };

			context->CSSetConstantBuffers(1, 1, &cbPtr);
			context->CSSetShaderResources(0, 1, srvs);
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			auto* activeStencilCS = (settings.debugDepthMap && stencilDebugDepthMapCS) ? stencilDebugDepthMapCS.get() : stencilCS.get();
			context->CSSetShader(activeStencilCS, nullptr, 0);
		}

		{
			TracyD3D11Zone(globals::state->tracyCtx, "StereoOpt - Mode Classify Dispatch");

			uint32_t fullWidth = texPerPixelMode->desc.Width;
			uint32_t fullHeight = texPerPixelMode->desc.Height;
			context->Dispatch((fullWidth + 7) / 8, (fullHeight + 7) / 8, 1);
		}

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
	{
		TracyD3D11Zone(globals::state->tracyCtx, "StereoOpt - Stencil Write");
		ExecuteStencilWritePass();
	}

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

	ID3D11ShaderResourceView* savedPSSRV = nullptr;
	context->PSGetShaderResources(0, 1, &savedPSSRV);

	ID3D11Buffer* savedPSCB = nullptr;
	context->PSGetConstantBuffers(1, 1, &savedPSCB);

	// ===== SET UP STENCIL WRITE PASS =====

	// Clear stencil buffer to 0 before writing classification.
	// The engine's z-prepass may have written stencil values for rendered geometry.
	// Without this clear, non-discarded pixels in StencilWritePS could inherit engine stencil
	// values that match our NOT_EQUAL ref=1 culling test and incorrectly skip geometry pixels.
	// StencilWritePS no longer binds a depth SRV, so we can use the normal writable DSV here.
	{
		auto& depthData = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
		context->ClearDepthStencilView(depthData.views[0], D3D11_CLEAR_STENCIL, 1.0f, 0);
	}

	// Use the normal DSV for stencil writes — no depth SRV is bound simultaneously,
	// so there is no D3D11 resource hazard and stencil writes are not suppressed.
	auto& depthData = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	context->OMSetRenderTargets(0, nullptr, depthData.views[0]);
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

	// Bind params CB to pixel shader (CS and PS have separate CB bindings)
	auto cbPtr = paramsCB->CB();
	context->PSSetConstantBuffers(1, 1, &cbPtr);

	// Fullscreen triangle: no VB/IB, procedurally generated in VS
	context->IASetInputLayout(nullptr);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	context->Draw(3, 0);

	// ===== RESTORE FULL D3D11 PIPELINE STATE =====

	ID3D11ShaderResourceView* nullSRV = nullptr;
	context->PSSetShaderResources(0, 1, &nullSRV);

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
	context->PSSetShaderResources(0, 1, &savedPSSRV);

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
	if (savedPSSRV)
		savedPSSRV->Release();
	if (savedPSCB)
		savedPSCB->Release();
}

//=============================================================================
// DSS CACHE: CLONE + STENCIL NOT_EQUAL ENFORCEMENT
//=============================================================================

ID3D11DepthStencilState* VRStereoOptimizations::GetOrCreateModifiedDSS(ID3D11DepthStencilState* originalDSS)
{
	if (!stencilActive)
		return originalDSS;

	// Check cache (nullptr is a valid key — represents D3D11 default state)
	if (auto it = dssCache.find(originalDSS); it != dssCache.end())
		return it->second.get();

	D3D11_DEPTH_STENCIL_DESC desc;
	if (originalDSS) {
		originalDSS->GetDesc(&desc);
	} else {
		// D3D11 default state: depth enabled, stencil disabled
		desc = {};
		desc.DepthEnable = TRUE;
		desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		desc.DepthFunc = D3D11_COMPARISON_LESS;
		desc.StencilEnable = FALSE;
		desc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
		desc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
		desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
		desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		desc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
		desc.BackFace = desc.FrontFace;
	}

	desc.StencilEnable = TRUE;
	desc.StencilReadMask = 0xFF;
	desc.StencilWriteMask = 0x00;

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
void VRStereoOptimizations::DeactivateStencil()
{
	if (!stencilActive)
		return;
	logger::trace("[VRStereoOptimizations] Frame: stencilSwapCount={}", stencilSwapCount);
	stencilActive = false;
}
