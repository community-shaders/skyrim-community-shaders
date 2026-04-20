#include "HDRDisplay.h"

#include "PCH.h"

#include "Buffer.h"
#include "Globals.h"
#include "LinearLighting.h"
#include "ShaderCache.h"
#include "State.h"
#include "Upscaling.h"
#include "Util.h"
#include <dxgi1_4.h>
#include <dxgi1_6.h>
#include <imgui.h>

#ifndef NTDDI_WIN11_GE
#	define NTDDI_WIN11_GE 0x0A000010
#endif

// Win11 24H2 structures - define if SDK doesn't have them
#ifndef DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO_2
#	define DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO_2 ((DISPLAYCONFIG_DEVICE_INFO_TYPE)13)

typedef enum
{
	DISPLAYCONFIG_ADVANCED_COLOR_MODE_SDR = 0,
	DISPLAYCONFIG_ADVANCED_COLOR_MODE_WCG = 1,
	DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR = 2
} DISPLAYCONFIG_ADVANCED_COLOR_MODE;

typedef struct DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2
{
	DISPLAYCONFIG_DEVICE_INFO_HEADER header;
	union
	{
		struct
		{
			UINT32 advancedColorSupported: 1;
			UINT32 advancedColorActive: 1;
			UINT32 reserved1: 1;
			UINT32 advancedColorLimitedByPolicy: 1;
			UINT32 highDynamicRangeSupported: 1;
			UINT32 highDynamicRangeUserEnabled: 1;
			UINT32 wideColorEnforced: 1;
			UINT32 reserved: 25;
		};
		UINT32 value;
	};
	DISPLAYCONFIG_COLOR_ENCODING colorEncoding;
	UINT32 bitsPerColorChannel;
	DISPLAYCONFIG_ADVANCED_COLOR_MODE activeColorMode;
} DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2;
#endif

// HDR display detection
// Credits: Luma Framework by Filippo Tarpini (MIT License)
// https://github.com/Filoppi/Luma-Framework/blob/f1fbc2a36f2d24fd551721ce90f26821a8e754c1/Source/Core/utils/display.hpp
namespace
{
	bool GetDisplayConfigPathInfo(HWND hwnd, DISPLAYCONFIG_PATH_INFO& outPathInfo)
	{
		uint32_t pathCount, modeCount;
		if (ERROR_SUCCESS != GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount))
			return false;

		std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
		std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
		if (ERROR_SUCCESS != QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr))
			return false;

		const HMONITOR monitorFromWindow = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL);
		for (auto& pathInfo : paths) {
			if (pathInfo.flags & DISPLAYCONFIG_PATH_ACTIVE && pathInfo.sourceInfo.statusFlags & DISPLAYCONFIG_SOURCE_IN_USE) {
				const bool bVirtual = pathInfo.flags & DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE;
				const uint32_t modeIndex = bVirtual ? pathInfo.sourceInfo.sourceModeInfoIdx : pathInfo.sourceInfo.modeInfoIdx;
				if (modeIndex == DISPLAYCONFIG_PATH_MODE_IDX_INVALID || modeIndex >= modeCount)
					continue;
				const DISPLAYCONFIG_SOURCE_MODE& sourceMode = modes[modeIndex].sourceMode;

				RECT rect{ sourceMode.position.x, sourceMode.position.y, sourceMode.position.x + (LONG)sourceMode.width, sourceMode.position.y + (LONG)sourceMode.height };
				if (!IsRectEmpty(&rect)) {
					const HMONITOR monitorFromMode = MonitorFromRect(&rect, MONITOR_DEFAULTTONULL);
					if (monitorFromMode != nullptr && monitorFromMode == monitorFromWindow) {
						outPathInfo = pathInfo;
						return true;
					}
				}
			}
		}
		return false;
	}

	bool GetAdvancedColorInfo(HWND hwnd, DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO& outColorInfo)
	{
		DISPLAYCONFIG_PATH_INFO pathInfo{};
		if (GetDisplayConfigPathInfo(hwnd, pathInfo)) {
			DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO colorInfo{};
			colorInfo.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
			colorInfo.header.size = sizeof(colorInfo);
			colorInfo.header.adapterId = pathInfo.targetInfo.adapterId;
			colorInfo.header.id = pathInfo.targetInfo.id;
			if (ERROR_SUCCESS == DisplayConfigGetDeviceInfo(&colorInfo.header)) {
				outColorInfo = colorInfo;
				return true;
			}
		}
		return false;
	}

	// Win11 24H2+ API - uses runtime detection, will fail gracefully on older Windows
	bool GetAdvancedColorInfo2(HWND hwnd, DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2& outColorInfo2)
	{
		DISPLAYCONFIG_PATH_INFO pathInfo{};
		if (GetDisplayConfigPathInfo(hwnd, pathInfo)) {
			DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2 colorInfo2{};
			colorInfo2.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO_2;
			colorInfo2.header.size = sizeof(colorInfo2);
			colorInfo2.header.adapterId = pathInfo.targetInfo.adapterId;
			colorInfo2.header.id = pathInfo.targetInfo.id;
			// This will fail on older Windows versions that don't support the API
			if (ERROR_SUCCESS == DisplayConfigGetDeviceInfo(&colorInfo2.header)) {
				outColorInfo2 = colorInfo2;
				return true;
			}
		}
		return false;
	}

	bool CheckSwapChainHDRSupport(IDXGISwapChain* swapChain, bool& supported, bool& enabled)
	{
		winrt::com_ptr<IDXGIOutput> output;
		if (SUCCEEDED(swapChain->GetContainingOutput(output.put()))) {
			winrt::com_ptr<IDXGIOutput6> output6;
			if (SUCCEEDED(output->QueryInterface(IID_PPV_ARGS(output6.put())))) {
				DXGI_OUTPUT_DESC1 desc1;
				if (SUCCEEDED(output6->GetDesc1(&desc1))) {
					enabled = desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ||
					          desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
					supported |= enabled;
					logger::debug("[HDR] DXGI output detection: colorSpace={}, maxLuminance={}", static_cast<int>(desc1.ColorSpace), desc1.MaxLuminance);
				}
			}
		}

		winrt::com_ptr<IDXGISwapChain3> swapChain3;
		if (SUCCEEDED(swapChain->QueryInterface(IID_PPV_ARGS(swapChain3.put())))) {
			UINT colorSpaceSupported = 0;
			if (SUCCEEDED(swapChain3->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, &colorSpaceSupported))) {
				supported |= (colorSpaceSupported & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) != 0;
			}
			if (SUCCEEDED(swapChain3->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, &colorSpaceSupported))) {
				supported |= (colorSpaceSupported & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) != 0;
			}
		}
		return true;
	}

	bool IsHDRSupportedAndEnabled(HWND hwnd, bool& supported, bool& enabled, IDXGISwapChain* swapChain = nullptr)
	{
		supported = false;
		enabled = false;

		// Try Windows 11 24H2+ API first - distinguishes HDR from WCG
		DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2 colorInfo2{};
		if (GetAdvancedColorInfo2(hwnd, colorInfo2)) {
			// WCG (Wide Color Gamut) allows wider color range without higher brightness peak
			// We only consider true HDR mode, not WCG
			enabled = colorInfo2.activeColorMode == DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR;
			supported = enabled || (colorInfo2.highDynamicRangeSupported && !colorInfo2.advancedColorLimitedByPolicy);
			// Copy bitfield members to avoid non-const reference binding issues
			UINT32 hdrSupported = colorInfo2.highDynamicRangeSupported;
			UINT32 limitedByPolicy = colorInfo2.advancedColorLimitedByPolicy;
			logger::debug("[HDR] Win11 24H2 detection: activeColorMode={}, hdrSupported={}, limitedByPolicy={}",
				static_cast<int>(colorInfo2.activeColorMode), hdrSupported, limitedByPolicy);
			return true;
		}

		// Fallback for older Windows versions
		DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO colorInfo{};
		if (GetAdvancedColorInfo(hwnd, colorInfo)) {
			enabled = colorInfo.advancedColorEnabled;
			supported = enabled || (colorInfo.advancedColorSupported && !colorInfo.advancedColorForceDisabled);
			// Copy bitfield members to avoid non-const reference binding issues
			UINT32 advancedEnabled = colorInfo.advancedColorEnabled;
			UINT32 advancedSupported = colorInfo.advancedColorSupported;
			UINT32 forceDisabled = colorInfo.advancedColorForceDisabled;
			logger::debug("[HDR] Legacy detection: advancedColorEnabled={}, advancedColorSupported={}, forceDisabled={}",
				advancedEnabled, advancedSupported, forceDisabled);
			return true;
		}

		// Last resort: check swap chain color space support
		if (swapChain) {
			__try {
				CheckSwapChainHDRSupport(swapChain, supported, enabled);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				logger::warn("[HDR] Exception during swap chain HDR detection (possibly due to Streamline interposer), skipping swap chain queries");
			}
		}

		return false;
	}

	// Hook structs for the HDR pipeline - installed in PostPostLoad when Upscaling is not loaded.
	// When Upscaling IS loaded, it installs equivalent hooks covering the same addresses.
	struct HDR_Main_PostProcessing
	{
		static void thunk(RE::ImageSpaceManager* a_this, uint32_t a3, RE::RENDER_TARGET a_target, void* a_4, bool a_5)
		{
			auto* hdr = &globals::features::hdrDisplay;
			hdr->RedirectFramebuffer();
			func(a_this, a3, a_target, a_4, a_5);
			hdr->RestoreFramebuffer();

			// VR: RedirectFramebuffer made ISHDR write to hdrTexture (float16); after
			// RestoreFramebuffer kFRAMEBUFFER reverts to its original texture.
			// ISCopy reads kFRAMEBUFFER.SRV to distribute the frame to the HMD and
			// companion window, so we must write the tonemapped content back into
			// kFRAMEBUFFER before ISCopy runs.
			//
			// TODO (future HDR HMD support): The correct pipeline is to run the full
			// HDR composite (PQ encode, paper white, peak nits) HERE, writing the
			// result back to kFRAMEBUFFER so ISCopy distributes HDR-processed content
			// to both the HMD and companion at their native sizes.  The post-Present
			// ApplyHDR path cannot do this correctly because ISCopy has already run
			// and the companion back buffer (1024x1024) does not match outputTexture
			// (sized from kMAIN).  Requires hooking the ISCopy vfunc to fire
			// HDROutputCS before distribution.
			if (globals::game::isVR && hdr->settings.enableHDR &&
				hdr->hdrTexture && hdr->hdrTexture->resource) {
				auto& fb = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kFRAMEBUFFER];
				if (fb.texture)
					globals::d3d::context->CopyResource(fb.texture, hdr->hdrTexture->resource.get());
			}
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct HDR_MenuManagerDrawInterfaceStartHook
	{
		static void thunk(int64_t a1)
		{
			globals::features::hdrDisplay.SetUIBuffer();
			func(a1);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

bool HDRDisplay::isHDRMonitor = false;

bool HDRDisplay::DetectHDR()
{
	bool hdrSupported = false;
	bool hdrEnabled = false;

	HWND hwnd = reinterpret_cast<HWND>(RE::BSGraphics::Renderer::GetSingleton()->GetCurrentRenderWindow());
	IsHDRSupportedAndEnabled(hwnd, hdrSupported, hdrEnabled, globals::d3d::swapChain);

	isHDRMonitor = hdrSupported;
	logger::info("[HDR] HDR display detection: supported={}, enabled={}", hdrSupported, hdrEnabled);
	return hdrSupported;
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	HDRDisplay::Settings,
	enableHDR,
	hdrPaperWhite,
	hdrPeakNits,
	hdrUIBrightness,
	dontShowHDRWarning,
	hdrAutoDetected);

void HDRDisplay::DrawSettings()
{
	if (isHDRMonitor) {
		ImGui::TextColored(Util::Colors::GetSuccess(), "HDR Display Detected");
	} else {
		ImGui::TextColored(Util::Colors::GetWarning(), "SDR Display (HDR not detected)");
	}
	ImGui::Spacing();

	// Gate HDR checkbox behind monitor detection
	bool oldEnableHDR;
	bool currentEnableHDR;
	{
		std::lock_guard<std::mutex> lock(settingsMutex);
		oldEnableHDR = settings.enableHDR;
		currentEnableHDR = settings.enableHDR;
	}

	// Disable checkbox if no HDR monitor detected AND HDR is not already enabled
	// (Allow disabling HDR even on SDR if it's already on from saved settings)
	if (!isHDRMonitor && !currentEnableHDR) {
		ImGui::BeginDisabled();
	}

	if (ImGui::Checkbox("Enable HDR", &currentEnableHDR)) {
		{
			std::lock_guard<std::mutex> lock(settingsMutex);
			settings.enableHDR = currentEnableHDR;
			if (settings.enableHDR && !oldEnableHDR) {
				logger::info("HDR: enableHDR changed to: true");
				UpdateHDRData();
				UpdateSwapChainColorSpace();
			} else if (!settings.enableHDR && oldEnableHDR) {
				logger::info("HDR: enableHDR changed to: false");
				UpdateHDRData();
				UpdateSwapChainColorSpace();
			}
		}
	}

	if (!isHDRMonitor && !oldEnableHDR) {
		ImGui::EndDisabled();
	}

	if (auto _tt = Util::HoverTooltipWrapper()) {
		if (isHDRMonitor) {
			ImGui::Text("Enable HDR output. Matches vanilla visuals with extended dynamic range.");
		} else {
			ImGui::Text("HDR display not detected. Use Advanced button to override.");
		}
	}

	// Advanced override button for SDR monitors
	if (!isHDRMonitor && !oldEnableHDR) {
		ImGui::SameLine();
		if (ImGui::Button("Advanced")) {
			bool dontShowWarning;
			{
				std::lock_guard<std::mutex> lock(settingsMutex);
				dontShowWarning = settings.dontShowHDRWarning;
			}
			if (!dontShowWarning) {
				pendingHDREnable = true;
				showHDRWarningPopup = true;
				ImGui::OpenPopup("HDR Warning##HDRDisplay");
			} else {
				// User previously dismissed warnings, enable directly
				{
					std::lock_guard<std::mutex> lock(settingsMutex);
					settings.enableHDR = true;
					logger::info("HDR: enableHDR changed to: true (advanced override, warning suppressed)");
					UpdateHDRData();
					UpdateSwapChainColorSpace();
				}
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Force enable HDR even without detection (not recommended).");
		}
	}

	// Show notice if HDR is enabled on SDR monitor
	{
		std::lock_guard<std::mutex> lock(settingsMutex);
		if (!isHDRMonitor && settings.enableHDR) {
			ImGui::Spacing();
			ImGui::PushStyleColor(ImGuiCol_Text, Util::Colors::GetWarning());
			ImGui::TextWrapped("HDR is enabled but no HDR display was detected.");
			ImGui::PopStyleColor();
		}
	}

	if (ImGui::BeginPopupModal("HDR Warning##HDRDisplay", &showHDRWarningPopup, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
		// Center popup on screen
		ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

		// Prevent background dimming by pushing lower modal dimming
		ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);

		ImGui::TextColored(Util::Colors::GetWarning(), "WARNING: Force Enable HDR");
		ImGui::Separator();
		ImGui::Spacing();
		ImGui::PushStyleColor(ImGuiCol_Text, Util::Colors::GetWarning());
		ImGui::TextWrapped("HDR was not detected on your monitor.");
		ImGui::TextWrapped("The game will look VERY WRONG on an SDR (standard) display.");
		ImGui::PopStyleColor();
		ImGui::Spacing();
		ImGui::TextWrapped("Only proceed if you have an HDR-capable display that was not detected correctly.");
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		if (ImGui::Button("Force Enable HDR", ImVec2(150, 0))) {
			{
				std::lock_guard<std::mutex> lock(settingsMutex);
				settings.enableHDR = true;
				logger::info("HDR: enableHDR changed to: true (forced override)");
				UpdateHDRData();
				UpdateSwapChainColorSpace();
			}
			showHDRWarningPopup = false;
			pendingHDREnable = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(150, 0))) {
			{
				std::lock_guard<std::mutex> lock(settingsMutex);
				settings.enableHDR = false;
			}
			showHDRWarningPopup = false;
			pendingHDREnable = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// Add smaller "don't show again" checkbox
		bool dontShowWarning;
		{
			std::lock_guard<std::mutex> lock(settingsMutex);
			dontShowWarning = settings.dontShowHDRWarning;
		}
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, ImGui::GetStyle().FramePadding.y * 0.5f));
		ImGui::SetWindowFontScale(0.9f);
		if (ImGui::Checkbox("Don't show me this again", &dontShowWarning)) {
			std::lock_guard<std::mutex> lock(settingsMutex);
			settings.dontShowHDRWarning = dontShowWarning;
		}
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopStyleVar();

		ImGui::PopStyleVar();
		ImGui::EndPopup();
	}

	// HDR settings sliders
	bool isHDREnabled;
	{
		std::lock_guard<std::mutex> lock(settingsMutex);
		isHDREnabled = settings.enableHDR;
	}

	if (isHDREnabled) {
		ImGui::Spacing();

		uint oldPaperWhite;
		uint currentPaperWhite;
		uint oldPeakNits;
		uint currentPeakNits;
		{
			std::lock_guard<std::mutex> lock(settingsMutex);
			oldPaperWhite = settings.hdrPaperWhite;
			currentPaperWhite = settings.hdrPaperWhite;
			oldPeakNits = settings.hdrPeakNits;
			currentPeakNits = settings.hdrPeakNits;
		}

		ImGui::SliderInt("Paper White (nits)", reinterpret_cast<int*>(&currentPaperWhite), 80, 500);
		{
			std::lock_guard<std::mutex> lock(settingsMutex);
			if (currentPaperWhite >= settings.hdrPeakNits) {
				currentPaperWhite = settings.hdrPeakNits - 1;
			}
			settings.hdrPaperWhite = currentPaperWhite;
			if (oldPaperWhite != settings.hdrPaperWhite) {
				UpdateHDRData();
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("How bright SDR white appears on your HDR display.");
			ImGui::Text("203 nits is the ITU BT.2408 reference. Increase for a brighter image.");
		}

		ImGui::SliderInt("Peak Brightness (nits)", reinterpret_cast<int*>(&currentPeakNits), 400, 2000);
		{
			std::lock_guard<std::mutex> lock(settingsMutex);
			if (currentPeakNits <= settings.hdrPaperWhite) {
				currentPeakNits = settings.hdrPaperWhite + 1;
			}
			settings.hdrPeakNits = currentPeakNits;
			if (oldPeakNits != settings.hdrPeakNits) {
				UpdateHDRData();
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Maximum brightness your display can produce.");
			ImGui::Text("Set to match your display's actual peak brightness.");
		}

		ImGui::TextDisabled("Display reports: %.0f nits max", cachedDisplayMaxLuminance);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Reported by OS/driver (DXGI MaxLuminance), not a direct meter reading.");
			ImGui::Text("It may be EDID metadata and can differ from real highlight peak output.");
			ImGui::Text("Treat this as a starting point and tune Peak Brightness as needed.");
		}
	}

	// UI brightness slider - only shown when HDR is enabled
	ImGui::Spacing();
	{
		std::lock_guard<std::mutex> lock(settingsMutex);
		if (settings.enableHDR) {
			float oldUIBrightness = settings.hdrUIBrightness;
			float currentUIBrightness = settings.hdrUIBrightness;

			ImGui::SliderFloat("UI Brightness Multiplier", &currentUIBrightness, 0.5f, 5.0f, "%.2fx");
			if (oldUIBrightness != currentUIBrightness) {
				settings.hdrUIBrightness = currentUIBrightness;
				UpdateHDRData();
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("UI brightness = Paper White × this multiplier in HDR mode.");
				ImGui::Text("1.00x = UI renders at Paper White brightness. Higher values make UI brighter relative to scene content.");
				ImGui::Text("Note: Main menu and loading screens always render at Paper White brightness.");
			}
		}
	}
}

void HDRDisplay::SaveSettings(json& o_json)
{
	std::lock_guard<std::mutex> lock(settingsMutex);
	o_json = settings;
}

void HDRDisplay::LoadSettings(json& o_json)
{
	std::lock_guard<std::mutex> lock(settingsMutex);

	bool oldEnableHDR = settings.enableHDR;

	settings = o_json;

	// Defer auto-detection to SetupResources where the renderer is available.
	// DetectHDR() needs a valid HWND which doesn't exist during early plugin init.
	// hdrAutoDetected starts false in defaults and is only set true after auto-detect
	// completes in SetupResources, so this correctly triggers on first launch even
	// when the default config was auto-generated with enableHDR: false.
	if (!settings.hdrAutoDetected) {
		pendingAutoDetect = true;
		logger::info("[HDR] Auto-detection not yet run - deferring to SetupResources");
	}

	if (settings.enableHDR != oldEnableHDR) {
		UpdateHDRData();
		UpdateSwapChainColorSpace();
	}
}

void HDRDisplay::RestoreDefaultSettings()
{
	bool hdrMonitor = DetectHDR();
	settings.enableHDR = hdrMonitor;
	settings.hdrPaperWhite = 203;
	settings.hdrPeakNits = 1000;
	settings.hdrUIBrightness = 1.0f;
	settings.dontShowHDRWarning = false;
}

void HDRDisplay::DataLoaded()
{
	// Use Skyrim's built-in ini setting to upgrade all HDR render targets to 16-bit float format.
	auto setting = RE::GetINISetting("bUse64bitsHDRRenderTarget:Display");
	if (setting) {
		setting->data.b = true;
		logger::info("[HDR Display] Enabled bUse64bitsHDRRenderTarget - all required render targets will use R16G16B16A16_FLOAT");
	} else {
		logger::warn("[HDR Display] bUse64bitsHDRRenderTarget ini setting not found");
	}
}

void HDRDisplay::PostPostLoad()
{
	// When Upscaling is loaded it installs equivalent hooks for these same addresses in its own
	// PostPostLoad. Only install here when Upscaling is absent.
	if (!globals::features::upscaling.loaded) {
		logger::info("[HDR Display] Installing HDR pipeline hooks (Upscaling not loaded)");
		stl::detour_thunk<HDR_MenuManagerDrawInterfaceStartHook>(REL::RelocationID(79947, 82084));
		stl::write_thunk_call<HDR_Main_PostProcessing>(REL::RelocationID(100430, 107148).address() + REL::Relocate(0x1F0, 0x1E7, 0x206));
	}
}

void HDRDisplay::SetupResources()
{
	// Clean up existing resources to prevent memory leaks on re-initialization
	if (hdrTexture || outputTexture || uiTexture || hdrDataCB) {
		DestroyResources();
	}

	DetectHDR();

	if (pendingAutoDetect) {
		pendingAutoDetect = false;
		std::lock_guard<std::mutex> lock(settingsMutex);
		settings.enableHDR = isHDRMonitor;
		settings.hdrAutoDetected = true;
		logger::info("[HDR] Auto-configured HDR based on display: {}", isHDRMonitor ? "enabled" : "disabled");
	}

	// Cache display max luminance for UI display
	cachedDisplayMaxLuminance = GetDisplayMaxLuminance();

	// Set up swap chain color space BEFORE querying format and creating textures
	// This ensures outputTexture matches the actual swap chain format for CopyResource
	UpdateSwapChainColorSpace();

	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC texDesc{};
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

	main.texture->GetDesc(&texDesc);
	main.SRV->GetDesc(&srvDesc);
	main.UAV->GetDesc(&uavDesc);

	// Get the actual swap chain format for output texture
	DXGI_FORMAT swapChainFormat = DXGI_FORMAT_R10G10B10A2_UNORM;  // HDR format
	DXGI_SWAP_CHAIN_DESC scDesc;
	if (SUCCEEDED(globals::d3d::swapChain->GetDesc(&scDesc))) {
		swapChainFormat = scDesc.BufferDesc.Format;
		logger::info("[HDR] Swap chain format: {} ({})", (int)swapChainFormat,
			swapChainFormat == DXGI_FORMAT_R10G10B10A2_UNORM  ? "R10G10B10A2_UNORM (HDR10)" :
			swapChainFormat == DXGI_FORMAT_R16G16B16A16_FLOAT ? "R16G16B16A16_FLOAT (scRGB)" :
			swapChainFormat == DXGI_FORMAT_R8G8B8A8_UNORM     ? "R8G8B8A8_UNORM (SDR 8-bit)" :
			swapChainFormat == DXGI_FORMAT_B8G8R8A8_UNORM     ? "B8G8R8A8_UNORM (SDR 8-bit)" :
																"other");
	}

	// Intermediate texture for HDR processing
	texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	srvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	hdrTexture = new Texture2D(texDesc);
	hdrTexture->CreateSRV(srvDesc);
	hdrTexture->CreateUAV(uavDesc);

	// RTV so ISHDR can render directly into this float texture
	D3D11_RENDER_TARGET_VIEW_DESC hdrRtvDesc{};
	hdrRtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	hdrRtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	hdrRtvDesc.Texture2D.MipSlice = 0;
	hdrTexture->CreateRTV(hdrRtvDesc);

	// Output texture must match the swap chain format: ApplyHDR does
	// CopyResource(backBuffer, outputTexture) and CopyResource requires identical formats.
	texDesc.Format = swapChainFormat;
	srvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	outputTexture = new Texture2D(texDesc);
	outputTexture->CreateSRV(srvDesc);
	outputTexture->CreateUAV(uavDesc);

	// UI texture for separate UI rendering
	// Use R8G8B8A8_UNORM (8-bit SDR) - vanilla UI is SDR and 8-bit precision
	// naturally truncates near-black ghost bar artifacts to zero
	D3D11_TEXTURE2D_DESC uiTexDesc = texDesc;
	uiTexDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	uiTexDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	D3D11_SHADER_RESOURCE_VIEW_DESC uiSrvDesc = srvDesc;
	uiSrvDesc.Format = uiTexDesc.Format;

	D3D11_UNORDERED_ACCESS_VIEW_DESC uiUavDesc = uavDesc;
	uiUavDesc.Format = uiTexDesc.Format;

	uiTexture = new Texture2D(uiTexDesc);
	uiTexture->CreateSRV(uiSrvDesc);
	uiTexture->CreateUAV(uiUavDesc);

	// Create RTV for UI texture
	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;
	uiTexture->CreateRTV(rtvDesc);

	hdrDataCB = new ConstantBuffer(ConstantBufferDesc<HDRDataCB>());

	UpdateHDRData();

	GetHDROutputCS();
	GetUIBrightnessCS();

	UpgradeLDRRenderTargets();
}

void HDRDisplay::BeginUIRendering()
{
	// Skip if D3D12 frame gen is active - it has its own UI buffer handling
	if (globals::features::upscaling.d3d12SwapChainActive)
		return;

	if (renderingUI)
		return;

	if (!uiTexture || !uiTexture->rtv)
		return;

	auto context = globals::d3d::context;

	// Release any existing saved render targets before overwriting
	if (savedRTV) {
		savedRTV->Release();
		savedRTV = nullptr;
	}
	if (savedDSV) {
		savedDSV->Release();
		savedDSV = nullptr;
	}

	// Save current render target so we can restore after ImGui
	context->OMGetRenderTargets(1, &savedRTV, &savedDSV);

	// Do NOT clear - vanilla UI has already rendered to uiTexture via SetUIBuffer()
	// Just ensure ImGui also renders to the same texture
	ID3D11RenderTargetView* rtv = uiTexture->rtv.get();
	context->OMSetRenderTargets(1, &rtv, nullptr);

	renderingUI = true;
}

void HDRDisplay::EndUIRendering()
{
	// Skip if D3D12 frame gen is active
	if (globals::features::upscaling.d3d12SwapChainActive)
		return;

	if (!renderingUI)
		return;

	auto context = globals::d3d::context;

	// Restore original render target
	context->OMSetRenderTargets(1, &savedRTV, savedDSV);

	if (savedRTV) {
		savedRTV->Release();
		savedRTV = nullptr;
	}
	if (savedDSV) {
		savedDSV->Release();
		savedDSV = nullptr;
	}

	renderingUI = false;
}

void HDRDisplay::RedirectFramebuffer()
{
	if (!settings.enableHDR || !hdrTexture || !hdrTexture->rtv)
		return;

	if (!GetHDROutputCS())
		return;

	if (framebufferRedirected)
		return;

	auto& fb = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER];

	// Save originals
	savedFramebufferTexture = fb.texture;
	savedFramebufferSRV = fb.SRV;
	savedFramebufferRTV = fb.RTV;

	// Redirect to hdrTexture (R16G16B16A16_FLOAT) so ISHDR can write values >1.0
	fb.texture = reinterpret_cast<ID3D11Texture2D*>(hdrTexture->resource.get());
	fb.SRV = hdrTexture->srv.get();
	fb.RTV = hdrTexture->rtv.get();

	framebufferRedirected = true;
}

void HDRDisplay::RestoreFramebuffer()
{
	if (!framebufferRedirected)
		return;

	auto& fb = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER];

	fb.texture = savedFramebufferTexture;
	fb.SRV = savedFramebufferSRV;
	fb.RTV = savedFramebufferRTV;

	savedFramebufferTexture = nullptr;
	savedFramebufferSRV = nullptr;
	savedFramebufferRTV = nullptr;
	framebufferRedirected = false;
}

void HDRDisplay::SetUIBuffer()
{
	// VR: ISCopy reads kFRAMEBUFFER.SRV to distribute the frame to the HMD and
	// companion window.  Redirecting kFRAMEBUFFER.RTV here would cause vanilla UI
	// to render into uiTexture instead, so ISCopy would send a UI-less frame to
	// the HMD.  Leave kFRAMEBUFFER alone; vanilla UI bakes directly into it.
	if (globals::game::isVR)
		return;

	auto& fb = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER];

	// Handle Frame Generation case - redirect to FG's UI buffer
	if (globals::features::upscaling.d3d12SwapChainActive) {
		auto& upscaling = globals::features::upscaling;
		if (!upscaling.dx12SwapChain.uiBufferWrapped || !upscaling.dx12SwapChain.uiBufferWrapped->rtv)
			return;

		float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		globals::d3d::context->ClearRenderTargetView(upscaling.dx12SwapChain.uiBufferWrapped->rtv, clearColor);

		fb.RTV = upscaling.dx12SwapChain.uiBufferWrapped->rtv;
		globals::d3d::context->OMSetRenderTargets(1, &fb.RTV, nullptr);
		return;
	}

	// SDR mode: vanilla UI composites directly to kFRAMEBUFFER, no redirect needed
	if (!settings.enableHDR)
		return;

	// Don't redirect if the HDR compute shader isn't available - vanilla UI path works without it
	if (!GetHDROutputCS())
		return;

	// Skip if resources aren't ready
	if (!uiTexture || !uiTexture->rtv || !hdrDataCB || !outputTexture)
		return;

	// Save original RTV for restoration after Present
	if (!savedFramebufferRTV) {
		savedFramebufferRTV = fb.RTV;
	}

	// Clear UI texture before vanilla UI renders
	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	globals::d3d::context->ClearRenderTargetView(uiTexture->rtv.get(), clearColor);

	// Redirect to our UI texture
	fb.RTV = uiTexture->rtv.get();
	globals::d3d::context->OMSetRenderTargets(1, &fb.RTV, nullptr);
}

void HDRDisplay::ClearUIBuffer()
{
	// Skip if D3D12 frame gen is active
	if (globals::features::upscaling.d3d12SwapChainActive)
		return;

	if (!uiTexture || !uiTexture->rtv)
		return;

	// Clear UI buffer with transparent black for next frame
	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	globals::d3d::context->ClearRenderTargetView(uiTexture->rtv.get(), clearColor);

	// Restore original framebuffer RTV
	if (savedFramebufferRTV) {
		auto& data = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER];
		data.RTV = savedFramebufferRTV;
		savedFramebufferRTV = nullptr;
	}
}

void HDRDisplay::ApplyHDR()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "HDR Processing");

	std::lock_guard<std::mutex> lock(settingsMutex);

	if (!hdrDataCB || !hdrTexture || !outputTexture)
		return;

	auto& upscaling = globals::features::upscaling;

	auto context = globals::d3d::context;
	auto state = globals::state;
	auto renderer = globals::game::renderer;

	// Update constant buffer before applying HDR
	UpdateHDRData();

	state->BeginPerfEvent("HDR Processing");

	{
		auto dispatchCount = Util::GetScreenDispatchCount(false);

		// When HDR is enabled, ISHDR wrote to hdrTexture (float16, values >1.0 preserved).
		// When SDR, ISHDR wrote to kFRAMEBUFFER (UNORM, tonemapped 0-1).
		auto& framebufferRT = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kFRAMEBUFFER];

		// Scene SRV selection:
		// - VR: kFRAMEBUFFER at this point has scene + vanilla UI + ImGui all baked in
		//   (thunk restored hdrTexture→kFRAMEBUFFER, vanilla UI rendered on top, ImGui
		//   just rendered to kFRAMEBUFFER.RTV above). Use it directly so the companion
		//   window gets everything without a separate uiTexture capture pass.
		// - Non-VR HDR: hdrTexture has float16 scene values >1.0 preserved from ISHDR.
		// - Non-VR SDR: kFRAMEBUFFER has the tonemapped 0-1 ISHDR output.
		ID3D11ShaderResourceView* sceneSRV =
			globals::game::isVR                                   ? framebufferRT.SRV :
			(settings.enableHDR && hdrTexture && hdrTexture->srv) ? hdrTexture->srv.get() :
																	framebufferRT.SRV;

		// Choose the correct UI buffer based on which path is active.
		// VR uses the framebuffer directly, which already contains vanilla UI/ImGui.
		// Binding a separate uiTexture here would duplicate the UI layer.
		ID3D11ShaderResourceView* uiSRV = nullptr;
		if (!globals::game::isVR) {
			if (upscaling.d3d12SwapChainActive && upscaling.dx12SwapChain.uiBufferWrapped) {
				uiSRV = upscaling.dx12SwapChain.uiBufferWrapped->srv;
			} else if (uiTexture && uiTexture->srv) {
				uiSRV = uiTexture->srv.get();
			}
		}

		ID3D11ShaderResourceView* views[2] = { sceneSRV, uiSRV };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[1] = { outputTexture->uav.get() };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11Buffer* cbs[1] = { hdrDataCB->CB() };

		context->CSSetConstantBuffers(0, ARRAYSIZE(cbs), cbs);

		auto computeShader = GetHDROutputCS();
		if (!computeShader) {
			// Fallback: HDR shader files not present - copy kFRAMEBUFFER directly to output
			// Cleanup any bound resources
			views[0] = nullptr;
			views[1] = nullptr;
			context->CSSetShaderResources(0, ARRAYSIZE(views), views);

			uavs[0] = { nullptr };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			cbs[0] = { nullptr };
			context->CSSetConstantBuffers(0, ARRAYSIZE(cbs), cbs);

			// Copy kFRAMEBUFFER directly to destination (bypassing HDR processing)
			if (upscaling.d3d12SwapChainActive) {
				// Frame Gen path: copy to D3D12 swap chain wrapped buffer
				context->CopyResource(upscaling.dx12SwapChain.swapChainBufferWrapped->resource11, framebufferRT.texture);
			} else {
				// Normal path: copy directly to swap chain back buffer
				ID3D11Texture2D* backBuffer = nullptr;
				HRESULT hr = globals::d3d::swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
				if (SUCCEEDED(hr) && backBuffer) {
					context->CopyResource(backBuffer, framebufferRT.texture);
					backBuffer->Release();
				}
			}

			state->EndPerfEvent();
			return;
		}

		context->CSSetShader(computeShader, nullptr, 0);
		context->Dispatch(dispatchCount.x, dispatchCount.y, 1);

		views[0] = nullptr;
		views[1] = nullptr;
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		uavs[0] = { nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		cbs[0] = { nullptr };
		context->CSSetConstantBuffers(0, ARRAYSIZE(cbs), cbs);

		context->CSSetShader(nullptr, nullptr, 0);
	}

	// Copy result to appropriate destination
	// When Frame Gen is active, copy to the D3D12 swap chain buffer
	// Otherwise copy directly to the D3D11 swap chain back buffer
	if (upscaling.d3d12SwapChainActive) {
		// Frame Gen path: copy to D3D12 swap chain wrapped buffer
		if (upscaling.dx12SwapChain.swapChainBufferWrapped &&
			upscaling.dx12SwapChain.swapChainBufferWrapped->resource11 &&
			outputTexture && outputTexture->resource) {
			context->CopyResource(upscaling.dx12SwapChain.swapChainBufferWrapped->resource11, outputTexture->resource.get());
		}
	} else {
		// Normal path: copy directly to swap chain back buffer
		ID3D11Texture2D* backBuffer = nullptr;
		HRESULT hr = globals::d3d::swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
		if (SUCCEEDED(hr) && backBuffer) {
			context->CopyResource(backBuffer, outputTexture->resource.get());
			backBuffer->Release();
		}
	}

	state->EndPerfEvent();
}

void HDRDisplay::DestroyResources()
{
	if (hdrTexture) {
		hdrTexture->srv = nullptr;
		hdrTexture->uav = nullptr;
		hdrTexture->rtv = nullptr;
		hdrTexture->resource = nullptr;
		delete hdrTexture;
		hdrTexture = nullptr;
	}

	if (outputTexture) {
		outputTexture->srv = nullptr;
		outputTexture->uav = nullptr;
		outputTexture->resource = nullptr;
		delete outputTexture;
		outputTexture = nullptr;
	}

	if (uiTexture) {
		uiTexture->srv = nullptr;
		uiTexture->uav = nullptr;
		uiTexture->rtv = nullptr;
		uiTexture->resource = nullptr;
		delete uiTexture;
		uiTexture = nullptr;
	}

	if (hdrDataCB) {
		delete hdrDataCB;
		hdrDataCB = nullptr;
	}

	RestoreLDRRenderTargets();
}

void HDRDisplay::UpgradeLDRRenderTargets()
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	static const RE::RENDER_TARGETS::RENDER_TARGET ldrTargets[] = {
		RE::RENDER_TARGETS::kLDR_DOWNSAMPLE0,
		RE::RENDER_TARGETS::kLDR_BLURSWAP,
		RE::RENDER_TARGETS::kBLURFULL_BUFFER,
		RE::RENDER_TARGETS::kIMAGESPACE_TEMP_COPY,
		RE::RENDER_TARGETS::kIMAGESPACE_TEMP_COPY2,
		RE::RENDER_TARGETS::kTEMPORAL_AA_UI_ACCUMULATION_1,
		RE::RENDER_TARGETS::kTEMPORAL_AA_UI_ACCUMULATION_2,
	};

	for (auto targetId : ldrTargets) {
		auto& rt = renderer->GetRuntimeData().renderTargets[targetId];
		if (!rt.texture)
			continue;

		D3D11_TEXTURE2D_DESC origDesc{};
		rt.texture->GetDesc(&origDesc);

		if (origDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT)
			continue;

		SavedRenderTarget saved;
		saved.texture = rt.texture;
		saved.RTV = rt.RTV;
		saved.SRV = rt.SRV;

		D3D11_TEXTURE2D_DESC newDesc = origDesc;
		newDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

		ID3D11Texture2D* newTexture = nullptr;
		if (FAILED(device->CreateTexture2D(&newDesc, nullptr, &newTexture)))
			continue;

		ID3D11RenderTargetView* newRTV = nullptr;
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
		rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2D.MipSlice = 0;
		if (FAILED(device->CreateRenderTargetView(newTexture, &rtvDesc, &newRTV))) {
			newTexture->Release();
			continue;
		}

		ID3D11ShaderResourceView* newSRV = nullptr;
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;
		if (FAILED(device->CreateShaderResourceView(newTexture, &srvDesc, &newSRV))) {
			newRTV->Release();
			newTexture->Release();
			continue;
		}

		rt.texture = newTexture;
		rt.RTV = newRTV;
		rt.SRV = newSRV;

		savedLDRTargets.push_back({ targetId, saved });
		logger::info("[HDR] Upgraded render target {} to R16G16B16A16_FLOAT (was format {})", static_cast<int>(targetId), static_cast<int>(origDesc.Format));
	}
}

void HDRDisplay::RestoreLDRRenderTargets()
{
	auto renderer = globals::game::renderer;

	for (auto& [targetId, saved] : savedLDRTargets) {
		auto& rt = renderer->GetRuntimeData().renderTargets[targetId];

		if (rt.texture)
			rt.texture->Release();
		if (rt.RTV)
			rt.RTV->Release();
		if (rt.SRV)
			rt.SRV->Release();

		rt.texture = saved.texture;
		rt.RTV = saved.RTV;
		rt.SRV = saved.SRV;
	}
	savedLDRTargets.clear();
}

void HDRDisplay::ClearShaderCache()
{
	if (hdrOutputCS) {
		hdrOutputCS->Release();
		hdrOutputCS = nullptr;
	}
	if (uiBrightnessCS) {
		uiBrightnessCS->Release();
		uiBrightnessCS = nullptr;
	}
}

ID3D11ComputeShader* HDRDisplay::GetHDROutputCS()
{
	if (!hdrOutputCS) {
		std::vector<std::pair<const char*, const char*>> defines;
		hdrOutputCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\HDRDisplay\\HDROutputCS.hlsl", defines, "cs_5_0"));
		if (!hdrOutputCS) {
			logger::error("HDR: Failed to compile HDROutputCS.hlsl");
		}
	}
	return hdrOutputCS;
}

ID3D11ComputeShader* HDRDisplay::GetUIBrightnessCS()
{
	if (!uiBrightnessCS) {
		std::vector<std::pair<const char*, const char*>> defines;
		uiBrightnessCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\HDRDisplay\\UIBrightnessCS.hlsl", defines, "cs_5_0"));
		if (!uiBrightnessCS) {
			logger::error("HDR: Failed to compile UIBrightnessCS.hlsl");
		}
	}
	return uiBrightnessCS;
}

void HDRDisplay::ScaleUIBrightnessForFG()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "UI Brightness Scale");

	auto& upscaling = globals::features::upscaling;
	bool isMainOrLoadingMenu = globals::state->isMainMenuOpen || globals::state->isLoadingMenuOpen;

	auto* ui = globals::game::ui;
	// FG merges PQ UI from this pass; when paused, UI stays gamma — HDROutput must composite (skipUIComposite stays 0).
	bool fgCompositing = upscaling.d3d12SwapChainActive &&
	                     upscaling.settings.frameGenerationMode &&
	                     ui && !ui->GameIsPaused() &&
	                     !isMainOrLoadingMenu &&
	                     !globals::game::isVR;
	if (!fgCompositing)
		return;

	if (!settings.enableHDR)
		return;

	if (!hdrDataCB || !upscaling.dx12SwapChain.uiBufferWrapped || !upscaling.dx12SwapChain.uiBufferWrapped->uav)
		return;

	auto context = globals::d3d::context;
	auto state = globals::state;

	state->BeginPerfEvent("UI Brightness Scale");

	// Update constant buffer with current settings
	UpdateHDRData();

	auto dispatchCount = Util::GetScreenDispatchCount(false);

	ID3D11UnorderedAccessView* uavs[1] = { upscaling.dx12SwapChain.uiBufferWrapped->uav };
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

	ID3D11Buffer* cbs[1] = { hdrDataCB->CB() };
	context->CSSetConstantBuffers(0, 1, cbs);

	auto computeShader = GetUIBrightnessCS();
	if (computeShader) {
		context->CSSetShader(computeShader, nullptr, 0);
		context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
	}

	// Cleanup
	uavs[0] = nullptr;
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
	cbs[0] = nullptr;
	context->CSSetConstantBuffers(0, 1, cbs);
	context->CSSetShader(nullptr, nullptr, 0);

	state->EndPerfEvent();
}

float HDRDisplay::GetDisplayMaxLuminance() const
{
	float maxLuminance = 1000.0f;

	winrt::com_ptr<IDXGIOutput> output;
	if (globals::d3d::swapChain && SUCCEEDED(globals::d3d::swapChain->GetContainingOutput(output.put()))) {
		winrt::com_ptr<IDXGIOutput6> output6;
		if (SUCCEEDED(output->QueryInterface(IID_PPV_ARGS(output6.put())))) {
			DXGI_OUTPUT_DESC1 desc1;
			if (SUCCEEDED(output6->GetDesc1(&desc1))) {
				maxLuminance = desc1.MaxLuminance;
				if (maxLuminance < 80.0f) {
					maxLuminance = 1000.0f;  // Fallback if display reports invalid value
				}
			}
		}
	}
	return maxLuminance;
}

float4 HDRDisplay::GetSharedDataHDR() const
{
	if (!loaded)
		return { 0.0f, 0.0f, 0.0f, 0.0f };

	auto* state = globals::state;
	const bool isMainOrLoading = state->isMainMenuOpen || state->isLoadingMenuOpen;
	auto* ui = globals::game::ui;
	const bool inMenuOrPause =
		ui && (ui->GameIsPaused() || state->isMainMenuOpen || state->isLoadingMenuOpen || state->isMapMenuOpen);

	float menuSceneEncoding = kHdrMenuSceneGameplay;
	if (isMainOrLoading) {
		menuSceneEncoding = kHdrMenuSceneMainOrLoading;
	} else if (inMenuOrPause) {
		menuSceneEncoding = kHdrMenuScenePauseOrMap;
	}

	return {
		settings.enableHDR ? 1.0f : 0.0f,
		static_cast<float>(settings.hdrPaperWhite),
		static_cast<float>(settings.hdrPeakNits),
		menuSceneEncoding
	};
}

void HDRDisplay::UpdateHDRData() const
{
	if (!hdrDataCB)
		return;

	auto& upscaling = globals::features::upscaling;

	// Don't skip UI composite in main menu or loading screens - causes ghosting and brightness issues
	bool isMainOrLoadingMenu = globals::state->isMainMenuOpen || globals::state->isLoadingMenuOpen;

	auto* ui = globals::game::ui;
	bool fgActiveThisFrame = upscaling.d3d12SwapChainActive &&
	                         upscaling.settings.frameGenerationMode &&
	                         ui && !ui->GameIsPaused() &&
	                         !isMainOrLoadingMenu &&
	                         !globals::game::isVR;
	bool skipUIComposite = fgActiveThisFrame;

	// Linear Lighting keeps the pipeline linear throughout.
	// Without it, ISHDR gamma-encodes its output even in HDR mode.
	bool isSceneLinear = globals::features::linearLighting.settings.enableLinearLighting;

	// Use user-specified peak brightness for highlights compression
	float effectivePeakNits = static_cast<float>(settings.hdrPeakNits);

	HDRDataCB data;
	data.enableHDR = settings.enableHDR ? 1.f : 0.f;
	data.paperWhite = static_cast<float>(settings.hdrPaperWhite);
	data.peakNits = effectivePeakNits;
	data.skipUIComposite = skipUIComposite ? 1.f : 0.f;
	data.uiBrightness = settings.hdrUIBrightness;
	data.isSceneLinear = isSceneLinear ? 1.f : 0.f;
	data.pad0 = isMainOrLoadingMenu ? 1.f : 0.f;
	// TweenMenu = pause UI. ScaleUIBrightnessForFG skips while GameIsPaused(), so HDROutputCS applies the same mid-alpha boost when compositing gamma UI.
	data.fgTweenMenuMidAlphaBoost = (ui && ui->IsMenuOpen(RE::TweenMenu::MENU_NAME)) ? 1.f : 0.f;
	hdrDataCB->Update(data);
}

void HDRDisplay::UpdateSwapChainColorSpace() const
{
	auto& upscaling = globals::features::upscaling;

	// For Frame Gen, update the D3D12 swap chain color space
	if (upscaling.d3d12SwapChainActive) {
		upscaling.dx12SwapChain.SetColorSpace(settings.enableHDR);
		// HDR metadata is not set - some monitors have issues with HDR10 static metadata.
		// DX12SwapChain handles color space only; metadata control is centralized here.
		if (upscaling.dx12SwapChain.swapChain) {
			upscaling.dx12SwapChain.swapChain->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr);
		}
		return;
	}

	IDXGISwapChain4* swapChain4 = nullptr;

	if (globals::d3d::swapChain) {
		globals::d3d::swapChain->QueryInterface(IID_PPV_ARGS(&swapChain4));
	}

	if (!swapChain4)
		return;

	if (settings.enableHDR) {
		HRESULT hr = swapChain4->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
		if (SUCCEEDED(hr)) {
			logger::info("[HDR] Set swap chain color space to HDR10 (PQ/BT.2020)");
			// Do NOT set HDR10 static metadata - it can cause issues on some monitors
			// The HGiG approach is to handle highlights compression in the shader instead.
			swapChain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr);
		} else {
			logger::warn("[HDR] Failed to set HDR10 color space");
		}
	} else {
		HRESULT hr = swapChain4->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
		if (SUCCEEDED(hr)) {
			logger::info("[HDR] Set swap chain color space to SDR (sRGB)");
			swapChain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr);
		} else {
			logger::warn("[HDR] Failed to set SDR color space");
		}
	}

	swapChain4->Release();
}
