#include "Upscaling.h"

#include "../I18n/I18n.h"
#include "Deferred.h"
#include "HDRDisplay.h"
#include "Hooks.h"
#include "State.h"
#include "Upscaling/DX12SwapChain.h"
#include "Upscaling/FidelityFX.h"
#include "Upscaling/IntelXeSS.h"
#include "Upscaling/IntelXeSSFrameGeneration.h"
#include "Upscaling/Streamline.h"
#include "Utils/Format.h"
#include "Utils/Game.h"
#include "Utils/UI.h"
#include "Utils/VersionedRelocation.h"
#include <Windows.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <directx/d3dx12.h>
#include <format>

#define I18N_KEY_PREFIX "feature.upscaling."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Upscaling::Settings,
	upscaleMethod,
	upscaleMethodNoDLSS,
	qualityMode,
	qualityModeXeSS,
	frameLimitMode,
	frameGenerationMode,
	frameGenerationForceEnable,
	frameGenerationAllowInMenus,
	frameGenerationMultiplier,
	frameGenerationXeSSUIMode,
	streamlineLogLevel,
	sharpnessFSR,
	sharpnessEnabledDLSS,
	sharpnessDLSS,
	presetDLSS,
	reflexLowLatencyMode,
	reflexLowLatencyBoost,
	reflexUseMarkersToOptimize,
	reflexUseFPSLimit,
	reflexFPSLimit);

decltype(&D3D11CreateDeviceAndSwapChain) ptrD3D11CreateDeviceAndSwapChainUpscaling;

namespace
{
	xess_quality_settings_t ToXeSSQuality(uint32_t a_qualityMode)
	{
		switch (a_qualityMode) {
		case 0:
			return XESS_QUALITY_SETTING_AA;
		case 1:
			return XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS;
		case 2:
			return XESS_QUALITY_SETTING_ULTRA_QUALITY;
		case 3:
			return XESS_QUALITY_SETTING_QUALITY;
		case 4:
			return XESS_QUALITY_SETTING_BALANCED;
		case 5:
			return XESS_QUALITY_SETTING_PERFORMANCE;
		case 6:
		default:
			return XESS_QUALITY_SETTING_ULTRA_PERFORMANCE;
		}
	}

	bool WaitForD3D11Idle()
	{
		if (!globals::d3d::device || !globals::d3d::context)
			return true;

		D3D11_QUERY_DESC queryDesc{};
		queryDesc.Query = D3D11_QUERY_EVENT;
		winrt::com_ptr<ID3D11Query> query;
		if (FAILED(globals::d3d::device->CreateQuery(&queryDesc, query.put()))) {
			logger::error("[XeSS-SR] Failed to create the GPU-idle synchronization query");
			return false;
		}

		globals::d3d::context->End(query.get());
		globals::d3d::context->Flush();
		const ULONGLONG deadline = GetTickCount64() + 5000;
		while (true) {
			const HRESULT result = globals::d3d::context->GetData(query.get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH);
			if (result == S_OK)
				return true;
			if (FAILED(result) || GetTickCount64() >= deadline) {
				logger::error("[XeSS-SR] Timed out waiting for pending GPU work before an SDK lifecycle change");
				return false;
			}
			SwitchToThread();
		}
	}
}

/**
 * @brief Creates a Direct3D 11 device and swap chain, with support for advanced upscaling and frame generation features.
 *
 * This function intercepts the standard D3D11 device and swap chain creation process to enable integration with Streamline and FidelityFX technologies, as well as optional D3D12 proxying for frame generation. It adjusts swap chain flags for tearing support, manages feature checks, and conditionally routes device creation through Streamline or FidelityFX proxies based on runtime settings and hardware capabilities. If frame generation is enabled and supported, a D3D12 proxy is used; otherwise, the standard D3D11 creation path is followed.
 *
 * @return HRESULT indicating the success or failure of device and swap chain creation.
 */
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
	upscaling.LoadUpscalingSDKs();
	upscaling.isIntelAdapter = adapterDesc.VendorId == 0x8086;
	// Which upscaler paths (native XeSS-SR, XMX) apply is a per-GPU and per-driver fact; log both.
	LARGE_INTEGER umdVersion{};
	const bool haveDriverVersion = SUCCEEDED(pAdapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umdVersion));
	logger::info("[Upscaling] Adapter: {} (vendor 0x{:04X}, device 0x{:04X}, {} MB dedicated VRAM, driver {}.{}.{}.{})",
		Util::WStringToString(adapterDesc.Description),
		adapterDesc.VendorId,
		adapterDesc.DeviceId,
		adapterDesc.DedicatedVideoMemory / (1024ull * 1024ull),
		haveDriverVersion ? HIWORD(umdVersion.HighPart) : 0,
		haveDriverVersion ? LOWORD(umdVersion.HighPart) : 0,
		haveDriverVersion ? HIWORD(umdVersion.LowPart) : 0,
		haveDriverVersion ? LOWORD(umdVersion.LowPart) : 0);

	// FLIP_DISCARD requires BufferCount >= 2 and a flip-model-compatible (non-sRGB) format.
	pSwapChainDesc->SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	if (pSwapChainDesc->BufferCount < 2)
		pSwapChainDesc->BufferCount = 2;

	if (globals::features::hdrDisplay.loaded) {
		logger::info("[Upscaling] Requesting R10G10B10A2_UNORM swap chain (was format {}) for the frame-generation proxy; HDR output is decided separately", static_cast<int>(pSwapChainDesc->BufferDesc.Format));
		pSwapChainDesc->BufferDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
	} else if (pSwapChainDesc->BufferDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
		pSwapChainDesc->BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	} else if (pSwapChainDesc->BufferDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
		pSwapChainDesc->BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	}

	auto refreshRate = Upscaling::GetRefreshRate(pSwapChainDesc->OutputWindow);
	upscaling.refreshRate = refreshRate;

	// DLSS availability is not known until after device creation, so prepare the XeSS path
	// when either settings slot asks for it; GetUpscaleMethod picks the authoritative slot later.
	const bool wantsXeSS =
		static_cast<Upscaling::UpscaleMethod>(upscaling.settings.upscaleMethod) == Upscaling::UpscaleMethod::kXESS ||
		static_cast<Upscaling::UpscaleMethod>(upscaling.settings.upscaleMethodNoDLSS) == Upscaling::UpscaleMethod::kXESS;
	// Intel GPUs take the driver's native D3D11 XeSS-SR; everyone else needs the D3D12 library.
	const bool wantsXeSSD3D12 = wantsXeSS && !upscaling.isIntelAdapter;
	const bool frameGenerationEligible = upscaling.settings.frameGenerationMode &&
	                                     (refreshRate >= 120 || upscaling.settings.frameGenerationForceEnable);
	const bool frameGenerationRuntimeAvailable = frameGenerationEligible && upscaling.HasFrameGenModule();
	const bool xessD3D12RuntimeAvailable = wantsXeSSD3D12 && upscaling.intelXeSSD3D12.IsRuntimeAvailable();
	bool shouldProxy = pSwapChainDesc->Windowed && (frameGenerationRuntimeAvailable || xessD3D12RuntimeAvailable);

	upscaling.lowRefreshRate = refreshRate < 120;
	upscaling.isWindowed = pSwapChainDesc->Windowed;
	logger::info(
		"[Upscaling] Display refresh rate {:.2f} Hz (windowed={}, frame generation eligible={}, force enable={}, frame limit={})",
		refreshRate,
		pSwapChainDesc->Windowed != 0,
		frameGenerationEligible,
		upscaling.settings.frameGenerationForceEnable != 0,
		upscaling.settings.frameLimitMode != 0);

	const D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_1;

	if (shouldProxy) {
		logger::info(
			"[Upscaling] Using D3D12 proxy (frame generation={}, cross-vendor XeSS-SR={})",
			frameGenerationRuntimeAvailable,
			xessD3D12RuntimeAvailable);

		{
			DX::ThrowIfFailed(D3D11CreateDevice(
				pAdapter,
				DriverType,
				Software,
				Flags,
				&featureLevel,
				1,
				SDKVersion,
				ppDevice,
				pFeatureLevel,
				ppImmediateContext));

			upscaling.SetProxyD3D11Device(*ppDevice);
			upscaling.SetProxyD3D11DeviceContext(*ppImmediateContext);
			if (upscaling.isIntelAdapter)
				upscaling.intelXeSS.Initialize(*ppDevice);
			if (upscaling.CreateProxySwapChain(pAdapter, *pSwapChainDesc, frameGenerationRuntimeAvailable)) {
				upscaling.CreateProxyInterop();
				upscaling.xessD3D12PathActive = xessD3D12RuntimeAvailable &&
				                                upscaling.intelXeSSD3D12.Initialize(upscaling.dx12SwapChain.d3d12Device.get());

				*ppSwapChain = upscaling.GetProxySwapChain();

				upscaling.d3d12SwapChainActive = true;
				upscaling.activeFrameGenerationMode = frameGenerationRuntimeAvailable ?
				                                          upscaling.settings.frameGenerationMode :
				                                          static_cast<uint>(Upscaling::FrameGenerationMethod::kNONE);

				if (upscaling.IsBackendInitialized()) {
					upscaling.UpgradeBackendInterface((void**)&(*ppDevice));
					// Don't wrap the swap chain with Streamline when using the D3D12
					// proxy. The proxy must remain the outermost layer so GetDevice
					// can continue exposing the D3D11 device to other SKSE plugins.
					upscaling.SetBackendD3DDevice(*ppDevice);
					upscaling.CheckBackendFeatures(pAdapter);
					upscaling.PostBackendDevice();
				}

				return S_OK;
			}

			logger::error("[Upscaling] Failed to initialize the D3D12 proxy; falling back to the native D3D11 swap chain");
			if (frameGenerationEligible && upscaling.settings.frameGenerationMode == static_cast<uint>(Upscaling::FrameGenerationMethod::kXESS))
				upscaling.intelXeSSFrameGenerationMissing = true;
			else if (frameGenerationEligible)
				upscaling.fidelityFXMissing = true;
			if (ppImmediateContext && *ppImmediateContext) {
				(*ppImmediateContext)->Release();
				*ppImmediateContext = nullptr;
			}
			if (ppDevice && *ppDevice) {
				(*ppDevice)->Release();
				*ppDevice = nullptr;
			}
		}
	}

	auto ret = ptrD3D11CreateDeviceAndSwapChainUpscaling(pAdapter,
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

	if (SUCCEEDED(ret) && ppDevice && *ppDevice && upscaling.isIntelAdapter)
		upscaling.intelXeSS.Initialize(*ppDevice);

	if (upscaling.IsBackendInitialized()) {
		upscaling.UpgradeBackendInterface((void**)&(*ppDevice));
		upscaling.UpgradeBackendInterface((void**)&(*ppSwapChain));
		upscaling.SetBackendD3DDevice(*ppDevice);
		// Feature availability (notably Reflex/PCL) is only reliable after device bind.
		upscaling.CheckBackendFeatures(pAdapter);
		upscaling.PostBackendDevice();
	}

	return ret;
}

void Upscaling::DrawSettings()
{
	struct UpscaleModeOption
	{
		UpscaleMethod method;
		std::string label;
	};

	std::vector<UpscaleModeOption> upscaleModes = {
		{ UpscaleMethod::kNONE, T(TKEY("method_none"), "None") },
		{ UpscaleMethod::kTAA, T(TKEY("method_taa"), "TAA") },
		{ UpscaleMethod::kFSR, "AMD FSR 3.1" }
	};
	const bool xessRuntimeAvailable = isIntelAdapter ? intelXeSS.IsRuntimeAvailable() : intelXeSSD3D12.IsRuntimeAvailable();
	if (xessRuntimeAvailable)
		upscaleModes.push_back({ UpscaleMethod::kXESS, T(TKEY("method_xess"), "Intel XeSS-SR") });
	if (streamline.featureDLSS)
		upscaleModes.push_back({ UpscaleMethod::kDLSS, "NVIDIA DLSS" });

	auto* selectedSetting = streamline.featureDLSS ? &settings.upscaleMethod : &settings.upscaleMethodNoDLSS;
	auto selectedMethod = static_cast<UpscaleMethod>(*selectedSetting);
	if (*selectedSetting > static_cast<uint>(UpscaleMethod::kXESS))
		selectedMethod = GetUpscaleMethod();
	int selectedIndex = 0;
	std::vector<const char*> modeLabels;
	modeLabels.reserve(upscaleModes.size());
	for (size_t i = 0; i < upscaleModes.size(); ++i) {
		modeLabels.push_back(upscaleModes[i].label.c_str());
		if (upscaleModes[i].method == selectedMethod)
			selectedIndex = static_cast<int>(i);
	}
	if (ImGui::Combo(T(TKEY("method"), "Method"), &selectedIndex, modeLabels.data(), static_cast<int>(modeLabels.size()))) {
		selectedMethod = upscaleModes[static_cast<size_t>(selectedIndex)].method;
		*selectedSetting = static_cast<uint32_t>(selectedMethod);
		if (selectedMethod != UpscaleMethod::kDLSS)
			settings.upscaleMethodNoDLSS = static_cast<uint32_t>(selectedMethod);
	}

	// Check the current upscale method
	auto upscaleMethod = GetUpscaleMethod();
	if (selectedMethod == UpscaleMethod::kXESS && upscaleMethod != UpscaleMethod::kXESS && xessRuntimeAvailable)
		Util::Text::Warning("%s", T(TKEY("xess_restart_warning"), "XeSS backend change requires a restart."));

	// Display warning for DLSS resolution limits
	if (upscaleMethod == UpscaleMethod::kDLSS) {
		float2 screenSize{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
		if (screenSize.x > streamline.MAX_RESOLUTION || screenSize.y > streamline.MAX_RESOLUTION) {
			Util::Text::Warning("Warning: Requested resolution %.0f x %.0f exceeds maximum supported resolution %d x %d for DLSS.",
				screenSize.x, screenSize.y, streamline.MAX_RESOLUTION, streamline.MAX_RESOLUTION);
			Util::Text::Warning("DLSS will not function. Lower your resolution or select a different upscaling method.");
		}
	}
	if (upscaleMethod == UpscaleMethod::kXESS &&
		(xessD3D12PathActive ? intelXeSSD3D12.oldDriverWarning : intelXeSS.oldDriverWarning))
		Util::Text::Warning("%s", T(TKEY("xess_old_driver_warning"), "Warning: XeSS supports this GPU, but Intel recommends a newer graphics driver."));

	// Display upscaling settings if applicable
	if (upscaleMethod != UpscaleMethod::kNONE && upscaleMethod != UpscaleMethod::kTAA) {
		const char* upscalePresetsDLSS[] = {
			T(TKEY("preset_ultra_performance"), "Ultra Performance"),
			T(TKEY("preset_performance"), "Performance"),
			T(TKEY("preset_balanced"), "Balanced"),
			T(TKEY("preset_quality"), "Quality"),
			T(TKEY("preset_dlaa"), "DLAA")
		};
		const char* upscalePresets[] = {
			T(TKEY("preset_ultra_performance"), "Ultra Performance"),
			T(TKEY("preset_performance"), "Performance"),
			T(TKEY("preset_balanced"), "Balanced"),
			T(TKEY("preset_quality"), "Quality"),
			T(TKEY("preset_native_aa"), "Native AA")
		};
		const char* upscalePresetsXeSS[] = {
			T(TKEY("preset_ultra_performance"), "Ultra Performance"),
			T(TKEY("preset_performance"), "Performance"),
			T(TKEY("preset_balanced"), "Balanced"),
			T(TKEY("preset_quality"), "Quality"),
			T(TKEY("preset_ultra_quality"), "Ultra Quality"),
			T(TKEY("preset_ultra_quality_plus"), "Ultra Quality Plus"),
			T(TKEY("preset_native_aa"), "Native AA")
		};

		// Compute a safe preset index (4 - qualityMode) clamped to [0,4] to avoid negative/overflow indexing
		int presetIndex = 0;
		if (upscaleMethod == UpscaleMethod::kXESS) {
			if (settings.qualityModeXeSS <= 6)
				presetIndex = 6 - static_cast<int>(settings.qualityModeXeSS);
			presetIndex = std::clamp(presetIndex, 0, 6);
		} else {
			if (settings.qualityMode <= 4)
				presetIndex = 4 - static_cast<int>(settings.qualityMode);
			presetIndex = std::clamp(presetIndex, 0, 4);
		}

		// Choose preset name set and the corresponding scales once, then show a
		// single SliderInt to avoid duplicated calls.
		const char* baseLabel = nullptr;

		if (upscaleMethod == UpscaleMethod::kXESS) {
			baseLabel = upscalePresetsXeSS[presetIndex];
		} else if (upscaleMethod == UpscaleMethod::kFSR) {
			baseLabel = upscalePresets[presetIndex];
		} else if (upscaleMethod == UpscaleMethod::kDLSS) {
			baseLabel = upscalePresetsDLSS[presetIndex];
		}

		if (baseLabel) {
			// Format the label with preset name and resolution scale
			std::string labelWithScale = std::format("{} ( {:.2f}x )", baseLabel, (resolutionScale.x + resolutionScale.y) * 0.5f);

			if (upscaleMethod == UpscaleMethod::kXESS)
				ImGui::SliderInt(T(TKEY("upscale_preset"), "Upscale Preset"), (int*)&settings.qualityModeXeSS, 0, 6, labelWithScale.c_str());
			else
				ImGui::SliderInt(T(TKEY("upscale_preset"), "Upscale Preset"), (int*)&settings.qualityMode, 0, 4, labelWithScale.c_str());
		}

		if (upscaleMethod == UpscaleMethod::kFSR) {
			ImGui::SliderFloat(T(TKEY("sharpness"), "Sharpness"), &settings.sharpnessFSR, 0.0f, 1.0f, "%.1f");
		} else if (upscaleMethod == UpscaleMethod::kDLSS) {
			ImGui::Checkbox(T(TKEY("enable_sharpening"), "Enable Sharpening"), &settings.sharpnessEnabledDLSS);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("%s", T(TKEY("enable_sharpening_tooltip"),
									  "Applies RCAS sharpening to the DLSS output.\n"
									  "Off by default; DLSS already resolves a sharp image."));
			}

			if (settings.sharpnessEnabledDLSS)
				ImGui::SliderFloat(T(TKEY("sharpness"), "Sharpness"), &settings.sharpnessDLSS, 0.0f, 1.0f, "%.1f");

			const char* presets[] = {
				T(TKEY("dlss_model_preset_default"), "Default"),
				T(TKEY("dlss_model_preset_j"), "Preset J"),
				T(TKEY("dlss_model_preset_k"), "Preset K"),
				T(TKEY("dlss_model_preset_l"), "Preset L"),
				T(TKEY("dlss_model_preset_m"), "Preset M")
			};
			ImGui::Combo(T(TKEY("dlss_model_preset"), "DLSS Model Preset"), (int*)&settings.presetDLSS, presets, 5);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("%s", T(TKEY("dlss_model_preset_tooltip"),
									  "Choose which DLSS AI model preset to use.\n"
									  "Each model offers different visual quality, performance, and motion stability.\n"
									  "Set to 'Default' for automatic selection based on your Upscale Preset and hardware.\n"
									  "Changing this setting requires a restart to take effect."));
			}
		}
	}

	const bool frameGenerationDx12PathActive = IsFrameGenerationDx12PathActive();

	if (ImGui::TreeNodeEx(T(TKEY("frame_generation"), "Frame Generation"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text("%s", T(TKEY("frame_generation_desc"),
							  "Frame Generation interpolates real frames with generated ones for a smoother experience"));

		const char* frameGenerationProviders[] = {
			T(TKEY("frame_generation_off"), "Off"),
			T(TKEY("frame_generation_fsr_provider"), "AMD FSR Frame Generation"),
			T(TKEY("frame_generation_xess_provider"), "Intel XeSS-FG with XeLL")
		};
		int frameGenerationProvider = std::clamp(static_cast<int>(settings.frameGenerationMode), 0, 2);
		if (ImGui::Combo(T(TKEY("frame_generation_provider"), "Technology"), &frameGenerationProvider, frameGenerationProviders, IM_ARRAYSIZE(frameGenerationProviders)))
			settings.frameGenerationMode = static_cast<uint>(frameGenerationProvider);

		if (settings.frameGenerationMode == static_cast<uint>(FrameGenerationMethod::kXESS))
			ImGui::Text("%s", T(TKEY("frame_generation_xess_tech"), "Uses Intel XeSS Frame Generation with mandatory XeLL latency reduction"));
		else if (settings.frameGenerationMode == static_cast<uint>(FrameGenerationMethod::kFSR))
			ImGui::Text("%s", T(TKEY("frame_generation_fsr_tech"), "Uses AMD FSR Frame Generation technology"));

		if (settings.frameGenerationMode == static_cast<uint>(FrameGenerationMethod::kXESS)) {
			// The proxy swap chain is created with the adapter maximum, so the multiplier can be
			// changed live. Before it exists, offer the full range the SDK can support.
			const uint reportedMax = intelXeSSFrameGeneration.GetMaxInterpolatedFrames();
			const uint supportedMultiplier = reportedMax ?
			                                     std::min<uint>(kMaxFrameGenerationMultiplier, 1 + reportedMax) :
			                                     kMaxFrameGenerationMultiplier;

			const char* multipliers[] = {
				T(TKEY("frame_generation_multiplier_2x"), "2x (1 generated frame)"),
				T(TKEY("frame_generation_multiplier_3x"), "3x (2 generated frames)"),
				T(TKEY("frame_generation_multiplier_4x"), "4x (3 generated frames)")
			};
			const int optionCount = static_cast<int>(supportedMultiplier - kMinFrameGenerationMultiplier) + 1;
			int multiplierIndex = std::clamp(static_cast<int>(settings.frameGenerationMultiplier) - static_cast<int>(kMinFrameGenerationMultiplier), 0, optionCount - 1);
			ImGui::Combo(T(TKEY("frame_generation_multiplier"), "Multi Frame Generation"), &multiplierIndex, multipliers, optionCount);
			// Written back unconditionally so a saved multiplier the adapter cannot reach is clamped here too.
			settings.frameGenerationMultiplier = static_cast<uint>(multiplierIndex) + kMinFrameGenerationMultiplier;
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted(T(TKEY("frame_generation_multiplier_tooltip_1"), "How many frames are presented for every frame the game renders."));
				ImGui::TextUnformatted(T(TKEY("frame_generation_multiplier_tooltip_2"), "Higher multipliers are smoother but generate more of the image, which adds latency and artifacts."));
				ImGui::TextUnformatted(T(TKEY("frame_generation_multiplier_tooltip_3"), "With Frame Limit enabled the rendered frame rate is divided by the multiplier, so the presented rate stays at your refresh rate."));
			}
			if (reportedMax && supportedMultiplier < kMaxFrameGenerationMultiplier) {
				const std::string cappedNote = I18n::GetSingleton()->Format(
					TKEY("frame_generation_multiplier_capped"),
					{ { "multiplier", std::to_string(supportedMultiplier) } },
					"This GPU supports up to {multiplier}x frame generation.");
				ImGui::TextDisabled("%s", cappedNote.c_str());
			}

			// Indices match xefg_swapchain_ui_mode_t so the setting stores the SDK value directly.
			const char* uiModes[] = {
				T(TKEY("frame_generation_xess_ui_mode_auto"), "Auto (SDK decides)"),
				T(TKEY("frame_generation_xess_ui_mode_none"), "Interpolate UI (no composition)"),
				T(TKEY("frame_generation_xess_ui_mode_backbuffer_ui"), "Back buffer + UI texture"),
				T(TKEY("frame_generation_xess_ui_mode_hudless_ui"), "HUD-less + UI texture"),
				T(TKEY("frame_generation_xess_ui_mode_backbuffer_hudless"), "HUD-less + extract UI from back buffer"),
				T(TKEY("frame_generation_xess_ui_mode_backbuffer_hudless_ui"), "HUD-less + UI texture + back buffer refinement")
			};
			int uiModeIndex = std::clamp(static_cast<int>(settings.frameGenerationXeSSUIMode), 0, IM_ARRAYSIZE(uiModes) - 1);
			if (ImGui::Combo(T(TKEY("frame_generation_xess_ui_mode"), "XeSS-FG UI Handling"), &uiModeIndex, uiModes, IM_ARRAYSIZE(uiModes)))
				settings.frameGenerationXeSSUIMode = static_cast<uint>(uiModeIndex);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted(T(TKEY("frame_generation_xess_ui_mode_tooltip_1"), "How XeSS-FG puts the HUD onto generated frames."));
				ImGui::TextUnformatted(T(TKEY("frame_generation_xess_ui_mode_tooltip_2"), "'Interpolate UI' is Intel's default and never depends on the UI texture; the composition modes keep the HUD sharper but rely on it."));
				ImGui::TextUnformatted(T(TKEY("frame_generation_xess_ui_mode_tooltip_3"), "Intel recommends the back buffer refinement mode for HDR10 output, where the UI texture only has 2-bit alpha."));
			}
			if (frameGenerationDx12PathActive && settings.frameGenerationXeSSUIMode != intelXeSSFrameGeneration.GetUiMode())
				Util::Text::Warning("%s", T(TKEY("frame_generation_xess_ui_mode_restart"), "Warning: UI handling change requires restart"));
		}

		if (settings.frameGenerationMode && HasFrameGenModule())
			ImGui::Text("%s", T(TKEY("frame_generation_available"), "The selected Frame Generation runtime is available."));
		ImGui::Text("%s", T(TKEY("frame_generation_proxy_note"),
							  "Requires a D3D11 to D3D12 proxy which can create compatibility issues"));
		ImGui::Text("%s", T(TKEY("frame_generation_restart_note"),
							  "Changing this setting requires a restart to work correctly"));

		bool onlyRequiresRestart = true;

		if (!isWindowed) {
			Util::Text::Warning("Warning: Requires windowed mode");

			onlyRequiresRestart = false;
		}

		if (lowRefreshRate && !settings.frameGenerationForceEnable) {
			Util::Text::Warning("Warning: Requires a high refresh rate monitor or Force Enable Frame Generation");

			onlyRequiresRestart = false;
		}

		if (settings.frameGenerationMode == static_cast<uint>(FrameGenerationMethod::kFSR) && (fidelityFXMissing || !fidelityFX.featureFSR3FG)) {
			Util::Text::Warning("Warning: FidelityFX DLLs are not loaded");

			onlyRequiresRestart = false;
		}
		if (settings.frameGenerationMode == static_cast<uint>(FrameGenerationMethod::kXESS) && (intelXeSSFrameGenerationMissing || !intelXeSSFrameGeneration.IsAvailable())) {
			Util::Text::Warning("%s", T(TKEY("frame_generation_xess_missing"), "Warning: XeSS-FG or XeLL DLLs are not loaded"));
			onlyRequiresRestart = false;
		}

		if (onlyRequiresRestart && settings.frameGenerationMode != activeFrameGenerationMode)
			Util::Text::Warning("Warning: Requires restart");

		if (!frameGenerationDx12PathActive)
			ImGui::BeginDisabled();

		bool flEnabled = settings.frameLimitMode != 0;
		if (ImGui::Checkbox(T(TKEY("frame_limit_vrr"), "Frame Limit (Variable Refresh Rate)"), &flEnabled))
			settings.frameLimitMode = flEnabled ? 1 : 0;

		if (!frameGenerationDx12PathActive)
			ImGui::EndDisabled();

		ImGui::TextWrapped("Allows frame generation to function on low refresh rate monitors. Detected: %.2f Hz", refreshRate);
		bool fgForce = settings.frameGenerationForceEnable != 0;
		if (ImGui::Checkbox(T(TKEY("force_enable_frame_generation"), "Force Enable Frame Generation"), &fgForce))
			settings.frameGenerationForceEnable = fgForce ? 1 : 0;

		ImGui::Checkbox(T(TKEY("frame_generation_in_menus"), "Frame Generation in Menus"), &settings.frameGenerationAllowInMenus);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T(TKEY("frame_generation_in_menus_tooltip_1"), "Keeps frame generation active while game menus are open."));
			ImGui::TextUnformatted(T(TKEY("frame_generation_in_menus_tooltip_2"), "May feel smoother, but increases menu input latency."));
		}

		ImGui::TreePop();
	}

	if (streamline.reflexSupportedOnCurrentAdapter && ImGui::TreeNodeEx(T(TKEY("nvidia_reflex"), "NVIDIA Reflex"), ImGuiTreeNodeFlags_DefaultOpen)) {
		const bool reflexBlockedByFrameGeneration = frameGenerationDx12PathActive;
		const bool reflexAvailable = streamline.initialized && streamline.featureReflex;
		const bool reflexControlsAvailable = reflexAvailable && !reflexBlockedByFrameGeneration;
		const bool markerOptimizationAvailable = reflexControlsAvailable && streamline.featurePCL;
		if (reflexBlockedByFrameGeneration) {
			ImGui::TextDisabled("%s", T(TKEY("reflex_blocked_by_fg"), "Reflex is unavailable while the DX12 frame-generation swapchain is active."));
		}

		if (!reflexAvailable) {
			ImGui::TextDisabled("%s", T(TKEY("reflex_not_available"), "Reflex is not available. Ensure sl.reflex.dll is present and restart."));
		}

		if (!reflexControlsAvailable)
			ImGui::BeginDisabled();

		ImGui::Checkbox(T(TKEY("low_latency_mode"), "Low Latency Mode"), &settings.reflexLowLatencyMode);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T(TKEY("low_latency_mode_tooltip_1"), "Cuts input delay by syncing CPU work closer to the GPU."));
			ImGui::TextUnformatted(T(TKEY("low_latency_mode_tooltip_2"), "Can reduce max FPS a little, but usually feels more responsive."));
		}

		if (!settings.reflexLowLatencyMode)
			ImGui::BeginDisabled();

		ImGui::Checkbox(T(TKEY("low_latency_boost"), "Low Latency Boost"), &settings.reflexLowLatencyBoost);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T(TKEY("low_latency_boost_tooltip_1"), "Keeps GPU clocks higher to avoid latency spikes at low GPU load."));
			ImGui::TextUnformatted(T(TKEY("low_latency_boost_tooltip_2"), "Useful if frametime jumps; costs extra power and heat."));
		}

		if (!markerOptimizationAvailable)
			ImGui::BeginDisabled();

		ImGui::Checkbox(T(TKEY("use_markers_to_optimize"), "Use Markers To Optimize"), &settings.reflexUseMarkersToOptimize);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T(TKEY("use_markers_to_optimize_tooltip_1"), "Uses frame markers for tighter Reflex timing."));
			ImGui::TextUnformatted(T(TKEY("use_markers_to_optimize_tooltip_2"), "Try On first; turn Off if it causes stutter on your setup."));
		}

		if (!markerOptimizationAvailable)
			ImGui::EndDisabled();

		if (!markerOptimizationAvailable) {
			ImGui::TextDisabled("%s", T(TKEY("marker_optimization_unavailable"), "Marker optimization unavailable (PCL not loaded)."));
		}

		ImGui::Checkbox(T(TKEY("use_fps_limit"), "Use FPS Limit"), &settings.reflexUseFPSLimit);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T(TKEY("use_fps_limit_tooltip_1"), "Uses Reflex's internal FPS cap for steadier frametimes."));
			ImGui::TextUnformatted(T(TKEY("use_fps_limit_tooltip_2"), "Can lower latency versus uncapped rendering."));
		}

		if (!settings.reflexLowLatencyMode)
			ImGui::EndDisabled();

		if (!settings.reflexUseFPSLimit)
			ImGui::BeginDisabled();

		if (!std::isfinite(settings.reflexFPSLimit))
			settings.reflexFPSLimit = 60.0f;
		settings.reflexFPSLimit = std::clamp(settings.reflexFPSLimit, 20.0f, 240.0f);
		ImGui::SliderFloat(T(TKEY("fps_limit"), "FPS Limit"), &settings.reflexFPSLimit, 20.0f, 240.0f, "%.0f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T(TKEY("fps_limit_tooltip_1"), "Set your frame cap target."));
			ImGui::TextUnformatted(T(TKEY("fps_limit_tooltip_2"), "Start about 2-3 FPS below refresh rate (e.g. 117 for 120 Hz)."));
		}

		if (!settings.reflexUseFPSLimit)
			ImGui::EndDisabled();

		if (!reflexControlsAvailable)
			ImGui::EndDisabled();

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx(T(TKEY("backend_diagnostics"), "Backend Diagnostics"))) {
		// Streamline log level selection
		const char* logLevels[] = { "Off", "Default", "Verbose" };
		int logLevelIdx = static_cast<int>(settings.streamlineLogLevel);
		if (ImGui::Combo(T(TKEY("streamline_logging"), "Streamline Logging"), &logLevelIdx, logLevels, IM_ARRAYSIZE(logLevels))) {
			settings.streamlineLogLevel = static_cast<uint>(logLevelIdx);
		}
		ImGui::TextUnformatted(T(TKEY("streamline_logging_restart_note"), "Changing this requires a restart to take effect."));
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("streamline_logging_tooltip"), "Streamline logging controls the verbosity of NVIDIA Streamline backend logs. Useful for debugging issues with DLSS/DLSS-G."));
		}

		ImGui::Separator();
		Util::DrawDllVersionTable("AMD FidelityFX DLLs (click to open folder)", FidelityFX::PluginDir, FidelityFX::dllVersions, "ffx_dll_versions");
		Util::DrawDllVersionTable("NVIDIA Streamline DLLs (click to open folder)", Streamline::PluginDir, Streamline::dllVersions, "sl_dll_versions");
		Util::DrawDllVersionTable(T(TKEY("xess_dll_versions"), "Intel XeSS/XeLL DLLs (click to open folder)"), IntelXeSS::PluginDir, IntelXeSS::dllVersions, "xess_dll_versions");
		ImGui::TreePop();
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

	// Sanitize loaded settings to ensure enum indices are valid
	constexpr auto enumCount = 5;
	if (settings.upscaleMethod >= static_cast<uint>(enumCount)) {
		logger::warn("[Upscaling] Loaded upscaleMethod {} out of range, clamping to {}", settings.upscaleMethod, enumCount ? enumCount - 1 : 0);
		settings.upscaleMethod = enumCount ? enumCount - 1 : 0;
	}
	if (settings.upscaleMethodNoDLSS >= static_cast<uint>(enumCount)) {
		logger::warn("[Upscaling] Loaded upscaleMethodNoDLSS {} out of range, clamping to {}", settings.upscaleMethodNoDLSS, enumCount ? enumCount - 1 : 0);
		settings.upscaleMethodNoDLSS = enumCount ? enumCount - 1 : 0;
	}
	if (settings.qualityMode > 4) {
		logger::warn("[Upscaling] Loaded qualityMode {} out of range, resetting to Quality", settings.qualityMode);
		settings.qualityMode = 1;
	}
	if (settings.qualityModeXeSS > 6) {
		logger::warn("[Upscaling] Loaded qualityModeXeSS {} out of range, resetting to Quality", settings.qualityModeXeSS);
		settings.qualityModeXeSS = 3;
	}
	if (settings.presetDLSS > 4) {
		logger::warn("[Upscaling] Loaded presetDLSS {} out of range, resetting to 0 (Default)", settings.presetDLSS);
		settings.presetDLSS = 0;
	}
	if (settings.frameGenerationMode > static_cast<uint>(FrameGenerationMethod::kXESS)) {
		logger::warn("[Upscaling] Loaded frameGenerationMode {} out of range, resetting to AMD FSR Frame Generation", settings.frameGenerationMode);
		settings.frameGenerationMode = static_cast<uint>(FrameGenerationMethod::kFSR);
	}
	const uint clampedMultiplier = std::clamp(settings.frameGenerationMultiplier, kMinFrameGenerationMultiplier, kMaxFrameGenerationMultiplier);
	if (clampedMultiplier != settings.frameGenerationMultiplier) {
		logger::warn("[Upscaling] Loaded frameGenerationMultiplier {} out of range, clamping to {}", settings.frameGenerationMultiplier, clampedMultiplier);
		settings.frameGenerationMultiplier = clampedMultiplier;
	}
	if (settings.frameGenerationXeSSUIMode > static_cast<uint>(XEFG_SWAPCHAIN_UI_MODE_BACKBUFFER_HUDLESS_UITEXTURE)) {
		logger::warn("[Upscaling] Loaded frameGenerationXeSSUIMode {} out of range, resetting to HUD-less + UI texture", settings.frameGenerationXeSSUIMode);
		settings.frameGenerationXeSSUIMode = static_cast<uint>(XEFG_SWAPCHAIN_UI_MODE_HUDLESS_UITEXTURE);
	}
	const float originalReflexFPSLimit = settings.reflexFPSLimit;
	if (!std::isfinite(settings.reflexFPSLimit)) {
		settings.reflexFPSLimit = 60.0f;
		logger::warn(
			"[Upscaling] Loaded reflexFPSLimit {} is not finite, resetting to {}",
			originalReflexFPSLimit,
			settings.reflexFPSLimit);
	}
	const float clampedReflexFPSLimit = std::clamp(settings.reflexFPSLimit, 20.0f, 240.0f);
	if (clampedReflexFPSLimit != settings.reflexFPSLimit) {
		logger::warn(
			"[Upscaling] Loaded reflexFPSLimit {} out of range, clamping to {}",
			settings.reflexFPSLimit,
			clampedReflexFPSLimit);
	}
	settings.reflexFPSLimit = clampedReflexFPSLimit;
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
	// PostPostLoad can run before the UI event source is available. Retry here;
	// registration is idempotent so normal startup does not add the sink twice.
	MenuOpenCloseEventHandler::Register();

	// Fix screenshots fix from Engine Fixes
	Util::DisableVanillaTAA();

	// The game defaults this to a non-zero value
	static auto fDRClampOffset = RE::GetINISetting("fDRClampOffset:Display");
	fDRClampOffset->data.f = 0.0f;
}

void Upscaling::Load()
{
	*(uintptr_t*)&ptrD3D11CreateDeviceAndSwapChainUpscaling = SKSE::PatchIAT(hk_D3D11CreateDeviceAndSwapChainUpscaling, "d3d11.dll", "D3D11CreateDeviceAndSwapChain");
}

struct BSImageSpace_Init_FXAA
{
	static void thunk()
	{
		func();

		// Force FXAA off safely
		auto fxaaEnabled = reinterpret_cast<bool*>(REL::RelocationID(513281, 391028).address());
		*fxaaEnabled = false;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};
void Upscaling::PostPostLoad()
{
	MenuOpenCloseEventHandler::Register();

	bool isGOG = !GetModuleHandle(L"steam_api64.dll");
	stl::detour_thunk<MenuManagerDrawInterfaceStartHook>(REL::RelocationID(79947, 82084));

	// Calculates resolution and jitter
	stl::write_thunk_call<Main_UpdateJitter>(REL::RelocationID(75460, 77245).address() + Util::VersionedRelocation::Select(0xE5, isGOG ? 0x133 : 0xE2, 0x133));

	// Disables the original dynamic resolution system
	REL::safe_write(REL::RelocationID(35556, 36555).address() + REL::Relocate(0x2D, 0x2D), REL::NOP5, sizeof(REL::NOP5));

	// Performs upscaling in between volumetric lighting and post processing
	stl::write_thunk_call<Main_PostProcessing>(REL::RelocationID(100430, 107148).address() + REL::Relocate(0x1F0, 0x1E7));

	// Patches RSSetScissorRect calls to use dynamic resolution
	stl::detour_thunk<SetScissorRect>(REL::RelocationID(75564, 77365));

	// Patches facegen texture generation to not use dynamic resolution
	stl::detour_thunk<BSFaceGenManager_UpdatePendingCustomizationTextures>(REL::RelocationID(26455, 27041));

	// Patches precipitation camera to not use dynamic resolution
	stl::write_thunk_call<Main_RenderPrecipitation>(REL::RelocationID(35560, 36559).address() + Util::VersionedRelocation::Select(0x3A1, 0x3A1, 0x3BF));

	// Forces FXAA off
	stl::detour_thunk<BSImageSpace_Init_FXAA>(REL::RelocationID(98974, 105626));

	logger::info("[Upscaling] Installed hooks");
}

RE::BSEventNotifyControl Upscaling::MenuOpenCloseEventHandler::ProcessEvent(
	const RE::MenuOpenCloseEvent* a_event,
	RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	if (a_event && a_event->menuName == RE::LoadingMenu::MENU_NAME && !a_event->opening) {
		globals::features::upscaling.pendingXeSSReset.store(true, std::memory_order_release);
		globals::features::upscaling.pendingXeSSFrameGenerationReset.store(true, std::memory_order_release);
	}
	return RE::BSEventNotifyControl::kContinue;
}

bool Upscaling::MenuOpenCloseEventHandler::Register()
{
	static MenuOpenCloseEventHandler singleton;
	static bool registered = false;
	if (registered)
		return true;

	auto* ui = globals::game::ui;
	if (!ui) {
		logger::error("[Upscaling] UI event source not found; temporal history resets on loading transitions are unavailable");
		return false;
	}
	auto* source = ui->GetEventSource<RE::MenuOpenCloseEvent>();
	if (!source)
		return false;
	source->AddEventSink(&singleton);
	registered = true;
	logger::info("[Upscaling] Registered MenuOpenCloseEventHandler");
	return true;
}

#undef I18N_KEY_PREFIX

bool Upscaling::XeSSSharesFrameGenerationInputs() const
{
	return d3d12SwapChainActive && !xessD3D12PathActive &&
	       dx12SwapChain.depthBufferShared12 && dx12SwapChain.depthBufferShared12->uav &&
	       dx12SwapChain.motionVectorBufferShared12 && dx12SwapChain.motionVectorBufferShared12->uav;
}

ID3D11Resource* Upscaling::GetXeSSDepthResource() const
{
	if (XeSSSharesFrameGenerationInputs())
		return dx12SwapChain.depthBufferShared12->resource11;
	return xessDepthTexture ? xessDepthTexture->resource.get() : nullptr;
}

ID3D11UnorderedAccessView* Upscaling::GetXeSSDepthUAV() const
{
	if (XeSSSharesFrameGenerationInputs())
		return dx12SwapChain.depthBufferShared12->uav;
	return xessDepthTexture ? xessDepthTexture->uav.get() : nullptr;
}

Upscaling::UpscaleMethod Upscaling::GetUpscaleMethod() const
{
	const auto isAvailable = [this](UpscaleMethod a_method) {
		switch (a_method) {
		case UpscaleMethod::kDLSS:
			return streamline.featureDLSS;
		case UpscaleMethod::kXESS:
			return xessD3D12PathActive ? intelXeSSD3D12.available : intelXeSS.available;
		case UpscaleMethod::kNONE:
		case UpscaleMethod::kTAA:
		case UpscaleMethod::kFSR:
			return true;
		default:
			return false;
		}
	};

	// The menu edits upscaleMethod only when DLSS exists and upscaleMethodNoDLSS otherwise, so
	// the slot it shows must be the slot that decides. Consulting upscaleMethod first on a
	// non-DLSS machine let a stale FSR/TAA value in that slot silently override the user's
	// XeSS choice, with the menu insisting a restart would fix it.
	const bool dlssAvailable = streamline.featureDLSS;
	const auto configured = static_cast<UpscaleMethod>(dlssAvailable ? settings.upscaleMethod : settings.upscaleMethodNoDLSS);
	UpscaleMethod resolved = UpscaleMethod::kFSR;
	if (isAvailable(configured)) {
		resolved = configured;
	} else if (dlssAvailable && isAvailable(static_cast<UpscaleMethod>(settings.upscaleMethodNoDLSS))) {
		resolved = static_cast<UpscaleMethod>(settings.upscaleMethodNoDLSS);
	}

	// Logged once per change so a fallback shows up in the log instead of only in the image.
	static UpscaleMethod lastLoggedConfigured = static_cast<UpscaleMethod>(~0u);
	static UpscaleMethod lastLoggedResolved = static_cast<UpscaleMethod>(~0u);
	if (configured != lastLoggedConfigured || resolved != lastLoggedResolved) {
		lastLoggedConfigured = configured;
		lastLoggedResolved = resolved;
		if (resolved != configured) {
			logger::warn("[Upscaling] Configured upscaler {} is unavailable; using {} (DLSS available={})",
				static_cast<uint>(configured), static_cast<uint>(resolved), dlssAvailable);
		} else {
			logger::info("[Upscaling] Active upscaler: {} (DLSS available={})", static_cast<uint>(resolved), dlssAvailable);
		}
	}
	return resolved;
}

void Upscaling::CreateUpscalingTextureResources(UpscaleMethod a_upscalemethod)
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
	const auto matchesTexture = [](const Texture2D* a_texture, const D3D11_TEXTURE2D_DESC& a_desc) {
		if (!a_texture)
			return false;
		const auto& current = a_texture->desc;
		return current.Width == a_desc.Width &&
		       current.Height == a_desc.Height &&
		       current.MipLevels == a_desc.MipLevels &&
		       current.ArraySize == a_desc.ArraySize &&
		       current.Format == a_desc.Format &&
		       current.SampleDesc.Count == a_desc.SampleDesc.Count &&
		       current.SampleDesc.Quality == a_desc.SampleDesc.Quality &&
		       current.BindFlags == a_desc.BindFlags;
	};

	if (a_upscalemethod == UpscaleMethod::kDLSS || a_upscalemethod == UpscaleMethod::kFSR || a_upscalemethod == UpscaleMethod::kXESS) {
		texDesc.Format = DXGI_FORMAT_R8_UNORM;
		srvDesc.Format = texDesc.Format;
		uavDesc.Format = texDesc.Format;
		if (reactiveMaskTexture && !matchesTexture(reactiveMaskTexture, texDesc)) {
			delete reactiveMaskTexture;
			reactiveMaskTexture = nullptr;
		}
		if (transparencyCompositionMaskTexture && !matchesTexture(transparencyCompositionMaskTexture, texDesc)) {
			delete transparencyCompositionMaskTexture;
			transparencyCompositionMaskTexture = nullptr;
		}

		if (!reactiveMaskTexture) {
			reactiveMaskTexture = new Texture2D(texDesc);
			reactiveMaskTexture->CreateSRV(srvDesc);
			reactiveMaskTexture->CreateUAV(uavDesc);
		}

		if (!transparencyCompositionMaskTexture) {
			transparencyCompositionMaskTexture = new Texture2D(texDesc);
			transparencyCompositionMaskTexture->CreateSRV(srvDesc);
			transparencyCompositionMaskTexture->CreateUAV(uavDesc);
		}
	}

	if (a_upscalemethod == UpscaleMethod::kXESS) {
		main.texture->GetDesc(&texDesc);
		texDesc.Format = DXGI_FORMAT_R32_FLOAT;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		// The private depth input is only needed when XeSS-SR cannot read the shared FG buffer.
		if (xessDepthTexture && (XeSSSharesFrameGenerationInputs() || !matchesTexture(xessDepthTexture, texDesc))) {
			delete xessDepthTexture;
			xessDepthTexture = nullptr;
		}
		if (!xessDepthTexture && !XeSSSharesFrameGenerationInputs()) {
			srvDesc = {};
			srvDesc.Format = texDesc.Format;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = 1;

			uavDesc = {};
			uavDesc.Format = texDesc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;

			xessDepthTexture = new Texture2D(texDesc);
			xessDepthTexture->CreateSRV(srvDesc);
			xessDepthTexture->CreateUAV(uavDesc);
			Util::SetResourceName(xessDepthTexture->resource.get(), "XeSS_InputDepth");
		}

		if (!xessD3D12PathActive) {
			main.texture->GetDesc(&texDesc);
			texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			if (xessOutputTexture && !matchesTexture(xessOutputTexture, texDesc)) {
				delete xessOutputTexture;
				xessOutputTexture = nullptr;
			}
			if (!xessOutputTexture) {
				srvDesc = {};
				srvDesc.Format = texDesc.Format;
				srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				srvDesc.Texture2D.MostDetailedMip = 0;
				srvDesc.Texture2D.MipLevels = 1;

				uavDesc = {};
				uavDesc.Format = texDesc.Format;
				uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
				uavDesc.Texture2D.MipSlice = 0;

				xessOutputTexture = new Texture2D(texDesc);
				xessOutputTexture->CreateSRV(srvDesc);
				xessOutputTexture->CreateUAV(uavDesc);
				Util::SetResourceName(xessOutputTexture->resource.get(), "XeSS_OutputColor");
			}
		}
	}

	// Motion vector copy texture is only needed for DLSS
	if (a_upscalemethod == UpscaleMethod::kDLSS) {
		if (!motionVectorCopyTexture) {
			auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

			D3D11_TEXTURE2D_DESC motionTexDesc{};
			motionVector.texture->GetDesc(&motionTexDesc);

			texDesc.Format = motionTexDesc.Format;
			srvDesc.Format = texDesc.Format;
			uavDesc.Format = texDesc.Format;

			motionVectorCopyTexture = new Texture2D(motionTexDesc);
			motionVectorCopyTexture->CreateSRV(srvDesc);
			motionVectorCopyTexture->CreateUAV(uavDesc);
		}

		// RCAS sharpener texture - matches kMAIN format for HDR sharpening
		if (!sharpenerTexture) {
			main.texture->GetDesc(&texDesc);
			main.SRV->GetDesc(&srvDesc);

			texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

			srvDesc.Format = texDesc.Format;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = 1;

			uavDesc.Format = texDesc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;

			sharpenerTexture = new Texture2D(texDesc);
			sharpenerTexture->CreateSRV(srvDesc);
			sharpenerTexture->CreateUAV(uavDesc);
		}
	}
}

void Upscaling::DestroyUpscalingTextureResources(UpscaleMethod a_upscalemethod)
{
	logger::debug("[Upscaling] Destroying texture resources for method {} ({})", static_cast<int>(a_upscalemethod), magic_enum::enum_name(a_upscalemethod));

	// Clean up D3D11 textures that are no longer needed
	// Only destroy textures when switching away from methods that use them
	if (a_upscalemethod != UpscaleMethod::kDLSS && a_upscalemethod != UpscaleMethod::kFSR && a_upscalemethod != UpscaleMethod::kXESS) {
		if (reactiveMaskTexture) {
			reactiveMaskTexture->srv = nullptr;
			reactiveMaskTexture->uav = nullptr;
			reactiveMaskTexture->resource = nullptr;

			delete reactiveMaskTexture;
			reactiveMaskTexture = nullptr;
		}

		if (transparencyCompositionMaskTexture) {
			transparencyCompositionMaskTexture->srv = nullptr;
			transparencyCompositionMaskTexture->uav = nullptr;
			transparencyCompositionMaskTexture->resource = nullptr;

			delete transparencyCompositionMaskTexture;
			transparencyCompositionMaskTexture = nullptr;
		}
	}

	if (a_upscalemethod != UpscaleMethod::kXESS) {
		delete xessDepthTexture;
		xessDepthTexture = nullptr;
		delete xessOutputTexture;
		xessOutputTexture = nullptr;
	}

	// Motion vector copy texture is only needed for DLSS - destroy when switching away from DLSS
	if (a_upscalemethod != UpscaleMethod::kDLSS) {
		if (motionVectorCopyTexture) {
			motionVectorCopyTexture->srv = nullptr;
			motionVectorCopyTexture->uav = nullptr;
			motionVectorCopyTexture->resource = nullptr;

			delete motionVectorCopyTexture;
			motionVectorCopyTexture = nullptr;
		}
		if (sharpenerTexture) {
			sharpenerTexture->srv = nullptr;
			sharpenerTexture->uav = nullptr;
			sharpenerTexture->resource = nullptr;

			delete sharpenerTexture;
			sharpenerTexture = nullptr;
		}
	}
}

void Upscaling::CheckResources(UpscaleMethod a_upscalemethod)
{
	static auto previousUpscaleMode = UpscaleMethod::kTAA;
	static bool previousFrameGenMode = false;

	bool frameGenModeCurrent = (settings.frameGenerationMode && d3d12SwapChainActive);
	bool frameGenModeChanged = frameGenModeCurrent != previousFrameGenMode;
	bool upscaleModeChanged = (previousUpscaleMode != a_upscalemethod);

	// A drain failure below leaves the XeSS textures and the SDK context alive while the
	// tracking update still moves previousUpscaleMode off kXESS, so the teardown branch would
	// never be entered again. Retry it here, before that state is consulted, until the GPU
	// drains or the retry budget runs out.
	if (pendingXeSSTeardown) {
		if (a_upscalemethod == UpscaleMethod::kXESS) {
			// The context was never destroyed, so switching back simply reuses it.
			pendingXeSSTeardown = false;
			pendingXeSSTeardownAttempts = 0;
		} else if (xessD3D12PathActive ? dx12SwapChain.WaitForIdle() : WaitForD3D11Idle()) {
			DestroyUpscalingTextureResources(a_upscalemethod);
			if (xessD3D12PathActive)
				intelXeSSD3D12.DestroyResources();
			else
				intelXeSS.DestroyResources();
			pendingXeSSTeardown = false;
			pendingXeSSTeardownAttempts = 0;
			logger::info("[XeSS-SR] Deferred teardown completed once pending GPU work finished");
		} else if (++pendingXeSSTeardownAttempts >= kMaxXeSSTeardownAttempts) {
			// Each attempt can block for the full drain timeout; retrying every frame forever
			// would stall the game harder than the leak it is trying to avoid.
			pendingXeSSTeardown = false;
			logger::error("[XeSS-SR] Giving up on the deferred teardown after {} attempts; the inactive context stays alive for this session", pendingXeSSTeardownAttempts);
		}
	}

	if (upscaleModeChanged || frameGenModeChanged) {
		logger::debug("[Upscaling] Resource change detected - Upscale: {} ({}) -> {} ({}), FrameGen: {} -> {} (d3d12Active={})",
			static_cast<int>(previousUpscaleMode), magic_enum::enum_name(previousUpscaleMode), static_cast<int>(a_upscalemethod), magic_enum::enum_name(a_upscalemethod), previousFrameGenMode, frameGenModeCurrent, d3d12SwapChainActive);

		// Destroy previous upscaling method resources (only if they were actually active)
		if (upscaleModeChanged) {
			const bool xessLifecycleSafe = previousUpscaleMode != UpscaleMethod::kXESS ||
			                               (xessD3D12PathActive ? dx12SwapChain.WaitForIdle() : WaitForD3D11Idle());
			if (xessLifecycleSafe)
				DestroyUpscalingTextureResources(a_upscalemethod);
			else
				logger::warn("[XeSS-SR] Retaining inactive XeSS textures because pending GPU work did not finish safely");

			// Only destroy SDK resources if the previous method was actually performing upscaling
			if (previousUpscaleMode == UpscaleMethod::kXESS) {
				if (xessLifecycleSafe) {
					if (xessD3D12PathActive)
						intelXeSSD3D12.DestroyResources();
					else
						intelXeSS.DestroyResources();
				} else {
					// Queue the retry; the tracking update below erases the last trace of kXESS.
					pendingXeSSTeardown = true;
					pendingXeSSTeardownAttempts = 0;
					logger::warn("[XeSS-SR] Keeping the inactive context alive because pending GPU work did not finish safely; teardown deferred to a later frame");
				}
			} else if (previousUpscalingWasActive) {
				if (previousUpscaleMode == UpscaleMethod::kDLSS)
					streamline.DestroyDLSSResources();
				else if (previousUpscaleMode == UpscaleMethod::kFSR)
					fidelityFX.DestroyFSRResources();
			}
			if (a_upscalemethod == UpscaleMethod::kFSR)
				fidelityFX.CreateFSRResources();
		}

		// Create new upscaling method resources
		if (upscaleModeChanged) {
			CreateUpscalingTextureResources(a_upscalemethod);
		}

		// Update tracking for next call
		previousUpscaleMode = a_upscalemethod;
		previousFrameGenMode = (settings.frameGenerationMode && d3d12SwapChainActive);
		previousUpscalingWasActive = IsUpscalingActive();
	}
}

ID3D11ComputeShader* Upscaling::GetEncodeTexturesCS()
{
	auto upscaleMethod = GetUpscaleMethod();
	uint methodIndex = (uint)upscaleMethod;

	if (!encodeTexturesCS[methodIndex]) {
		logger::debug("Compiling EncodeTexturesCS.hlsl for upscale method {}", methodIndex);

		std::vector<std::pair<const char*, const char*>> defines;

		// Add upscale method define
		switch (upscaleMethod) {
		case UpscaleMethod::kDLSS:
			defines.push_back({ "DLSS", "" });
			break;
		case UpscaleMethod::kFSR:
			defines.push_back({ "FSR", "" });
			break;
		case UpscaleMethod::kXESS:
			defines.push_back({ "XESS", "" });
			defines.push_back({ "DEPTH_OUTPUT", "" });
			// Session-constant, so it is safe to bake into the cached permutation.
			if (XeSSSharesFrameGenerationInputs())
				defines.push_back({ "SHARED_MOTION_VECTORS", "" });
			break;
		default:
			// No define for NONE or TAA
			break;
		}

		encodeTexturesCS[methodIndex].attach((ID3D11ComputeShader*)Util::CompileShader(L"Data/Shaders/Upscaling/EncodeTexturesCS.hlsl", defines, "cs_5_0"));
	}
	return encodeTexturesCS[methodIndex].get();
}

ID3D11PixelShader* Upscaling::GetDepthRefractionUpscalePS()
{
	if (!depthRefractionUpscalePS) {
		logger::debug("Compiling DepthRefractionUpscalePS.hlsl");
		std::vector<std::pair<const char*, const char*>> defines = { { "PSHADER", "" } };
		depthRefractionUpscalePS.attach((ID3D11PixelShader*)Util::CompileShader(L"Data/Shaders/Upscaling/DepthRefractionUpscalePS.hlsl", defines, "ps_5_0"));
	}

	return depthRefractionUpscalePS.get();
}

ID3D11PixelShader* Upscaling::GetUnderwaterMaskUpscalePS()
{
	if (!underwaterMaskUpscalePS) {
		logger::debug("Compiling UnderwaterMaskPS.hlsl");
		std::vector<std::pair<const char*, const char*>> defines = { { "PSHADER", "" } };
		underwaterMaskUpscalePS.attach((ID3D11PixelShader*)Util::CompileShader(L"Data/Shaders/Upscaling/UnderwaterMaskUpscalePS.hlsl", defines, "ps_5_0"));
	}

	return underwaterMaskUpscalePS.get();
}

ID3D11VertexShader* Upscaling::GetUpscaleVS()
{
	if (!upscaleVS) {
		logger::debug("Compiling UpscaleVS.hlsl");
		upscaleVS.attach((ID3D11VertexShader*)Util::CompileShader(L"Data/Shaders/Upscaling/UpscaleVS.hlsl", { { "VSHADER", "" } }, "vs_5_0"));
	}

	return upscaleVS.get();
}

ID3D11ComputeShader* Upscaling::GetUICompositeCS()
{
	if (!uiCompositeCS) {
		logger::debug("Compiling UICompositeCS.hlsl");
		uiCompositeCS.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data/Shaders/Upscaling/UICompositeCS.hlsl", {}, "cs_5_0"));
	}

	return uiCompositeCS.get();
}

bool Upscaling::CompositeFrameGenerationUI()
{
	ZoneScoped;

	static bool warnedMissingCompositeInputs = false;
	static bool warnedMissingCompositeShader = false;

	auto* hudless = dx12SwapChain.swapChainBufferWrapped;
	auto* ui = dx12SwapChain.uiBufferWrapped;
	auto* present = dx12SwapChain.presentBufferWrapped;
	if (!hudless || !hudless->srv || !ui || !ui->srv || !present || !present->uav) {
		if (!warnedMissingCompositeInputs) {
			warnedMissingCompositeInputs = true;
			logger::error("[XeSS-FG] UI composite skipped: interop textures are missing; the presented frame stays HUD-less");
		}
		return false;
	}

	auto* computeShader = GetUICompositeCS();
	if (!computeShader) {
		if (!warnedMissingCompositeShader) {
			warnedMissingCompositeShader = true;
			logger::error("[XeSS-FG] UI composite skipped: Data/Shaders/Upscaling/UICompositeCS.hlsl failed to compile or is not installed; the presented frame stays HUD-less");
		}
		return false;
	}

	auto context = globals::d3d::context;
	auto state = globals::state;

	state->BeginPerfEvent("Frame Generation UI Composite");

	ID3D11ShaderResourceView* views[2] = { hudless->srv, ui->srv };
	context->CSSetShaderResources(0, ARRAYSIZE(views), views);

	ID3D11UnorderedAccessView* uavs[1] = { present->uav };
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

	context->CSSetShader(computeShader, nullptr, 0);

	globals::profiler->BeginPass("Upscaling::FrameGenerationUIComposite");
	context->Dispatch((dx12SwapChain.swapChainDesc.Width + 7) / 8, (dx12SwapChain.swapChainDesc.Height + 7) / 8, 1);
	globals::profiler->EndPass();

	views[0] = nullptr;
	views[1] = nullptr;
	context->CSSetShaderResources(0, ARRAYSIZE(views), views);
	uavs[0] = nullptr;
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
	context->CSSetShader(nullptr, nullptr, 0);

	state->EndPerfEvent();
	return true;
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
	const auto jitterPhaseCount = static_cast<int32_t>(std::ceil(basePhaseCount * std::pow((float(displayWidth) / renderWidth), 2.0f)));
	return std::max(1, jitterPhaseCount);
}

// Calculate halton number for index and base.
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

	// Force enable TAA if needed
	Util::SetTemporal(upscaleMethod != UpscaleMethod::kNONE);
}

void Upscaling::ConfigureUpscaling(RE::BSGraphics::State* a_viewport)
{
	auto upscaleMethod = GetUpscaleMethod();

	// Delete or create resources as necessary
	CheckResources(upscaleMethod);

	// Cache original TAA values for UI
	projectionPosScaleX = a_viewport->projectionPosScaleX;
	projectionPosScaleY = a_viewport->projectionPosScaleY;

	// Get full screen size
	auto state = globals::state;
	float2 screenSize{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };

	auto screenWidth = static_cast<int>(screenSize.x);
	auto screenHeight = static_cast<int>(screenSize.y);

	int renderWidth = screenWidth;
	int renderHeight = screenHeight;
	bool useTemporalUpscaler = upscaleMethod != UpscaleMethod::kNONE && upscaleMethod != UpscaleMethod::kTAA;

	if (upscaleMethod == UpscaleMethod::kXESS) {
		const xess_2d_t outputResolution{ static_cast<uint32_t>(screenWidth), static_cast<uint32_t>(screenHeight) };
		const auto quality = ToXeSSQuality(settings.qualityModeXeSS);
		bool requiresInitialization = false;
		bool safeToInitialize = false;
		bool initialized = false;
		uint32_t optimalWidth = 0;
		uint32_t optimalHeight = 0;

		if (xessD3D12PathActive) {
			const auto& currentOutput = intelXeSSD3D12.GetOutputResolution();
			requiresInitialization = !intelXeSSD3D12.initialized ||
			                         currentOutput.x != outputResolution.x || currentOutput.y != outputResolution.y ||
			                         intelXeSSD3D12.GetQuality() != quality || !intelXeSSD3D12.UsesResponsiveMask() ||
			                         !intelXeSSD3D12.UsesAutoExposure();
			safeToInitialize = !intelXeSSD3D12.initialized || !requiresInitialization || dx12SwapChain.WaitForIdle();
			if (safeToInitialize)
				initialized = intelXeSSD3D12.CreateResources(outputResolution, quality, true, true);
			if (initialized) {
				const auto& inputRange = intelXeSSD3D12.GetInputResolutionRange();
				optimalWidth = inputRange.optimal.x;
				optimalHeight = inputRange.optimal.y;
			}
		} else {
			const auto& currentOutput = intelXeSS.GetOutputResolution();
			requiresInitialization = !intelXeSS.initialized ||
			                         currentOutput.x != outputResolution.x || currentOutput.y != outputResolution.y ||
			                         intelXeSS.GetQuality() != quality || !intelXeSS.UsesResponsiveMask() || !intelXeSS.UsesAutoExposure();
			safeToInitialize = !intelXeSS.initialized || !requiresInitialization || WaitForD3D11Idle();
			if (safeToInitialize)
				initialized = intelXeSS.CreateResources(outputResolution, quality, true, true);
			if (initialized) {
				const auto& inputRange = intelXeSS.GetInputResolutionRange();
				optimalWidth = inputRange.optimal.x;
				optimalHeight = inputRange.optimal.y;
			}
		}

		if (safeToInitialize)
			CreateUpscalingTextureResources(upscaleMethod);
		if (initialized) {
			renderWidth = static_cast<int>(optimalWidth);
			renderHeight = static_cast<int>(optimalHeight);
			if (requiresInitialization)
				pendingXeSSReset.store(true, std::memory_order_release);
		} else {
			logger::error("[XeSS-SR] Could not configure resources; falling back for this session");
			if (xessD3D12PathActive)
				intelXeSSD3D12.available = false;
			else
				intelXeSS.available = false;
			upscaleMethod = GetUpscaleMethod();
			CheckResources(upscaleMethod);
			useTemporalUpscaler = upscaleMethod != UpscaleMethod::kNONE && upscaleMethod != UpscaleMethod::kTAA;
		}
	}

	if (useTemporalUpscaler && upscaleMethod != UpscaleMethod::kXESS) {
		const float resolutionScaleBase = 1.0f / ffxFsr3GetUpscaleRatioFromQualityMode((FfxFsr3QualityMode)settings.qualityMode);
		renderWidth = static_cast<int>(screenWidth * resolutionScaleBase);
		renderHeight = static_cast<int>(screenHeight * resolutionScaleBase);
	}

	if (useTemporalUpscaler) {
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

	// Disable dynamic resolution unless the game explicitly enables it
	runtimeData.dynamicResolutionLock = 1;
}

void Upscaling::SetupResources()
{
	QueryPerformanceFrequency(&qpf);

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
	depthStencilDesc.DepthEnable = true;                           // Enable depth testing
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;  // Write to all depth bits
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;          // Always pass depth test (write all depths)

	depthStencilDesc.StencilEnable = false;  // Disable stencil testing

	DX::ThrowIfFailed(globals::d3d::device->CreateDepthStencilState(&depthStencilDesc, upscaleDepthStencilState.put()));

	// Create jitter offset constant buffer for depth upscaling
	jitterCB = new ConstantBuffer(ConstantBufferDesc<JitterCB>());

	// Create upscaling data constant buffer for encode textures compute shader
	upscalingDataCB = new ConstantBuffer(ConstantBufferDesc<UpscalingDataCB>());

	// Create blend state for depth upscaling
	D3D11_BLEND_DESC blendDesc = {};
	blendDesc.AlphaToCoverageEnable = false;
	blendDesc.IndependentBlendEnable = false;
	blendDesc.RenderTarget[0].BlendEnable = false;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	DX::ThrowIfFailed(globals::d3d::device->CreateBlendState(&blendDesc, upscaleBlendState.put()));

	// Create rasterizer state for fullscreen rendering
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

	if (d3d12SwapChainActive)
		dx12SwapChain.CreateSharedResources();

	copyDepthToSharedBufferPS.attach((ID3D11PixelShader*)Util::CompileShader(L"Data\\Shaders\\Upscaling\\CopyDepthToSharedBufferPS.hlsl", { { "PSHADER", "" } }, "ps_5_0"));
}

void Upscaling::ClearShaderCache()
{
	for (size_t i = 0; i < (size_t)UpscaleMethod::kTOTAL; ++i) {
		encodeTexturesCS[i] = nullptr;  // com_ptr automatically releases
	}

	depthRefractionUpscalePS = nullptr;  // com_ptr automatically releases
	underwaterMaskUpscalePS = nullptr;   // com_ptr automatically releases
	upscaleVS = nullptr;                 // com_ptr automatically releases
	uiCompositeCS = nullptr;             // com_ptr automatically releases
}

void Upscaling::CopySharedD3D12Resources()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Upscaling - Copy Shared D3D12 Resources");
	globals::state->BeginPerfEvent("Copy Shared D3D12 Resources");

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	// Native XeSS-SR consumes the D3D11 side of these same buffers, so EncodeTexturesCS writes
	// depth and motion vectors into them later this frame (before Present). Nothing to copy.
	if (GetUpscaleMethod() == UpscaleMethod::kXESS && XeSSSharesFrameGenerationInputs()) {
		globals::state->EndPerfEvent();
		return;
	}

	auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	context->CopyResource(dx12SwapChain.motionVectorBufferShared12->resource11, motionVector.texture);

	auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

	{
		// Set up viewport for fullscreen rendering
		float2 screenSize{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };

		D3D11_VIEWPORT viewport = {};
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
		viewport.Width = screenSize.x;
		viewport.Height = screenSize.y;
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		context->RSSetViewports(1, &viewport);

		// Set up Input Assembler for fullscreen triangle
		context->IASetInputLayout(nullptr);
		context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
		context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// Set up vertex shader
		context->VSSetShader(GetUpscaleVS(), nullptr, 0);

		// Set up rasterizer and blend states
		context->RSSetState(upscaleRasterizerState.get());
		context->OMSetBlendState(upscaleBlendState.get(), nullptr, 0xffffffff);

		// Set up pixel shader resources
		ID3D11ShaderResourceView* views[1] = { depth.depthSRV };
		context->PSSetShaderResources(0, ARRAYSIZE(views), views);

		// Set render target view for pixel shader output
		ID3D11RenderTargetView* rtvs[1] = { dx12SwapChain.depthBufferShared12->rtv };
		context->OMSetRenderTargets(ARRAYSIZE(rtvs), rtvs, nullptr);

		context->PSSetShader(copyDepthToSharedBufferPS.get(), nullptr, 0);

		globals::profiler->BeginPass("Upscaling::CopyDepthD3D12");
		context->Draw(3, 0);
		globals::profiler->EndPass();
	}

	// Clean up
	ID3D11ShaderResourceView* views[1] = { nullptr };
	context->PSSetShaderResources(0, ARRAYSIZE(views), views);

	context->OMSetRenderTargets(0, nullptr, nullptr);
	context->PSSetShader(nullptr, nullptr, 0);
	context->VSSetShader(nullptr, nullptr, 0);

	globals::state->EndPerfEvent();
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

	if (d3d12SwapChainActive)
		globals::features::hdrDisplay.SetUIBuffer();

	globals::state->UpdateSharedData(false, false);
}

void Upscaling::TimerSleepQPC(int64_t targetQPC)
{
	LARGE_INTEGER currentQPC;
	do {
		QueryPerformanceCounter(&currentQPC);
	} while (currentQPC.QuadPart < targetQPC);
}

void Upscaling::FrameLimiter()
{
	if (activeFrameGenerationMode == static_cast<uint>(FrameGenerationMethod::kXESS))
		return;

	if (d3d12SwapChainActive) {
		// Use frame latency waitable object if available for better frame pacing
		HANDLE waitableObject = GetFrameLatencyWaitableObject();

		// Wait for the next frame presentation slot
		WaitForSingleObject(waitableObject, INFINITE);

		if (settings.frameLimitMode) {
			static constexpr int64_t kNanosecondsPerSecond = 1000000000LL;
			static constexpr double kFrameGenerationRateScale = 0.5;
			const double frameRateScale = ShouldUseFrameGenerationThisFrame() ? kFrameGenerationRateScale : 1.0;
			int64_t targetFrameTimeNS = int64_t(static_cast<double>(kNanosecondsPerSecond) / (refreshRate * frameRateScale));
			int64_t targetFrameTicks = (targetFrameTimeNS * qpf.QuadPart) / kNanosecondsPerSecond;

			static LARGE_INTEGER lastFrame = {};
			LARGE_INTEGER timeNow;
			QueryPerformanceCounter(&timeNow);

			int64_t delta = timeNow.QuadPart - lastFrame.QuadPart;
			if (delta < targetFrameTicks) {
				TimerSleepQPC(lastFrame.QuadPart + targetFrameTicks);
			}
			QueryPerformanceCounter(&lastFrame);
		}
	}
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
		// using the CCD get the associated path and display configuration
		UINT32 requiredPaths, requiredModes;
		if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &requiredPaths, &requiredModes) == ERROR_SUCCESS) {
			std::vector<DISPLAYCONFIG_PATH_INFO> paths(requiredPaths);
			std::vector<DISPLAYCONFIG_MODE_INFO> modes2(requiredModes);
			if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &requiredPaths, paths.data(), &requiredModes, modes2.data(), nullptr) == ERROR_SUCCESS) {
				// iterate through all the paths until find the exact source to match
				for (auto& p : paths) {
					DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName;
					sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
					sourceName.header.size = sizeof(sourceName);
					sourceName.header.adapterId = p.sourceInfo.adapterId;
					sourceName.header.id = p.sourceInfo.id;
					if (DisplayConfigGetDeviceInfo(&sourceName.header) == ERROR_SUCCESS && wcscmp(info.szDevice, sourceName.viewGdiDeviceName) == 0) {
						// find the matched device which is associated with current device
						// there may be the possibility that display may be duplicated and windows may be one of them in such scenario
						// there may be two callback because source is same target will be different
						// as window is on both the display so either selecting either one is ok
						// get the refresh rate
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

bool Upscaling::IsFrameGenerationDx12PathActive() const
{
	return d3d12SwapChainActive &&
	       activeFrameGenerationMode != static_cast<uint>(FrameGenerationMethod::kNONE) &&
	       dx12SwapChain.UsesFrameGenerationProvider();
}

bool Upscaling::IsFrameGenerationActive() const
{
	if (!IsFrameGenerationDx12PathActive())
		return false;
	if (activeFrameGenerationMode == static_cast<uint>(FrameGenerationMethod::kXESS))
		return intelXeSSFrameGeneration.IsActive();
	if (activeFrameGenerationMode == static_cast<uint>(FrameGenerationMethod::kFSR))
		return fidelityFX.isFrameGenActive;
	return false;
}

bool Upscaling::ShouldUseFrameGenerationThisFrame() const
{
	auto* state = globals::state;
	const bool menuOpen = state && state->IsPausedOrMenuOpen(globals::game::ui);
	return IsFrameGenerationDx12PathActive() &&
	       settings.frameGenerationMode == activeFrameGenerationMode &&
	       activeFrameGenerationMode != static_cast<uint>(FrameGenerationMethod::kNONE) &&
	       (settings.frameGenerationAllowInMenus || !menuOpen);
}

uint32_t Upscaling::GetFrameGenerationMultiplier() const
{
	if (!ShouldUseFrameGenerationThisFrame())
		return 1;

	// FSR-FG in this integration always presents one interpolated frame per rendered frame.
	if (activeFrameGenerationMode != static_cast<uint>(FrameGenerationMethod::kXESS))
		return kMinFrameGenerationMultiplier;

	const uint32_t supportedMax = std::min<uint32_t>(
		kMaxFrameGenerationMultiplier,
		1 + std::max<uint32_t>(1, intelXeSSFrameGeneration.GetMaxInterpolatedFrames()));
	return std::clamp<uint32_t>(settings.frameGenerationMultiplier, kMinFrameGenerationMultiplier, supportedMax);
}

bool Upscaling::IsUpscalingActive() const
{
	auto method = GetUpscaleMethod();

	// Only consider vendor upscalers as "active" when the
	// selected method actually produces a downscale. If the renderer is
	// currently running at 1:1 (no downscale), treat upscaling as inactive.
	if (!(method == UpscaleMethod::kFSR || method == UpscaleMethod::kDLSS || method == UpscaleMethod::kXESS)) {
		return false;
	}

	// resolutionScale.x represents renderWidth / displayWidth.
	return resolutionScale.x < .99f;
}

/**
 * @brief Retrieves the current frame time for frame generation.
 *
 * Returns the frame time from the D3D12 swap chain if frame generation is active; otherwise, returns 0.
 *
 * @return float The current frame time in seconds, or 0 if frame generation is inactive.
 */
float Upscaling::GetFrameGenerationFrameTime() const
{
	if (!IsFrameGenerationActive())
		return 0.0f;

	// Get the current frame time from D3D12 swapchain
	if (dx12SwapChain.swapChain) {
		// Get frame time from the D3D12 SwapChain
		return GetFrameTime();
	}

	return 0.0f;
}

// Unified interface methods
void Upscaling::LoadUpscalingSDKs()
{
	// Initialize upscaling SDK components during plugin startup
	// This ensures all SDKs are available before any D3D device creation
	streamline.LoadInterposer();
	fidelityFX.LoadFFX();  // Only for frame generation now
	intelXeSS.Load();
	intelXeSSD3D12.Load();
	intelXeSSFrameGeneration.Load();
}

HANDLE Upscaling::GetFrameLatencyWaitableObject() const
{
	return dx12SwapChain.GetFrameLatencyWaitableObject();
}

float Upscaling::GetFrameTime() const
{
	return dx12SwapChain.GetFrameTime();
}

// Backend interface methods
bool Upscaling::IsBackendInitialized() const
{
	return streamline.initialized;
}

void Upscaling::CheckBackendFeatures(IDXGIAdapter* adapter)
{
	streamline.CheckFeatures(adapter);
}

void Upscaling::UpgradeBackendInterface(void** ppInterface)
{
	streamline.slUpgradeInterface(ppInterface);
}

void Upscaling::SetBackendD3DDevice(ID3D11Device* device)
{
	streamline.slSetD3DDevice(device);
}

void Upscaling::PostBackendDevice()
{
	streamline.PostDevice();
}

// Module availability methods
bool Upscaling::HasFrameGenModule() const
{
	if (settings.frameGenerationMode == static_cast<uint>(FrameGenerationMethod::kXESS))
		return intelXeSSFrameGeneration.IsAvailable();
	if (settings.frameGenerationMode == static_cast<uint>(FrameGenerationMethod::kFSR))
		return fidelityFX.featureFSR3FG;
	return false;
}

// Proxy interface methods
void Upscaling::SetProxyD3D11Device(ID3D11Device* device)
{
	dx12SwapChain.SetD3D11Device(device);
}

void Upscaling::SetProxyD3D11DeviceContext(ID3D11DeviceContext* context)
{
	dx12SwapChain.SetD3D11DeviceContext(context);
}

bool Upscaling::CreateProxySwapChain(IDXGIAdapter* adapter, DXGI_SWAP_CHAIN_DESC swapChainDesc, bool enableFrameGenerationProvider)
{
	return dx12SwapChain.CreateSwapChain(adapter, swapChainDesc, enableFrameGenerationProvider);
}

void Upscaling::CreateProxyInterop()
{
	dx12SwapChain.CreateInterop();
}

IDXGISwapChain* Upscaling::GetProxySwapChain()
{
	return dx12SwapChain.GetSwapChainProxy();
}

Upscaling::BlurResources Upscaling::GetBlurResources() const
{
	if (d3d12SwapChainActive) {
		return dx12SwapChain.GetBlurResources();
	}
	return {};
}

void Upscaling::Upscale()
{
	ZoneScoped;
	auto upscaleMethod = GetUpscaleMethod();

	auto state = globals::state;
	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;

	context->OMSetRenderTargets(0, nullptr, nullptr);  // Unbind all bound render targets

	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

	{
		globals::profiler->BeginPass("Upscaling::EncodeTextures");
		state->BeginPerfEvent("Encode Upscaling Textures");
		TracyD3D11Zone(globals::state->tracyCtx, "Encode Upscaling Textures");

		auto& temporalAAMask = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kTEMPORAL_AA_MASK];
		auto& normals = renderer->GetRuntimeData().renderTargets[globals::deferred->forwardRenderTargets[2]];
		auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

		auto renderSize = Util::ConvertToDynamic(float2{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight });
		uint32_t renderWidth = (uint32_t)renderSize.x;
		uint32_t renderHeight = (uint32_t)renderSize.y;

		ID3D11ShaderResourceView* views[4] = { temporalAAMask.SRV, normals.SRV, motionVector.SRV, depth.depthSRV };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);
		context->CSSetShader(GetEncodeTexturesCS(), nullptr, 0);

		UpscalingDataCB upscalingData;
		upscalingData.trueSamplingDim = float2((float)renderWidth, (float)renderHeight);
		upscalingDataCB->Update(upscalingData);
		auto upscalingBuffer = upscalingDataCB->CB();
		context->CSSetConstantBuffers(0, 1, &upscalingBuffer);

		// u2 (MotionVectorOutput): DLSS only — 5x5 dilated MVec for ghosting reduction.
		ID3D11UnorderedAccessView* uavs[4] = {
			reactiveMaskTexture->uav.get(),
			transparencyCompositionMaskTexture->uav.get(),
			(upscaleMethod == UpscaleMethod::kDLSS)                                      ? motionVectorCopyTexture->uav.get() :
			(upscaleMethod == UpscaleMethod::kXESS && XeSSSharesFrameGenerationInputs()) ? dx12SwapChain.motionVectorBufferShared12->uav :
																						   nullptr,
			(upscaleMethod == UpscaleMethod::kXESS) ? GetXeSSDepthUAV() : nullptr
		};
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		context->Dispatch((renderWidth + 7) / 8, (renderHeight + 7) / 8, 1);

		ID3D11ShaderResourceView* nullViews[4] = { nullptr, nullptr, nullptr, nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(nullViews), nullViews);

		ID3D11UnorderedAccessView* nullUAVs[4] = { nullptr, nullptr, nullptr, nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAVs), nullUAVs, nullptr);

		ID3D11Buffer* nullBuffer = nullptr;
		context->CSSetConstantBuffers(0, 1, &nullBuffer);

		ID3D11ComputeShader* shader = nullptr;
		context->CSSetShader(shader, nullptr, 0);

		state->EndPerfEvent();
		globals::profiler->EndPass();
	}

	{
		globals::profiler->BeginPass("Upscaling::Upscale");
		state->BeginPerfEvent("Upscaling");
		TracyD3D11Zone(globals::state->tracyCtx, "Upscaling Dispatch");

		if (upscaleMethod == UpscaleMethod::kDLSS) {
			streamline.Upscale(main.texture, reactiveMaskTexture->resource.get(), transparencyCompositionMaskTexture->resource.get(), motionVectorCopyTexture->resource.get());
		} else if (upscaleMethod == UpscaleMethod::kFSR) {
			fidelityFX.Upscale(main.texture, reactiveMaskTexture->resource.get(), transparencyCompositionMaskTexture->resource.get(), motionVector.texture, settings.sharpnessFSR);
		} else if (upscaleMethod == UpscaleMethod::kXESS && GetXeSSDepthResource() && (xessD3D12PathActive || xessOutputTexture)) {
			const auto renderSize = Util::ConvertToDynamic(float2{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight });
			const bool resetHistory = pendingXeSSReset.exchange(false, std::memory_order_acq_rel);
			const bool succeeded = xessD3D12PathActive ?
			                           dx12SwapChain.ExecuteXeSS(
										   main.texture,
										   GetXeSSDepthResource(),
										   motionVector.texture,
										   reactiveMaskTexture->resource.get(),
										   main.texture,
										   static_cast<uint32_t>(renderSize.x),
										   static_cast<uint32_t>(renderSize.y),
										   -jitter.x,
										   -jitter.y,
										   resetHistory) :
			                           intelXeSS.Upscale(
										   main.texture,
										   GetXeSSDepthResource(),
										   motionVector.texture,
										   reactiveMaskTexture->resource.get(),
										   xessOutputTexture->resource.get(),
										   static_cast<uint32_t>(renderSize.x),
										   static_cast<uint32_t>(renderSize.y),
										   -jitter.x,
										   -jitter.y,
										   resetHistory);
			if (succeeded) {
				if (!xessD3D12PathActive)
					context->CopyResource(main.texture, xessOutputTexture->resource.get());
			} else {
				logger::error("[XeSS-SR] Dispatch failed; falling back on the next frame");
				if (xessD3D12PathActive)
					intelXeSSD3D12.available = false;
				else
					intelXeSS.available = false;
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

	// Disable dynamic resolution past this point
	runtimeData.dynamicResolutionLock = 1;

	// Updates the PerFrame constant buffer so that dynamic resolution settings are disabled
	UpdateCameraData();
}

void Upscaling::UpscaleDepth()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Upscaling - Depth");
	// Optimization overview:
	// 1) Early validation exits before issuing GPU work.
	// 2) Wide-kernel depth mode uses hysteresis to avoid frequent toggles.
	// 3) Resource copies are skipped for aliased src/dst to reduce copy churn.

	// (1) Early validation exits
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

	// Set up Input Assembler for fullscreen triangle (no vertex/index buffers needed)
	context->IASetInputLayout(nullptr);
	context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
	context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Set up vertex shader that generates fullscreen triangle using SV_VertexID
	context->VSSetShader(fullscreenVS, nullptr, 0);

	// Set up viewport for fullscreen rendering
	D3D11_VIEWPORT viewport = {};
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = screenSize.x;
	viewport.Height = screenSize.y;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	context->RSSetViewports(1, &viewport);

	// Set rasterizer and blend state
	context->RSSetState(upscaleRasterizerState.get());
	context->OMSetBlendState(upscaleBlendState.get(), nullptr, 0xffffffff);

	ID3D11SamplerState* samplers[] = { deferred->linearSampler };
	context->PSSetSamplers(0, ARRAYSIZE(samplers), samplers);

	// Set up jitter/depth-kernel constant buffer for upscaling
	JitterCB jitterData;
	jitterData.jitter = jitter;
	// (2) Wide-kernel hysteresis
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

	// (3) Skip aliased copies
	const auto copyIfNonAliased = [&](ID3D11Resource* dst, ID3D11Resource* src) {
		if (dst && src && dst != src) {
			context->CopyResource(dst, src);
		}
	};

	{
		TracyD3D11Zone(globals::state->tracyCtx, "Upscaling - Depth Upscale");

		// Sometimes this is not already copied e.g. map menu.
		// Skip alias copies to reduce unnecessary copy churn.
		copyIfNonAliased(depthCopy.texture, depth.texture);

		// Set depth stencil state to write 0x00
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

		// t0: vanilla mask copy, t1: original depth.
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

void Upscaling::ApplySharpening()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Upscaling - Sharpening");

	if (!sharpenerTexture)
		return;

	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	if (!main.texture)
		return;

	context->OMSetRenderTargets(0, nullptr, nullptr);

	if (settings.sharpnessEnabledDLSS && settings.sharpnessDLSS > 0.0f && main.UAV) {
		// Match FSR3's slider->RCAS conversion exactly (ffx_fsr3upscaler.cpp + FsrRcasCon):
		//   sharpenessRemapped = -2*slider + 2   (sharpness in stops)
		//   rcasAttenuation    = exp2(-sharpenessRemapped) = exp2(2*slider - 2)
		float currentSharpness = (-2.0f * settings.sharpnessDLSS) + 2.0f;
		currentSharpness = exp2(-currentSharpness);

		// DLSS has already written to sharpenerTexture; sharpen directly into kMAIN.UAV.
		rcas.ApplySharpen(sharpenerTexture->srv.get(), main.UAV, currentSharpness);
	} else {
		// Sharpening is disabled: resolve the DLSS output without altering it.
		context->CopyResource(main.texture, sharpenerTexture->resource.get());
	}

	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
}

void Upscaling::Main_UpdateJitter::thunk(RE::BSGraphics::State* a_state)
{
	auto& upscaling = globals::features::upscaling;
	// First per-frame render work the mod sees: the XeLL simulation phase ends here.
	if (upscaling.activeFrameGenerationMode == static_cast<uint>(FrameGenerationMethod::kXESS))
		upscaling.intelXeSSFrameGeneration.BeginRenderSubmit();
	upscaling.ConfigureTAA();
	func(a_state);
	upscaling.ConfigureUpscaling(a_state);
}

void Upscaling::MenuManagerDrawInterfaceStartHook::thunk(int64_t a1)
{
	globals::features::upscaling.PostDisplay();

	// For non-Frame Gen HDR: redirect kFRAMEBUFFER.RTV to UI texture before vanilla UI renders
	// When FG is active, its SetUIBuffer redirects to uiBufferWrapped instead
	// When HDR Display is not loaded, skip entirely so vanilla UI renders to kFRAMEBUFFER
	auto& upscaling = globals::features::upscaling;
	if (!upscaling.d3d12SwapChainActive && globals::features::hdrDisplay.loaded) {
		globals::features::hdrDisplay.SetUIBuffer();
	}

	func(a1);
}

void Upscaling::Main_PostProcessing::thunk(RE::ImageSpaceManager* a_this, uint32_t a3, RE::RENDER_TARGET a_target, void* a_4, bool a_5)
{
	auto& upscaling = globals::features::upscaling;
	auto upscaleMethod = upscaling.GetUpscaleMethod();

	if (upscaling.ShouldUseFrameGenerationThisFrame())
		upscaling.CopySharedD3D12Resources();

	if (upscaleMethod != UpscaleMethod::kNONE && upscaleMethod != UpscaleMethod::kTAA)
		upscaling.PerformUpscaling();

	if (upscaleMethod == UpscaleMethod::kDLSS)
		upscaling.ApplySharpening();

	Util::SetTemporal(upscaleMethod == UpscaleMethod::kTAA);

	// Redirect kFRAMEBUFFER to float texture before ISHDR runs so HDR values >1.0 survive
	// When HDR Display is not loaded, ISHDR writes to vanilla kFRAMEBUFFER (SDR path)
	bool hdrLoaded = globals::features::hdrDisplay.loaded;
	if (hdrLoaded)
		globals::features::hdrDisplay.RedirectFramebuffer();

	func(a_this, a3, a_target, a_4, a_5);

	// Restore kFRAMEBUFFER after ISHDR — hdrTexture now has the HDR scene
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
