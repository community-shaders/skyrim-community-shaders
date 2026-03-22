#include "VR.h"
#include "Menu.h"
#include "RE/B/BSOpenVR.h"
#include "RE/P/PlayerCharacter.h"
#include "Upscaling.h"
#include "VR/OpenVRDetection.h"

#include "State.h"
#include "Utils/VRUtils.h"

#include <d3d11.h>
#include <imgui_impl_dx11.h>
#include <openvr.h>

using AttachMode = VR::Settings::OverlayAttachMode;

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	VR::Settings,
	EnableDepthBufferCullingInterior,
	EnableDepthBufferCullingExterior,
	MinOccludeeBoxExtent,
	VRMenuScale,
	VRMenuPositioningMethod,
	attachMode,
	VRMenuAttachController,
	VRMenuOffsetX,
	VRMenuOffsetY,
	VRMenuOffsetZ,
	VRMenuControllerOffsetX,
	VRMenuControllerOffsetY,
	VRMenuControllerOffsetZ,
	mouseDeadzone,
	mouseSpeed,
	dragHighlightColor,
	VRMenuOpenKeys,
	VRMenuCloseKeys,
	VROverlayOpenKeys,
	VROverlayCloseKeys,
	comboTimeout,
	EnableDragToReposition,
	kAutoHideSeconds,
	VRMenuAutoResetDistance,
	EnableWandPointing,
	EnableStereoBlend,
	StereoBlendDepthSigma,
	StereoBlendMaxFactor,
	StereoBlendColorThreshold)

//=============================================================================
// FEATURE BASE CLASS OVERRIDES
//=============================================================================

void VR::LoadSettings(json& o_json)
{
	settings = o_json.get<Settings>();
	settings.ClampToValidRanges();
}

void VR::SaveSettings(json& o_json)
{
	o_json = settings;
}

void VR::RestoreDefaultSettings()
{
	settings = {};
}

void VR::SetupResources()
{
	// Compile stereo blend compute shader and create copy texture
	std::vector<std::pair<const char*, const char*>> defines = { { "VR", "" }, { "FRAMEBUFFER", "" } };
	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\VR\\StereoBlendCS.hlsl", defines, "cs_5_0")))
		stereoBlendCS.attach(rawPtr);

	auto backCheckDefines = defines;
	backCheckDefines.push_back({ "DEBUG_BACKCHECK", "" });
	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\VR\\StereoBlendCS.hlsl", backCheckDefines, "cs_5_0")))
		stereoBlendDebugBackCheckCS.attach(rawPtr);

	auto blendWeightDefines = defines;
	blendWeightDefines.push_back({ "DEBUG_BLEND_WEIGHT", "" });
	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\VR\\StereoBlendCS.hlsl", blendWeightDefines, "cs_5_0")))
		stereoBlendDebugBlendWeightCS.attach(rawPtr);

	auto edgeDetectionDefines = defines;
	edgeDetectionDefines.push_back({ "DEBUG_EDGE_DETECTION", "" });
	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\VR\\StereoBlendCS.hlsl", edgeDetectionDefines, "cs_5_0")))
		stereoBlendDebugEdgeDetectionCS.attach(rawPtr);

	auto renderer = globals::game::renderer;
	auto mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	D3D11_TEXTURE2D_DESC mainDesc;
	mainTex.texture->GetDesc(&mainDesc);
	mainDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	mainDesc.MiscFlags = 0;
	stereoBlendCopyTex = eastl::make_unique<Texture2D>(mainDesc);
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
		.Format = mainDesc.Format,
		.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
		.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
	};
	stereoBlendCopyTex->CreateSRV(srvDesc);
	stereoBlendCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<StereoBlendCB>());

	DetectOpenVRInfo();

	if (openVRInfo.isAvailable) {
		logger::info("OpenVR DLL detected:");
		logger::info("  Path: {}", openVRInfo.dllPath);
		logger::info("  Version: {}", openVRInfo.version);
		logger::info("  Size: {} bytes", openVRInfo.fileSize);
		logger::info("  Modified: {}", openVRInfo.modificationTime);
		logger::info("  Runtime: {}", VRDetection::RuntimeTypeToString(openVRInfo.runtimeType));
		logger::info("  Interface probing: {}", openVRInfo.probingSucceeded ? "Passed" : "Failed");
		logger::info("    Overlay (IVROverlay_016): {}", openVRInfo.hasOverlayInterface ? "Yes" : "No");
		logger::info("    System (IVRSystem_017): {}", openVRInfo.hasSystemInterface ? "Yes" : "No");
		logger::info("    Compositor (IVRCompositor_021): {}", openVRInfo.hasCompositorInterface ? "Yes" : "No");
		logger::info("  Compatible: {}", openVRInfo.isCompatible ? "Yes" : "No");

		if (!openVRInfo.isCompatible) {
			if (globals::state->IsDeveloperMode()) {
				logger::info("OpenVR not natively compatible, but developer mode is active - VR menus enabled");
			} else {
				logger::info("OpenVR version is incompatible. Community Shaders VR menus will be disabled for stability");
			}
		}
	} else {
		logger::info("OpenVR DLL not available in current process");
	}
}

void VR::PostPostLoad()
{
	gDepthBufferCulling = reinterpret_cast<bool*>(REL::Offset(0x1EC6B88).address());
	if (!gDepthBufferCulling) {
		static bool s_defaultDepthBufferCulling = false;
		gDepthBufferCulling = &s_defaultDepthBufferCulling;
		logger::warn("VR: gDepthBufferCulling address not found - using fallback default (false)");
	}

	gMinOccludeeBoxExtent = reinterpret_cast<float*>(REL::Offset(0x1ED64E8).address());
	if (!gMinOccludeeBoxExtent) {
		static float s_defaultMinOccludeeBoxExtent = 10.0f;
		gMinOccludeeBoxExtent = &s_defaultMinOccludeeBoxExtent;
		logger::warn("VR: gMinOccludeeBoxExtent address not found - using fallback default (10.0)");
	}

	// Migration: Fix legacy overlay keybinds
	if (settings.VROverlayCloseKeys.size() == 1) {
		auto& closeKey = settings.VROverlayCloseKeys[0];
		if (closeKey.GetDevice() == ControllerDevice::Keyboard && closeKey.GetKey() == 32) {
			settings.VROverlayCloseKeys[0] = InputCombo::Primary(32);
			logger::info("VR: Migrated VROverlayCloseKeys from Keyboard(32) to Primary(32)");
		}
	}
	if (settings.VROverlayOpenKeys.size() == 1) {
		auto& openKey = settings.VROverlayOpenKeys[0];
		if (openKey.GetDevice() == ControllerDevice::Keyboard && openKey.GetKey() == 32) {
			settings.VROverlayOpenKeys[0] = InputCombo::Secondary(32);
			logger::info("VR: Migrated VROverlayOpenKeys from Keyboard(32) to Secondary(32)");
		}
	}

	REL::safe_write(REL::RelocationID(0, 0, 69528).address() + REL::Relocate(0, 0, 0xD9) + 0x2, 0x148);
	REL::safe_write(REL::RelocationID(0, 0, 69528).address() + REL::Relocate(0, 0, 0xE5) + 0x2, 0x14C);
	REL::safe_write(REL::RelocationID(0, 0, 69528).address() + REL::Relocate(0, 0, 0xF1) + 0x2, 0x150);
}

void VR::DataLoaded()
{
	// Initialize occlusion culling based on user settings and current interior/exterior state.
	UpdateDepthBufferCulling();

	if (gMinOccludeeBoxExtent) {
		*gMinOccludeeBoxExtent = settings.MinOccludeeBoxExtent;
	} else {
		logger::warn("VR::DataLoaded: gMinOccludeeBoxExtent is null, skipping assignment");
	}
}

void VR::EarlyPrepass()
{
	// Apply culling setting each prepass based on current interior/exterior state.
	UpdateDepthBufferCulling();
}

//=============================================================================
// OVERLAY SUBMIT AND DEPTH BUFFER CULLING
//=============================================================================

void VR::RecreateOverlayTexturesIfNeeded()
{
	Util::CreateOverlayTextureAndRTV(globals::d3d::device, Config::kOverlayWidth, Config::kOverlayHeight, menuTexture.put(), menuRTV.put());
}

void VR::SubmitOverlayFrame()
{
	InstallSubmitHook();

	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	if (!openvr || !openvr->vrSystem) {
		return;
	}

	auto& enabled = globals::menu->IsEnabled;
	auto& overlayVisible = globals::menu->overlayVisible;

	if ((enabled || overlayVisible || settings.kAutoHideSeconds > 0) && menuTexture.get() && menuRTV.get()) {
		UpdateFixedWorldPositioning();
		UpdateOverlayDrag();

		ID3D11RenderTargetView* oldRTV = nullptr;
		globals::d3d::context->OMGetRenderTargets(1, &oldRTV, nullptr);
		ID3D11RenderTargetView* menuRTVPtr = menuRTV.get();
		globals::d3d::context->OMSetRenderTargets(1, &menuRTVPtr, nullptr);
		float clearColor[4] = { 0, 0, 0, 0 };
		globals::d3d::context->ClearRenderTargetView(menuRTV.get(), clearColor);
		ImGui::Render();
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		globals::d3d::context->OMSetRenderTargets(1, &oldRTV, nullptr);

		bool beingDragged = settings.EnableDragToReposition && overlayDragState.dragging;
		Util::ApplyHighlightTintToTexture(menuTexture.get(), beingDragged, settings.dragHighlightColor);

		if (oldRTV)
			oldRTV->Release();
	}
}

// Helper to centralize VR depth buffer culling logic, reducing duplication between DataLoaded, EarlyPrepass, and Settings UI.
void VR::UpdateDepthBufferCulling()
{
	if (!gDepthBufferCulling) {
		return;
	}

	const auto* tes = globals::game::tes;
	const bool inInterior = tes && tes->interiorCell != nullptr;
	const bool desired = inInterior ? settings.EnableDepthBufferCullingInterior : settings.EnableDepthBufferCullingExterior;

	const bool previous = *gDepthBufferCulling;
	*gDepthBufferCulling = desired;

	if (previous != desired) {
		logger::info("VR depth buffer culling set to {}", desired);
	}
}

//=============================================================================
// OPENVR VERSION DETECTION AND COMPATIBILITY
//=============================================================================

void VR::DetectOpenVRInfo()
{
	openVRInfo = {};

	auto result = VRDetection::Detect();

	openVRInfo.isAvailable = result.isAvailable;
	openVRInfo.isCompatible = result.isCompatible;
	openVRInfo.dllPath = result.dllPath;
	openVRInfo.version = result.version;
	openVRInfo.fileSize = result.fileSize;
	openVRInfo.modificationTime = result.modificationTime;
	openVRInfo.hasOverlayInterface = result.hasOverlayInterface;
	openVRInfo.hasSystemInterface = result.hasSystemInterface;
	openVRInfo.hasCompositorInterface = result.hasCompositorInterface;
	openVRInfo.runtimeType = result.runtimeType;
	openVRInfo.probingSucceeded = result.probingSucceeded;
}

bool VR::IsOpenVRCompatible() const
{
	return globals::game::isVR && openVRInfo.isCompatible;
}
