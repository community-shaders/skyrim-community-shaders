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

// Win11 24H2 display config types. Compat_ prefix avoids collision with SDK enum members.
typedef enum
{
	Compat_DISPLAYCONFIG_ADVANCED_COLOR_MODE_SDR = 0,
	Compat_DISPLAYCONFIG_ADVANCED_COLOR_MODE_WCG = 1,
	Compat_DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR = 2
} Compat_DISPLAYCONFIG_ADVANCED_COLOR_MODE;

typedef struct Compat_DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2
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
			UINT32 wideColorSupported: 1;
			UINT32 wideColorUserEnabled: 1;
			UINT32 reserved: 24;
		};
		UINT32 value;
	};
	DISPLAYCONFIG_COLOR_ENCODING colorEncoding;
	UINT32 bitsPerColorChannel;
	Compat_DISPLAYCONFIG_ADVANCED_COLOR_MODE activeColorMode;
} Compat_DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2;

static constexpr DISPLAYCONFIG_DEVICE_INFO_TYPE kDisplayConfigGetAdvancedColorInfo2 =
	static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(15);

// HDR display detection
// Credits: Luma Framework by Filippo Tarpini (MIT License)
// https://github.com/Filoppi/Luma-Framework/blob/f1fbc2a36f2d24fd551721ce90f26821a8e754c1/Source/Core/utils/display.hpp
namespace
{
	// Returns the GDI device name for the swap chain's output via GetContainingOutput.
	// Returns false if the output cannot be determined (e.g. Streamline wraps the swap chain).
	bool GetSwapChainOutputDeviceName(IDXGISwapChain* swapChain, WCHAR (&outDeviceName)[32])
	{
		winrt::com_ptr<IDXGIOutput> output;
		if (FAILED(swapChain->GetContainingOutput(output.put())))
			return false;
		DXGI_OUTPUT_DESC desc{};
		if (FAILED(output->GetDesc(&desc)))
			return false;
		wcsncpy_s(outDeviceName, desc.DeviceName, _TRUNCATE);
		// DeviceName is ASCII (e.g. "\\.\DISPLAY1") — safe to log as narrow string
		char narrowName[32]{};
		WideCharToMultiByte(CP_UTF8, 0, desc.DeviceName, -1, narrowName, sizeof(narrowName), nullptr, nullptr);
		logger::debug("[HDR] Swap chain output device: {}", narrowName);
		return true;
	}

	bool GetDisplayConfigPathInfo(IDXGISwapChain* swapChain, DISPLAYCONFIG_PATH_INFO& outPathInfo)
	{
		WCHAR deviceName[32]{};
		if (!GetSwapChainOutputDeviceName(swapChain, deviceName))
			return false;

		uint32_t pathCount, modeCount;
		if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
			return false;

		std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
		std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
		if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS)
			return false;

		for (auto& pathInfo : paths) {
			// DISPLAYCONFIG_SOURCE_IN_USE skips inactive sources (disconnected displays)
			if (!(pathInfo.flags & DISPLAYCONFIG_PATH_ACTIVE) ||
				!(pathInfo.sourceInfo.statusFlags & DISPLAYCONFIG_SOURCE_IN_USE))
				continue;

			// QDC_ONLY_ACTIVE_PATHS excludes virtual-mode paths; no index selection needed
			DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName{};
			sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
			sourceName.header.size = sizeof(sourceName);
			sourceName.header.adapterId = pathInfo.sourceInfo.adapterId;
			sourceName.header.id = pathInfo.sourceInfo.id;
			if (DisplayConfigGetDeviceInfo(&sourceName.header) == ERROR_SUCCESS) {
				if (wcscmp(sourceName.viewGdiDeviceName, deviceName) == 0) {
					outPathInfo = pathInfo;
					return true;
				}
			}
		}
		return false;
	}

	bool IsHDRSupportedAndEnabled(IDXGISwapChain* swapChain, bool& supported, bool& enabled)
	{
		supported = false;
		enabled = false;

		DISPLAYCONFIG_PATH_INFO pathInfo{};
		if (!GetDisplayConfigPathInfo(swapChain, pathInfo)) {
			// GetContainingOutput fails under frame-gen wrappers. Fall back to enumerating
			// the device adapter's outputs by HMONITOR. Only detects active HDR, not capable.
			HWND outputWindow = nullptr;
			DXGI_SWAP_CHAIN_DESC scDescFull{};
			if (SUCCEEDED(swapChain->GetDesc(&scDescFull)))
				outputWindow = scDescFull.OutputWindow;

			HMONITOR hMonitor = outputWindow ? MonitorFromWindow(outputWindow, MONITOR_DEFAULTTONEAREST) : nullptr;
			if (!hMonitor) {
				logger::warn("[HDR] HDR detection failed - cannot determine monitor from swap chain OutputWindow");
				return false;
			}

			// Enumerate outputs on the device's own adapter; avoids touching other GPUs.
			winrt::com_ptr<IDXGIDevice> dxgiDevice;
			winrt::com_ptr<IDXGIAdapter> adapter;
			if (globals::d3d::device &&
				SUCCEEDED(globals::d3d::device->QueryInterface(IID_PPV_ARGS(dxgiDevice.put()))) &&
				SUCCEEDED(dxgiDevice->GetAdapter(adapter.put()))) {
				for (UINT oi = 0;; ++oi) {
					winrt::com_ptr<IDXGIOutput> out;
					HRESULT hr = adapter->EnumOutputs(oi, out.put());
					if (hr == DXGI_ERROR_NOT_FOUND)
						break;
					if (FAILED(hr)) {
						logger::debug("[HDR] EnumOutputs failed: hr=0x{:08X}", static_cast<unsigned>(hr));
						break;
					}
					DXGI_OUTPUT_DESC outDesc{};
					if (FAILED(out->GetDesc(&outDesc)) || outDesc.Monitor != hMonitor)
						continue;
					winrt::com_ptr<IDXGIOutput6> out6;
					if (SUCCEEDED(out->QueryInterface(IID_PPV_ARGS(out6.put())))) {
						DXGI_OUTPUT_DESC1 desc1{};
						if (SUCCEEDED(out6->GetDesc1(&desc1))) {
							enabled = desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
							supported = enabled;
							logger::debug("[HDR] DXGI fallback detection: colorSpace={}", static_cast<int>(desc1.ColorSpace));
							return true;
						}
					}
				}
			}
			logger::warn("[HDR] HDR detection failed - cannot determine monitor (Streamline/frame-gen?)");
			return false;
		}

		// Try Windows 11 24H2+ API first - directly reports HDR hardware capability
		// Credits: renodx by clshortfuse (MIT License)
		// https://github.com/clshortfuse/renodx/blob/01f3739685ba8f850d82fb1a11a5ec1104e6b1b8/src/games/hollowknight-silksong/addon.cpp#L545
		Compat_DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2 colorInfo2{};
		colorInfo2.header.type = kDisplayConfigGetAdvancedColorInfo2;
		colorInfo2.header.size = sizeof(colorInfo2);
		colorInfo2.header.adapterId = pathInfo.targetInfo.adapterId;
		colorInfo2.header.id = pathInfo.targetInfo.id;
		if (DisplayConfigGetDeviceInfo(&colorInfo2.header) == ERROR_SUCCESS) {
			supported = colorInfo2.highDynamicRangeSupported != 0;
			enabled = colorInfo2.activeColorMode == Compat_DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR;
			UINT32 hdrSupported = colorInfo2.highDynamicRangeSupported;
			UINT32 activeMode = static_cast<UINT32>(colorInfo2.activeColorMode);
			logger::debug("[HDR] Win11 24H2 detection: highDynamicRangeSupported={}, activeColorMode={}", hdrSupported, activeMode);
			return true;
		}

		// Fallback for older Windows versions
		DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO colorInfo{};
		colorInfo.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
		colorInfo.header.size = sizeof(colorInfo);
		colorInfo.header.adapterId = pathInfo.targetInfo.adapterId;
		colorInfo.header.id = pathInfo.targetInfo.id;
		if (DisplayConfigGetDeviceInfo(&colorInfo.header) == ERROR_SUCCESS) {
			supported = colorInfo.advancedColorSupported != 0;

			DXGI_COLOR_SPACE_TYPE swapChainOutputColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
			{
				winrt::com_ptr<IDXGIOutput> containingOutput;
				if (SUCCEEDED(swapChain->GetContainingOutput(containingOutput.put()))) {
					winrt::com_ptr<IDXGIOutput6> out6;
					if (SUCCEEDED(containingOutput->QueryInterface(IID_PPV_ARGS(out6.put())))) {
						DXGI_OUTPUT_DESC1 desc1{};
						if (SUCCEEDED(out6->GetDesc1(&desc1)))
							swapChainOutputColorSpace = desc1.ColorSpace;
					}
				}
			}

			enabled = (colorInfo.advancedColorEnabled != 0) && (swapChainOutputColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
			UINT32 advancedSupported = colorInfo.advancedColorSupported;
			UINT32 advancedEnabled = colorInfo.advancedColorEnabled;
			int colorSpaceInt = static_cast<int>(swapChainOutputColorSpace);
			logger::debug("[HDR] Legacy detection: advancedColorSupported={}, advancedColorEnabled={}, swapChainColorSpace={}, enabled={}",
				advancedSupported, advancedEnabled, colorSpaceInt, enabled);
			return true;
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
bool HDRDisplay::isHDRCapableMonitor = false;
bool HDRDisplay::wasExclusiveFullscreen = false;

bool HDRDisplay::DetectHDR()
{
	if (!globals::d3d::swapChain) {
		isHDRMonitor = false;
		isHDRCapableMonitor = false;
		return false;
	}

	bool hdrSupported = false;
	bool hdrEnabled = false;

	IsHDRSupportedAndEnabled(globals::d3d::swapChain, hdrSupported, hdrEnabled);

	isHDRMonitor = hdrEnabled;
	isHDRCapableMonitor = hdrSupported;
	logger::info("[HDR] HDR display detection: supported={}, enabled={}", hdrSupported, hdrEnabled);
	return hdrEnabled;
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
		Util::Text::Success("HDR Display Detected");
	} else if (isHDRCapableMonitor) {
		Util::Text::Warning("HDR Capable Display (Windows HDR is off)");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Your monitor supports HDR, but Windows HDR is currently disabled.");
			ImGui::Text("Enable HDR in Windows Display Settings to allow auto-detection.");
		}
	} else {
		Util::Text::Warning("SDR Display (HDR not detected)");
	}

	const bool isExclusiveFullscreen = globals::features::upscaling.loaded ? !globals::features::upscaling.isWindowed : wasExclusiveFullscreen;

	if (isExclusiveFullscreen) {
		ImGui::Spacing();
		Util::Text::WrappedWarning("WARNING: Exclusive Fullscreen detected.");
		Util::Text::WrappedWarning("HDR is not compatible with Exclusive Fullscreen and may not work correctly. Switch to Borderless Windowed mode for proper HDR support.");
		ImGui::Spacing();
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
		} else if (isHDRCapableMonitor) {
			ImGui::Text("Monitor supports HDR but Windows HDR is off. Enable HDR in Windows Display Settings, then restart the game.");
		} else {
			ImGui::Text("HDR display not detected. Use Advanced button to override.");
		}
	}

	// Advanced override button — shown when HDR is neither active nor auto-detected
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
			if (isHDRCapableMonitor) {
				ImGui::Text("Enable Windows HDR instead of forcing it here.");
			} else {
				ImGui::Text("Force enable HDR even without detection (not recommended).");
			}
		}
	}

	// Show notice if HDR is enabled on SDR monitor
	{
		std::lock_guard<std::mutex> lock(settingsMutex);
		if (!isHDRMonitor && settings.enableHDR) {
			ImGui::Spacing();
			Util::Text::WrappedWarning("HDR is enabled but no HDR display was detected.");
		}
	}

	if (auto popup = Util::CenteredPopupModal("HDR Warning##HDRDisplay", &showHDRWarningPopup, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
		// Prevent background dimming by pushing lower modal dimming
		ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);

		Util::Text::Warning("WARNING: Force Enable HDR");
		ImGui::Separator();
		ImGui::Spacing();
		Util::Text::WrappedWarning("HDR was not detected on your monitor.");
		Util::Text::WrappedWarning("The game will look VERY WRONG on an SDR (standard) display.");
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

		ImGui::SliderInt("Peak Brightness (nits)", reinterpret_cast<int*>(&currentPeakNits), 400, 10000);
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

	// Defer auto-detection to SetupResources where the swap chain is available.
	// DetectHDR() needs globals::d3d::swapChain which isn't valid during early plugin init.
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

	hdrTexture = new Texture2D(texDesc, "HDR::HdrTexture");
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

	outputTexture = new Texture2D(texDesc, "HDR::OutputTexture");
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

	uiTexture = new Texture2D(uiTexDesc, "HDR::UiTexture");
	uiTexture->CreateSRV(uiSrvDesc);
	uiTexture->CreateUAV(uiUavDesc);

	// Create RTV for UI texture
	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;
	uiTexture->CreateRTV(rtvDesc);

	hdrDataCB = new ConstantBuffer(ConstantBufferDesc<HDRDataCB>(), "HDR::DataCB");

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

bool HDRDisplay::IsFGCompositingThisFrame() const
{
	return globals::features::upscaling.ShouldUseFrameGenerationThisFrame();
}

HDRDisplay::D3D12UIBufferMode HDRDisplay::GetD3D12UIBufferMode()
{
	D3D12UIBufferMode mode;
	if (!globals::features::upscaling.d3d12SwapChainActive)
		return mode;

	const bool hdrReady = loaded && hdrDataCB && outputTexture;
	const bool hdrShaderAvailable = hdrReady && GetHDROutputCS() != nullptr;

	mode.useUIBuffer = hdrShaderAvailable || IsFGCompositingThisFrame();
	mode.useFallbackCopy = hdrReady && !hdrShaderAvailable;
	return mode;
}

bool HDRDisplay::ShouldUseD3D12UIBuffer()
{
	return GetD3D12UIBufferMode().useUIBuffer;
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

	// D3D12 swap chain path: route UI to uiBufferWrapped only when a compositor
	// (ApplyHDR or FFX FG UI composition) will read it; otherwise render UI
	// directly into the wrapped back buffer so it survives Present when both
	// compositors are skipped (HDR unloaded + FG off/paused). If HDR is loaded
	// but the shader is missing, keep UI in kFRAMEBUFFER so the ApplyHDR
	// fallback copy carries it to the wrapped back buffer.
	if (globals::features::upscaling.d3d12SwapChainActive) {
		auto& upscaling = globals::features::upscaling;
		if (!upscaling.dx12SwapChain.swapChainBufferWrapped || !upscaling.dx12SwapChain.swapChainBufferWrapped->rtv)
			return;

		const auto uiBufferMode = GetD3D12UIBufferMode();

		if (uiBufferMode.useUIBuffer && (!upscaling.dx12SwapChain.uiBufferWrapped || !upscaling.dx12SwapChain.uiBufferWrapped->rtv))
			return;

		ID3D11RenderTargetView* targetRTV = uiBufferMode.useUIBuffer ?
		                                        upscaling.dx12SwapChain.uiBufferWrapped->rtv :
		                                    uiBufferMode.useFallbackCopy ? fb.RTV :
		                                                                  upscaling.dx12SwapChain.swapChainBufferWrapped->rtv;

		if (uiBufferMode.useUIBuffer) {
			float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			globals::d3d::context->ClearRenderTargetView(targetRTV, clearColor);
		}

		fb.RTV = targetRTV;
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
				// SetUIBuffer keeps non-FG fallback UI in kFRAMEBUFFER; FG keeps using
				// uiBufferWrapped for FidelityFX UI composition.
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
		saved.UAV = rt.UAV;

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

		ID3D11UnorderedAccessView* newUAV = nullptr;
		if (rt.UAV) {
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
			rt.UAV->GetDesc(&uavDesc);
			uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

			if (FAILED(device->CreateUnorderedAccessView(newTexture, &uavDesc, &newUAV))) {
				newSRV->Release();
				newRTV->Release();
				newTexture->Release();
				continue;
			}
		}

		rt.texture = newTexture;
		rt.RTV = newRTV;
		rt.SRV = newSRV;
		rt.UAV = newUAV;

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
		if (rt.UAV)
			rt.UAV->Release();

		rt.texture = saved.texture;
		rt.RTV = saved.RTV;
		rt.SRV = saved.SRV;
		rt.UAV = saved.UAV;
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
	// FG merges PQ UI from this pass; paused UI stays gamma for HDROutput.
	if (!IsFGCompositingThisFrame())
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

	bool isMainOrLoadingMenu = globals::state->isMainMenuOpen || globals::state->isLoadingMenuOpen;
	auto* ui = globals::game::ui;
	bool skipUIComposite = IsFGCompositingThisFrame();

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
