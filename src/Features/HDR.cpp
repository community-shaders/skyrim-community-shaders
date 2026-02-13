#include "HDR.h"

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
}

bool HDR::isHDRMonitor = false;

bool HDR::DetectHDRDisplay()
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
	HDR::Settings,
	enableHDR,
	hdrPaperWhite,
	hdrPeakNits,
	hdrUIBrightness,
	dontShowHDRWarning);

void HDR::DrawSettings()
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
	}

	// UI brightness slider - only shown when HDR is enabled
	ImGui::Spacing();
	{
		std::lock_guard<std::mutex> lock(settingsMutex);
		if (settings.enableHDR) {
			float oldUIBrightness = settings.hdrUIBrightness;
			float currentUIBrightness = settings.hdrUIBrightness;

			ImGui::SliderFloat("HDR UI Brightness", &currentUIBrightness, 0.5f, 5.0f, "%.1fx");
			if (oldUIBrightness != currentUIBrightness) {
				settings.hdrUIBrightness = currentUIBrightness;
				UpdateHDRData();
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Adjusts UI brightness in HDR mode. UI renders at 100 nits baseline,");
				ImGui::Text("independent of Paper White setting. 1.0x = 100 nits.");
			}
		}
	}
}

void HDR::SaveSettings(json& o_json)
{
	std::lock_guard<std::mutex> lock(settingsMutex);
	o_json = settings;
}

void HDR::LoadSettings(json& o_json)
{
	std::lock_guard<std::mutex> lock(settingsMutex);

	// Check if this is first launch (no enableHDR setting saved)
	bool isFirstLaunch = !o_json.contains("enableHDR");

	settings = o_json;

	// On first launch, auto-configure HDR based on display detection
	if (isFirstLaunch) {
		bool hdrMonitor = DetectHDRDisplay();
		settings.enableHDR = hdrMonitor;
		logger::info("[HDR] First launch detected - auto-configuring HDR based on display: {}", hdrMonitor ? "enabled" : "disabled");
	}
}

void HDR::RestoreDefaultSettings()
{
	bool hdrMonitor = DetectHDRDisplay();
	settings.enableHDR = hdrMonitor;
	settings.hdrPaperWhite = 203;
	settings.hdrPeakNits = 1000;
	settings.hdrUIBrightness = 2.3f;
	settings.dontShowHDRWarning = false;
}

void HDR::SetupResources()
{
	logger::info("[HDR] SetupResources called");

	// Clean up existing resources to prevent memory leaks on re-initialization
	if (hdrTexture || outputTexture || uiTexture || hdrDataCB) {
		DestroyResources();
	}

	DetectHDRDisplay();

	// Cache display max luminance for UI display
	cachedDisplayMaxLuminance = GetDisplayMaxLuminance();
	logger::info("[HDR] Display reports max luminance: {:.0f} nits", cachedDisplayMaxLuminance);

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
		logger::info("[HDR] Swap chain format: {}", static_cast<int>(swapChainFormat));
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

	// Output texture - must match swap chain format for CopyResource to work
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

	// Eagerly compile compute shaders so availability is known before first frame
	GetHDROutputCS();
	GetUIBrightnessCS();

	logger::info("[HDR] SetupResources complete - hdrDataCB={}, hdrTexture={}, outputTexture={}, hdrOutputCS={}, uiBrightnessCS={}",
		hdrDataCB ? "valid" : "NULL",
		hdrTexture ? "valid" : "NULL",
		outputTexture ? "valid" : "NULL",
		hdrOutputCS ? "valid" : "NULL",
		uiBrightnessCS ? "valid" : "NULL");
}

void HDR::BeginUIRendering()
{
	// Skip if D3D12 frame gen is active - it has its own UI buffer handling
	if (globals::features::upscaling.d3d12SwapChainActive) {
		static bool loggedOnce = false;
		if (!loggedOnce) {
			logger::info("[HDR] BeginUIRendering skipped - d3d12SwapChainActive");
			loggedOnce = true;
		}
		return;
	}

	// Prevent re-entrance - if already rendering UI, skip to avoid leak
	if (renderingUI) {
		return;
	}

	if (!uiTexture || !uiTexture->rtv) {
		static bool loggedOnce = false;
		if (!loggedOnce) {
			logger::warn("[HDR] BeginUIRendering skipped - uiTexture={}, rtv={}",
				uiTexture ? "valid" : "NULL",
				(uiTexture && uiTexture->rtv) ? "valid" : "NULL");
			loggedOnce = true;
		}
		return;
	}

	static bool loggedOnce = false;
	if (!loggedOnce) {
		logger::info("[HDR] BeginUIRendering - setting uiTexture as render target for ImGui");
		loggedOnce = true;
	}

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

void HDR::EndUIRendering()
{
	// Skip if D3D12 frame gen is active
	if (globals::features::upscaling.d3d12SwapChainActive)
		return;

	if (!renderingUI) {
		static bool loggedOnce = false;
		if (!loggedOnce) {
			logger::warn("[HDR] EndUIRendering called but renderingUI=false");
			loggedOnce = true;
		}
		return;
	}

	static bool loggedOnce = false;
	if (!loggedOnce) {
		logger::info("[HDR] EndUIRendering - restoring render target, UI captured");
		loggedOnce = true;
	}

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

void HDR::RedirectFramebuffer()
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
	if (!savedFramebufferRTV)
		savedFramebufferRTV = fb.RTV;

	// Redirect to hdrTexture (R16G16B16A16_FLOAT) so ISHDR can write values >1.0
	fb.texture = reinterpret_cast<ID3D11Texture2D*>(hdrTexture->resource.get());
	fb.SRV = hdrTexture->srv.get();
	fb.RTV = hdrTexture->rtv.get();

	framebufferRedirected = true;
}

void HDR::RestoreFramebuffer()
{
	if (!framebufferRedirected)
		return;

	auto& fb = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER];

	fb.texture = savedFramebufferTexture;
	fb.SRV = savedFramebufferSRV;
	// RTV restored later by ClearUIBuffer or kept for UI redirect

	savedFramebufferTexture = nullptr;
	savedFramebufferSRV = nullptr;
	framebufferRedirected = false;
}

void HDR::SetUIBuffer()
{
	// Skip if D3D12 frame gen is active - it has its own UI buffer handling
	if (globals::features::upscaling.d3d12SwapChainActive)
		return;

	// SDR mode: vanilla UI composites directly to kFRAMEBUFFER, no redirect needed
	if (!settings.enableHDR)
		return;

	// Don't redirect if the HDR compute shader isn't available - vanilla UI path works without it
	if (!GetHDROutputCS())
		return;

	// Skip if resources aren't ready
	if (!uiTexture || !uiTexture->rtv || !hdrDataCB || !outputTexture) {
		static bool loggedOnce = false;
		if (!loggedOnce) {
			logger::warn("[HDR] SetUIBuffer skipped - resources not ready: uiTexture={}, rtv={}, hdrDataCB={}, outputTexture={}",
				uiTexture ? "valid" : "NULL",
				(uiTexture && uiTexture->rtv) ? "valid" : "NULL",
				hdrDataCB ? "valid" : "NULL",
				outputTexture ? "valid" : "NULL");
			loggedOnce = true;
		}
		return;
	}

	// Redirect kFRAMEBUFFER.RTV to our UI texture so vanilla UI renders to it
	auto& data = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER];

	// Save original RTV for restoration after Present
	if (!savedFramebufferRTV) {
		savedFramebufferRTV = data.RTV;
	}

	// Clear UI texture before vanilla UI renders
	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	globals::d3d::context->ClearRenderTargetView(uiTexture->rtv.get(), clearColor);

	// Redirect to our UI texture
	data.RTV = uiTexture->rtv.get();
	globals::d3d::context->OMSetRenderTargets(1, &data.RTV, nullptr);

	static bool loggedOnce = false;
	if (!loggedOnce) {
		logger::info("[HDR] SetUIBuffer - redirected kFRAMEBUFFER.RTV to uiTexture");
		loggedOnce = true;
	}
}

void HDR::ClearUIBuffer()
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

void HDR::ApplyHDR()
{
	std::lock_guard<std::mutex> lock(settingsMutex);

	if (!hdrDataCB || !hdrTexture || !outputTexture) {
		static bool loggedOnce = false;
		if (!loggedOnce) {
			logger::warn("[HDR] ApplyHDR early return: hdrDataCB={}, hdrTexture={}, outputTexture={}",
				hdrDataCB ? "valid" : "NULL",
				hdrTexture ? "valid" : "NULL",
				outputTexture ? "valid" : "NULL");
			loggedOnce = true;
		}
		return;
	}

	auto& upscaling = globals::features::upscaling;

	auto context = globals::d3d::context;
	auto state = globals::state;
	auto renderer = globals::game::renderer;

	// Update constant buffer before applying HDR
	UpdateHDRData();

	state->BeginPerfEvent("HDR Processing");

	static bool loggedDispatch = false;
	if (!loggedDispatch) {
		logger::info("[HDR] ApplyHDR running - dispatching compute shader");
		logger::info("[HDR] uiTexture={}, uiTexture->srv={}",
			uiTexture ? "valid" : "NULL",
			(uiTexture && uiTexture->srv) ? "valid" : "NULL");
		loggedDispatch = true;
	}

	{
		auto dispatchCount = Util::GetScreenDispatchCount(false);

		// When HDR is enabled, ISHDR wrote to hdrTexture (float16, values >1.0 preserved).
		// When SDR, ISHDR wrote to kFRAMEBUFFER (UNORM, tonemapped 0-1).
		auto& framebufferRT = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kFRAMEBUFFER];
		ID3D11ShaderResourceView* sceneSRV = (settings.enableHDR && hdrTexture && hdrTexture->srv) ? hdrTexture->srv.get() : framebufferRT.SRV;

		// Choose the correct UI buffer based on which path is active
		// When D3D12 swap chain is active, vanilla UI renders to uiBufferWrapped
		// Otherwise it renders to our uiTexture
		ID3D11ShaderResourceView* uiSRV = nullptr;
		if (upscaling.d3d12SwapChainActive && upscaling.dx12SwapChain.uiBufferWrapped) {
			uiSRV = upscaling.dx12SwapChain.uiBufferWrapped->srv;
		} else if (uiTexture && uiTexture->srv) {
			uiSRV = uiTexture->srv.get();
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
			// This allows SDR output through ISHDR.hlsl when HDR display shaders aren't available
			static bool loggedFallback = false;
			if (!loggedFallback) {
				logger::warn("HDR: HDR shader files not available - using SDR fallback (ISHDR output)");
				loggedFallback = true;
			}

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

		ID3D11ComputeShader* shader = nullptr;
		context->CSSetShader(shader, nullptr, 0);
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
		} else {
			static bool loggedOnce = false;
			if (!loggedOnce) {
				logger::warn("[HDR] Frame Gen path missing required resources - swapChainBufferWrapped={}, resource11={}, outputTexture={}, fallback to normal copy skipped",
					upscaling.dx12SwapChain.swapChainBufferWrapped ? "valid" : "NULL",
					(upscaling.dx12SwapChain.swapChainBufferWrapped && upscaling.dx12SwapChain.swapChainBufferWrapped->resource11) ? "valid" : "NULL",
					(outputTexture && outputTexture->resource) ? "valid" : "NULL");
				loggedOnce = true;
			}
		}
	} else {
		// Normal path: copy directly to swap chain back buffer
		ID3D11Texture2D* backBuffer = nullptr;
		HRESULT hr = globals::d3d::swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
		if (SUCCEEDED(hr) && backBuffer) {
			context->CopyResource(backBuffer, outputTexture->resource.get());
			backBuffer->Release();
			static bool loggedOnce = false;
			if (!loggedOnce) {
				logger::info("[HDR] Copying to D3D11 swap chain back buffer");
				loggedOnce = true;
			}
		} else {
			static bool loggedOnce = false;
			if (!loggedOnce) {
				logger::error("[HDR] Failed to get swap chain back buffer!");
				loggedOnce = true;
			}
		}
	}

	state->EndPerfEvent();
}

void HDR::DestroyResources()
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
}

void HDR::ClearShaderCache()
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

ID3D11ComputeShader* HDR::GetHDROutputCS()
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

ID3D11ComputeShader* HDR::GetUIBrightnessCS()
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

void HDR::ScaleUIBrightnessForFG()
{
	auto& upscaling = globals::features::upscaling;

	// Only run when FG is actively compositing UI this frame
	bool fgCompositing = upscaling.d3d12SwapChainActive &&
	                     upscaling.settings.frameGenerationMode &&
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

float HDR::GetDisplayMaxLuminance() const
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

void HDR::UpdateHDRData() const
{
	if (!hdrDataCB) {
		static bool loggedOnce = false;
		if (!loggedOnce) {
			logger::warn("[HDR] UpdateHDRData called but hdrDataCB is null!");
			loggedOnce = true;
		}
		return;
	}

	auto& upscaling = globals::features::upscaling;

	// Don't skip UI composite in main menu or loading screens - causes ghosting and brightness issues
	bool isMainOrLoadingMenu = globals::game::ui &&
	                           (globals::game::ui->IsMenuOpen(RE::MainMenu::MENU_NAME) ||
								   globals::game::ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME));

	bool fgActiveThisFrame = upscaling.d3d12SwapChainActive &&
	                         upscaling.settings.frameGenerationMode &&
	                         !globals::game::ui->GameIsPaused() &&
	                         !isMainOrLoadingMenu &&
	                         !globals::game::isVR;
	bool skipUIComposite = fgActiveThisFrame;

	// Linear Lighting keeps the pipeline linear throughout.
	// Without it, ISHDR gamma-encodes its output even in HDR mode.
	bool isSceneLinear = globals::features::linearLighting.settings.enableLinearLighting;

	// Use user-specified peak brightness for highlights compression
	float effectivePeakNits = static_cast<float>(settings.hdrPeakNits);

	HDRDataCB data;

	data.parameters0 = DirectX::XMVectorSet(
		settings.enableHDR ? 1.f : 0.f,
		static_cast<float>(settings.hdrPaperWhite),
		effectivePeakNits,
		skipUIComposite ? 1.f : 0.f);
	// UI brightness only used when HDR is enabled
	float uiBrightness = settings.hdrUIBrightness;
	data.parameters1 = DirectX::XMVectorSet(
		uiBrightness,
		isSceneLinear ? 1.f : 0.f,
		0.f, 0.f);
	hdrDataCB->Update(data);
}

void HDR::UpdateSwapChainColorSpace() const
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
