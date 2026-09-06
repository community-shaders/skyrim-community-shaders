#include "Upscaling.h"

#include "../I18n/I18n.h"
#include "Deferred.h"
#include "DxvkLoader.h"
#include "Features/Effects11/D3D11StateBackup.h"
#include "HDRDisplay.h"
#include "Hooks.h"
#include "State.h"
#include "Upscaling/DXVKInterop.h"
#include "Upscaling/FrameGenController.h"
#include "Upscaling/Streamline.h"
#include "Utils/Game.h"
#include "Utils/UI.h"
#include "Utils/VersionedRelocation.h"
#include <Windows.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>
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
	globals::state->SetAdapterDescription(adapterDesc.Description);

	auto& upscaling = globals::features::upscaling;

	// FLIP_DISCARD requires BufferCount >= 2 and a flip-model-compatible (non-sRGB) format.
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

static bool DrawStepper(const char* a_label, int* a_index, const std::vector<const char*>& a_options, bool a_disabled = false)
{
	const int count = static_cast<int>(a_options.size());
	if (count == 0)
		return false;
	*a_index = std::clamp(*a_index, 0, count - 1);

	ImGui::BeginDisabled(a_disabled);
	const bool changed = ImGui::SliderInt(a_label, a_index, 0, count - 1, a_options[*a_index], ImGuiSliderFlags_NoInput);
	*a_index = std::clamp(*a_index, 0, count - 1);
	ImGui::EndDisabled();
	return changed;
}

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
	const bool fsrfgAvailable = streamline->IsFSRFGSupported();
	const bool reflexAvailable = streamline->IsReflexSupported();

	auto selectUpscaler = [&](UpscaleMethod a_m) {
		settings.upscaleMethod = (uint)a_m;
		if (a_m != UpscaleMethod::kDLSS)
			settings.upscaleMethodNoDLSS = (uint)a_m;
	};

	ImGui::SeparatorText(T(TKEY("display_header"), "Display"));
	{
		const bool vsyncForcedOff = IsFrameGenerationActive() && GetFrameGenMethod() == FrameGenMethod::kDLSSG;
		if (vsyncForcedOff) {
			bool effectiveVsync = false;
			DrawToggleStepper(T(TKEY("vsync"), "Vertical Synchronisation"), &effectiveVsync, /*disabled=*/true);
			ImGui::SameLine();
			ImGui::TextDisabled("%s", T(TKEY("vsync_dlssg"), "(forced off by DLSS-G)"));
		} else {
			DrawToggleStepper(T(TKEY("vsync"), "Vertical Synchronisation"), &settings.vsync);
		}

		const int refresh = GetMonitorRefreshRate();
		std::vector<int> divisorOptions;
		for (int d = 4; d >= 1; --d) {
			if (refresh / d >= 30)
				divisorOptions.push_back(d);
		}
		if (divisorOptions.empty())
			divisorOptions.push_back(1);
		divisorOptions.push_back(0);

		std::vector<std::string> fpsStrings;
		for (int d : divisorOptions)
			fpsStrings.push_back(d == 0 ?
			                         std::string(T(TKEY("frame_rate_unlocked"), "Unlocked (variable)")) :
			                         std::format("{} FPS", refresh / d));
		std::vector<const char*> fpsLabels;
		for (auto& s : fpsStrings)
			fpsLabels.push_back(s.c_str());

		const int maxSel = static_cast<int>(divisorOptions.size()) - 1;
		int sel = maxSel > 0 ? maxSel - 1 : 0;
		for (int i = 0; i <= maxSel; ++i) {
			if (divisorOptions[i] == settings.frameRateLimitDivisor)
				sel = i;
		}
		DrawStepper(T(TKEY("frame_rate"), "Frame Rate"), &sel, fpsLabels);
		settings.frameRateLimitDivisor = divisorOptions[std::clamp(sel, 0, maxSel)];
	}

	ImGui::SeparatorText(T(TKEY("upscaling_header"), "Upscaling"));
	{
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

		if (streamline->IsUnavailable())
			ImGui::TextDisabled("%s", T(TKEY("sl_unavailable_note"), "Streamline could not be loaded - upscalers and frame generation are unavailable"));

		const UpscaleMethod cur = (UpscaleMethod)settings.upscaleMethod;
		const bool hasPreset = (cur == UpscaleMethod::kFSR || cur == UpscaleMethod::kXeSS || cur == UpscaleMethod::kDLSS);
		if (hasPreset) {
			const std::vector<const char*> presets = {
				T(TKEY("preset_native"), "Native"),
				T(TKEY("preset_quality"), "Quality"),
				T(TKEY("preset_balanced"), "Balanced"),
				T(TKEY("preset_performance"), "Performance"),
				T(TKEY("preset_ultra_performance"), "Ultra Performance"),
			};
			int pIdx = std::clamp((int)settings.qualityMode, 0, 4);
			if (DrawStepper(T(TKEY("upscale_preset"), "Upscale Preset"), &pIdx, presets))
				settings.qualityMode = (uint)std::clamp(pIdx, 0, 4);

			if (cur == UpscaleMethod::kFSR)
				ImGui::SliderFloat(T(TKEY("sharpness"), "Sharpness"), &settings.sharpnessFSR, 0.0f, 1.0f, "%.1f");
		}
	}

	ImGui::SeparatorText(T(TKEY("fg_header"), "Frame Generation"));
	{
		std::vector<const char*>    fgLabels = { T(TKEY("fg_method_none"), "None") };
		std::vector<FrameGenMethod> fgMethods = { FrameGenMethod::kFSR };
		if (fsrfgAvailable) {
			fgLabels.push_back(T(TKEY("fg_method_fsr"), "FSR FG"));
			fgMethods.push_back(FrameGenMethod::kFSR);
		}
		if (dlssgAvailable) {
			fgLabels.push_back(T(TKEY("fg_method_dlssg"), "DLSS FG"));
			fgMethods.push_back(FrameGenMethod::kDLSSG);
		}

		int fgSel = 0;
		if (settings.frameGeneration) {
			const FrameGenMethod active = GetFrameGenMethod();
			for (int i = 1; i < static_cast<int>(fgMethods.size()); ++i)
				if (fgMethods[i] == active)
					fgSel = i;
		}
		if (DrawStepper(T(TKEY("fg_method"), "Frame Generation Method"), &fgSel, fgLabels)) {
			settings.frameGeneration = (fgSel != 0);
			if (fgSel != 0)
				settings.frameGenMethod = (uint)fgMethods[std::clamp(fgSel, 0, static_cast<int>(fgMethods.size()) - 1)];
		}

		// DLSS FG paces presentation to the display refresh, so the RENDERED rate -- and with it the
		// input latency -- is pinned to refresh / multiplier however much GPU headroom there is:
		// measured 30 fps rendered against 120 with frame generation off on a 60 Hz mode. Below a
		// refresh rate the GPU cannot already saturate, switching this on costs latency and buys no
		// extra displayed frames. That was only ever visible in the log, which is no use to someone
		// standing in this menu deciding whether to enable it, so it is said here too.
		if (settings.frameGeneration && GetFrameGenMethod() == FrameGenMethod::kDLSSG && dlssgAvailable) {
			const int  fgRefresh = GetMonitorRefreshRate();
			const int  fgBest = GetHighestRefreshRate();
			const uint fgMult = std::max<uint>(streamline->GetFrameGenerationMultiplier(), 2u);
			const int  fgRendered = fgRefresh / static_cast<int>(fgMult);
			const int  fgBestRendered = fgBest / static_cast<int>(fgMult);
			if (fgRefresh > 0) {
				ImGui::TextColored(ImVec4(1.0f, 0.76f, 0.25f, 1.0f), "%s",
					std::vformat(T(TKEY("fg_refresh_bound"),
					                 "Presentation is paced to the display, so the game renders at {} Hz / {}x = {} FPS "
					                 "and input latency is that of {} FPS."),
						std::make_format_args(fgRefresh, fgMult, fgRendered, fgRendered))
						.c_str());
				if (fgBest > fgRefresh)
					ImGui::TextDisabled("%s",
						std::vformat(T(TKEY("fg_refresh_available"),
						                 "This display supports {} Hz at this resolution, which would give {} FPS rendered."),
							std::make_format_args(fgBest, fgBestRendered))
							.c_str());
			}
		}

		if (settings.frameGeneration && GetFrameGenMethod() == FrameGenMethod::kDLSSG && dlssgAvailable) {
			const uint32_t maxFrames = streamline->GetDLSSGMaxFramesToGenerate();
			const uint     maxMultiplier = std::clamp<uint>(maxFrames > 0u ? maxFrames + 1u : 2u, 2u, 6u);
			std::vector<std::string> multStrings = {
				streamline->IsDLSSGDynamicSupported()
					? std::string(T(TKEY("fg_dynamic"), "Dynamic"))
					: std::string(T(TKEY("fg_auto"), "Auto")) };
			for (uint m = 2; m <= maxMultiplier; ++m)
				multStrings.push_back(std::format("{}x", m));
			std::vector<const char*> multStates;
			for (auto& s : multStrings)
				multStates.push_back(s.c_str());

			int multIdx = settings.dlssgDynamic ? 0 :
			              std::clamp((int)settings.frameGenMultiplier - 1, 1, (int)maxMultiplier - 1);
			if (DrawStepper(T(TKEY("fg_multiplier"), "Frame Generation Multiplier"), &multIdx, multStates)) {
				settings.dlssgDynamic = (multIdx == 0);
				if (multIdx >= 1)
					settings.frameGenMultiplier = (uint)(multIdx + 1);
			}
		}
	}

	if (reflexAvailable) {
		ImGui::SeparatorText(T(TKEY("ll_header"), "Low Latency"));
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

	constexpr auto enumCount = 5;
	if (settings.upscaleMethod >= static_cast<uint>(enumCount)) {
		logger::warn("[Upscaling] Loaded upscaleMethod {} out of range, clamping to {}", settings.upscaleMethod, enumCount - 1);
		settings.upscaleMethod = enumCount - 1;
	}
	if (settings.upscaleMethodNoDLSS >= static_cast<uint>(enumCount) ||
		settings.upscaleMethodNoDLSS == static_cast<uint>(UpscaleMethod::kDLSS))
		settings.upscaleMethodNoDLSS = static_cast<uint>(UpscaleMethod::kFSR);

	constexpr auto fgMethodCount = 2;
	if (settings.frameGenMethod >= static_cast<uint>(fgMethodCount))
		settings.frameGenMethod = static_cast<uint>(FrameGenMethod::kDLSSG);

	if (settings.reflexLowLatencyMode && !settings.reflexEnabled) {
		settings.reflexEnabled = true;
		settings.reflexBoost = settings.reflexLowLatencyBoost;
		settings.reflexLowLatencyMode = false;
		settings.reflexLowLatencyBoost = false;
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
	// Frame-generation proxies require synchronous present from the first frame.
	if (DxvkLoader::IsLoaded())
		Streamline::PushDxvkSyncPresent(settings.frameGeneration);

	if (DxvkLoader::IsLoaded()) {
		// Tear-free (MAILBOX) for the whole session, set before DXVK creates the swapchain.
		//
		// DLSS-G's flip metering cannot space its generated frame across tearing presents: with
		// IMMEDIATE the real and generated frames land back to back -- 49.6% of present intervals at
		// 0.0 ms and the rest near 48.5 ms instead of an even 24.2 ms -- which cost 5.1% of presents
		// to drops and left frame-time deviation at 16.5 ms. MAILBOX brings that to 2.3 ms and puts
		// output back on target.
		//
		// Applied unconditionally rather than only for DLSS-G. MAILBOX caps at the refresh rate, so
		// a target equal to the refresh gives up ~3%, but switching modes per frame-generation
		// method is worse: MAILBOX is outside IMMEDIATE's compatible-mode group on NVIDIA, so every
		// switch forces a full swapchain recreate, and doing that on each method change crashed the
		// game. One mode for the session avoids the recreate entirely.
		Streamline::PushDxvkTearingPreference(0u);
	}

	if (DxvkLoader::IsLoaded()) {
		// Always preload, before DXVK creates VkInstance so its Vulkan loader aliases the interposer.
		// This deliberately does not consult saved settings. Interposition can only be established
		// here, so gating it on whichever upscaler/frame-generation values happened to be saved is
		// what forced a restart to turn either on. With the interposer always installed, every
		// upscaler and frame generation can be switched in-game. Nothing is spent until a feature is
		// actually evaluated -- DXVK still runs on the real driver in the meantime.
		Streamline::GetSingleton()->PreloadInterposer();
	}

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

		// Force FXAA off after the game initializes its image-space state.
		auto fxaaEnabled = reinterpret_cast<bool*>(REL::RelocationID(513281, 391028).address());
		*fxaaEnabled = false;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

void Upscaling::PostPostLoad()
{
	bool isGOG = !GetModuleHandle(L"steam_api64.dll");
	stl::detour_thunk<MenuManagerDrawInterfaceStartHook>(REL::RelocationID(79947, 82084));

	// Calculates render resolution and temporal jitter.
	stl::write_thunk_call<Main_UpdateJitter>(REL::RelocationID(75460, 77245).address() + Util::VersionedRelocation::Select(0xE5, isGOG ? 0x133 : 0xE2, 0x133));

	// Disables the original dynamic-resolution system.
	REL::safe_write(REL::RelocationID(35556, 36555).address() + REL::Relocate(0x2D, 0x2D), REL::NOP5, sizeof(REL::NOP5));

	// Performs upscaling between volumetric lighting and post-processing.
	stl::write_thunk_call<Main_PostProcessing>(REL::RelocationID(100430, 107148).address() + REL::Relocate(0x1F0, 0x1E7));

	// Scales scissor rectangles with the dynamic render resolution.
	stl::detour_thunk<SetScissorRect>(REL::RelocationID(75564, 77365));

	// Prevents dynamic resolution from affecting face-generation textures.
	stl::detour_thunk<BSFaceGenManager_UpdatePendingCustomizationTextures>(REL::RelocationID(26455, 27041));

	// Prevents dynamic resolution from affecting the precipitation camera.
	stl::write_thunk_call<Main_RenderPrecipitation>(REL::RelocationID(35560, 36559).address() + Util::VersionedRelocation::Select(0x3A1, 0x3A1, 0x3BF));

	// Forces FXAA off.
	stl::detour_thunk<BSImageSpace_Init_FXAA>(REL::RelocationID(98974, 105626));

	logger::info("[Upscaling] Installed hooks");
}

#undef I18N_KEY_PREFIX

Upscaling::UpscaleMethod Upscaling::GetUpscaleMethod() const
{
	auto* streamline = Streamline::GetSingleton();
	auto* dxvk = DXVKInterop::GetSingleton();
	if (streamline->HasDispatchFaulted() ||
		(dxvk->IsAvailable() && !dxvk->CommandResourcesReady()))
		return UpscaleMethod::kTAA;

	auto method = static_cast<UpscaleMethod>(settings.upscaleMethod);
	for (uint32_t fallback = 0; fallback < 3; ++fallback) {
		if (method == UpscaleMethod::kDLSS &&
			(!streamline->IsDLSSSupported() || IsUpscaleMethodFailed(method))) {
			method = static_cast<UpscaleMethod>(settings.upscaleMethodNoDLSS);
			if (method == UpscaleMethod::kDLSS)
				method = UpscaleMethod::kFSR;
			continue;
		}
		if (method == UpscaleMethod::kXeSS &&
			(!streamline->IsXeSSSupported() || IsUpscaleMethodFailed(method))) {
			method = UpscaleMethod::kFSR;
			continue;
		}
		if (method == UpscaleMethod::kFSR &&
			(!streamline->IsFSRSupported() || IsUpscaleMethodFailed(method)))
			return UpscaleMethod::kTAA;
		return method;
	}
	return UpscaleMethod::kTAA;
}

bool Upscaling::IsUpscaleMethodFailed(UpscaleMethod a_method) const
{
	const auto index = static_cast<size_t>(a_method);
	return index < failedUpscaleMethods.size() && failedUpscaleMethods[index];
}

void Upscaling::MarkUpscaleMethodFailed(UpscaleMethod a_method)
{
	const auto index = static_cast<size_t>(a_method);
	if (index >= failedUpscaleMethods.size() || failedUpscaleMethods[index])
		return;

	failedUpscaleMethods[index] = true;
	const char* name = a_method == UpscaleMethod::kDLSS ? "DLSS" :
	                   a_method == UpscaleMethod::kXeSS ? "XeSS" : "FSR";
	logger::error("[Upscaling] {} evaluation failed; disabling that backend for this session", name);
}

void Upscaling::ApplyHardwareDefaults()
{
	static bool applied = false;
	if (applied)
		return;
	applied = true;

	if (settings.hardwareDefaultsApplied)
		return;
	settings.hardwareDefaultsApplied = true;

	auto* sl = Streamline::GetSingleton();

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

	if (!settings.reflexEnabled) {
		if (sl->IsReflexSupported()) {
			settings.reflexEnabled = true;
			logger::info("[Upscaling] Hardware default: Reflex low-latency enabled");
		}
	}
}

Upscaling::FrameGenMethod Upscaling::GetFrameGenMethod() const
{
	auto* sl = Streamline::GetSingleton();
	const auto selected = static_cast<FrameGenMethod>(settings.frameGenMethod);
	if (selected == FrameGenMethod::kDLSSG)
		return sl->IsDLSSGSupported() ? FrameGenMethod::kDLSSG : FrameGenMethod::kFSR;
	return sl->IsFSRFGSupported() ? FrameGenMethod::kFSR :
	       sl->IsDLSSGSupported() ? FrameGenMethod::kDLSSG :
	                                FrameGenMethod::kFSR;
}

bool Upscaling::IsFrameGenerationActive() const
{
	if (!loaded || !settings.frameGeneration)
		return false;
	if (Streamline::GetSingleton()->HasDispatchFaulted() ||
		DXVKInterop::GetSingleton()->HasCommandRingFault())
		return false;
	const auto& hdr = globals::features::hdrDisplay;
	const bool hdrActive = hdr.loaded && hdr.IsHDREnabledForFrame();
	if (!DXVKInterop::GetSingleton()->IsPresenterStateReadyForFrame(hdrActive))
		return false;
	auto fgMethod = GetFrameGenMethod();
	if (fgMethod == FrameGenMethod::kDLSSG)
		return Streamline::GetSingleton()->IsDLSSGSupported();
	return Streamline::GetSingleton()->IsFSRFGSupported();
}

void Upscaling::BeginRenderFrame()
{
	auto* dxvk = DXVKInterop::GetSingleton();
	// Report an unrecoverable ring once, not once per frame. This runs every frame, so a fault that
	// cannot be recovered -- a lost device above all -- used to re-enter recovery and log on every
	// single one: 81k "recovery failed" lines against 243k vkDeviceWaitIdle(-4) in one session.
	// frameGeneration is deliberately not cleared here either; it is persisted user configuration,
	// and IsFrameGenerationActive() already returns false while the ring is faulted.
	static bool ringRecoveryReported = false;
	if (dxvk->HasCommandRingFault()) {
		if (dxvk->IsDeviceLost()) {
			if (!std::exchange(ringRecoveryReported, true))
				logger::error("[Upscaling] device lost; frame generation is unavailable for this session");
		} else if (!dxvk->RecoverCommandRing()) {
			if (!std::exchange(ringRecoveryReported, true))
				logger::error("[Upscaling] Vulkan command-ring recovery failed; falling back to TAA");
		} else {
			ringRecoveryReported = false;
		}
	}

	auto& hdr = globals::features::hdrDisplay;
	if (hdr.loaded)
		hdr.BeginRenderFrame();
	else
		DXVKInterop::GetSingleton()->CommitPresenterSurfaceStateForRenderFrame();
}

bool Upscaling::GetEffectiveReflex() const
{
	if (IsFrameGenerationActive()) {
		switch (GetFrameGenMethod()) {
		case FrameGenMethod::kDLSSG:
			// Required: sl.dlss_g reports eFailReflexNotDetectedAtRuntime and generates nothing
			// without it, and Reflex's frame limiter works on this path (measured holding 41.25 fps
			// presented with DXVK's limiter disabled).
			return true;
		case FrameGenMethod::kFSR:
			// Off. FidelityFX's replacement swapchain owns present, and Reflex paces through the
			// present it owns, so the limit it is handed cannot be applied -- measured 37 fps
			// rendered against a 20.5 fps limit. Nor is there a latency benefit to weigh against
			// that: with Reflex off, on, and on+boost the frame rate and render-to-present latency
			// were identical (3.13 / 3.13 / 3.15 ms), because DXVK's SyncFrameLatency already holds
			// the queue at about one frame. Leaving it off also lets DXVK's limiter take the cap.
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

int Upscaling::GetHighestRefreshRate() const
{
	DEVMODEA current{};
	current.dmSize = sizeof(current);
	if (!EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &current))
		return GetMonitorRefreshRate();
	DWORD best = current.dmDisplayFrequency;
	for (DWORD i = 0;; ++i) {
		DEVMODEA mode{};
		mode.dmSize = sizeof(mode);
		if (!EnumDisplaySettingsA(nullptr, i, &mode))
			break;
		if (mode.dmPelsWidth == current.dmPelsWidth && mode.dmPelsHeight == current.dmPelsHeight)
			best = std::max(best, mode.dmDisplayFrequency);
	}
	return static_cast<int>(best);
}

double Upscaling::GetTargetFrameRate() const
{
	if (!loaded)
		return 0.0;
	const int divisor = settings.frameRateLimitDivisor;
	if (divisor <= 0) {
		// Unlocked means unlocked, for every frame-generation method. This used to return the
		// refresh rate for DLSS-G, reasoning that sl.dlss_g presents tear-free and so cannot exceed
		// the refresh rate anyway. That is true, but it does not justify silently capping a setting
		// the user set to unlimited: the cap lands on the PRESENTED cadence (see
		// GetRenderedFrameRateLimit), so 2x generation halved the RENDERED rate and with it the
		// input latency the user actually feels. FSR-FG never did this, which is how the
		// discrepancy was noticed. If guaranteed generation headroom is wanted, use the divisor.
		return 0.0;
	}
	// Deliberately NOT rounded to a whole frame rate. The target is a submultiple of the display
	// refresh, and rounding it breaks that relationship: at 165 Hz a divisor of 4 becomes 41 fps
	// (24.390 ms) instead of 41.25 (24.242 ms, exactly four refresh intervals). That 0.61% error
	// drifts a full refresh interval roughly once a second, so a frame slips and the display shows
	// one long interval followed by a short one -- measured as ~1.4% of frames beyond twice the
	// median, with the exact-dividing divisors 1 and 3 pacing visibly tighter.
	return std::max(1.0, static_cast<double>(GetMonitorRefreshRate()) / divisor);
}

uint32_t Upscaling::GetFixedDLSSGMultiplier() const
{
	uint32_t multiplier = std::clamp(settings.frameGenMultiplier, 2u, 6u);
	const uint32_t maxFrames = Streamline::GetSingleton()->GetDLSSGMaxFramesToGenerate();
	if (maxFrames > 0u)
		multiplier = std::min(multiplier, maxFrames + 1u);
	return multiplier;
}

double Upscaling::GetRenderedFrameRateLimit() const
{
	const double targetFps = GetTargetFrameRate();
	if (targetFps <= 0 || !IsFrameGenerationActive())
		return static_cast<double>(targetFps);

	switch (GetFrameGenMethod()) {
	case FrameGenMethod::kFSR:
		return static_cast<double>(targetFps) / 2.0;
	case FrameGenMethod::kDLSSG:
		// Unlike FFX -- whose replacement swapchain owns the present loop, so the limiter only
		// ever sees rendered frames -- sl.dlss_g emits its generated frame from inside the same
		// present that Reflex's frame-limit interval (and DXVK's limiter behind Streamline)
		// throttles. The limit therefore lands on the PRESENTED cadence, and pre-dividing by the
		// multiplier here divides a second time: measured 41 rendered / 82.6 presented against a
		// 165 Hz target, i.e. target / multiplier^2. Hand the limiter the output target and let
		// DLSS-G's own pacing derive the rendered cadence from it.
		return static_cast<double>(targetFps);
	default:
		return static_cast<double>(targetFps);
	}
}

void Upscaling::ApplyDxvkFrameRateLimit(double a_fps)
{
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
	auto* dxvk = DXVKInterop::GetSingleton();
	auto* streamline = Streamline::GetSingleton();
	auto requestFaultTeardown = [&](const char* a_reason) -> HRESULT {
		// A lost device can never prove completion, so the deferral below would suppress every
		// present for the rest of the session -- the screen stops updating while the log fills.
		// That is the frame-generation freeze: measured 243k vkDeviceWaitIdle(-4) and 638k log
		// lines in one session, the game hammering a device that was already gone. Nothing here can
		// recover it, so stop trying and let the present through; the runtime reports the loss.
		if (dxvk->IsDeviceLost()) {
			static bool deviceLossReported = false;
			if (!std::exchange(deviceLossReported, true))
				logger::critical("[Upscaling] device lost ({}); presenting without frame generation", a_reason);
			return a_present(a_swapChain, 0, a_flags);
		}
		logger::error("[Upscaling] {} - disabling frame generation", a_reason);
		settings.frameGeneration = false;
		const uint32_t displayWidth = globals::game::graphicsState ? globals::game::graphicsState->screenWidth : 0;
		const uint32_t displayHeight = globals::game::graphicsState ? globals::game::graphicsState->screenHeight : 0;
		if (!streamline->HasDispatchFaulted() && streamline->IsDLSSGLoaded() &&
			!streamline->SetDLSSGMode(false, displayWidth, displayHeight)) {
			logger::error("[Upscaling] DLSS-G mode-off failed; continuing with device-idle teardown");
		}
		const bool completionProven = dxvk->HasPendingPresentWaitSemaphore() ?
			dxvk->DiscardPendingPresentWaitSemaphore() : dxvk->WaitDeviceIdle();
		if (!completionProven) {
			// Deferring suppresses this present. That is correct for a transient fault, but only
			// while the wait can still succeed -- see the device-lost check at the top, which is
			// what stops this becoming permanent.
			static bool deferralReported = false;
			if (!std::exchange(deferralReported, true))
				logger::error("[Upscaling] frame-generation fault teardown deferred because GPU completion could not be proven");
			return DXGI_STATUS_OCCLUDED;
		}
		if (streamline->IsFSRFGLoaded() &&
			!streamline->DiscardFSRFrameGenerationPreparedFrame()) {
			logger::critical("[Upscaling] FSR prepared frame could not be discarded safely; present blocked");
			return E_FAIL;
		}
		// Ambiguous submissions keep their Vulkan views quarantined until FFX swapchain teardown.
		const auto& hdr = globals::features::hdrDisplay;
		dxvk->BeginPresenterColorSpaceTransition(hdr.loaded && hdr.IsHDREnabledForFrame(), true);
		streamline->SetDLSSGDesiredLoaded(false);
		streamline->SetFSRFGDesiredLoaded(false);
		Streamline::RequestDxvkSwapchainRecreate(a_reason);
		FrameGen::Controller::GetSingleton()->NotifyFaultTeardownRequested();
		return a_present(a_swapChain, 0, a_flags);
	};
	if (streamline->HasDispatchFaulted() || dxvk->HasCommandRingFault()) {
		settings.frameGeneration = false;
		if (streamline->IsDLSSGLoaded() || streamline->IsFSRFGLoaded() ||
			dxvk->HasPendingPresentWaitSemaphore())
			return requestFaultTeardown("Vulkan frame-generation dispatch fault");
		return a_present(a_swapChain, a_syncInterval, a_flags);
	}

	if (!IsFrameGenerationActive()) {
		if (dxvk->HasPendingPresentWaitSemaphore() && !dxvk->PushPendingPresentWaitSemaphore())
			return requestFaultTeardown("DLSS-G present synchronization failed");
		return a_present(a_swapChain, a_syncInterval, a_flags);
	}

	// Relax the fully-synchronous present once a proxy actually owns the swapchain. The switch
	// itself needs depth zero, and the proxy install re-asserts it, so this has to be (re)applied
	// from the steady-state present rather than once at settle. Depth zero makes
	// D3D11SwapChain::PresentImage drain its own status every frame, blocking the render thread in
	// waitForSubmission until the proxy's intercepted vkQueuePresentKHR returns.
	// FSR-FG only. DLSS-G paces by blocking inside its own present (eBlockPresentingClientQueue);
	// adding a second waiting gate on top deadlocks the pipeline -- measured as a main-thread hang
	// that only a process restart clears.
	if (streamline->IsFSRFGPresentOwner())
		Streamline::PushDxvkPresentQueueDepth(2u);

	auto fgMethod = GetFrameGenMethod();
	if (fgMethod != FrameGenMethod::kDLSSG) {
		if (dxvk->HasPendingPresentWaitSemaphore() && !dxvk->PushPendingPresentWaitSemaphore())
			return requestFaultTeardown("DLSS-G present synchronization failed");
		return a_present(a_swapChain, a_syncInterval, a_flags);
	}

	// DLSS-G requires a valid or passthrough tag for every present -- but only once it actually owns
	// the present. Between selecting it and sl.dlss_g being loaded (a swapchain recreate apart), the
	// tag path legitimately has nothing to do, and treating that as a fault tore frame generation
	// down on every enable.
	if (!streamline->IsDLSSGLoaded()) {
		if (dxvk->HasPendingPresentWaitSemaphore() && !dxvk->PushPendingPresentWaitSemaphore())
			return requestFaultTeardown("DLSS-G present synchronization failed");
		return a_present(a_swapChain, a_syncInterval, a_flags);
	}

	if ((dxvk->HasPendingPresentWaitSemaphore() || streamline->EnsureDLSSGPresentTag()) &&
		dxvk->PushPendingPresentWaitSemaphore())
		return a_present(a_swapChain, a_syncInterval, a_flags);

	return requestFaultTeardown("DLSS-G present synchronization failed");
}

void Upscaling::CreateUpscaledTexture()
{
	if (upscaledTexture)
		return;

	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC texDesc{};
	main.texture->GetDesc(&texDesc);

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
	D3D11_TEXTURE2D_DESC texDesc{};
	auto& hdr = globals::features::hdrDisplay;
	const bool hdrActive = hdr.loaded && hdr.IsHDREnabledForFrame();
	auto* dxvk = DXVKInterop::GetSingleton();
	const auto encoding = dxvk->GetPresenterEncodingForFrame();
	const VkFormat presenterFormat = dxvk->GetPresenterFormatForFrame();
	const bool nativeHDR = hdrActive && encoding == DXVKInterop::PresenterEncoding::kHDR10;
	if (hdrActive) {
		// Size from the composed HDR output; the blit in CaptureHudlessColor converts the
		// format, so this no longer requires the presenter to be exactly HDR10.
		if (!hdr.outputTexture || !hdr.outputTexture->resource)
			return;
		hdr.outputTexture->resource->GetDesc(&texDesc);
		if (nativeHDR && texDesc.Format != DXGI_FORMAT_R10G10B10A2_UNORM) {
			logger::error("[Upscaling] Native HDR output texture is not R10G10B10A2_UNORM (format={})",
				static_cast<int>(texDesc.Format));
			return;
		}
	} else {
		auto renderer = globals::game::renderer;
		auto& fb = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kFRAMEBUFFER];
		if (!fb.SRV)
			return;
		winrt::com_ptr<ID3D11Resource> fbResource;
		fb.SRV->GetResource(fbResource.put());
		winrt::com_ptr<ID3D11Texture2D> fbTexture;
		if (!fbResource || FAILED(fbResource->QueryInterface(IID_PPV_ARGS(fbTexture.put()))) || !fbTexture)
			return;
		fbTexture->GetDesc(&texDesc);
	}

	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	// HDR always lands in 10-bit: it is what the HDR10 backbuffer FFX subtracts against
	// uses, and the blit converts into it regardless of the composed source format.
	if (hdrActive) {
		format = DXGI_FORMAT_R10G10B10A2_UNORM;
	} else if (!hdrActive && encoding == DXVKInterop::PresenterEncoding::kSDR) {
		if (presenterFormat == VK_FORMAT_R8G8B8A8_UNORM)
			format = DXGI_FORMAT_R8G8B8A8_UNORM;
		else if (presenterFormat == VK_FORMAT_B8G8R8A8_UNORM)
			format = DXGI_FORMAT_B8G8R8A8_UNORM;
		else if (presenterFormat == VK_FORMAT_A2B10G10R10_UNORM_PACK32 ||
				 presenterFormat == VK_FORMAT_A2R10G10B10_UNORM_PACK32)
			format = DXGI_FORMAT_R10G10B10A2_UNORM;
	}
	if (format == DXGI_FORMAT_UNKNOWN) {
		logger::error("[Upscaling] Unsupported HUD-less presenter format {} for encoding {}",
			static_cast<int>(presenterFormat), static_cast<int>(encoding));
		return;
	}

	if (hudlessTexture &&
		hudlessTexture->desc.Width == texDesc.Width &&
		hudlessTexture->desc.Height == texDesc.Height &&
		hudlessTexture->desc.MipLevels == texDesc.MipLevels &&
		hudlessTexture->desc.ArraySize == texDesc.ArraySize &&
		hudlessTexture->desc.Format == format &&
		hudlessTexture->desc.SampleDesc.Count == texDesc.SampleDesc.Count &&
		hudlessTexture->desc.SampleDesc.Quality == texDesc.SampleDesc.Quality)
		return;

	if (!DestroyHudlessTexture())
		return;

	texDesc.Format = format;
	// Always give the hudless target a render-target view. FFX derives the UI as
	// (backbuffer - hudless), so the two must agree in format; blitting through an RTV
	// converts, whereas CopyResource requires an exact match and silently drops hudless
	// when it does not hold -- which leaves the UI to be interpolated, i.e. flashing.
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

	hudlessTexture = new Texture2D(texDesc);
	Util::SetResourceName(hudlessTexture->resource.get(), "Upscaling::HudlessTexture");

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = texDesc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	hudlessTexture->CreateSRV(srvDesc);

	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = texDesc.Format;
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	hudlessTexture->CreateRTV(rtvDesc);

	logger::info("[Upscaling] Created hudless texture ({}x{}, format={})",
		texDesc.Width, texDesc.Height, static_cast<int>(texDesc.Format));
}

bool Upscaling::DestroyHudlessTexture(bool a_commandRingDrained)
{
	if (hudlessTexture) {
		if (!a_commandRingDrained && !DXVKInterop::GetSingleton()->DrainCommandRing()) {
			logger::error("[Upscaling] hudless texture destruction deferred because command completion could not be proven");
			return false;
		}
		delete hudlessTexture;
		hudlessTexture = nullptr;
		logger::debug("[Upscaling] Destroyed hudless texture");
	}
	return true;
}

bool Upscaling::CopyHudlessColor(ID3D11ShaderResourceView* a_source)
{
	if (!a_source || !hudlessTexture || !hudlessTexture->rtv)
		return false;

	auto* vertexShader = GetUpscaleVS();
	auto* pixelShader = GetCopyHudlessPS();
	if (!vertexShader || !pixelShader)
		return false;

	auto* context = globals::d3d::context;
	Effects11Util::D3D11ScopedPostFxBackup backup;
	backup.Save(context);

	context->IASetInputLayout(nullptr);
	context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
	context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->VSSetShader(vertexShader, nullptr, 0);

	D3D11_VIEWPORT viewport{};
	viewport.Width = static_cast<float>(hudlessTexture->desc.Width);
	viewport.Height = static_cast<float>(hudlessTexture->desc.Height);
	viewport.MaxDepth = 1.0f;
	context->RSSetViewports(1, &viewport);
	context->RSSetState(nullptr);
	context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
	context->OMSetDepthStencilState(nullptr, 0);

	ID3D11ShaderResourceView* sources[] = { a_source };
	context->PSSetShaderResources(0, ARRAYSIZE(sources), sources);
	context->PSSetShader(pixelShader, nullptr, 0);
	ID3D11RenderTargetView* output = hudlessTexture->rtv.get();
	context->OMSetRenderTargets(1, &output, nullptr);
	context->Draw(3, 0);

	sources[0] = nullptr;
	context->PSSetShaderResources(0, ARRAYSIZE(sources), sources);
	backup.Restore(context);
	backup.Release();
	return true;
}

ID3D11Resource* Upscaling::CaptureHudlessColor()
{
	CreateHudlessTexture();
	if (!hudlessTexture || !hudlessTexture->resource)
		return nullptr;

	auto& hdr = globals::features::hdrDisplay;
	if (hdr.loaded && hdr.IsHDREnabledForFrame() && hdr.hdrTexture && hdr.hdrTexture->srv) {
		// Compose the scene with no UI, then BLIT it into the 10-bit hudless target.
		//
		// This used to CopyResource, which requires the source and destination formats to
		// match exactly and returned nullptr on any mismatch -- and nullptr means no
		// HUDLessColor reaches FFX at all, so FFX interpolates the UI along with the scene
		// and it flashes on generated frames. Blitting converts instead of refusing, so the
		// hudless buffer always lands in the same 10-bit format as the presented backbuffer
		// that FFX subtracts it from. Both frame-generation methods take this path.
		if (!hdr.ComposeCleanCapture(hdr.hdrTexture->srv.get(), false))
			return nullptr;
		if (!hdr.outputTexture || !hdr.outputTexture->srv)
			return nullptr;
		return CopyHudlessColor(hdr.outputTexture->srv.get()) ? hudlessTexture->resource.get() : nullptr;
	}

	auto& fb = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kFRAMEBUFFER];
	if (!fb.SRV)
		return nullptr;

	return CopyHudlessColor(fb.SRV) ? hudlessTexture->resource.get() : nullptr;
}

void Upscaling::CheckResources(UpscaleMethod a_upscalemethod)
{
	static auto previousUpscaleMode = UpscaleMethod::kTAA;

	if (previousUpscaleMode != a_upscalemethod) {
		logger::debug("[Upscaling] Upscale method changed: {} ({}) -> {} ({})",
			static_cast<int>(previousUpscaleMode), magic_enum::enum_name(previousUpscaleMode),
			static_cast<int>(a_upscalemethod), magic_enum::enum_name(a_upscalemethod));

		bool hadUpscale = (previousUpscaleMode == UpscaleMethod::kFSR ||
		                   previousUpscaleMode == UpscaleMethod::kDLSS ||
		                   previousUpscaleMode == UpscaleMethod::kXeSS) &&
		                  previousUpscalingWasActive;
		if (hadUpscale) {
			// DXVK does not track resources referenced by foreign Vulkan submissions.
			if (!DXVKInterop::GetSingleton()->DrainCommandRing()) {
				logger::error("[Upscaling] method change deferred because command completion could not be proven");
				return;
			}
			DestroyUpscaledTexture();
			DestroyHudlessTexture(true);
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

ID3D11PixelShader* Upscaling::GetCopyHudlessPS()
{
	if (!copyHudlessPS) {
		logger::debug("Compiling CopyHudlessPS.hlsl");
		copyHudlessPS.attach((ID3D11PixelShader*)Util::CompileShader(L"Data/Shaders/Upscaling/CopyHudlessPS.hlsl", {}, "ps_5_0"));
	}
	return copyHudlessPS.get();
}

int32_t GetJitterPhaseCount(int32_t renderWidth, int32_t displayWidth)
{
	const float basePhaseCount = 8.0f;
	const int32_t jitterPhaseCount = int32_t(basePhaseCount * pow((float(displayWidth) / renderWidth), 2.0f));
	return jitterPhaseCount;
}

// Calculates a Halton-sequence value for the given index and base.
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

	// Force temporal AA on for every method except the explicit no-AA mode.
	Util::SetTemporal(upscaleMethod != UpscaleMethod::kNONE);
}

void Upscaling::ConfigureUpscaling(RE::BSGraphics::State* a_viewport)
{
	auto upscaleMethod = GetUpscaleMethod();

	// Create or release method-specific resources as necessary.
	CheckResources(upscaleMethod);

	projectionPosScaleX = a_viewport->projectionPosScaleX;
	projectionPosScaleY = a_viewport->projectionPosScaleY;

	auto state = globals::state;
	// The graphics-state dimensions are the full display size.
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

	auto* dxvk = DXVKInterop::GetSingleton();
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

		auto* streamline = Streamline::GetSingleton();
		if (streamline->Initialize()) {
			streamline->SetVulkanDevice();
			Streamline::RegisterDxvkOwnershipPredicate();
		}

		ApplyHardwareDefaults();
	}
}

void Upscaling::ClearShaderCache()
{
	depthRefractionUpscalePS = nullptr;
	underwaterMaskUpscalePS = nullptr;
	upscaleVS = nullptr;
	copyHudlessPS = nullptr;
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

bool Upscaling::IsWindowMinimized()
{
	// Suspend all Streamline work while the swapchain cannot present.
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
	s_windowUnfocused.store(!a_focused, std::memory_order_relaxed);
}

void Upscaling::NotifyWindowModifying(bool a_modifying)
{
	s_windowModifying.store(a_modifying, std::memory_order_relaxed);
}

bool Upscaling::IsWindowUnusable()
{
	if (const auto foregroundWindow = GetForegroundWindow()) {
		DWORD foregroundProcess = 0;
		GetWindowThreadProcessId(foregroundWindow, &foregroundProcess);
		if (foregroundProcess != 0)
			s_windowUnfocused.store(foregroundProcess != GetCurrentProcessId(), std::memory_order_relaxed);
	}

	return IsWindowMinimized() ||
	       s_windowUnfocused.load(std::memory_order_relaxed) ||
	       s_windowModifying.load(std::memory_order_relaxed);
}


void Upscaling::Upscale()
{
	ZoneScoped;

	if (IsWindowMinimized())
		return;

	auto state = globals::state;
	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;
	const auto method = GetUpscaleMethod();

	context->OMSetRenderTargets(0, nullptr, nullptr);

	auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

	{
		globals::profiler->BeginPass("Upscaling::Upscale");
		state->BeginPerfEvent("Upscaling");
		TracyD3D11Zone(globals::state->tracyCtx, "Upscaling Dispatch");

		auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
		auto& depthTex = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
		const auto displaySize = float2{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
		const auto renderSize = Util::ConvertToDynamic(displaySize);
		auto result = Streamline::EvaluationResult::kFailed;

		if (upscaledTexture && upscaledTexture->resource) {
			switch (method) {
			case UpscaleMethod::kFSR:
				result = Streamline::GetSingleton()->EvaluateFSR(
					main.texture, upscaledTexture->resource.get(), depthTex.texture, motionVector.texture,
					(uint32_t)renderSize.x, (uint32_t)renderSize.y,
					(uint32_t)displaySize.x, (uint32_t)displaySize.y,
					settings.qualityMode, settings.sharpnessFSR, jitter.x, jitter.y);
				break;
			case UpscaleMethod::kDLSS:
				result = Streamline::GetSingleton()->EvaluateDLSS(
					main.texture, upscaledTexture->resource.get(), depthTex.texture, motionVector.texture,
					(uint32_t)renderSize.x, (uint32_t)renderSize.y,
					(uint32_t)displaySize.x, (uint32_t)displaySize.y,
					settings.qualityMode, jitter.x, jitter.y);
				break;
			case UpscaleMethod::kXeSS:
				result = Streamline::GetSingleton()->EvaluateXeSS(
					main.texture, upscaledTexture->resource.get(), depthTex.texture, motionVector.texture,
					(uint32_t)renderSize.x, (uint32_t)renderSize.y,
					(uint32_t)displaySize.x, (uint32_t)displaySize.y,
					settings.qualityMode, settings.sharpnessFSR, jitter.x, jitter.y);
				break;
			default:
				result = Streamline::EvaluationResult::kSkipped;
				break;
			}
		}

		if (result == Streamline::EvaluationResult::kReady)
			context->CopyResource(main.texture, upscaledTexture->resource.get());
		else if (result == Streamline::EvaluationResult::kFailed)
			MarkUpscaleMethodFailed(method);

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

void Upscaling::PrepareFrameGeneration(ID3D11Resource* a_hudlessColor)
{
	auto* ui = globals::game::ui;
	const bool gameplay = ui && !ui->GameIsPaused() && !globals::state->IsMainOrLoadingMenuOpen(ui);
	const auto fgMethod = GetFrameGenMethod();
	auto& hdr = globals::features::hdrDisplay;
	const bool hdrActive = hdr.loaded && hdr.IsHDREnabledForFrame();
	if (!DXVKInterop::GetSingleton()->IsPresenterStateReadyForFrame(hdrActive) ||
		(fgMethod == FrameGenMethod::kFSR &&
		 !FrameGen::Controller::GetSingleton()->IsFSRPresenterReady()))
		return;

	auto* renderer = globals::game::renderer;
	auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

	if (fgMethod == FrameGenMethod::kDLSSG) {
		FrameGen::Controller::GetSingleton()->EngageDLSSG();

		if (gameplay) {
			const auto displaySize = float2{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
			const auto renderSize = Util::ConvertToDynamic(displaySize, true);
			auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
			auto& depthCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN_COPY];
			ID3D11Resource* fgDepth = (IsUpscalingActive() && depthCopy.texture) ? depthCopy.texture : depth.texture;

			if (!IsUpscalingActive()) {
				CreateUpscaledTexture();
				auto& mainColor = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
				if (upscaledTexture && upscaledTexture->resource && mainColor.texture)
					(void)Streamline::GetSingleton()->EvaluateFSR(
						mainColor.texture, upscaledTexture->resource.get(), fgDepth, motionVector.texture,
						(uint32_t)displaySize.x, (uint32_t)displaySize.y, (uint32_t)displaySize.x, (uint32_t)displaySize.y,
						0, 0.0f, jitter.x, jitter.y);
			}

			static uint32_t s_lastTagFrame = UINT32_MAX;
			const uint32_t tagFrame = globals::state->frameCount;
			if (s_lastTagFrame != tagFrame) {
				s_lastTagFrame = tagFrame;
				Streamline::GetSingleton()->TagDLSSGResources(
					fgDepth, motionVector.texture, a_hudlessColor,
					(uint32_t)renderSize.x, (uint32_t)renderSize.y,
					(uint32_t)displaySize.x, (uint32_t)displaySize.y);
			}
		}
	} else if (fgMethod == FrameGenMethod::kFSR && gameplay) {
		const auto displaySize = float2{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
		const auto renderSize = Util::ConvertToDynamic(displaySize, true);
		auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
		auto& depthCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN_COPY];
		ID3D11Resource* fgDepth = (IsUpscalingActive() && depthCopy.texture) ? depthCopy.texture : depth.texture;
		(void)Streamline::GetSingleton()->EvaluateFSRFrameGen(
			fgDepth, motionVector.texture, a_hudlessColor,
			(uint32_t)renderSize.x, (uint32_t)renderSize.y,
			(uint32_t)displaySize.x, (uint32_t)displaySize.y,
			jitter.x, jitter.y);
		Streamline::GetSingleton()->CaptureFSRFrameGenState();
	}
}

void Upscaling::Main_UpdateJitter::thunk(RE::BSGraphics::State* a_state)
{
	auto& upscaling = globals::features::upscaling;
	upscaling.BeginRenderFrame();
	Streamline::GetSingleton()->BeginRenderFrame();
	upscaling.ConfigureTAA();
	func(a_state);
	upscaling.ConfigureUpscaling(a_state);
}

void Upscaling::MenuManagerDrawInterfaceStartHook::thunk(int64_t a1)
{
	auto& upscaling = globals::features::upscaling;

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

	// Keep markers, evaluation, tagging, and present on the same usable-window frame.
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

	Util::SetTemporal(upscaleMethod == UpscaleMethod::kTAA);

	bool hdrLoaded = globals::features::hdrDisplay.loaded;
	if (hdrLoaded)
		globals::features::hdrDisplay.RedirectFramebuffer();

	func(a_this, a3, a_target, a_4, a_5);

	if (hdrLoaded)
		globals::features::hdrDisplay.RestoreFramebuffer();

	if (windowUsable && upscaling.IsFrameGenerationActive()) {
		const auto& hdr = globals::features::hdrDisplay;
		const bool hdrActive = hdr.loaded && hdr.IsHDREnabledForFrame();
		const auto fgMethod = upscaling.GetFrameGenMethod();
		const bool presenterReady = DXVKInterop::GetSingleton()->IsPresenterStateReadyForFrame(hdrActive);
		const bool fsrReady = fgMethod != FrameGenMethod::kFSR ||
		                      FrameGen::Controller::GetSingleton()->IsFSRPresenterReady();
		if (presenterReady && fsrReady)
			upscaling.PrepareFrameGeneration(upscaling.CaptureHudlessColor());
	}

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
