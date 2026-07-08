#include "Upscaling.h"

#include "../I18n/I18n.h"
#include "Deferred.h"
#include "DxvkLoader.h"
#include "HDRDisplay.h"
#include "Hooks.h"
#include "State.h"
#include "Upscaling/DxvkInterop.h"
#include "Upscaling/FrameGenController.h"
#include "Upscaling/Streamline.h"
#include "Utils/Game.h"
#include "Utils/UI.h"
#include <Windows.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>

#define I18N_KEY_PREFIX "feature.upscaling."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Upscaling::Settings,
	upscaleMethod,
	upscaleMethodNoDLSS,
	qualityMode,
	sharpnessFSR,
	reflexEnabled,
	reflexBoost,
	reflexLowLatencyMode,
	reflexLowLatencyBoost,
	frameGeneration,
	frameGenMethod,
	frameGenMultiplier,
	dlssgDynamic,
	fgShowOnlyGenerated,
	fgDebugView,
	fgDebugTearLines,
	fgDebugPacingLines,
	hardwareDefaultsApplied,
	vsync,
	frameRateLimitDivisor);

decltype(&D3D11CreateDeviceAndSwapChain) ptrD3D11CreateDeviceAndSwapChainUpscaling;

HRESULT WINAPI hk_D3D11CreateDeviceAndSwapChainUpscaling(
	IDXGIAdapter* pAdapter,
	D3D_DRIVER_TYPE DriverType,
	HMODULE Software,
	UINT Flags,
	[[maybe_unused]] const D3D_FEATURE_LEVEL* pFeatureLevels,
	[[maybe_unused]] UINT FeatureLevels,
	UINT SDKVersion,
	DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
	IDXGISwapChain** ppSwapChain,
	ID3D11Device** ppDevice,
	D3D_FEATURE_LEVEL* pFeatureLevel,
	ID3D11DeviceContext** ppImmediateContext)
{
	DXGI_ADAPTER_DESC adapterDesc;
	pAdapter->GetDesc(&adapterDesc);
	globals::state->SetAdapterDescription(adapterDesc.Description, adapterDesc.VendorId);

	auto& upscaling = globals::features::upscaling;

	pSwapChainDesc->SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	if (pSwapChainDesc->BufferCount < 2)
		pSwapChainDesc->BufferCount = 2;

	if (globals::features::hdrDisplay.loaded) {
		logger::info("[Upscaling] Upgrading swap chain format from {} to R10G10B10A2_UNORM for HDR", static_cast<int>(pSwapChainDesc->BufferDesc.Format));
		pSwapChainDesc->BufferDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
	} else if (pSwapChainDesc->BufferDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
		pSwapChainDesc->BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	} else if (pSwapChainDesc->BufferDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
		pSwapChainDesc->BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	}

	auto refreshRate = Upscaling::GetRefreshRate(pSwapChainDesc->OutputWindow);
	upscaling.refreshRate = refreshRate;

	// Exclusive fullscreen is handled entirely in DXVK (dxgi.fullscreenNativeRefresh): it does the real
	// mode-set at the game resolution but forces the native refresh, so the swapchain IS the game
	// resolution (the display engine stretches it to the panel) while DLSS-G keeps its refresh headroom.
	// Nothing CS-side to do beyond recording the windowed state.
	upscaling.isWindowed = pSwapChainDesc->Windowed;

	const D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_1;

	return ptrD3D11CreateDeviceAndSwapChainUpscaling(pAdapter,
		DriverType,
		Software,
		Flags,
		&featureLevel,
		1,
		SDKVersion,
		pSwapChainDesc,
		ppSwapChain,
		ppDevice,
		pFeatureLevel,
		ppImmediateContext);
}

// Discrete selector backed by a native ImGui::SliderInt over the option index. The current option's text
// is shown as the slider's value (the format string carries no %d, so ImGui renders it literally and it
// updates as the handle moves). The slider's max bound is options.size()-1, so callers limit the range
// simply by limiting how many options they pass (e.g. the MFG multiplier list capped to the hardware max).
static bool DrawStepper(const char* a_label, int* a_index, const std::vector<const char*>& a_options, bool a_disabled = false)
{
	const int count = static_cast<int>(a_options.size());
	if (count == 0)
		return false;
	*a_index = std::clamp(*a_index, 0, count - 1);

	if (!Upscaling::useArrowSteppers) {
		ImGui::BeginDisabled(a_disabled);
		const bool changed = ImGui::SliderInt(a_label, a_index, 0, count - 1, a_options[*a_index], ImGuiSliderFlags_NoInput);
		*a_index = std::clamp(*a_index, 0, count - 1);
		ImGui::EndDisabled();
		return changed;
	}

	// PhotoMode-style "< value >" stepper (ported from powerof3/PhotoMode's ImGui::EnumSlider /
	// CenteredTextWithArrows, MIT): the label sits in the left half (dimmed until the row is hovered) and the
	// current option is centered in the right half, flanked by chevron arrows that highlight on hover. Click
	// the left/right half (or press Left/Right while the row is hovered) to step; the option list wraps.
	bool changed = false;
	ImGui::PushID(a_label);
	ImGui::BeginDisabled(a_disabled);

	const ImGuiStyle& style = ImGui::GetStyle();
	const float       rowH = ImGui::GetFrameHeight();
	const float       fullW = ImGui::GetContentRegionAvail().x;
	const float       labelHalf = fullW * 0.5f + style.ItemInnerSpacing.x;
	const float       valueW = std::max(rowH * 3.0f, fullW - labelHalf);

	const float  rowStartX = ImGui::GetCursorPosX();
	const ImVec2 rowScreen = ImGui::GetCursorScreenPos();
	const ImVec2 valueMin(rowScreen.x + labelHalf, rowScreen.y);
	const ImVec2 valueMax(valueMin.x + valueW, rowScreen.y + rowH);
	const bool   rowHovered = !a_disabled && ImGui::IsMouseHoveringRect(valueMin, valueMax);

	// Label (left half) — normal CS text colour; dims only when the control is disabled (BeginDisabled alpha).
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(a_label);

	// Value region (right half) — an invisible button gives hover/click; we draw the text + chevrons over it.
	ImGui::SameLine();
	ImGui::SetCursorPosX(rowStartX + labelHalf);
	const bool  navPressed = ImGui::InvisibleButton("##value", ImVec2(valueW, rowH));
	const bool  active = !a_disabled && (rowHovered || ImGui::IsItemHovered() || ImGui::IsItemFocused());
	// Normal CS text colour by default; brighten to pure white when the row is hovered/controller-focused.
	const ImU32 col = active ? IM_COL32_WHITE : ImGui::GetColorU32(ImGuiCol_Text);

	ImDrawList* dl = ImGui::GetWindowDrawList();
	const char* text = a_options[*a_index];
	const ImVec2 ts = ImGui::CalcTextSize(text);
	dl->AddText(ImVec2(valueMin.x + (valueW - ts.x) * 0.5f, valueMin.y + (rowH - ts.y) * 0.5f), col, text);

	// Chevron arrows at the inner edges of the value region.
	const float  midY = valueMin.y + rowH * 0.5f;
	const float  ax = rowH * 0.16f, ay = rowH * 0.26f, inset = rowH * 0.5f, th = 2.5f;
	const ImVec2 lc(valueMin.x + inset, midY), rc(valueMax.x - inset, midY);
	dl->AddLine(ImVec2(lc.x + ax, lc.y - ay), ImVec2(lc.x - ax, lc.y), col, th);
	dl->AddLine(ImVec2(lc.x - ax, lc.y), ImVec2(lc.x + ax, lc.y + ay), col, th);
	dl->AddLine(ImVec2(rc.x - ax, rc.y - ay), ImVec2(rc.x + ax, rc.y), col, th);
	dl->AddLine(ImVec2(rc.x + ax, rc.y), ImVec2(rc.x - ax, rc.y + ay), col, th);

	// Activation fires once on release (navPressed): a mouse click steps by which half was clicked, a
	// controller/keyboard activate (A / Enter) advances. (Don't also test IsItemClicked — it fires on the press
	// frame while navPressed fires on release, which double-steps a single mouse click.)
	if (navPressed) {
		if (ImGui::IsMouseHoveringRect(valueMin, valueMax)) {
			const float mx = ImGui::GetIO().MousePos.x;
			*a_index = (mx < valueMin.x + valueW * 0.5f) ? (*a_index - 1 + count) % count : (*a_index + 1) % count;
		} else {
			*a_index = (*a_index + 1) % count;
		}
		changed = true;
	}
	// D-pad / arrow keys step while the row is hovered or controller-focused (single column → nav left/right
	// has nowhere to go, so it falls through to here, matching PhotoMode's EnumSlider).
	if (active) {
		if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft)) {
			*a_index = (*a_index - 1 + count) % count;
			changed = true;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight)) {
			*a_index = (*a_index + 1) % count;
			changed = true;
		}
	}

	ImGui::EndDisabled();
	ImGui::PopID();
	*a_index = std::clamp(*a_index, 0, count - 1);
	return changed;
}

// Two-state Off/On selector bound to a bool, using the same native slider.
static bool DrawToggleStepper(const char* a_label, bool* a_value, bool a_disabled = false)
{
	const std::vector<const char*> onOff = { "Off", "On" };
	int idx = *a_value ? 1 : 0;
	if (DrawStepper(a_label, &idx, onOff, a_disabled)) {
		*a_value = (idx == 1);
		return true;
	}
	return false;
}

void Upscaling::DrawSettings()
{
	auto* streamline = Streamline::GetSingleton();
	const bool dlssAvailable = streamline->IsDLSSSupported();
	const bool xessAvailable = streamline->IsXeSSSupported();

	const bool dlssgAvailable = streamline->IsDLSSGSupported();
	const bool reflexAvailable = streamline->IsReflexSupported();

	// Selecting any upscaler sets the single upscaleMethod (so the others read off). Remember the last
	// non-DLSS pick in upscaleMethodNoDLSS for the GetUpscaleMethod fallback on non-NVIDIA GPUs.
	auto selectUpscaler = [&](UpscaleMethod a_m) {
		settings.upscaleMethod = (uint)a_m;
		if (a_m != UpscaleMethod::kDLSS)
			settings.upscaleMethodNoDLSS = (uint)a_m;
	};

	// ---- Display (VSync + frame limiter) ----
	ImGui::SeparatorText(T(TKEY("display_header"), "Display"));
	{
		// VSync stepper. DLSS-G forces it off (incompatible on Vulkan) — show it disabled with a "(forced off)" note.
		const bool vsyncForcedOff = IsFrameGenerationActive() && GetFrameGenMethod() == FrameGenMethod::kDLSSG;
		if (vsyncForcedOff) {
			bool effectiveVsync = false;
			DrawToggleStepper(T(TKEY("vsync"), "Vertical Synchronisation"), &effectiveVsync, /*disabled=*/true);
			ImGui::SameLine();
			ImGui::TextDisabled("%s", T(TKEY("vsync_dlssg"), "(forced off by DLSS-G)"));
		} else {
			DrawToggleStepper(T(TKEY("vsync"), "Vertical Synchronisation"), &settings.vsync);
		}

		// Frame Rate: a stepper across divisors of the monitor refresh rate, far-right stop = "Unlocked (variable)".
		// Stored as the divisor (settings.frameRateLimitDivisor): 1 = refresh, 2 = half…, 0 = unlocked. The stop text
		// shows the resolved fps so the stops track the actual monitor (e.g. 165 Hz → 41/55/83/165 FPS/Unlocked).
		const int refresh = GetMonitorRefreshRate();
		std::vector<int> divisorOptions;  // each entry is a divisor; 0 = unlocked
		for (int d = 4; d >= 1; --d) {
			if (refresh / d >= 30)
				divisorOptions.push_back(d);
		}
		if (divisorOptions.empty())
			divisorOptions.push_back(1);
		divisorOptions.push_back(0);  // Unlocked (variable) — far-right stop

		std::vector<std::string> fpsStrings;
		for (int d : divisorOptions)
			fpsStrings.push_back(d == 0 ?
			                         std::string(T(TKEY("frame_rate_unlocked"), "Unlocked (variable)")) :
			                         std::format("{} FPS", refresh / d));
		std::vector<const char*> fpsLabels;
		for (auto& s : fpsStrings)
			fpsLabels.push_back(s.c_str());

		const int maxSel = static_cast<int>(divisorOptions.size()) - 1;
		int sel = maxSel > 0 ? maxSel - 1 : 0;  // default to refresh (divisor 1) if the stored value isn't a stop
		for (int i = 0; i <= maxSel; ++i) {
			if (divisorOptions[i] == settings.frameRateLimitDivisor)
				sel = i;
		}
		DrawStepper(T(TKEY("frame_rate"), "Frame Rate"), &sel, fpsLabels);
		settings.frameRateLimitDivisor = divisorOptions[std::clamp(sel, 0, maxSel)];
	}

	// ---- Upscaling ----
	ImGui::SeparatorText(T(TKEY("upscaling_header"), "Upscaling"));
	{
		// Single Technique stepper: Off / TAA / FSR / (XeSS) / (DLSS), filtered by what the GPU supports.
		// Bound to upscaleMethod; Off = no AA, TAA = base temporal AA, the rest are upscalers.
		std::vector<const char*>   techLabels = { "Off", T(TKEY("method_taa"), "TAA"), "FSR" };
		std::vector<UpscaleMethod> techMethods = { UpscaleMethod::kNONE, UpscaleMethod::kTAA, UpscaleMethod::kFSR };
		if (xessAvailable) {
			techLabels.push_back("XeSS");
			techMethods.push_back(UpscaleMethod::kXeSS);
		}
		if (dlssAvailable) {
			techLabels.push_back("DLSS");
			techMethods.push_back(UpscaleMethod::kDLSS);
		}

		int techIdx = 0;
		for (int i = 0; i < static_cast<int>(techMethods.size()); ++i)
			if (settings.upscaleMethod == (uint)techMethods[i])
				techIdx = i;
		if (DrawStepper(T(TKEY("upscaling_technique"), "Upscaling Technique"), &techIdx, techLabels))
			selectUpscaler(techMethods[std::clamp(techIdx, 0, static_cast<int>(techMethods.size()) - 1)]);

		// Upscale Preset (only for upscalers that resolve a quality level). Native = qualityMode 0 (DLAA for
		// DLSS, native-res AA for FSR/XeSS), then Quality/Balanced/Performance/Ultra Performance.
		const UpscaleMethod cur = (UpscaleMethod)settings.upscaleMethod;
		const bool hasPreset = (cur == UpscaleMethod::kFSR || cur == UpscaleMethod::kXeSS || cur == UpscaleMethod::kDLSS);
		if (hasPreset) {
			const std::vector<const char*> presets = {
				T(TKEY("preset_native"), "Native"),                        // qualityMode 0
				T(TKEY("preset_quality"), "Quality"),                      // 1
				T(TKEY("preset_balanced"), "Balanced"),                    // 2
				T(TKEY("preset_performance"), "Performance"),              // 3
				T(TKEY("preset_ultra_performance"), "Ultra Performance"),  // 4
			};
			int pIdx = std::clamp((int)settings.qualityMode, 0, 4);
			if (DrawStepper(T(TKEY("upscale_preset"), "Upscale Preset"), &pIdx, presets))
				settings.qualityMode = (uint)std::clamp(pIdx, 0, 4);

			if (cur == UpscaleMethod::kFSR)
				ImGui::SliderFloat(T(TKEY("sharpness"), "Sharpness"), &settings.sharpnessFSR, 0.0f, 1.0f, "%.1f");
		}
	}

	// ---- Frame Generation ----
	ImGui::SeparatorText(T(TKEY("fg_header"), "Frame Generation"));
	{
		// ONE method per system, hardware-derived (GetFrameGenMethod): DLSS-G on hardware that
		// supports it, FSR-FG everywhere else. The user only toggles frame generation on/off
		// (plus the multiplier on DLSS-G hardware).
		if (dlssgAvailable) {
			// DLSS Frame Generation: Off / Dynamic (or Auto) / 2x … up to the hardware max.
			const uint32_t maxFrames = streamline->GetDLSSGMaxFramesToGenerate();
			const uint     maxMultiplier = std::clamp<uint>(maxFrames > 0u ? maxFrames + 1u : 2u, 2u, 6u);
			// The adaptive slot is "Dynamic" only when the hardware supports Dynamic
			// MFG (RTX 50+); otherwise DLSS-G runs its automatic single-frame mode,
			// so label it "Auto" (e.g. RTX 40 reports DynamicMFG=false).
			std::vector<std::string> fgStrings = { "Off",
				streamline->IsDLSSGDynamicSupported()
					? std::string(T(TKEY("fg_dynamic"), "Dynamic"))
					: std::string(T(TKEY("fg_auto"), "Auto")) };
			for (uint m = 2; m <= maxMultiplier; ++m)
				fgStrings.push_back(std::format("{}x", m));
			std::vector<const char*> fgStates;
			for (auto& s : fgStrings)
				fgStates.push_back(s.c_str());

			int fgIdx = !settings.frameGeneration ? 0 :
			            settings.dlssgDynamic ? 1 :
			            std::clamp((int)settings.frameGenMultiplier, 2, (int)maxMultiplier);
			if (DrawStepper(T(TKEY("nv_frame_generation"), "DLSS Frame Generation"), &fgIdx, fgStates)) {
				settings.frameGeneration = fgIdx != 0;
				if (fgIdx != 0) {
					settings.dlssgDynamic = (fgIdx == 1);
					if (fgIdx >= 2)
						settings.frameGenMultiplier = (uint)fgIdx;  // 2x..maxMultiplier
				}
			}
		} else {
			// FSR Frame Generation: Off / On.
			DrawToggleStepper(T(TKEY("amd_fsr_fg"), "FSR Frame Generation"), &settings.frameGeneration);
		}
	}

	// ---- Low Latency (NVIDIA Reflex) ----
	if (reflexAvailable) {
		ImGui::SeparatorText(T(TKEY("ll_header"), "Low Latency"));
		// Three states: Off / On / On + Boost. FG dictates it (DLSS-G forces on, FSR-FG forces off); else user toggle.
		const std::vector<const char*> reflexStates = { "Off", "On", T(TKEY("reflex_on_boost"), "On + Boost") };
		const bool fgForcesReflex = IsFrameGenerationActive() &&
		                            (GetFrameGenMethod() == FrameGenMethod::kDLSSG || GetFrameGenMethod() == FrameGenMethod::kFSR);
		if (fgForcesReflex) {
			int idx = GetEffectiveReflex() ? (settings.reflexBoost ? 2 : 1) : 0;
			DrawStepper(T(TKEY("nv_reflex"), "NVIDIA Reflex Low Latency"), &idx, reflexStates, /*disabled=*/true);
			ImGui::SameLine();
			ImGui::TextDisabled("%s", GetFrameGenMethod() == FrameGenMethod::kDLSSG ?
			                              T(TKEY("reflex_forced_dlssg"), "(forced on by DLSS-G)") :
			                              T(TKEY("reflex_forced_fsrfg"), "(forced off by FSR frame gen)"));
		} else {
			int idx = settings.reflexEnabled ? (settings.reflexBoost ? 2 : 1) : 0;
			if (DrawStepper(T(TKEY("nv_reflex"), "NVIDIA Reflex Low Latency"), &idx, reflexStates)) {
				settings.reflexEnabled = (idx >= 1);
				settings.reflexBoost = (idx == 2);
			}
		}
	}
}

void Upscaling::SaveSettings(json& o_json)
{
	o_json = settings;
	auto iniSettingCollection = globals::game::iniPrefSettingCollection;
	if (iniSettingCollection) {
		auto setting = iniSettingCollection->GetSetting("bUseTAA:Display");
		if (setting) {
			iniSettingCollection->WriteSetting(setting);
		}
	}
}

void Upscaling::LoadSettings(json& o_json)
{
	settings = o_json;

	constexpr auto enumCount = 5;  // kNONE, kTAA, kFSR, kDLSS, kXeSS
	if (settings.upscaleMethod >= static_cast<uint>(enumCount)) {
		logger::warn("[Upscaling] Loaded upscaleMethod {} out of range, clamping to {}", settings.upscaleMethod, enumCount - 1);
		settings.upscaleMethod = enumCount - 1;
	}
	if (settings.upscaleMethodNoDLSS >= static_cast<uint>(UpscaleMethod::kDLSS))
		settings.upscaleMethodNoDLSS = static_cast<uint>(UpscaleMethod::kFSR);

	constexpr auto fgMethodCount = 2;  // kFSR, kDLSSG
	if (settings.frameGenMethod >= static_cast<uint>(fgMethodCount))
		settings.frameGenMethod = static_cast<uint>(FrameGenMethod::kFSR);

	// Migrate legacy Reflex settings to new reflexEnabled bool.
	if (settings.reflexLowLatencyMode && !settings.reflexEnabled) {
		settings.reflexEnabled = true;
		settings.reflexBoost = settings.reflexLowLatencyBoost;
	}

	auto iniSettingCollection = globals::game::iniPrefSettingCollection;
	if (iniSettingCollection) {
		auto setting = iniSettingCollection->GetSetting("bUseTAA:Display");
		if (setting) {
			iniSettingCollection->ReadSetting(setting);
		}
	}
}

void Upscaling::RestoreDefaultSettings()
{
	settings = {};
}

void Upscaling::DataLoaded()
{
	Util::DisableVanillaTAA();

	static auto fDRClampOffset = RE::GetINISetting("fDRClampOffset:Display");
	fDRClampOffset->data.f = 0.0f;
}

void Upscaling::Load()
{
	if (DxvkLoader::IsLoaded()) {
		// Map sl.interposer.dll NOW so DXVK's Vulkan loader (which tries sl.interposer.dll first) aliases
		// it at its imminent first-DXGI VkInstance creation — routing DXVK's whole Vulkan surface through
		// Streamline (full interposition). Must precede DXVK's instance creation; this is that window.
		//
		// ALWAYS preloaded (do not gate on the configured upscale method): DLSS must be available
		// on capable hardware at all times, and FSR/XeSS run as sl.* plugins too, so every runtime
		// upscaler/frame-gen switch needs the interposition established at boot.
		Streamline::GetSingleton()->PreloadInterposer();
	}

	// Route the game's device creation to DXVK's subfolder-loaded export (set up by
	// DxvkLoader during InstallEarlyHooks), not the inert System32 d3d11 the IAT now
	// resolves to. Fall back to the IAT original only if DXVK failed to load.
	const auto iatOriginal = SKSE::PatchIAT(hk_D3D11CreateDeviceAndSwapChainUpscaling, "d3d11.dll", "D3D11CreateDeviceAndSwapChain");
	*(uintptr_t*)&ptrD3D11CreateDeviceAndSwapChainUpscaling = DxvkLoader::IsLoaded() ?
	                                                              reinterpret_cast<uintptr_t>(DxvkLoader::GetD3D11CreateDeviceAndSwapChain()) :
	                                                              iatOriginal;
}

struct BSImageSpace_Init_FXAA
{
	static void thunk()
	{
		func();

		auto fxaaEnabled = reinterpret_cast<bool*>(REL::RelocationID(513281, 391028).address());
		*fxaaEnabled = false;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

void Upscaling::PostPostLoad()
{
	bool isGOG = !GetModuleHandle(L"steam_api64.dll");
	stl::detour_thunk<MenuManagerDrawInterfaceStartHook>(REL::RelocationID(79947, 82084));

	stl::write_thunk_call<Main_UpdateJitter>(REL::RelocationID(75460, 77245).address() + REL::Relocate(0xE5, isGOG ? 0x133 : 0xE2));

	REL::safe_write(REL::RelocationID(35556, 36555).address() + REL::Relocate(0x2D, 0x2D), REL::NOP5, sizeof(REL::NOP5));

	stl::write_thunk_call<Main_PostProcessing>(REL::RelocationID(100430, 107148).address() + REL::Relocate(0x1F0, 0x1E7));

	stl::detour_thunk<SetScissorRect>(REL::RelocationID(75564, 77365));

	stl::detour_thunk<BSFaceGenManager_UpdatePendingCustomizationTextures>(REL::RelocationID(26455, 27041));

	stl::write_thunk_call<Main_RenderPrecipitation>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x3A1, 0x3A1));

	stl::detour_thunk<BSImageSpace_Init_FXAA>(REL::RelocationID(98974, 105626));

	logger::info("[Upscaling] Installed hooks");
}

#undef I18N_KEY_PREFIX

Upscaling::UpscaleMethod Upscaling::GetUpscaleMethod() const
{
	auto method = (UpscaleMethod)settings.upscaleMethod;
	if (method == UpscaleMethod::kDLSS &&
		!Streamline::GetSingleton()->IsDLSSSupported()) {
		method = (UpscaleMethod)settings.upscaleMethodNoDLSS;
		if (method == UpscaleMethod::kDLSS)
			method = UpscaleMethod::kFSR;
	}
	if (method == UpscaleMethod::kXeSS &&
		!Streamline::GetSingleton()->IsXeSSSupported()) {
		method = UpscaleMethod::kFSR;
	}
	// FSR is the fallback of the chains above but is itself an SL plugin (sl.fsr):
	// without Streamline (plugins missing, or disabled by configuration this session
	// because no SL feature was requested at startup) its backend cannot engage, and
	// a half-engaged upscaler (render-res switch with no evaluate) is undefined
	// behavior. Clamp to TAA — a restart with the new setting picks the real method.
	if (method == UpscaleMethod::kFSR &&
		!Streamline::GetSingleton()->IsFSRSupported()) {
		method = UpscaleMethod::kTAA;
	}
	return method;
}

void Upscaling::ApplyHardwareDefaults()
{
	static bool applied = false;
	if (applied)
		return;
	applied = true;

	// Auto-select methods only ONCE per install (persisted flag). After that the user's saved config is
	// authoritative — otherwise an explicit choice (e.g. switching FG to FSR because DLSS-G is unstable
	// on this GPU) would be forced back to the hardware default on every launch.
	if (settings.hardwareDefaultsApplied)
		return;
	settings.hardwareDefaultsApplied = true;

	auto* sl = Streamline::GetSingleton();

	// Upscaling priority: DLSS -> XeSS -> FSR 3
	if (settings.upscaleMethod == (uint)UpscaleMethod::kFSR) {
		if (sl->IsDLSSSupported()) {
			settings.upscaleMethod = (uint)UpscaleMethod::kDLSS;
			settings.upscaleMethodNoDLSS = (uint)UpscaleMethod::kFSR;
			logger::info("[Upscaling] Hardware default: DLSS selected (NVIDIA GPU detected)");
		} else if (sl->IsXeSSSupported()) {
			settings.upscaleMethod = (uint)UpscaleMethod::kXeSS;
			logger::info("[Upscaling] Hardware default: XeSS selected (Intel GPU detected via Streamline)");
		}
	}

	// Low-latency: Reflex if available
	if (!settings.reflexEnabled) {
		if (sl->IsReflexSupported()) {
			settings.reflexEnabled = true;
			logger::info("[Upscaling] Hardware default: Reflex low-latency enabled");
		}
	}
}

Upscaling::FrameGenMethod Upscaling::GetFrameGenMethod() const
{
	// ONE frame-generation method per system, hardware-derived and fixed for the session:
	// DLSS-G on hardware that supports it, FSR-FG everywhere else. Not user-selectable — the
	// two methods need opposite present paths (DLSS-G's pacer requires hardware flips; the FFX
	// present worker requires the copy path on NVIDIA), which the driver locks per window, so
	// a single hardware-matched method keeps everything switch-free and boot-independent.
	// settings.frameGenMethod is legacy-ignored.
	return Streamline::GetSingleton()->IsDLSSGSupported() ? FrameGenMethod::kDLSSG : FrameGenMethod::kFSR;
}

bool Upscaling::IsFrameGenerationActive() const
{
	if (!loaded || !settings.frameGeneration)
		return false;
	auto fgMethod = GetFrameGenMethod();
	if (fgMethod == FrameGenMethod::kDLSSG)
		return Streamline::GetSingleton()->IsDLSSGSupported();
	// kFSR: the sl.fsr plugin owns frame generation via its FFX FrameInterpolationSwapChain.
	// "Active" means FSR is supported and we've delivered the enable (settings.frameGeneration
	// already checked above).
	return Streamline::GetSingleton()->IsFSRSupported();
}

bool Upscaling::GetEffectiveReflex() const
{
	// DLSS-G frame generation REQUIRES Reflex on (it stalls otherwise); FSR frame generation requires it
	// OFF (FFX paces the swapchain itself, Reflex fights it); with no frame generation the user's saved
	// reflexEnabled toggle wins. The saved preference is never mutated — only the effective value changes.
	if (IsFrameGenerationActive()) {
		switch (GetFrameGenMethod()) {
		case FrameGenMethod::kDLSSG:
			return true;
		case FrameGenMethod::kFSR:
			return false;
		default:
			break;
		}
	}
	return settings.reflexEnabled;
}

int Upscaling::GetMonitorRefreshRate() const
{
	if (refreshRate >= 1.0)
		return static_cast<int>(std::lround(refreshRate));
	DEVMODEA dm{};
	dm.dmSize = sizeof(dm);
	if (EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &dm) && (dm.dmFields & DM_DISPLAYFREQUENCY) && dm.dmDisplayFrequency > 1)
		return static_cast<int>(dm.dmDisplayFrequency);
	return 60;
}

int Upscaling::GetTargetFrameRate() const
{
	// Divisor of the monitor refresh rate: 0 (unlocked) => no cap; otherwise refresh / divisor (so the cap
	// stays at/below refresh — VSync-on stays inside the VRR window and DLSS-G's generated frames don't
	// outrun the display). Returns 0 for "no limit".
	const int divisor = settings.frameRateLimitDivisor;
	if (divisor <= 0)
		return 0;
	return std::max(1, static_cast<int>(std::lround(static_cast<double>(GetMonitorRefreshRate()) / divisor)));
}

void Upscaling::ApplyDxvkFrameRateLimit(double a_fps)
{
	// Non-Reflex fallback: drive DXVK's own frame limiter via the dxvkSetTargetFrameRate export (same
	// GetProcAddress pattern as the other dxvk* hooks). a_fps<=0 clears the limit. Graceful no-op if the export
	// is absent (e.g. a DXVK build without it) so the Reflex path is unaffected. Only pushes on change.
	using SetFrameRateFn = void (*)(double);
	static SetFrameRateFn fn = nullptr;
	static bool resolved = false;
	if (!resolved) {
		resolved = true;
		if (HMODULE m = GetModuleHandleW(L"dxvk_d3d11.dll"))
			fn = reinterpret_cast<SetFrameRateFn>(GetProcAddress(m, "dxvkSetTargetFrameRate"));
	}
	static double lastFps = -2.0;
	if (fn && a_fps != lastFps) {
		lastFps = a_fps;
		fn(a_fps > 0.0 ? a_fps : 0.0);
	}
}

HRESULT Upscaling::PresentWithFrameGeneration(IDXGISwapChain* a_swapChain, UINT a_syncInterval, UINT a_flags,
	const std::function<HRESULT(IDXGISwapChain*, UINT, UINT)>& a_present)
{
	if (!IsFrameGenerationActive())
		return a_present(a_swapChain, a_syncInterval, a_flags);

	auto fgMethod = GetFrameGenMethod();

	// DLSS-G: Streamline interpolates + paces inside its interposed vkQueuePresentKHR hook.
	// SL's present hook requires a resource tag EVERY present or it waits forever on absent
	// inputs — so guarantee one here. Then present normally; DXVK's present reaches SL's
	// interposed present hook, which does the frame generation.
	if (fgMethod == FrameGenMethod::kDLSSG) {
		Streamline::GetSingleton()->EnsureDLSSGPresentTag();
		return a_present(a_swapChain, a_syncInterval, a_flags);
	}

	// FSR FG: the sl.fsr plugin's FFX FrameInterpolationSwapChain (installed via its
	// CreateSwapchainKHR hook) interpolates + double-presents inside the plugin's vkQueuePresentKHR
	// hook. DXVK's present reaches that hook through the interposer — present once and let the
	// plugin run.
	return a_present(a_swapChain, a_syncInterval, a_flags);
}

void Upscaling::CreateUpscaledTexture()
{
	if (upscaledTexture)
		return;

	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC texDesc{};
	main.texture->GetDesc(&texDesc);

	// Display-resolution texture with SRV+UAV for upscale output.
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	upscaledTexture = new Texture2D(texDesc);
	Util::SetResourceName(upscaledTexture->resource.get(), "Upscaling::UpscaledTexture");

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = texDesc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	upscaledTexture->CreateSRV(srvDesc);

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = texDesc.Format;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Texture2D.MipSlice = 0;
	upscaledTexture->CreateUAV(uavDesc);

	logger::info("[Upscaling] Created upscaled texture ({}x{}, format={})",
		texDesc.Width, texDesc.Height, static_cast<int>(texDesc.Format));
}

void Upscaling::DestroyUpscaledTexture()
{
	if (upscaledTexture) {
		delete upscaledTexture;
		upscaledTexture = nullptr;
		logger::debug("[Upscaling] Destroyed upscaled texture");
	}
}

void Upscaling::CreateHudlessTexture()
{
	auto renderer = globals::game::renderer;
	// Size + format from kFRAMEBUFFER (the swapchain back buffer) — the hudless is captured FROM it just
	// before the UI composites, so matching its format means the hudless is already in the back-buffer
	// format FFX/DLSS-G expect (no precision-group mismatch).
	// Source = kFRAMEBUFFER (the swapchain back buffer). Its `.texture` field is NULL under CS+DXVK, but its
	// SRV is valid and references the real back-buffer texture — fetch the resource from the SRV. This gives
	// the back-buffer FORMAT (R10G10B10A2 / 10-bit when HDR is on; kMAIN is RGBA16F / 16-bit), so the hudless
	// matches the back buffer's precision group exactly (no conversion needed for FFX's UI extraction).
	auto& fb = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kFRAMEBUFFER];
	if (!fb.SRV)
		return;  // framebuffer SRV not ready yet (early load) — defer; the capture hook creates it lazily
	winrt::com_ptr<ID3D11Resource> fbResource;
	fb.SRV->GetResource(fbResource.put());
	winrt::com_ptr<ID3D11Texture2D> fbTexture;
	if (!fbResource || FAILED(fbResource->QueryInterface(IID_PPV_ARGS(fbTexture.put()))) || !fbTexture)
		return;

	D3D11_TEXTURE2D_DESC texDesc{};
	fbTexture->GetDesc(&texDesc);
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	hudlessTexture = new Texture2D(texDesc);
	Util::SetResourceName(hudlessTexture->resource.get(), "Upscaling::HudlessTexture");

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = texDesc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	hudlessTexture->CreateSRV(srvDesc);

	logger::info("[Upscaling] Created hudless texture ({}x{}, format={})",
		texDesc.Width, texDesc.Height, static_cast<int>(texDesc.Format));
}

void Upscaling::DestroyHudlessTexture()
{
	if (hudlessTexture) {
		delete hudlessTexture;
		hudlessTexture = nullptr;
		logger::debug("[Upscaling] Destroyed hudless texture");
	}
}

void Upscaling::CheckResources(UpscaleMethod a_upscalemethod)
{
	static auto previousUpscaleMode = UpscaleMethod::kTAA;

	if (previousUpscaleMode != a_upscalemethod) {
		logger::debug("[Upscaling] Upscale method changed: {} ({}) -> {} ({})",
			static_cast<int>(previousUpscaleMode), magic_enum::enum_name(previousUpscaleMode),
			static_cast<int>(a_upscalemethod), magic_enum::enum_name(a_upscalemethod));

		// Upscaled texture lifecycle — shared by all upscale methods that need a separate output texture.
		bool hadUpscale = (previousUpscaleMode == UpscaleMethod::kFSR ||
		                   previousUpscaleMode == UpscaleMethod::kDLSS ||
		                   previousUpscaleMode == UpscaleMethod::kXeSS) &&
		                  previousUpscalingWasActive;
		if (hadUpscale) {
			// The upscaler eval (DLSS/FSR/XeSS via cs_EvaluateFeatureCore) submits to DXVK's queue
			// asynchronously (waitIdle=false) and is invisible to DXVK's resource-lifetime tracker, so an
			// in-flight dispatch may still be writing these interop images. Drain the interop ring before
			// freeing them, else DXVK can recycle the backing VkImage under a running dispatch -> device
			// lost. Costs a CPU stall only on this rare resource-change frame; the per-frame path stays async.
			if (auto* dxvk = DxvkInterop::GetSingleton(); dxvk && dxvk->CommandResourcesReady())
				dxvk->DrainCommandRing();
			DestroyUpscaledTexture();
			DestroyHudlessTexture();
		}
		if (a_upscalemethod == UpscaleMethod::kFSR ||
		    a_upscalemethod == UpscaleMethod::kDLSS ||
		    a_upscalemethod == UpscaleMethod::kXeSS) {
			CreateUpscaledTexture();
			CreateHudlessTexture();
		}

		previousUpscaleMode = a_upscalemethod;
		previousUpscalingWasActive = IsUpscalingActive();
	}

	// Frame-generation present ownership (DLSS-G / FSR-FG lifecycle, plugin
	// load state, swapchain recreates) is owned by the FrameGen controller.
	FrameGen::Controller::GetSingleton()->Reconcile();
}

ID3D11PixelShader* Upscaling::GetDepthRefractionUpscalePS()
{
	if (!depthRefractionUpscalePS) {
		logger::debug("Compiling DepthRefractionUpscalePS.hlsl");
		depthRefractionUpscalePS.attach((ID3D11PixelShader*)Util::CompileShader(L"Data/Shaders/Upscaling/DepthRefractionUpscalePS.hlsl", {}, "ps_5_0"));
	}
	return depthRefractionUpscalePS.get();
}

ID3D11PixelShader* Upscaling::GetUnderwaterMaskUpscalePS()
{
	if (!underwaterMaskUpscalePS) {
		logger::debug("Compiling UnderwaterMaskUpscalePS.hlsl");
		underwaterMaskUpscalePS.attach((ID3D11PixelShader*)Util::CompileShader(L"Data/Shaders/Upscaling/UnderwaterMaskUpscalePS.hlsl", {}, "ps_5_0"));
	}
	return underwaterMaskUpscalePS.get();
}

ID3D11VertexShader* Upscaling::GetUpscaleVS()
{
	if (!upscaleVS) {
		logger::debug("Compiling UpscaleVS.hlsl");
		upscaleVS.attach((ID3D11VertexShader*)Util::CompileShader(L"Data/Shaders/Upscaling/UpscaleVS.hlsl", {}, "vs_5_0"));
	}
	return upscaleVS.get();
}

eastl::unique_ptr<Texture2D> Upscaling::CreateTextureFromSource(ID3D11Resource* src, uint32_t width, uint32_t height,
	bool copyBindFlags, bool createSRV, bool createUAV, const char* name)
{
	D3D11_TEXTURE2D_DESC srcDesc;
	static_cast<ID3D11Texture2D*>(src)->GetDesc(&srcDesc);

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = srcDesc.Format;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = copyBindFlags ? srcDesc.BindFlags : (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);

	auto tex = eastl::make_unique<Texture2D>(desc);

	if (name) {
		Util::SetResourceName(tex->resource.get(), name);
	}

	if (createSRV) {
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = srcDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;
		tex->CreateSRV(srvDesc);
	}
	if (createUAV) {
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = srcDesc.Format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = 0;
		tex->CreateUAV(uavDesc);
	}
	return tex;
}

int32_t GetJitterPhaseCount(int32_t renderWidth, int32_t displayWidth)
{
	const float basePhaseCount = 8.0f;
	const int32_t jitterPhaseCount = int32_t(basePhaseCount * pow((float(displayWidth) / renderWidth), 2.0f));
	return jitterPhaseCount;
}

static float Halton(int32_t index, int32_t base)
{
	float f = 1.0f, result = 0.0f;

	for (int32_t currentIndex = index; currentIndex > 0;) {
		f /= (float)base;
		result = result + f * (float)(currentIndex % base);
		currentIndex = (uint32_t)(floorf((float)(currentIndex) / (float)(base)));
	}

	return result;
}

void GetJitterOffset(float* outX, float* outY, int32_t index, int32_t phaseCount)
{
	const float x = Halton((index % phaseCount) + 1, 2) - 0.5f;
	const float y = Halton((index % phaseCount) + 1, 3) - 0.5f;

	*outX = x;
	*outY = y;
}

void Upscaling::ConfigureTAA()
{
	auto upscaleMethod = GetUpscaleMethod();

	Util::SetTemporal(upscaleMethod != UpscaleMethod::kNONE);
}

void Upscaling::ConfigureUpscaling(RE::BSGraphics::State* a_viewport)
{
	auto upscaleMethod = GetUpscaleMethod();

	CheckResources(upscaleMethod);

	projectionPosScaleX = a_viewport->projectionPosScaleX;
	projectionPosScaleY = a_viewport->projectionPosScaleY;

	auto state = globals::state;
	float2 screenSize{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };

	auto screenWidth = static_cast<int>(screenSize.x);
	auto screenHeight = static_cast<int>(screenSize.y);

	if (upscaleMethod == UpscaleMethod::kFSR || upscaleMethod == UpscaleMethod::kXeSS || upscaleMethod == UpscaleMethod::kDLSS) {
		auto getUpscaleRatio = [](uint qualityMode) -> float {
			switch (qualityMode) {
			case 0:
				return 1.0f;  // Native (Quality)
			case 1:
				return 1.5f;  // Quality
			case 2:
				return 1.7f;  // Balanced
			case 3:
				return 2.0f;  // Performance
			case 4:
				return 3.0f;  // Ultra Performance
			default:
				return 1.5f;
			}
		};
		float resolutionScaleBase = 1.0f / getUpscaleRatio(settings.qualityMode);

		auto renderWidth = static_cast<int>(screenWidth * resolutionScaleBase);
		auto renderHeight = static_cast<int>(screenHeight * resolutionScaleBase);

		resolutionScale.x = static_cast<float>(renderWidth) / static_cast<float>(screenWidth);
		resolutionScale.y = static_cast<float>(renderHeight) / static_cast<float>(screenHeight);

		auto phaseCount = GetJitterPhaseCount(renderWidth, screenWidth);

		GetJitterOffset(&jitter.x, &jitter.y, state->frameCount, phaseCount);

		a_viewport->projectionPosScaleX = -2.0f * jitter.x / renderWidth;

		a_viewport->projectionPosScaleY = 2.0f * jitter.y / renderHeight;
	} else {
		resolutionScale = { 1.0f, 1.0f };

		jitter.x = -a_viewport->projectionPosScaleX * screenWidth / 2.0f;

		jitter.y = a_viewport->projectionPosScaleY * screenHeight / 2.0f;
	}

	auto& runtimeData = a_viewport->GetRuntimeData();

	runtimeData.dynamicResolutionPreviousWidthRatio = dynamicResolutionWidthRatio;
	runtimeData.dynamicResolutionPreviousHeightRatio = dynamicResolutionHeightRatio;
	runtimeData.dynamicResolutionWidthRatio = resolutionScale.x;
	runtimeData.dynamicResolutionHeightRatio = resolutionScale.y;

	dynamicResolutionWidthRatio = resolutionScale.x;
	dynamicResolutionHeightRatio = resolutionScale.y;

	runtimeData.dynamicResolutionLock = 1;

	// Log the active upscaler's render vs display resolution whenever it changes.
	static float s_loggedScale = -1.0f;
	static int s_loggedMethod = -1;
	if (std::abs(resolutionScale.x - s_loggedScale) > 0.001f || (int)upscaleMethod != s_loggedMethod) {
		s_loggedScale = resolutionScale.x;
		s_loggedMethod = (int)upscaleMethod;
		logger::debug("[Upscaling] active method={} scale={:.3f} render={}x{} display={}x{}",
			(int)upscaleMethod, resolutionScale.x,
			(int)(screenSize.x * resolutionScale.x), (int)(screenSize.y * resolutionScale.y),
			(int)screenSize.x, (int)screenSize.y);
	}
}

void Upscaling::SetupResources()
{
	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC texDesc{};
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

	main.texture->GetDesc(&texDesc);
	main.SRV->GetDesc(&srvDesc);
	main.UAV->GetDesc(&uavDesc);

	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	D3D11_DEPTH_STENCIL_DESC depthStencilDesc = {};
	depthStencilDesc.DepthEnable = true;
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
	depthStencilDesc.StencilEnable = false;

	DX::ThrowIfFailed(globals::d3d::device->CreateDepthStencilState(&depthStencilDesc, upscaleDepthStencilState.put()));

	jitterCB = new ConstantBuffer(ConstantBufferDesc<JitterCB>());

	D3D11_BLEND_DESC blendDesc = {};
	blendDesc.AlphaToCoverageEnable = false;
	blendDesc.IndependentBlendEnable = false;
	blendDesc.RenderTarget[0].BlendEnable = false;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	DX::ThrowIfFailed(globals::d3d::device->CreateBlendState(&blendDesc, upscaleBlendState.put()));

	D3D11_RASTERIZER_DESC rasterizerDesc = {};
	rasterizerDesc.FillMode = D3D11_FILL_SOLID;
	rasterizerDesc.CullMode = D3D11_CULL_NONE;
	rasterizerDesc.FrontCounterClockwise = false;
	rasterizerDesc.DepthBias = 0;
	rasterizerDesc.DepthBiasClamp = 0.0f;
	rasterizerDesc.SlopeScaledDepthBias = 0.0f;
	rasterizerDesc.DepthClipEnable = false;
	rasterizerDesc.ScissorEnable = false;
	rasterizerDesc.MultisampleEnable = false;
	rasterizerDesc.AntialiasedLineEnable = false;
	DX::ThrowIfFailed(globals::d3d::device->CreateRasterizerState(&rasterizerDesc, upscaleRasterizerState.put()));

	CheckResources(GetUpscaleMethod());

	rcas.Initialize();

	if (globals::features::hdrDisplay.loaded) {
		globals::features::hdrDisplay.SetupResources();
	}

	// Bridge to DXVK's own Vulkan device for Streamline interposition.
	// On native D3D11 this is a no-op (IsAvailable() stays false).
	auto* dxvk = DxvkInterop::GetSingleton();
	if (dxvk->Initialize()) {
		VkImage probeImage = VK_NULL_HANDLE;
		VkImageCreateInfo probeInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		if (dxvk->GetVkImage(main.texture, &probeImage, nullptr, &probeInfo) && probeImage != VK_NULL_HANDLE) {
			logger::info("[Upscaling] DXVK texture->VkImage probe OK: main RT VkImage={:#x} ({}x{}, vkFormat={})",
				reinterpret_cast<uintptr_t>(probeImage), probeInfo.extent.width, probeInfo.extent.height, static_cast<int>(probeInfo.format));
		} else {
			logger::warn("[Upscaling] DXVK texture->VkImage probe FAILED");
		}

		dxvk->CreateCommandResources(3);

		// NVIDIA Streamline (DLSS/Reflex) on DXVK's Vulkan device.
		auto* streamline = Streamline::GetSingleton();
		if (streamline->Initialize()) {
			streamline->SetVulkanDevice();
			// Register the DXVK ownership predicate so DXVK knows when the interposer
			// owns presentation (frame-gen swapchain active).
			Streamline::RegisterDxvkOwnershipPredicate();
		}

		// Hardware-adaptive defaults (one-shot, first run only). Applied after feature
		// probing so we know what's available. User settings override on subsequent loads.
		ApplyHardwareDefaults();
	}
}

void Upscaling::ClearShaderCache()
{
	depthRefractionUpscalePS = nullptr;
	underwaterMaskUpscalePS = nullptr;
	upscaleVS = nullptr;
}

void UpdateCameraData()
{
	using func_t = decltype(&UpdateCameraData);
	static REL::Relocation<func_t> func{ RELOCATION_ID(75472, 77258) };
	func();
}

void Upscaling::PostDisplay()
{
	auto viewport = globals::game::graphicsState;

	viewport->projectionPosScaleX = projectionPosScaleX;
	viewport->projectionPosScaleY = projectionPosScaleY;

	auto& runtimeData = viewport->GetRuntimeData();

	runtimeData.dynamicResolutionPreviousWidthRatio = 1;
	runtimeData.dynamicResolutionPreviousHeightRatio = 1;
	runtimeData.dynamicResolutionWidthRatio = 1;
	runtimeData.dynamicResolutionHeightRatio = 1;
	runtimeData.dynamicResolutionLock = 1;

	globals::game::renderer->UpdateViewPort(0, 0, 1);
	UpdateCameraData();

	globals::state->UpdateSharedData(false, false);
}

/*
* Copyright (c) 2022-2023 NVIDIA CORPORATION. All rights reserved
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

double Upscaling::GetRefreshRate(HWND a_window)
{
	HMONITOR monitor = MonitorFromWindow(a_window, MONITOR_DEFAULTTONEAREST);
	MONITORINFOEXW info;
	info.cbSize = sizeof(info);
	if (GetMonitorInfoW(monitor, &info) != 0) {
		UINT32 requiredPaths, requiredModes;
		if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &requiredPaths, &requiredModes) == ERROR_SUCCESS) {
			std::vector<DISPLAYCONFIG_PATH_INFO> paths(requiredPaths);
			std::vector<DISPLAYCONFIG_MODE_INFO> modes2(requiredModes);
			if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &requiredPaths, paths.data(), &requiredModes, modes2.data(), nullptr) == ERROR_SUCCESS) {
				for (auto& p : paths) {
					DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName;
					sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
					sourceName.header.size = sizeof(sourceName);
					sourceName.header.adapterId = p.sourceInfo.adapterId;
					sourceName.header.id = p.sourceInfo.id;
					if (DisplayConfigGetDeviceInfo(&sourceName.header) == ERROR_SUCCESS && wcscmp(info.szDevice, sourceName.viewGdiDeviceName) == 0) {
						UINT numerator = p.targetInfo.refreshRate.Numerator;
						UINT denominator = p.targetInfo.refreshRate.Denominator;
						return (double)numerator / (double)denominator;
					}
				}
			}
		}
	}
	logger::error("Failed to retrieve refresh rate from swap chain");
	return 60;
}

bool Upscaling::IsUpscalingActive() const
{
	auto method = GetUpscaleMethod();

	if (method != UpscaleMethod::kFSR && method != UpscaleMethod::kXeSS && method != UpscaleMethod::kDLSS) {
		return false;
	}

	return resolutionScale.x < .99f;
}

bool Upscaling::IsWindowGapActive()
{
	// Minimized window: ALL Streamline/GPU-interop work must stop, not just presents — the
	// frame generator's in-flight flip cannot retire against an iconic window and its pacer
	// holds a driver lock while it times out; any SL evaluate in that window then blocks the
	// render thread on that lock and wedges the pipeline (stack-proven). The Streamline sample
	// survives minimize precisely because its whole loop stops; this is the equivalent gate.
	static HWND s_window = nullptr;
	if (!s_window && globals::d3d::swapChain) {
		DXGI_SWAP_CHAIN_DESC desc{};
		if (SUCCEEDED(globals::d3d::swapChain->GetDesc(&desc)))
			s_window = desc.OutputWindow;
	}
	return s_window && IsIconic(s_window);
}

void Upscaling::NotifyWindowFocus(bool a_focused)
{
	// WndProc thread. Atomic store only — the present hook reads it (same thread) to skip presenting
	// while the window is not focused, matching the Streamline sample's not-visible frame skip.
	s_windowUnfocused.store(!a_focused, std::memory_order_relaxed);
}

void Upscaling::NotifyWindowModifying(bool a_modifying)
{
	// WndProc thread (WM_ENTERSIZEMOVE/WM_EXITSIZEMOVE). Atomic store only.
	s_windowModifying.store(a_modifying, std::memory_order_relaxed);
}

bool Upscaling::IsWindowUnusable()
{
	// Minimized OR unfocused/occluded OR being resized/moved. Frame generation is suspended for
	// the whole duration (see the present hook + FrameGen::Controller::SuspendForWindowGap): DXVK
	// recreates the swapchain on occlusion and an FG-wrapped swapchain freezes the GPU.
	return IsWindowGapActive() ||
	       s_windowUnfocused.load(std::memory_order_relaxed) ||
	       s_windowModifying.load(std::memory_order_relaxed);
}

void Upscaling::Upscale()
{
	ZoneScoped;

	if (IsWindowGapActive())
		return;

	auto state = globals::state;
	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;

	context->OMSetRenderTargets(0, nullptr, nullptr);

	auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

	{
		globals::profiler->BeginPass("Upscaling::Upscale");
		state->BeginPerfEvent("Upscaling");
		TracyD3D11Zone(globals::state->tracyCtx, "Upscaling Dispatch");

		if (GetUpscaleMethod() == UpscaleMethod::kFSR) {
			// FSR3 upscale runs UNDER STREAMLINE via the sl.fsr plugin (slEvaluateFeature(kFeatureFSR)).
			// Upscale into the separate texture, then copy back.
			auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
			auto& depthTex = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
			const auto displaySize = float2{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
			const auto renderSize = Util::ConvertToDynamic(displaySize);
			if (upscaledTexture && upscaledTexture->resource) {
				Streamline::GetSingleton()->EvaluateFSR(
					main.texture, upscaledTexture->resource.get(), depthTex.texture, motionVector.texture,
					(uint32_t)renderSize.x, (uint32_t)renderSize.y,
					(uint32_t)displaySize.x, (uint32_t)displaySize.y,
					settings.qualityMode, settings.sharpnessFSR,
					jitter.x, jitter.y);
				globals::d3d::context->CopyResource(main.texture, upscaledTexture->resource.get());
			}
		} else if (GetUpscaleMethod() == UpscaleMethod::kDLSS) {
			auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
			auto& depthTex = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
			const auto displaySize = float2{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
			const auto renderSize = Util::ConvertToDynamic(displaySize);
			// DLSS cannot upscale in place: reading the low-res render sub-rect while writing the
			// full-res output of the SAME texture corrupts it. Upscale into the separate display-res
			// output texture, then copy it back to main.
			if (upscaledTexture && upscaledTexture->resource) {
				Streamline::GetSingleton()->EvaluateDLSS(
					main.texture, upscaledTexture->resource.get(), depthTex.texture, motionVector.texture,
					(uint32_t)renderSize.x, (uint32_t)renderSize.y,
					(uint32_t)displaySize.x, (uint32_t)displaySize.y,
					settings.qualityMode, settings.sharpnessFSR,
					jitter.x, jitter.y);
				globals::d3d::context->CopyResource(main.texture, upscaledTexture->resource.get());
			}
		} else if (GetUpscaleMethod() == UpscaleMethod::kXeSS) {
			auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
			auto& depthTex = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
			const auto displaySize = float2{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
			const auto renderSize = Util::ConvertToDynamic(displaySize);
			// XeSS runs UNDER STREAMLINE via the sl.xess plugin (slEvaluateFeature(kFeatureXeSS)).
			// Upscale into the separate texture, then copy back.
			if (upscaledTexture && upscaledTexture->resource) {
				Streamline::GetSingleton()->EvaluateXeSS(
					main.texture, upscaledTexture->resource.get(), depthTex.texture, motionVector.texture,
					(uint32_t)renderSize.x, (uint32_t)renderSize.y,
					(uint32_t)displaySize.x, (uint32_t)displaySize.y,
					settings.qualityMode, settings.sharpnessFSR,
					jitter.x, jitter.y);
				globals::d3d::context->CopyResource(main.texture, upscaledTexture->resource.get());
			}
		}

		state->EndPerfEvent();
		globals::profiler->EndPass();
	}
}

void Upscaling::PerformUpscaling()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Upscaling");
	Upscale();
	UpscaleDepth();

	auto& runtimeData = globals::game::graphicsState->GetRuntimeData();

	runtimeData.dynamicResolutionLock = 1;

	UpdateCameraData();
}

void Upscaling::UpscaleDepth()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Upscaling - Depth");

	if (!IsUpscalingActive()) {
		return;
	}

	auto state = globals::state;
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	auto deferred = globals::deferred;
	if (!state || !renderer || !context || !deferred || !deferred->linearSampler || !jitterCB || !upscaleRasterizerState || !upscaleBlendState || !upscaleDepthStencilState) {
		return;
	}

	float2 screenSize{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
	if (screenSize.x <= 0.0f || screenSize.y <= 0.0f) {
		return;
	}

	auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	auto& depthCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN_COPY];
	auto& refractionNormals = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kREFRACTION_NORMALS];
	auto& saoCameraZ = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kSAO_CAMERAZ];
	auto& underwaterMask = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kUNDERWATER_MASK];

	if (!depth.texture || !depth.views[0] || !depthCopy.texture || !depthCopy.depthSRV ||
		!refractionNormals.texture || !refractionNormals.textureCopy || !refractionNormals.SRVCopy || !refractionNormals.RTV || !saoCameraZ.RTV ||
		!underwaterMask.texture || !underwaterMask.textureCopy || !underwaterMask.SRVCopy || !underwaterMask.RTV) {
		return;
	}
	auto* fullscreenVS = GetUpscaleVS();
	auto* depthUpscalePS = GetDepthRefractionUpscalePS();
	auto* underwaterMaskPS = GetUnderwaterMaskUpscalePS();
	if (!fullscreenVS || !depthUpscalePS || !underwaterMaskPS) {
		return;
	}

	state->BeginPerfEvent("Render Target Upscaling");

	context->IASetInputLayout(nullptr);
	context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
	context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	context->VSSetShader(fullscreenVS, nullptr, 0);

	D3D11_VIEWPORT viewport = {};
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = screenSize.x;
	viewport.Height = screenSize.y;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	context->RSSetViewports(1, &viewport);

	context->RSSetState(upscaleRasterizerState.get());
	context->OMSetBlendState(upscaleBlendState.get(), nullptr, 0xffffffff);

	ID3D11SamplerState* samplers[] = { deferred->linearSampler };
	context->PSSetSamplers(0, ARRAYSIZE(samplers), samplers);

	JitterCB jitterData;
	jitterData.jitter = jitter;
	{
		constexpr float kEnterWideKernelRatio = 1.55f;
		constexpr float kExitWideKernelRatio = 1.45f;
		const float minScale = std::max(std::min(resolutionScale.x, resolutionScale.y), FLT_EPSILON);
		const float upscaleRatio = 1.0f / minScale;

		if (depthUpscaleUseWideKernel) {
			if (upscaleRatio < kExitWideKernelRatio) {
				depthUpscaleUseWideKernel = false;
			}
		} else {
			if (upscaleRatio > kEnterWideKernelRatio) {
				depthUpscaleUseWideKernel = true;
			}
		}

		jitterData.useWideKernel = depthUpscaleUseWideKernel ? 1.0f : 0.0f;
		jitterData.pad0 = 0.0f;
	}

	jitterCB->Update(jitterData);
	auto bufferArray = jitterCB->CB();
	context->PSSetConstantBuffers(0, 1, &bufferArray);

	const auto copyIfNonAliased = [&](ID3D11Resource* dst, ID3D11Resource* src) {
		if (dst && src && dst != src) {
			context->CopyResource(dst, src);
		}
	};

	{
		TracyD3D11Zone(globals::state->tracyCtx, "Upscaling - Depth Upscale");

		copyIfNonAliased(depthCopy.texture, depth.texture);

		context->OMSetDepthStencilState(upscaleDepthStencilState.get(), 0x00);

		copyIfNonAliased(refractionNormals.textureCopy, refractionNormals.texture);

		ID3D11ShaderResourceView* srvs[] = { refractionNormals.SRVCopy, depthCopy.depthSRV, depthCopy.stencilSRV };
		context->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11RenderTargetView* rtvs[] = { refractionNormals.RTV, saoCameraZ.RTV };
		context->OMSetRenderTargets(2, rtvs, depth.views[0]);

		context->PSSetShader(depthUpscalePS, nullptr, 0);
		globals::profiler->BeginPass("Upscaling::DepthUpscale");
		context->Draw(3, 0);
		globals::profiler->EndPass();
	}

	{
		TracyD3D11Zone(globals::state->tracyCtx, "Upscaling - Underwater Mask");

		viewport.Width = screenSize.x * 0.5f;
		viewport.Height = screenSize.y * 0.5f;
		context->RSSetViewports(1, &viewport);

		copyIfNonAliased(underwaterMask.textureCopy, underwaterMask.texture);

		context->OMSetDepthStencilState(nullptr, 0x00);

		ID3D11ShaderResourceView* srvs[] = { underwaterMask.SRVCopy, depthCopy.depthSRV };
		context->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11RenderTargetView* rtvs[] = { underwaterMask.RTV };
		context->OMSetRenderTargets(ARRAYSIZE(rtvs), rtvs, nullptr);

		context->PSSetShader(underwaterMaskPS, nullptr, 0);
		globals::profiler->BeginPass("Upscaling::UnderwaterMaskUpscale");
		context->Draw(3, 0);
		globals::profiler->EndPass();
	}

	ID3D11ShaderResourceView* nullPSResources[3] = { nullptr, nullptr, nullptr };
	context->PSSetShaderResources(0, ARRAYSIZE(nullPSResources), nullPSResources);

	state->EndPerfEvent();
}

void Upscaling::Main_UpdateJitter::thunk(RE::BSGraphics::State* a_state)
{
	// Establish this render frame's explicit SL frame ID before any SL call of the frame
	// (constants, tags, evaluates, markers all fetch their token with it).
	Streamline::GetSingleton()->BeginRenderFrame();
	globals::features::upscaling.ConfigureTAA();
	func(a_state);
	globals::features::upscaling.ConfigureUpscaling(a_state);
}

void Upscaling::MenuManagerDrawInterfaceStartHook::thunk(int64_t a1)
{
	auto& upscaling = globals::features::upscaling;

	// Capture the hudless scene for frame generation HERE — this hook fires just before the UI/HUD is drawn
	// (func(a1) below), so the back buffer holds the fully-composited scene WITHOUT UI, in the back-buffer
	// format. The game's kFRAMEBUFFER `.texture` is NULL under DXVK, but its SRV references the real back
	// buffer — read the resource from the SRV. Both DLSS-G and FSR FG consume this as their HUDLessColor.
	if (upscaling.IsFrameGenerationActive()) {
		if (!upscaling.hudlessTexture || !upscaling.hudlessTexture->resource)
			upscaling.CreateHudlessTexture();
		auto& fb = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kFRAMEBUFFER];
		if (fb.SRV && upscaling.hudlessTexture && upscaling.hudlessTexture->resource) {
			winrt::com_ptr<ID3D11Resource> fbResource;
			fb.SRV->GetResource(fbResource.put());
			if (fbResource)
				globals::d3d::context->CopyResource(upscaling.hudlessTexture->resource.get(), fbResource.get());
		}
	}

	upscaling.PostDisplay();

	auto& hdr = globals::features::hdrDisplay;
	if (hdr.loaded)
		hdr.SetUIBuffer();

	func(a1);
}

void Upscaling::Main_PostProcessing::thunk(RE::ImageSpaceManager* a_this, uint32_t a3, RE::RENDER_TARGET a_target, void* a_4, bool a_5)
{
	auto& upscaling = globals::features::upscaling;
	auto upscaleMethod = upscaling.GetUpscaleMethod();

	// Reflex/PCL: the game simulation has finished and post-process/upscale render submission
	// happens here — mark the simulation-end / render-submit-start boundary.
	// Main_PostProcessing runs MORE THAN ONCE per rendered frame (same reason the constants/evaluate
	// pair below is guarded once-per-frame). Fire these two markers only on the FIRST invocation each
	// frame: otherwise SimulationEnd/RenderSubmitStart land ~2×/frame while their partners
	// (RenderSubmitEnd/PresentStart/PresentEnd) fire 1×/frame on the present thread, doubling and
	// desyncing the Sim→Submit→Present timeline DLSS-G paces the *interpolated* frame from. That
	// systematically biases the generated frame's camera-reprojection phase — the whole static world
	// gets flagged dynamic and reprojected wrong, worsening with camera rotation speed (real frames,
	// rendered by the engine, are unaffected). Matches the once-per-frame SimulationStart rule.
	// Match the Streamline sample (DeviceManager::AnimateRenderPresent): when the window is not
	// visible/focused it runs NONE of the per-frame Streamline work — the Reflex/PCL markers, the
	// upscale evaluate, the frame-gen tag AND Present() all live inside one `m_windowVisible &&
	// (m_windowIsInFocus || ShouldRenderUnfocused())` gate. Running only PART of that sequence (the
	// old behaviour: evaluate + tag + markers, but skip Present) leaves SL's per-frame contract
	// incoherent — the "Out of order frame" / "Unable to find 'common' constants" desync — and worse,
	// EvaluateDLSS's forced full CS-thread sync (FlushRenderingCommands -> SynchronizeCsThread)
	// DEADLOCKS behind the DLSS-G present stalled in the driver whenever the surface can't present
	// (alt-tab occlusion, or loading while alt-tabbed away). So gate the entire block below on a
	// usable window; the present hook already skips Present() for the same condition, so together CS
	// skips the whole frame's SL work as a unit, exactly like the sample.
	const bool windowUsable = !Upscaling::IsWindowUnusable();

	auto* streamline = Streamline::GetSingleton();
	if (windowUsable && upscaling.GetEffectiveReflex()) {
		static uint32_t s_lastSimEndFrame = UINT32_MAX;
		const uint32_t gameFrame = globals::state->frameCount;
		if (s_lastSimEndFrame != gameFrame) {
			s_lastSimEndFrame = gameFrame;
			streamline->SetPCLMarker(Streamline::PclMarker::SimulationEnd);
			streamline->SetPCLMarker(Streamline::PclMarker::RenderSubmitStart);
		}
	}

	if (windowUsable && (upscaleMethod == UpscaleMethod::kFSR || upscaleMethod == UpscaleMethod::kXeSS || upscaleMethod == UpscaleMethod::kDLSS))
		upscaling.PerformUpscaling();

	// (HUDLessColor is captured later, in MenuManagerDrawInterfaceStartHook just before the UI draws, from
	// kFRAMEBUFFER — the fully-composited pre-UI swapchain in back-buffer format.)

	// Frame-gen resource tagging is independent of which upscaler ran (an upscaler + frame
	// generation can be active together), so this is its own `if`, not an `else if`.
	if (windowUsable && upscaling.IsFrameGenerationActive()) {
		auto fgMethod = upscaling.GetFrameGenMethod();
		auto renderer = globals::game::renderer;
		auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

		auto* ui = globals::game::ui;
		const bool gameplay = ui && !ui->GameIsPaused() && !globals::state->IsMainOrLoadingMenuOpen(ui);
		ID3D11Resource* hudless = (upscaling.hudlessTexture && upscaling.hudlessTexture->resource)
		                              ? upscaling.hudlessTexture->resource.get()
		                              : nullptr;

		if (fgMethod == FrameGenMethod::kDLSSG) {
			// Turn DLSS-G interpolation on. The controller defers the first
			// slDLSSGSetOptions(on) until the load reconcile settled on the final
			// swapchain (single engagement, no toggle bounce), and SetDLSSGMode
			// caches so steady-state frames are no-ops. slDLSSGSetOptions is NOT
			// thread-safe vs Streamline's present hook (guide section 6.0), which
			// is why the mode is never flipped per-frame.
			FrameGen::Controller::GetSingleton()->EngageDLSSG();

			if (gameplay) {
				const auto dispSize = float2{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
				// renderSize MUST be the actual DRS render size where depth/MV are valid — ignoreLock=true, else
				// ConvertToDynamic returns full res and SL reads stale MV in the region outside the render sub-rect.
				const auto rendSize = Util::ConvertToDynamic(dispSize, /*ignoreLock=*/true);

				// FG depth = the pre-upscale render-res kMAIN. With an active upscaler UpscaleDepth() rewrote kMAIN
				// to display res, but first copied the render-res depth into kMAIN_COPY — tag that. TAA (no upscaler):
				// kMAIN is still render-res. DLSS-G tags this eOnlyValidNow, so SL snapshots it at tag time (instead
				// of a CS-side CopyResource) and the later in-place depth upscale / next frame can't disturb it.
				auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
				auto& depthCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN_COPY];
				ID3D11Resource* fgDepth = (upscaling.IsUpscalingActive() && depthCopy.texture)
				                              ? depthCopy.texture
				                              : depth.texture;

				// Under DXVK full-interposition, sl.dlss_g's present hook only PROCESSES (generates) a frame
				// when a per-frame slEvaluateFeature(kFeatureFSR/DLSS) ran that frame — proven by exhaustive
				// visual testing (the NVIDIA DLSSG dev overlay appears for FSR/DLSS, never for bare TAA). The
				// sample needs no evaluate only because it uses eUseManualHooking (native synchronous present),
				// which is mutually exclusive with the interposition the whole CS integration relies on. So on
				// the no-upscaler (TAA) path drive a 1.0x FSR evaluate as the DLSS-G keep-alive: it sets the
				// viewport-0 constants AND provides the per-frame SL-tracked evaluate sl.dlss_g requires. Output
				// goes to the throwaway upscaledTexture (never copied back) so TAA's image is untouched.
				if (!upscaling.IsUpscalingActive()) {
					upscaling.CreateUpscaledTexture();  // idempotent throwaway target
					auto& mainColor = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
					if (upscaling.upscaledTexture && upscaling.upscaledTexture->resource && mainColor.texture)
						Streamline::GetSingleton()->EvaluateFSR(
							mainColor.texture, upscaling.upscaledTexture->resource.get(), fgDepth, motionVector.texture,
							(uint32_t)dispSize.x, (uint32_t)dispSize.y, (uint32_t)dispSize.x, (uint32_t)dispSize.y,
							0 /*native 1.0x*/, 0.0f, upscaling.jitter.x, upscaling.jitter.y);
				}

				// Copy + tag once per rendered frame: Main_PostProcessing runs more than once per frame
				// (see the marker gate above). Re-tagging is redundant (same token, same content — SL keeps
				// the last), and each extra call would advance the MV ring, halving its reuse distance.
				static uint32_t s_lastTagFrame = UINT32_MAX;
				const uint32_t tagFrame = globals::state->frameCount;
				if (s_lastTagFrame != tagFrame) {
					s_lastTagFrame = tagFrame;
					// Tag the LIVE kMOTION_VECTOR, sample-style. NOTE: the present-hook bound that
					// guaranteed the GPU executed this frame's MV writes + tag before the present (which had
					// validated the former 3-deep MV ring copy redundant) was REMOVED; the MV ring stays
					// removed, so live-MV tagging no longer has that ordering backing it.
					Streamline::GetSingleton()->TagDLSSGResources(
						fgDepth, motionVector.texture, hudless,
						(uint32_t)rendSize.x, (uint32_t)rendSize.y,
						(uint32_t)dispSize.x, (uint32_t)dispSize.y);
				}
				Streamline::GetSingleton()->LogReflexStatus();
			}
		} else if (fgMethod == FrameGenMethod::kFSR && gameplay) {
			// Drive the FSR FG-prepare every gameplay frame from depth + motion vectors, independent of the
			// active upscaler (the sl.fsr plugin runs FrameGenerationPrepare on a color-less FSR evaluate).
			const auto dispSize = float2{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
			// renderSize MUST be the actual DRS render size where depth/MV are valid (see the DLSS-G branch
			// above) — ignoreLock=true, else ConvertToDynamic returns full res and FFX reads stale MV in the
			// 3/4 of the texture outside the render sub-rect (the "motion vectors don't fill the screen" bug).
			const auto rendSize = Util::ConvertToDynamic(dispSize, /*ignoreLock=*/true);
			// FG depth = pre-upscale render-res kMAIN (see the DLSS-G branch): kMAIN_COPY holds the render-res
			// depth UpscaleDepth() saved before rewriting kMAIN to display res. TAA (no upscaler): use kMAIN. The
			// sl.fsr FG-prepare consumes depth at evaluate (mid-frame), so kMAIN_COPY is still the correct depth.
			auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
			auto& depthCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN_COPY];
			ID3D11Resource* fgDepth = (upscaling.IsUpscalingActive() && depthCopy.texture)
			                              ? depthCopy.texture
			                              : depth.texture;
			Streamline::GetSingleton()->EvaluateFSRFrameGen(
				fgDepth, motionVector.texture, hudless,
				(uint32_t)rendSize.x, (uint32_t)rendSize.y,
				(uint32_t)dispSize.x, (uint32_t)dispSize.y,
				upscaling.jitter.x, upscaling.jitter.y);
			Streamline::GetSingleton()->LogFSRFrameGenStats();
		}
	}

	Util::SetTemporal(upscaleMethod == UpscaleMethod::kTAA);

	bool hdrLoaded = globals::features::hdrDisplay.loaded;
	if (hdrLoaded)
		globals::features::hdrDisplay.RedirectFramebuffer();

	func(a_this, a3, a_target, a_4, a_5);

	if (hdrLoaded)
		globals::features::hdrDisplay.RestoreFramebuffer();

	Util::SetTemporal(false);
}

void Upscaling::SetScissorRect::thunk(RE::BSGraphics::Renderer* This, int a_left, int a_top, int a_right, int a_bottom)
{
	auto viewport = globals::game::graphicsState;
	auto& runtimeData = viewport->GetRuntimeData();

	if (!runtimeData.dynamicResolutionLock) {
		a_left = static_cast<int>(a_left * runtimeData.dynamicResolutionWidthRatio);
		a_right = static_cast<int>(a_right * runtimeData.dynamicResolutionWidthRatio);

		a_top = static_cast<int>(a_top * runtimeData.dynamicResolutionHeightRatio);
		a_bottom = static_cast<int>(a_bottom * runtimeData.dynamicResolutionHeightRatio);
	}

	func(This, a_left, a_top, a_right, a_bottom);
}

void Upscaling::Main_RenderPrecipitation::thunk()
{
	auto& runtimeData = globals::game::graphicsState->GetRuntimeData();
	runtimeData.dynamicResolutionLock = 1;
	func();
	runtimeData.dynamicResolutionLock = 0;
}

void Upscaling::BSFaceGenManager_UpdatePendingCustomizationTextures::thunk()
{
	auto& runtimeData = globals::game::graphicsState->GetRuntimeData();
	runtimeData.dynamicResolutionLock = 1;
	func();
	runtimeData.dynamicResolutionLock = 0;
}
