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
	fgAllowTearing,
	frameRateLimitDivisor,
	frameRateLimit);

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
	const bool tearingSupported = Upscaling::IsTearingSupported();
	const bool allowTearing = upscaling.settings.fgAllowTearing && tearingSupported && pSwapChainDesc->Windowed;
	DxvkLoader::SetTearingPreference(upscaling.settings.frameGeneration ? (allowTearing ? 1u : 0u) : 2u);
	if (allowTearing)
		pSwapChainDesc->Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
	logger::info("[Upscaling] Frame-generation tearing {} (requested={} supported={} windowed={} swapchainFlags=0x{:X})",
		allowTearing ? "enabled" : "disabled", upscaling.settings.fgAllowTearing,
		tearingSupported, pSwapChainDesc->Windowed, pSwapChainDesc->Flags);

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
	auto* streamline = &Runtime().Session();
	const auto capabilities = Runtime().GetCapabilities();
	const bool dlssAvailable = capabilities.dlss;
	const bool xessAvailable = capabilities.xess;
	const bool dlssgAvailable = capabilities.dlssg;
	const bool fsrfgAvailable = capabilities.fsrfg;
	const bool reflexAvailable = capabilities.reflex;

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
		const bool tearingSupported = IsTearingSupported();
		if (DrawToggleStepper(T(TKEY("fg_allow_tearing"), "Allow Tearing with Frame Generation"),
				&settings.fgAllowTearing, !tearingSupported)) {
			DxvkLoader::SetTearingPreference(settings.fgAllowTearing ? 1u : 0u);
			Runtime().Dxvk().RequestSwapchainRecreate("frame-generation tearing preference changed");
		}
		if (!tearingSupported) {
			ImGui::SameLine();
			ImGui::TextDisabled("%s", T(TKEY("tearing_unsupported"), "(not supported by this display path)"));
		}

		const int refresh = GetMonitorRefreshRate();
		std::vector<int> fpsOptions;
		for (int d = 4; d >= 1; --d) {
			if (refresh / d >= 30)
				fpsOptions.push_back(refresh / d);
		}
		for (int fps : { 30, 40, 48, 50, 60, 72, 90, 100, 120, 144, 165, 180, 200, 240 })
			if (fps <= refresh)
				fpsOptions.push_back(fps);
		fpsOptions.push_back(refresh);
		std::sort(fpsOptions.begin(), fpsOptions.end());
		fpsOptions.erase(std::unique(fpsOptions.begin(), fpsOptions.end()), fpsOptions.end());
		fpsOptions.push_back(0);

		std::vector<std::string> fpsStrings;
		for (int fps : fpsOptions)
			fpsStrings.push_back(fps == 0 ?
			                         std::string(T(TKEY("frame_rate_unlocked"), "Unlocked (variable)")) :
			                         std::format("{} FPS", fps));
		std::vector<const char*> fpsLabels;
		for (auto& s : fpsStrings)
			fpsLabels.push_back(s.c_str());

		const int maxSel = static_cast<int>(fpsOptions.size()) - 1;
		int sel = maxSel > 0 ? maxSel - 1 : 0;
		const int currentTarget = GetTargetFrameRate();
		for (int i = 0; i <= maxSel; ++i) {
			if (fpsOptions[i] == currentTarget)
				sel = i;
		}
		if (DrawStepper(T(TKEY("frame_rate"), "Frame Rate"), &sel, fpsLabels)) {
			settings.frameRateLimit = fpsOptions[std::clamp(sel, 0, maxSel)];
			settings.frameRateLimitDivisor = 0;
		}
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

		if (streamline->IsDisabledByConfig())
			ImGui::TextDisabled("%s", T(TKEY("sl_restart_note"), "Upscalers and frame generation activate after a restart"));

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
		if (streamline->IsDisabledByConfig() && fgLabels.size() == 1) {
			DrawToggleStepper(T(TKEY("fg_enable_restart"), "Frame Generation (after restart)"), &settings.frameGeneration);
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
		                            GetFrameGenMethod() == FrameGenMethod::kFSR;
		if (fgForcesReflex) {
			int idx = GetEffectiveReflex() ? (settings.reflexBoost ? 2 : 1) : 0;
			DrawStepper(T(TKEY("nv_reflex"), "NVIDIA Reflex Low Latency"), &idx, reflexStates, /*disabled=*/true);
			ImGui::SameLine();
			ImGui::TextDisabled("%s", T(TKEY("reflex_forced_fsrfg"), "(forced off by FSR frame gen)"));
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
	// FSR-G requires synchronous present bookkeeping continuously. DLSS-G is
	// synchronous only around acknowledged option/ownership transitions.
	if (DxvkLoader::IsLoaded()) {
		const auto fgMethod = static_cast<FrameGenMethod>(settings.frameGenMethod);
		const auto queuePolicy = !settings.frameGeneration ? DxvkControl::PresentQueuePolicy::kUnrestricted :
			fgMethod == FrameGenMethod::kFSR ? DxvkControl::PresentQueuePolicy::kSynchronous :
			                                  DxvkControl::PresentQueuePolicy::kBoundedOverlap;
		Runtime().Dxvk().SetPresentQueuePolicy(queuePolicy);
	}

	if (DxvkLoader::IsLoaded()) {
		// Interposition must be selected from saved settings before DXVK creates VkInstance.
		const auto savedMethod = static_cast<UpscaleMethod>(settings.upscaleMethod);
		char forceSL[2] = {};
		const bool needsSL = settings.frameGeneration ||
		                     (savedMethod != UpscaleMethod::kNONE && savedMethod != UpscaleMethod::kTAA) ||
		                     settings.reflexEnabled ||
		                     (GetEnvironmentVariableA("CS_FORCE_SL_LOAD", forceSL, sizeof(forceSL)) && forceSL[0] == '1');
		if (needsSL) {
			Runtime().Session().PreloadInterposer();
		} else {
			Runtime().Session().SetDisabledByConfig();
			logger::info("[Upscaling] Streamline disabled by config (upscaleMethod={}, frameGeneration=off) - "
			             "DXVK runs on the real Vulkan driver; enabling an SL upscaler or frame generation requires a restart",
				settings.upscaleMethod);
		}
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
	stl::write_thunk_call<Main_UpdateJitter>(REL::RelocationID(75460, 77245).address() + REL::Relocate(0xE5, isGOG ? 0x133 : 0xE2));

	// Disables the original dynamic-resolution system.
	REL::safe_write(REL::RelocationID(35556, 36555).address() + REL::Relocate(0x2D, 0x2D), REL::NOP5, sizeof(REL::NOP5));

	// Performs upscaling between volumetric lighting and post-processing.
	stl::write_thunk_call<Main_PostProcessing>(REL::RelocationID(100430, 107148).address() + REL::Relocate(0x1F0, 0x1E7));

	// Scales scissor rectangles with the dynamic render resolution.
	stl::detour_thunk<SetScissorRect>(REL::RelocationID(75564, 77365));

	// Prevents dynamic resolution from affecting face-generation textures.
	stl::detour_thunk<BSFaceGenManager_UpdatePendingCustomizationTextures>(REL::RelocationID(26455, 27041));

	// Prevents dynamic resolution from affecting the precipitation camera.
	stl::write_thunk_call<Main_RenderPrecipitation>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x3A1, 0x3A1));

	// Forces FXAA off.
	stl::detour_thunk<BSImageSpace_Init_FXAA>(REL::RelocationID(98974, 105626));

	logger::info("[Upscaling] Installed hooks");
}

#undef I18N_KEY_PREFIX

UpscalingRuntime& Upscaling::Runtime()
{
	return globals::features::upscaling.runtime;
}

Upscaling::UpscaleMethod Upscaling::GetUpscaleMethod() const
{
	auto* streamline = &Runtime().Session();
	auto* dxvk = &Runtime().Vulkan();
	const auto capabilities = Runtime().GetCapabilities();
	if (streamline->HasDispatchFaulted() ||
		(dxvk->IsAvailable() && !dxvk->CommandResourcesReady()))
		return UpscaleMethod::kTAA;

	auto method = static_cast<UpscaleMethod>(settings.upscaleMethod);
	for (uint32_t fallback = 0; fallback < 3; ++fallback) {
		if (method == UpscaleMethod::kDLSS &&
			(!capabilities.dlss || IsUpscaleMethodFailed(method))) {
			method = static_cast<UpscaleMethod>(settings.upscaleMethodNoDLSS);
			if (method == UpscaleMethod::kDLSS)
				method = UpscaleMethod::kFSR;
			continue;
		}
		if (method == UpscaleMethod::kXeSS &&
			(!capabilities.xess || IsUpscaleMethodFailed(method))) {
			method = UpscaleMethod::kFSR;
			continue;
		}
		if (method == UpscaleMethod::kFSR &&
			(!capabilities.fsr || IsUpscaleMethodFailed(method)))
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

	auto* sl = &Runtime().Session();

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
	const auto capabilities = Runtime().GetCapabilities();
	const auto selected = static_cast<FrameGenMethod>(settings.frameGenMethod);
	if (selected == FrameGenMethod::kDLSSG)
		return capabilities.dlssg ? FrameGenMethod::kDLSSG : FrameGenMethod::kFSR;
	return capabilities.fsrfg ? FrameGenMethod::kFSR :
	       capabilities.dlssg ? FrameGenMethod::kDLSSG :
	                                FrameGenMethod::kFSR;
}

bool Upscaling::IsFrameGenerationRequested() const
{
	if (!loaded || !settings.frameGeneration)
		return false;
	const auto method = GetFrameGenMethod();
	const auto capabilities = Runtime().GetCapabilities();
	return method == FrameGenMethod::kDLSSG ? capabilities.dlssg : capabilities.fsrfg;
}

bool Upscaling::IsFrameGenerationActive() const
{
	if (!IsFrameGenerationRequested())
		return false;
	const auto& hdr = globals::features::hdrDisplay;
	const bool hdrActive = hdr.loaded && hdr.IsHDREnabledForFrame();
	const auto status = Runtime().GetFrameGenerationStatus(hdrActive);
	return status.presenterReady && !status.dispatchFaulted && !status.submissionFaulted;
}

void Upscaling::BeginRenderFrame()
{
	auto* dxvk = &Runtime().Vulkan();
	if (dxvk->HasCommandRingFault() &&
		!Runtime().Session().IsFSRFGLoaded() &&
		!dxvk->RecoverCommandRing()) {
		settings.frameGeneration = false;
		logger::error("[Upscaling] Vulkan command-ring recovery failed; falling back to TAA");
	}

	auto& hdr = globals::features::hdrDisplay;
	if (hdr.loaded)
		hdr.BeginRenderFrame();
	else
		Runtime().Vulkan().CommitPresenterSurfaceStateForRenderFrame();
}

bool Upscaling::GetEffectiveReflex() const
{
	if (IsFrameGenerationActive()) {
		switch (GetFrameGenMethod()) {
		case FrameGenMethod::kDLSSG:
			return settings.reflexEnabled;
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
	if (!loaded)
		return 0;
	if (settings.frameRateLimit >= 0)
		return settings.frameRateLimit;
	const int divisor = settings.frameRateLimitDivisor;
	if (divisor <= 0)
		return 0;
	return std::max(1, static_cast<int>(std::lround(static_cast<double>(GetMonitorRefreshRate()) / divisor)));
}

bool Upscaling::IsTearingSupported()
{
	static const bool supported = [] {
		winrt::com_ptr<IDXGIFactory5> factory;
		if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory5), factory.put_void())))
			return false;
		BOOL allowTearing = FALSE;
		return SUCCEEDED(factory->CheckFeatureSupport(
			DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing))) && allowTearing;
	}();
	return supported;
}

uint32_t Upscaling::GetFixedDLSSGMultiplier() const
{
	uint32_t multiplier = std::clamp(settings.frameGenMultiplier, 2u, 6u);
	const uint32_t maxFrames = Runtime().Session().GetDLSSGMaxFramesToGenerate();
	if (maxFrames > 0u)
		multiplier = std::min(multiplier, maxFrames + 1u);
	return multiplier;
}

double Upscaling::GetRenderedFrameRateLimit() const
{
	const int targetFps = GetTargetFrameRate();
	if (targetFps <= 0 || !IsFrameGenerationActive())
		return static_cast<double>(targetFps);

	switch (GetFrameGenMethod()) {
	case FrameGenMethod::kFSR:
		return static_cast<double>(targetFps) / 2.0;
	case FrameGenMethod::kDLSSG:
		// Dynamic MFG owns the final-output target. Fixed MFG needs the rendered
		// cadence reduced so real + generated frames add up to that target.
		return settings.dlssgDynamic ? static_cast<double>(targetFps) :
		                               static_cast<double>(targetFps) / GetFixedDLSSGMultiplier();
	default:
		return static_cast<double>(targetFps);
	}
}

void Upscaling::ApplyDxvkFrameRateLimit(double a_fps)
{
	static double lastFps = -2.0;
	if (a_fps != lastFps && DxvkLoader::SetTargetFrameRate(a_fps > 0.0 ? a_fps : 0.0)) {
		lastFps = a_fps;
	}
}

HRESULT Upscaling::PresentWithFrameGeneration(IDXGISwapChain* a_swapChain, UINT a_syncInterval, UINT a_flags,
	const std::function<HRESULT(IDXGISwapChain*, UINT, UINT)>& a_present)
{
	auto* dxvk = &Runtime().Vulkan();
	auto* streamline = &Runtime().Session();
	auto requestFaultTeardown = [&](const char* a_reason) -> HRESULT {
		logger::error("[Upscaling] {} - disabling frame generation", a_reason);
		settings.frameGeneration = false;
		const uint32_t displayWidth = globals::game::graphicsState ? globals::game::graphicsState->screenWidth : 0;
		const uint32_t displayHeight = globals::game::graphicsState ? globals::game::graphicsState->screenHeight : 0;
		if (!streamline->HasDispatchFaulted() && streamline->IsDLSSGLoaded() &&
			!streamline->SetDLSSGMode(false, displayWidth, displayHeight, displayWidth, displayHeight)) {
			// SetOptions is delivered on the Vulkan present thread. Let this Present
			// apply eOff and retry teardown on the next frame; draining before that
			// acknowledgement can race the still-active plugin.
			logger::info("[Upscaling] DLSS-G mode-off queued; deferring fault teardown until after Present");
			return a_present(a_swapChain, a_syncInterval, a_flags);
		}
		const bool completionProven = dxvk->HasPendingPresentWaitSemaphore() ?
			dxvk->DiscardPendingPresentWaitSemaphore() : dxvk->WaitDeviceIdle();
		if (!completionProven) {
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
		Runtime().Dxvk().RequestSwapchainRecreate(a_reason);
		Runtime().FrameGeneration().NotifyFaultTeardownRequested();
		return a_present(a_swapChain, a_syncInterval, a_flags);
	};
	if (streamline->HasDispatchFaulted() || dxvk->HasCommandRingFault()) {
		settings.frameGeneration = false;
		if (streamline->IsDLSSGLoaded() || streamline->IsFSRFGLoaded() ||
			dxvk->HasPendingPresentWaitSemaphore())
			return requestFaultTeardown("Vulkan frame-generation dispatch fault");
		return a_present(a_swapChain, a_syncInterval, a_flags);
	}

	if (!IsFrameGenerationActive()) {
		if (dxvk->HasPendingPresentWaitSemaphore() && !dxvk->CommitPendingPresentWait())
			return requestFaultTeardown("DLSS-G present synchronization failed");
		return a_present(a_swapChain, a_syncInterval, a_flags);
	}

	auto fgMethod = GetFrameGenMethod();
	if (fgMethod != FrameGenMethod::kDLSSG) {
		if (dxvk->HasPendingPresentWaitSemaphore() && !dxvk->CommitPendingPresentWait())
			return requestFaultTeardown("DLSS-G present synchronization failed");
		return a_present(a_swapChain, a_syncInterval, a_flags);
	}

	// DLSS-G requires a valid or passthrough tag for every present. The tag work
	// is submitted on the graphics queue, while DXVK may present on a dedicated
	// queue, so explicitly make the matching present wait on its completion
	// semaphore. This preserves asynchronous CPU/GPU execution without allowing
	// the present queue to overtake the inputs.
	if ((dxvk->HasPendingPresentWaitSemaphore() || streamline->EnsureDLSSGPresentTag()) &&
		dxvk->CommitPendingPresentWait())
		return a_present(a_swapChain, a_syncInterval, a_flags);

	return requestFaultTeardown("DLSS-G present synchronization failed");
}

bool Upscaling::BeginPresentMarkers()
{
	auto& session = Runtime().Session();
	session.SetPCLMarker(StreamlineSession::PclMarker::RenderSubmitEnd);
	const bool dlssgActive = IsFrameGenerationActive() && GetFrameGenMethod() == FrameGenMethod::kDLSSG;
	const bool bridged = dlssgActive && session.QueueDLSSGPresentMarkers();
	if (!bridged)
		session.SetPCLMarker(StreamlineSession::PclMarker::PresentStart);
	return bridged;
}

void Upscaling::EndPresentMarkers(bool a_bridgedPresentMarkers)
{
	if (a_bridgedPresentMarkers)
		Runtime().Session().CompleteDXVKPresentMarker();
	else
		Runtime().Session().SetPCLMarker(StreamlineSession::PclMarker::PresentEnd);
}

void Upscaling::NotifyPresentResult(HRESULT a_result)
{
	auto& vulkan = Runtime().Vulkan();
	if (SUCCEEDED(a_result))
		vulkan.RefreshPresenterSurfaceState();
	if (a_result == S_OK && Runtime().Session().IsFSRFGPresentOwner())
		vulkan.NotifyFSRFrameConsumed();
	vulkan.NotifyPresentWaitQueued();
}

bool Upscaling::DrainBackendWork()
{
	return Runtime().Vulkan().DrainCommandRing();
}

void Upscaling::CommitPresenterStateForRenderFrame()
{
	Runtime().Vulkan().CommitPresenterSurfaceStateForRenderFrame();
}

void Upscaling::BeginPresenterColorSpaceTransition(bool a_hdr)
{
	auto& vulkan = Runtime().Vulkan();
	if (vulkan.IsAvailable() || vulkan.Initialize())
		vulkan.BeginPresenterColorSpaceTransition(a_hdr);
}

void Upscaling::CancelPresenterColorSpaceTransition(bool a_hdr)
{
	Runtime().Vulkan().CancelPresenterColorSpaceTransition(a_hdr);
}

bool Upscaling::IsDLSSGRuntimeLoaded() const
{
	return Runtime().Session().IsDLSSGLoaded();
}

uint32_t Upscaling::GetFrameGenerationMultiplier() const
{
	return Runtime().Session().GetFrameGenerationMultiplier();
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
	auto* dxvk = &Runtime().Vulkan();
	const auto encoding = dxvk->GetPresenterEncodingForFrame();
	const VkFormat presenterFormat = dxvk->GetPresenterFormatForFrame();
	const bool nativeHDR = hdrActive && encoding == VulkanDeviceContext::PresenterEncoding::kHDR10;
	if (hdrActive) {
		if (!nativeHDR || !hdr.outputTexture || !hdr.outputTexture->resource)
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
	if (nativeHDR && presenterFormat == VK_FORMAT_A2B10G10R10_UNORM_PACK32) {
		format = DXGI_FORMAT_R10G10B10A2_UNORM;
	} else if (!hdrActive && encoding == VulkanDeviceContext::PresenterEncoding::kSDR) {
		if (presenterFormat == VK_FORMAT_B8G8R8A8_UNORM)
			format = DXGI_FORMAT_B8G8R8A8_UNORM;
		else if (presenterFormat == VK_FORMAT_R8G8B8A8_UNORM)
			format = DXGI_FORMAT_R8G8B8A8_UNORM;
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
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	if (!hdrActive)
		texDesc.BindFlags |= D3D11_BIND_RENDER_TARGET;

	hudlessTexture = new Texture2D(texDesc);
	Util::SetResourceName(hudlessTexture->resource.get(), "Upscaling::HudlessTexture");

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = texDesc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	hudlessTexture->CreateSRV(srvDesc);

	if (!hdrActive) {
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
		rtvDesc.Format = texDesc.Format;
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		hudlessTexture->CreateRTV(rtvDesc);
	}

	logger::info("[Upscaling] Created hudless texture ({}x{}, format={})",
		texDesc.Width, texDesc.Height, static_cast<int>(texDesc.Format));
}

bool Upscaling::DestroyHudlessTexture(bool a_commandRingDrained)
{
	if (hudlessTexture) {
		if (!a_commandRingDrained && !Runtime().Vulkan().DrainCommandRing()) {
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
		ID3D11Texture2D* composed = hdr.ComposeCleanCapture(hdr.hdrTexture->srv.get(), false);
		if (!composed)
			return nullptr;

		const auto encoding = Runtime().Vulkan().GetPresenterEncodingForFrame();
		if (encoding == VulkanDeviceContext::PresenterEncoding::kHDR10) {
			D3D11_TEXTURE2D_DESC sourceDesc{};
			composed->GetDesc(&sourceDesc);
			const auto& destinationDesc = hudlessTexture->desc;
			if (sourceDesc.Width != destinationDesc.Width ||
				sourceDesc.Height != destinationDesc.Height ||
				sourceDesc.MipLevels != destinationDesc.MipLevels ||
				sourceDesc.ArraySize != destinationDesc.ArraySize ||
				sourceDesc.Format != destinationDesc.Format ||
				sourceDesc.SampleDesc.Count != destinationDesc.SampleDesc.Count ||
				sourceDesc.SampleDesc.Quality != destinationDesc.SampleDesc.Quality)
				return nullptr;
			globals::d3d::context->CopyResource(hudlessTexture->resource.get(), composed);
			return hudlessTexture->resource.get();
		}

		return nullptr;
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
			if (!Runtime().Vulkan().DrainCommandRing()) {
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

	Runtime().FrameGeneration().Reconcile();
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

	auto* dxvk = &Runtime().Vulkan();
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

		auto* streamline = &Runtime().Session();
		if (streamline->Initialize()) {
			streamline->SetVulkanDevice();
			StreamlineSession::RegisterDxvkOwnershipPredicate();
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
		auto result = StreamlineSession::EvaluationResult::kFailed;

		if (upscaledTexture && upscaledTexture->resource) {
			StreamlineSession::UpscaleRequest request{
				.resources = { main.texture, upscaledTexture->resource.get(), depthTex.texture, motionVector.texture },
				.dimensions = { (uint32_t)renderSize.x, (uint32_t)renderSize.y,
					(uint32_t)displaySize.x, (uint32_t)displaySize.y },
				.options = { settings.qualityMode, 0.0f, jitter.x, jitter.y },
			};
			switch (method) {
			case UpscaleMethod::kFSR:
				request.upscaler = StreamlineSession::Upscaler::kFSR;
				request.options.sharpness = settings.sharpnessFSR;
				break;
			case UpscaleMethod::kDLSS:
				request.upscaler = StreamlineSession::Upscaler::kDLSS;
				break;
			case UpscaleMethod::kXeSS:
				request.upscaler = StreamlineSession::Upscaler::kXeSS;
				request.options.sharpness = settings.sharpnessFSR;
				break;
			default:
				result = StreamlineSession::EvaluationResult::kSkipped;
				break;
			}
			if (method == UpscaleMethod::kFSR || method == UpscaleMethod::kDLSS || method == UpscaleMethod::kXeSS)
				result = Runtime().EvaluateUpscaler(request);
		}

		if (result == StreamlineSession::EvaluationResult::kReady)
			context->CopyResource(main.texture, upscaledTexture->resource.get());
		else if (result == StreamlineSession::EvaluationResult::kFailed)
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
	if (!Runtime().Vulkan().IsPresenterStateReadyForFrame(hdrActive) ||
		(fgMethod == FrameGenMethod::kFSR &&
		 !Runtime().FrameGeneration().IsFSRPresenterReady()))
		return;

	auto* renderer = globals::game::renderer;
	auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

	if (fgMethod == FrameGenMethod::kDLSSG) {
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
					(void)Runtime().EvaluateUpscaler({
						.upscaler = StreamlineSession::Upscaler::kFSR,
						.resources = { mainColor.texture, upscaledTexture->resource.get(), fgDepth, motionVector.texture },
						.dimensions = { (uint32_t)displaySize.x, (uint32_t)displaySize.y,
							(uint32_t)displaySize.x, (uint32_t)displaySize.y },
						.options = { 0, 0.0f, jitter.x, jitter.y },
					});
			}

			static uint32_t s_lastTagFrame = UINT32_MAX;
			const uint32_t tagFrame = globals::state->frameCount;
			if (s_lastTagFrame != tagFrame) {
				s_lastTagFrame = tagFrame;
				Runtime().Session().TagDLSSGResources(
					fgDepth, motionVector.texture, a_hudlessColor,
					(uint32_t)renderSize.x, (uint32_t)renderSize.y,
					(uint32_t)displaySize.x, (uint32_t)displaySize.y);
			}
			// Enable only after this frame's constants and real input tags exist.
			Runtime().FrameGeneration().EngageDLSSG();
		}
	} else if (fgMethod == FrameGenMethod::kFSR && gameplay) {
		const auto displaySize = float2{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
		const auto renderSize = Util::ConvertToDynamic(displaySize, true);
		auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
		auto& depthCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN_COPY];
		ID3D11Resource* fgDepth = (IsUpscalingActive() && depthCopy.texture) ? depthCopy.texture : depth.texture;
		(void)Runtime().Session().EvaluateFSRFrameGen(
			fgDepth, motionVector.texture, a_hudlessColor,
			(uint32_t)renderSize.x, (uint32_t)renderSize.y,
			(uint32_t)displaySize.x, (uint32_t)displaySize.y,
			jitter.x, jitter.y);
		Runtime().Session().CaptureFSRFrameGenState();
	}
}

void Upscaling::Main_UpdateJitter::thunk(RE::BSGraphics::State* a_state)
{
	auto& upscaling = globals::features::upscaling;
	// Skyrim's main update cadence is decoupled from render submission. Keep
	// Reflex sleep and SimulationStart on this once-per-render-frame boundary.
	const bool wantReflex = upscaling.GetEffectiveReflex();
	const double renderedFpsLimit = upscaling.GetRenderedFrameRateLimit();
	const int outputFpsLimit = upscaling.GetTargetFrameRate();
	// slReflexSleep paces rendered simulation frames, not generated presents.
	// Give its limiter the rendered cadence for fixed frame generation so there
	// is only one pacing controller. The final-output target would otherwise run
	// Reflex at the wrong rate and an additional DXVK limiter would form a second,
	// delayed feedback loop through the asynchronous presenter.
	const double reflexFpsLimit = upscaling.IsFrameGenerationActive() ? renderedFpsLimit : outputFpsLimit;
	const uint32_t reflexLimitUs = (wantReflex && reflexFpsLimit > 0.0) ?
		static_cast<uint32_t>(std::lround(1000000.0 / reflexFpsLimit)) : 0u;
	auto* streamline = &Runtime().Session();
	streamline->UpdateReflex(wantReflex, wantReflex && upscaling.settings.reflexBoost, reflexLimitUs);
	upscaling.ApplyDxvkFrameRateLimit(wantReflex ? 0.0 : renderedFpsLimit);
	streamline->SetPCLMarker(StreamlineSession::PclMarker::SimulationStart);
	upscaling.BeginRenderFrame();
	streamline->BeginRenderFrame();
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

	auto* streamline = &Runtime().Session();
	if (windowUsable && upscaling.GetEffectiveReflex()) {
		static uint32_t s_lastSimEndFrame = UINT32_MAX;
		const uint32_t gameFrame = globals::state->frameCount;
		if (s_lastSimEndFrame != gameFrame) {
			s_lastSimEndFrame = gameFrame;
			streamline->SetPCLMarker(StreamlineSession::PclMarker::SimulationEnd);
			streamline->SetPCLMarker(StreamlineSession::PclMarker::RenderSubmitStart);
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
		const bool presenterReady = Runtime().Vulkan().IsPresenterStateReadyForFrame(hdrActive);
		const bool fsrReady = fgMethod != FrameGenMethod::kFSR ||
		                      Runtime().FrameGeneration().IsFSRPresenterReady();
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
