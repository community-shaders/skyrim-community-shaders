#include "Upscaling.h"

#include "Deferred.h"
#include "HDRDisplay.h"
#include "Hooks.h"
#include "State.h"
#include "Upscaling/DX12SwapChain.h"
#include "Upscaling/FidelityFX.h"
#include "Upscaling/Streamline.h"
#include "Utils/UI.h"
#include <Windows.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <directx/d3dx12.h>
#include <format>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Upscaling::Settings,
	upscaleMethod,
	upscaleMethodNoDLSS,
	qualityMode,
	frameLimitMode,
	frameGenerationMode,
	frameGenerationForceEnable,
	streamlineLogLevel,
	sharpnessFSR,
	sharpnessDLSS,
	presetDLSS,
	reflexLowLatencyMode,
	reflexLowLatencyBoost,
	reflexUseMarkersToOptimize,
	reflexUseFPSLimit,
	reflexFPSLimit);

decltype(&D3D11CreateDeviceAndSwapChain) ptrD3D11CreateDeviceAndSwapChainUpscaling;

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

	if (upscaling.IsBackendInitialized())
		upscaling.CheckBackendFeatures(pAdapter);

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

	bool shouldProxy = !globals::game::isVR;
	if (shouldProxy)
		if (!pSwapChainDesc->Windowed)
			shouldProxy = false;

	auto refreshRate = Upscaling::GetRefreshRate(pSwapChainDesc->OutputWindow);
	upscaling.refreshRate = refreshRate;

	if (shouldProxy) {
		if (upscaling.settings.frameGenerationMode)
			if (refreshRate >= 120)
				shouldProxy = true;
			else if (upscaling.settings.frameGenerationForceEnable)
				shouldProxy = true;
			else
				shouldProxy = false;
		else
			shouldProxy = false;
	}

	upscaling.lowRefreshRate = refreshRate < 120;
	upscaling.isWindowed = pSwapChainDesc->Windowed;

	const D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_1;

	if (shouldProxy) {
		logger::info("[Frame Generation] Frame Generation enabled, using D3D12 proxy");

		if (upscaling.HasFrameGenModule()) {
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
			upscaling.CreateProxySwapChain(pAdapter, *pSwapChainDesc);
			upscaling.CreateProxyInterop();

			*ppSwapChain = upscaling.GetProxySwapChain();

			upscaling.d3d12SwapChainActive = true;

			if (upscaling.IsBackendInitialized()) {
				upscaling.UpgradeBackendInterface((void**)&(*ppDevice));
				upscaling.UpgradeBackendInterface((void**)&(*ppSwapChain));
				upscaling.SetBackendD3DDevice(*ppDevice);
				// Some features (notably Reflex/PCL) may report availability only after device bind.
				upscaling.CheckBackendFeatures(pAdapter);
				upscaling.PostBackendDevice();
			}

			return S_OK;
		} else {
			logger::warn("[Frame Generation] FidelityFX DLLs are not loaded, skipping proxy");
			upscaling.fidelityFXMissing = true;
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

	if (upscaling.IsBackendInitialized()) {
		upscaling.UpgradeBackendInterface((void**)&(*ppDevice));
		upscaling.UpgradeBackendInterface((void**)&(*ppSwapChain));
		upscaling.SetBackendD3DDevice(*ppDevice);
		// Re-check after device bind to ensure feature availability is accurate.
		upscaling.CheckBackendFeatures(pAdapter);
		upscaling.PostBackendDevice();
	}

	return ret;
}

void Upscaling::DrawSettings()
{
	// Display upscaling options in the UI
	std::vector<std::string> upscaleModes = { "None", "TAA" };

	std::string fsrLabel = "AMD FSR 3.1";
	upscaleModes.push_back(fsrLabel);

	std::string dlssLabel = "NVIDIA DLSS";
	upscaleModes.push_back(dlssLabel);

	// Determine available modes
	bool featureDLSS = streamline.featureDLSS;
	bool featureFSR = true;  // FSR is always available

	uint32_t* currentUpscaleMode = &settings.upscaleMethod;
	uint32_t availableModes = 1;  // Start with TAA
	if (featureFSR)
		availableModes = 2;  // Add FSR
	if (featureDLSS)
		availableModes = 3;  // Add DLSS if available
	else
		currentUpscaleMode = &settings.upscaleMethodNoDLSS;

	// Dropdown for method selection
	std::vector<const char*> modeLabels;
	for (uint32_t i = 0; i <= availableModes; ++i)
		modeLabels.push_back(upscaleModes[i].c_str());
	ImGui::Combo("Method", (int*)currentUpscaleMode, modeLabels.data(), (int)modeLabels.size());

	*currentUpscaleMode = std::min(availableModes, *currentUpscaleMode);

	// Check the current upscale method
	auto upscaleMethod = GetUpscaleMethod();

	// Display warning for DLSS resolution limits (non-VR only; VR handles this automatically)
	if (!globals::game::isVR && upscaleMethod == UpscaleMethod::kDLSS) {
		auto screenSize = globals::state->screenSize;
		if (screenSize.x > streamline.MAX_RESOLUTION || screenSize.y > streamline.MAX_RESOLUTION) {
			ImGui::PushStyleColor(ImGuiCol_Text, Util::Colors::GetWarning());
			ImGui::Text("Warning: Requested resolution %.0f x %.0f exceeds maximum supported resolution %d x %d for DLSS.",
				screenSize.x, screenSize.y, streamline.MAX_RESOLUTION, streamline.MAX_RESOLUTION);
			ImGui::Text("DLSS will not function. Lower your resolution or select a different upscaling method.");
			ImGui::PopStyleColor();
		}
	}

	// Display upscaling settings if applicable
	if (upscaleMethod != UpscaleMethod::kNONE && upscaleMethod != UpscaleMethod::kTAA) {
		const char* upscalePresetsDLSS[] = { "Ultra Performance", "Performance", "Balanced", "Quality", "DLAA" };
		const char* upscalePresets[] = { "Ultra Performance", "Performance", "Balanced", "Quality", "Native AA" };

		// Compute a safe preset index (4 - qualityMode) clamped to [0,4] to avoid negative/overflow indexing
		int presetIndex = 0;
		if (settings.qualityMode <= 4)
			presetIndex = 4 - static_cast<int>(settings.qualityMode);
		presetIndex = std::clamp(presetIndex, 0, 4);

		// Choose preset name set and the corresponding scales once, then show a
		// single SliderInt to avoid duplicated calls.
		const char* baseLabel = nullptr;

		if (upscaleMethod == UpscaleMethod::kFSR) {
			baseLabel = upscalePresets[presetIndex];
		} else if (upscaleMethod == UpscaleMethod::kDLSS) {
			baseLabel = upscalePresetsDLSS[presetIndex];
		}

		if (baseLabel) {
			// Format the label with preset name and resolution scale
			std::string labelWithScale = std::format("{} ( {:.2f}x )", baseLabel, (resolutionScale.x + resolutionScale.y) * 0.5f);

			ImGui::SliderInt("Upscale Preset", (int*)&settings.qualityMode, 0, 4, labelWithScale.c_str());
		}

		if (upscaleMethod == UpscaleMethod::kFSR) {
			ImGui::SliderFloat("Sharpness", &settings.sharpnessFSR, 0.0f, 1.0f, "%.1f");
		} else if (upscaleMethod == UpscaleMethod::kDLSS) {
			ImGui::SliderFloat("Sharpness", &settings.sharpnessDLSS, 0.0f, 1.0f, "%.1f");

			const char* presets[] = { "Default", "Preset J", "Preset K", "Preset L", "Preset M" };
			ImGui::Combo("DLSS Model Preset", (int*)&settings.presetDLSS, presets, 5);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Choose which DLSS AI model preset to use.");
				ImGui::Text("Each model offers different visual quality, performance, and motion stability.");
				ImGui::Text("Set to 'Default' for automatic selection based on your Upscale Preset and hardware.");
				ImGui::Text("Changing this setting requires a restart to take effect.");
			}
		}
	}

	const bool frameGenerationDx12PathActive = IsFrameGenerationDx12PathActive();

	if (!globals::game::isVR) {
		if (ImGui::TreeNodeEx("Frame Generation", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Text("Frame Generation interpolates real frames with generated ones for a smoother experience");
			ImGui::Text("Uses AMD FSR Frame Generation technology");
			if (HasFrameGenModule())
				ImGui::Text("AMD FSR Frame Generation is available.");
			ImGui::Text("Requires a D3D11 to D3D12 proxy which can create compatibility issues");
			ImGui::Text("Toggling this setting requires a restart to work correctly");

			bool onlyRequiresRestart = true;

			if (!isWindowed) {
				ImGui::PushStyleColor(ImGuiCol_Text, Util::Colors::GetWarning());
				ImGui::Text("Warning: Requires windowed mode");
				ImGui::PopStyleColor();

				onlyRequiresRestart = false;
			}

			if (lowRefreshRate && !settings.frameGenerationForceEnable) {
				ImGui::PushStyleColor(ImGuiCol_Text, Util::Colors::GetWarning());
				ImGui::Text("Warning: Requires a high refresh rate monitor or Force Enable Frame Generation");
				ImGui::PopStyleColor();

				onlyRequiresRestart = false;
			}

			if (fidelityFXMissing) {
				ImGui::PushStyleColor(ImGuiCol_Text, Util::Colors::GetWarning());
				ImGui::Text("Warning: FidelityFX DLLs are not loaded");
				ImGui::PopStyleColor();

				onlyRequiresRestart = false;
			}

			if (onlyRequiresRestart && settings.frameGenerationMode && !frameGenerationDx12PathActive) {
				ImGui::PushStyleColor(ImGuiCol_Text, Util::Colors::GetWarning());
				ImGui::Text("Warning: Requires restart");
				ImGui::PopStyleColor();
			}

			if (!settings.frameGenerationMode && frameGenerationDx12PathActive) {
				ImGui::PushStyleColor(ImGuiCol_Text, Util::Colors::GetWarning());
				ImGui::Text("Warning: Requires restart");
				ImGui::PopStyleColor();
			}

			bool fgEnabled = settings.frameGenerationMode != 0;
			if (ImGui::Checkbox("Frame Generation", &fgEnabled))
				settings.frameGenerationMode = fgEnabled ? 1 : 0;

			if (!frameGenerationDx12PathActive)
				ImGui::BeginDisabled();

			bool flEnabled = settings.frameLimitMode != 0;
			if (ImGui::Checkbox("Frame Limit (Variable Refresh Rate)", &flEnabled))
				settings.frameLimitMode = flEnabled ? 1 : 0;

			if (!frameGenerationDx12PathActive)
				ImGui::EndDisabled();

			ImGui::TextWrapped("Allows frame generation to function on low refresh rate monitors. Detected: %.2f Hz", refreshRate);
			bool fgForce = settings.frameGenerationForceEnable != 0;
			if (ImGui::Checkbox("Force Enable Frame Generation", &fgForce))
				settings.frameGenerationForceEnable = fgForce ? 1 : 0;

			ImGui::TreePop();
		}
	}

	if (streamline.reflexSupportedOnCurrentAdapter && ImGui::TreeNodeEx("NVIDIA Reflex", ImGuiTreeNodeFlags_DefaultOpen)) {
		const bool reflexBlockedByFrameGeneration = frameGenerationDx12PathActive;
		const bool reflexAvailable = streamline.initialized && streamline.featureReflex;
		const bool reflexControlsAvailable = reflexAvailable && !reflexBlockedByFrameGeneration;
		const bool markerOptimizationAvailable = reflexControlsAvailable && streamline.featurePCL;
		if (reflexBlockedByFrameGeneration) {
			ImGui::TextDisabled("Reflex is unavailable while the DX12 frame-generation swapchain is active.");
		}

		if (!reflexAvailable) {
			ImGui::TextDisabled("Reflex is not available. Ensure sl.reflex.dll is present and restart.");
		}

		if (!reflexControlsAvailable)
			ImGui::BeginDisabled();

		ImGui::Checkbox("Low Latency Mode", &settings.reflexLowLatencyMode);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Cuts input delay by syncing CPU work closer to the GPU.");
			ImGui::TextUnformatted("Can reduce max FPS a little, but usually feels more responsive.");
		}

		if (!settings.reflexLowLatencyMode)
			ImGui::BeginDisabled();

		ImGui::Checkbox("Low Latency Boost", &settings.reflexLowLatencyBoost);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Keeps GPU clocks higher to avoid latency spikes at low GPU load.");
			ImGui::TextUnformatted("Useful if frametime jumps; costs extra power and heat.");
		}

		if (!markerOptimizationAvailable)
			ImGui::BeginDisabled();

		ImGui::Checkbox("Use Markers To Optimize", &settings.reflexUseMarkersToOptimize);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Uses frame markers for tighter Reflex timing.");
			ImGui::TextUnformatted("Try On first; turn Off if it causes stutter on your setup.");
		}

		if (!markerOptimizationAvailable)
			ImGui::EndDisabled();

		if (!markerOptimizationAvailable) {
			ImGui::TextDisabled("Marker optimization unavailable (PCL not loaded).");
		}

		ImGui::Checkbox("Use FPS Limit", &settings.reflexUseFPSLimit);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Uses Reflex's internal FPS cap for steadier frametimes.");
			ImGui::TextUnformatted("Can lower latency versus uncapped rendering.");
		}

		if (!settings.reflexLowLatencyMode)
			ImGui::EndDisabled();

		if (!settings.reflexUseFPSLimit)
			ImGui::BeginDisabled();

		if (!std::isfinite(settings.reflexFPSLimit))
			settings.reflexFPSLimit = 60.0f;
		settings.reflexFPSLimit = std::clamp(settings.reflexFPSLimit, 20.0f, 240.0f);
		ImGui::SliderFloat("FPS Limit", &settings.reflexFPSLimit, 20.0f, 240.0f, "%.0f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Set your frame cap target.");
			ImGui::TextUnformatted("Start about 2-3 FPS below refresh rate (e.g. 117 for 120 Hz).");
		}

		if (!settings.reflexUseFPSLimit)
			ImGui::EndDisabled();

		if (!reflexControlsAvailable)
			ImGui::EndDisabled();

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Backend Diagnostics")) {
		// Streamline log level selection
		const char* logLevels[] = { "Off", "Default", "Verbose" };
		int logLevelIdx = static_cast<int>(settings.streamlineLogLevel);
		if (ImGui::Combo("Streamline Logging", &logLevelIdx, logLevels, IM_ARRAYSIZE(logLevels))) {
			settings.streamlineLogLevel = static_cast<uint>(logLevelIdx);
		}
		ImGui::TextUnformatted("Changing this requires a restart to take effect.");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Streamline logging controls the verbosity of NVIDIA Streamline backend logs. Useful for debugging issues with DLSS/DLSS-G.");
		}

		// VR Debug visualization -- per-eye buffers and native inputs
		if (globals::game::isVR) {
			ImGui::Separator();
			static float debugRescale = 0.15f;
			ImGui::SliderFloat("View Resize", &debugRescale, 0.05f, 1.f);

			if (ImGui::TreeNode("Upscaling Intermediates")) {
				if (vrIntermediateMotionVectors[0]) {
					bool isDLSS = GetUpscaleMethod() == UpscaleMethod::kDLSS;
					if (vrIntermediateColorIn[0] && vrIntermediateColorOut[0]) {
						BUFFER_VIEWER_NODE_TITLE(vrIntermediateColorIn[0], "Left Eye In", debugRescale)
						BUFFER_VIEWER_NODE_TITLE(vrIntermediateColorIn[1], "Right Eye In", debugRescale)
						if (!isDLSS)
							BUFFER_VIEWER_NODE_TITLE(vrIntermediateColorOut[0], "Left Eye Out", debugRescale)
						BUFFER_VIEWER_NODE_TITLE(vrIntermediateColorOut[1], "Right Eye Out", debugRescale)
					}
					BUFFER_VIEWER_NODE_TITLE(vrIntermediateMotionVectors[0], "Left Eye MVec", debugRescale)
					BUFFER_VIEWER_NODE_TITLE(vrIntermediateMotionVectors[1], "Right Eye MVec", debugRescale)
					BUFFER_VIEWER_NODE_TITLE(vrIntermediateReactiveMask[0], "Left Eye Reactive", debugRescale)
					BUFFER_VIEWER_NODE_TITLE(vrIntermediateReactiveMask[1], "Right Eye Reactive", debugRescale)
					if (vrIntermediateTransparencyMask[0]) {
						BUFFER_VIEWER_NODE_TITLE(vrIntermediateTransparencyMask[0], "Left Eye Transparency", debugRescale)
						BUFFER_VIEWER_NODE_TITLE(vrIntermediateTransparencyMask[1], "Right Eye Transparency", debugRescale)
					}
				} else {
					ImGui::TextDisabled("VR intermediates not yet created (enter game world)");
				}
				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Native Inputs")) {
				auto renderer = globals::game::renderer;
				auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
				auto& mvec = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
				auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

				auto DisplayRT = [&](const char* label, ID3D11Texture2D* tex, ID3D11ShaderResourceView* srv) {
					if (srv && tex) {
						D3D11_TEXTURE2D_DESC desc;
						tex->GetDesc(&desc);
						char buf[128];
						snprintf(buf, sizeof(buf), "%s (%ux%u)", label, desc.Width, desc.Height);
						if (ImGui::TreeNode(buf)) {
							ImGui::Image(srv, { desc.Width * debugRescale, desc.Height * debugRescale });
							ImGui::TreePop();
						}
					}
				};

				DisplayRT("kMAIN (Color Input)", (ID3D11Texture2D*)main.texture, (ID3D11ShaderResourceView*)main.SRV);
				DisplayRT("Motion Vectors", (ID3D11Texture2D*)mvec.texture, (ID3D11ShaderResourceView*)mvec.SRV);
				DisplayRT("Depth", depth.texture, depth.depthSRV);

				if (reactiveMaskTexture)
					BUFFER_VIEWER_NODE_TITLE(reactiveMaskTexture, "Reactive Mask", debugRescale)
				if (transparencyCompositionMaskTexture)
					BUFFER_VIEWER_NODE_TITLE(transparencyCompositionMaskTexture, "Transparency Mask", debugRescale)

				ImGui::TreePop();
			}
		}

		ImGui::Separator();
		Util::DrawDllVersionTable("AMD FidelityFX DLLs (click to open folder)", FidelityFX::PluginDir, FidelityFX::dllVersions, "ffx_dll_versions");
		Util::DrawDllVersionTable("NVIDIA Streamline DLLs (click to open folder)", Streamline::PluginDir, Streamline::dllVersions, "sl_dll_versions");
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
	constexpr auto enumCount = 4;  // UpscaleMethod has 4 values: kNONE, kTAA, kFSR, kDLSS
	if (settings.upscaleMethod >= static_cast<uint>(enumCount)) {
		logger::warn("[Upscaling] Loaded upscaleMethod {} out of range, clamping to {}", settings.upscaleMethod, enumCount ? enumCount - 1 : 0);
		settings.upscaleMethod = enumCount ? enumCount - 1 : 0;
	}
	if (settings.upscaleMethodNoDLSS >= static_cast<uint>(enumCount)) {
		logger::warn("[Upscaling] Loaded upscaleMethodNoDLSS {} out of range, clamping to {}", settings.upscaleMethodNoDLSS, enumCount ? enumCount - 1 : 0);
		settings.upscaleMethodNoDLSS = enumCount ? enumCount - 1 : 0;
	}
	if (settings.presetDLSS > 4) {
		logger::warn("[Upscaling] Loaded presetDLSS {} out of range, resetting to 0 (Default)", settings.presetDLSS);
		settings.presetDLSS = 0;
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
	// Fix screenshots fix from Engine Fixes
	RE::GetINISetting("bUseTAA:Display")->data.b = false;

	// The game defaults this to a non-zero value
	static auto fDRClampOffset = RE::GetINISetting("fDRClampOffset:Display");
	fDRClampOffset->data.f = 0.0f;

	// VR + DLSS workaround: rebuild the DLSS feature on cell/worldspace transitions to
	// clear a persistent post-load GPU-time regression (see pendingDLSSReset comment).
	if (globals::game::isVR)
		MenuOpenCloseEventHandler::Register();
}

RE::BSEventNotifyControl Upscaling::MenuOpenCloseEventHandler::ProcessEvent(
	const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	if (a_event && a_event->menuName == RE::LoadingMenu::MENU_NAME && !a_event->opening)
		globals::features::upscaling.pendingDLSSReset.store(true, std::memory_order_relaxed);
	return RE::BSEventNotifyControl::kContinue;
}

bool Upscaling::MenuOpenCloseEventHandler::Register()
{
	static MenuOpenCloseEventHandler singleton;
	auto ui = globals::game::ui;
	if (!ui) {
		logger::error("[Upscaling] UI event source not found; DLSS reset-on-load disabled");
		return false;
	}
	ui->GetEventSource<RE::MenuOpenCloseEvent>()->AddEventSink(&singleton);
	logger::info("[Upscaling] Registered MenuOpenCloseEventHandler for DLSS reset-on-load");
	return true;
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
	bool isGOG = !GetModuleHandle(L"steam_api64.dll");
	stl::detour_thunk<MenuManagerDrawInterfaceStartHook>(REL::RelocationID(79947, 82084));

	// Calculates resolution and jitter
	stl::write_thunk_call<Main_UpdateJitter>(REL::RelocationID(75460, 77245).address() + REL::Relocate(0xE5, isGOG ? 0x133 : 0xE2, 0x104));

	// Disables the original dynamic resolution system
	REL::safe_write(REL::RelocationID(35556, 36555).address() + REL::Relocate(0x2D, 0x2D, 0x25), REL::NOP5, sizeof(REL::NOP5));

	// Performs upscaling in between volumetric lighting and post processing
	stl::write_thunk_call<Main_PostProcessing>(REL::RelocationID(100430, 107148).address() + REL::Relocate(0x1F0, 0x1E7, 0x206));

	// Patches RSSetScissorRect calls to use dynamic resolution
	// This is a PC-specific function hence it was missing
	if (!globals::game::isVR)
		stl::detour_thunk<SetScissorRect>(REL::RelocationID(75564, 77365));

	// Patches facegen texture generation to not use dynamic resolution
	stl::detour_thunk<BSFaceGenManager_UpdatePendingCustomizationTextures>(REL::RelocationID(26455, 27041));

	// Patches precipitation camera to not use dynamic resolution
	stl::write_thunk_call<Main_RenderPrecipitation>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x3A1, 0x3A1, 0x2FA));

	// Forces FXAA off
	stl::detour_thunk<BSImageSpace_Init_FXAA>(REL::RelocationID(98974, 105626));

	logger::info("[Upscaling] Installed hooks");
}

Upscaling::UpscaleMethod Upscaling::GetUpscaleMethod() const
{
	if (streamline.featureDLSS)
		return (UpscaleMethod)settings.upscaleMethod;
	return (UpscaleMethod)settings.upscaleMethodNoDLSS;
}

void Upscaling::CreateUpscalingTextureResources(UpscaleMethod a_upscalemethod)
{
	logger::debug("[Upscaling] Creating texture resources for method {} ({})", static_cast<int>(a_upscalemethod), magic_enum::enum_name(a_upscalemethod));

	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC texDesc{};
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	main.texture->GetDesc(&texDesc);
	main.SRV->GetDesc(&srvDesc);
	main.UAV->GetDesc(&uavDesc);

	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	if (a_upscalemethod == UpscaleMethod::kDLSS || a_upscalemethod == UpscaleMethod::kFSR) {
		texDesc.Format = DXGI_FORMAT_R8_UNORM;
		srvDesc.Format = texDesc.Format;
		uavDesc.Format = texDesc.Format;

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
	if (a_upscalemethod != UpscaleMethod::kDLSS && a_upscalemethod != UpscaleMethod::kFSR) {
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

	if (upscaleModeChanged || frameGenModeChanged) {
		logger::debug("[Upscaling] Resource change detected - Upscale: {} ({}) -> {} ({}), FrameGen: {} -> {} (d3d12Active={})",
			static_cast<int>(previousUpscaleMode), magic_enum::enum_name(previousUpscaleMode), static_cast<int>(a_upscalemethod), magic_enum::enum_name(a_upscalemethod), previousFrameGenMode, frameGenModeCurrent, d3d12SwapChainActive);

		// Destroy previous upscaling method resources (only if they were actually active)
		if (upscaleModeChanged) {
			DestroyUpscalingTextureResources(a_upscalemethod);

			// Only destroy SDK resources if the previous method was actually performing upscaling
			if (previousUpscalingWasActive) {
				if (previousUpscaleMode == UpscaleMethod::kDLSS)
					streamline.DestroyDLSSResources();
				else if (previousUpscaleMode == UpscaleMethod::kFSR)
					fidelityFX.DestroyFSRResources();

				if (globals::game::isVR) {
					for (int i = 0; i < 2; i++) {
						vrIntermediateColorIn[i].reset();
						vrIntermediateColorOut[i].reset();
						vrIntermediateLinearDepth[i].reset();
						vrIntermediateMotionVectors[i].reset();
						vrIntermediateReactiveMask[i].reset();
						vrIntermediateTransparencyMask[i].reset();
					}
					vrIntermediateDepth.reset();
				}
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

	// VR FSR needs a separate variant: DEPTH_OUTPUT converts the R24G8_TYPELESS game depth to
	// R32_FLOAT so GetFfxResourceDescriptionDX11() returns a valid format instead of UNKNOWN.
	if (globals::game::isVR && upscaleMethod == UpscaleMethod::kFSR) {
		if (!encodeTexturesCSDepthOutput) {
			logger::debug("Compiling EncodeTexturesCS.hlsl for VR FSR (FSR + DEPTH_OUTPUT)");
			std::vector<std::pair<const char*, const char*>> defines = {
				{ "FSR", "" },
				{ "DEPTH_OUTPUT", "" }
			};
			encodeTexturesCSDepthOutput.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data/Shaders/Upscaling/EncodeTexturesCS.hlsl", defines, "cs_5_0"));
		}
		return encodeTexturesCSDepthOutput.get();
	}

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
		if (globals::game::isVR)
			defines.push_back({ "VR", "" });
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

void Upscaling::CreateVRIntermediateTextures(uint32_t inWidth, uint32_t inHeight, uint32_t outWidth, uint32_t outHeight,
	ID3D11Resource* colorSrc, ID3D11Resource* mvecSrc, ID3D11Resource* reactiveSrc, ID3D11Resource* transparencySrc)
{
	// Right-eye-only depth intermediate for DLSS. Streamline.Upscale copies the right-eye depth
	// slice here before evaluating DLSS eye 1; eye 0 reads the combined stereo depth directly at
	// zero offset. R24G8_TYPELESS matches the game's D24S8_TYPELESS cast group — R32_TYPELESS is
	// a different cast group and produces silent zero-copy failures.
	{
		D3D11_TEXTURE2D_DESC depthDesc = {};
		depthDesc.Width = inWidth;
		depthDesc.Height = inHeight;
		depthDesc.MipLevels = 1;
		depthDesc.ArraySize = 1;
		depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
		depthDesc.SampleDesc.Count = 1;
		depthDesc.Usage = D3D11_USAGE_DEFAULT;
		depthDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		vrIntermediateDepth = eastl::make_unique<Texture2D>(depthDesc);

		Util::SetResourceName(vrIntermediateDepth->resource.get(), "Upscale_Depth_Right");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		vrIntermediateDepth->CreateSRV(srvDesc);
	}

	// All buffers are per-eye: Streamline validates all extents against the input color texture
	// dimensions, so every tagged resource must be isolated per-eye at {0,0}.
	for (int i = 0; i < 2; i++) {
		std::string suffix = (i == 0) ? "Left" : "Right";

		vrIntermediateColorIn[i] = CreateTextureFromSource(colorSrc, inWidth, inHeight, false, true, true, ("Upscale_ColorIn_" + suffix).c_str());
		vrIntermediateColorOut[i] = CreateTextureFromSource(colorSrc, outWidth, outHeight, false, true, false, ("Upscale_ColorOut_" + suffix).c_str());

		// Linear depth: R32_FLOAT so FSR's GetFfxResourceDescriptionDX11() returns a valid format.
		// EncodeTexturesCS writes the non-linear depth as R32_FLOAT for FSR. Kept separate from
		// vrIntermediateDepth (R24G8_TYPELESS) which Streamline copies into for DLSS right eye.
		{
			D3D11_TEXTURE2D_DESC ldDesc = {};
			ldDesc.Width = inWidth;
			ldDesc.Height = inHeight;
			ldDesc.MipLevels = 1;
			ldDesc.ArraySize = 1;
			ldDesc.Format = DXGI_FORMAT_R32_FLOAT;
			ldDesc.SampleDesc.Count = 1;
			ldDesc.Usage = D3D11_USAGE_DEFAULT;
			ldDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			vrIntermediateLinearDepth[i] = eastl::make_unique<Texture2D>(ldDesc);

			Util::SetResourceName(vrIntermediateLinearDepth[i]->resource.get(), ("Upscale_LinearDepth_" + suffix).c_str());

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc2 = {};
			srvDesc2.Format = DXGI_FORMAT_R32_FLOAT;
			srvDesc2.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc2.Texture2D.MipLevels = 1;
			vrIntermediateLinearDepth[i]->CreateSRV(srvDesc2);

			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc2 = {};
			uavDesc2.Format = DXGI_FORMAT_R32_FLOAT;
			uavDesc2.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc2.Texture2D.MipSlice = 0;
			vrIntermediateLinearDepth[i]->CreateUAV(uavDesc2);
		}

		// UAV required: EncodeTexturesCS writes directly into these per-eye buffers
		vrIntermediateMotionVectors[i] = CreateTextureFromSource(mvecSrc, inWidth, inHeight, false, true, true, ("Upscale_MVec_" + suffix).c_str());
		vrIntermediateReactiveMask[i] = CreateTextureFromSource(reactiveSrc, inWidth, inHeight, false, true, true, ("Upscale_Reactive_" + suffix).c_str());
		vrIntermediateTransparencyMask[i] = CreateTextureFromSource(transparencySrc, inWidth, inHeight, false, true, true, ("Upscale_Transparency_" + suffix).c_str());
	}

	logger::info("[Upscaling] Created VR intermediate textures: per-eye in {}x{}, out {}x{}",
		inWidth, inHeight, outWidth, outHeight);
}

void Upscaling::EnsureVRIntermediateTextures()
{
	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	auto& motionVectorRT = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

	auto screenSize = globals::state->screenSize;
	auto renderSize = Util::ConvertToDynamic(screenSize);

	uint32_t eyeWidthOut = (uint32_t)(screenSize.x / 2);
	uint32_t eyeHeightOut = (uint32_t)screenSize.y;
	uint32_t eyeWidthIn = (uint32_t)(renderSize.x / 2);
	uint32_t eyeHeightIn = (uint32_t)renderSize.y;

	bool needsRecreate = !vrIntermediateColorIn[0] || !vrIntermediateColorOut[0] || !vrIntermediateLinearDepth[0];
	if (!needsRecreate) {
		needsRecreate = (vrIntermediateColorIn[0]->desc.Width != eyeWidthIn ||
						 vrIntermediateColorIn[0]->desc.Height != eyeHeightIn ||
						 vrIntermediateColorOut[0]->desc.Width != eyeWidthOut ||
						 vrIntermediateColorOut[0]->desc.Height != eyeHeightOut);
	}
	if (needsRecreate) {
		logger::info("[Upscaling] (Re)creating VR intermediates: per-eye in {}x{}, out {}x{}",
			eyeWidthIn, eyeHeightIn, eyeWidthOut, eyeHeightOut);
		CreateVRIntermediateTextures(eyeWidthIn, eyeHeightIn, eyeWidthOut, eyeHeightOut,
			main.texture, motionVectorRT.texture,
			reactiveMaskTexture->resource.get(), transparencyCompositionMaskTexture->resource.get());
	}
}

void Upscaling::PreparePerEyeInputs(ID3D11Resource* colorSrc)
{
	if (!globals::game::isVR)
		return;

	auto state = globals::state;
	if (state->frameAnnotations)
		state->BeginPerfEvent("VR Upscaling Prepare");

	auto context = globals::d3d::context;
	auto renderSize = Util::ConvertToDynamic(globals::state->screenSize);

	uint32_t eyeWidthIn = (uint32_t)(renderSize.x / 2);
	uint32_t eyeHeightIn = (uint32_t)renderSize.y;

	// Textures guaranteed to exist: EnsureVRIntermediateTextures() was called in Upscale()
	// Read the original game depth SRV for ClearHMDMask — the combined stereo buffer is
	// definitively valid here, whereas the per-eye copy may silently produce zeros on some
	// depth-stencil format / driver combinations.
	auto& depthTexture = globals::game::renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	auto& motionVectorRT = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

	for (uint32_t i = 0; i < 2; ++i) {
		uint32_t offsetXIn = (i == 1) ? eyeWidthIn : 0;
		D3D11_BOX srcBox = { offsetXIn, 0, 0, offsetXIn + eyeWidthIn, eyeHeightIn, 1 };

		context->CopySubresourceRegion(vrIntermediateColorIn[i]->resource.get(), 0, 0, 0, 0, colorSrc, 0, &srcBox);
		context->CopySubresourceRegion(vrIntermediateMotionVectors[i]->resource.get(), 0, 0, 0, 0, motionVectorRT.texture, 0, &srcBox);

		uint32_t depthOffset = (i == 1) ? eyeWidthIn : 0;
		ClearHMDMask(vrIntermediateColorIn[i]->uav.get(), depthTexture.depthSRV,
			eyeWidthIn, eyeHeightIn, depthOffset, 0);
	}

	if (state->frameAnnotations)
		state->EndPerfEvent();
}

void Upscaling::FinalizePerEyeOutputs(ID3D11Resource* colorDst)
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "VR Upscaling - Finalize Per Eye");

	if (!globals::game::isVR)
		return;

	auto state = globals::state;
	if (state->frameAnnotations)
		state->BeginPerfEvent("VR Upscaling Finalize");

	auto context = globals::d3d::context;
	auto screenSize = state->screenSize;

	uint32_t eyeWidthOut = (uint32_t)(screenSize.x / 2);
	uint32_t eyeHeightOut = (uint32_t)screenSize.y;

	// Write upscaled outputs back
	for (uint32_t i = 0; i < 2; ++i) {
		uint32_t offsetXOut = (i == 1) ? eyeWidthOut : 0;
		D3D11_BOX outBox = { 0, 0, 0, eyeWidthOut, eyeHeightOut, 1 };
		context->CopySubresourceRegion(colorDst, 0, offsetXOut, 0, 0, vrIntermediateColorOut[i]->resource.get(), 0, &outBox);
	}

	if (state->frameAnnotations)
		state->EndPerfEvent();
}

void Upscaling::ClearHMDMask(ID3D11UnorderedAccessView* colorUAV, ID3D11ShaderResourceView* depthSRV,
	uint32_t eyeWidth, uint32_t eyeHeight, uint32_t depthOffsetX, uint32_t colorOffsetX)
{
	if (!globals::game::isVR)
		return;

	auto context = globals::d3d::context;

	if (!vrClearHMDMaskCS) {
		vrClearHMDMaskCS.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data/Shaders/Upscaling/ClearHMDMaskCS.hlsl", {}, "cs_5_0"));

		D3D11_BUFFER_DESC cbDesc = {};
		cbDesc.ByteWidth = 16;  // 4 uints
		cbDesc.Usage = D3D11_USAGE_DYNAMIC;
		cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		DX::ThrowIfFailed(globals::d3d::device->CreateBuffer(&cbDesc, nullptr, vrClearHMDMaskCB.put()));
	}

	if (vrClearHMDMaskCS) {
		auto dispatchX = (eyeWidth + 7) / 8;
		auto dispatchY = (eyeHeight + 7) / 8;

		context->CSSetShader(vrClearHMDMaskCS.get(), nullptr, 0);

		ID3D11ShaderResourceView* srvs[1] = { depthSRV };
		context->CSSetShaderResources(0, 1, srvs);

		ID3D11UnorderedAccessView* uavs[1] = { colorUAV };
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

		D3D11_MAPPED_SUBRESOURCE mapped{};
		context->Map(vrClearHMDMaskCB.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);

		uint32_t offsets[4] = { depthOffsetX, colorOffsetX, 0, 0 };

		memcpy(mapped.pData, offsets, sizeof(offsets));
		context->Unmap(vrClearHMDMaskCB.get(), 0);

		ID3D11Buffer* cbs[1] = { vrClearHMDMaskCB.get() };
		context->CSSetConstantBuffers(0, 1, cbs);

		context->Dispatch(dispatchX, dispatchY, 1);

		// Unbind
		ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
		ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
		ID3D11Buffer* nullCB[1] = { nullptr };
		context->CSSetShaderResources(0, 1, nullSRV);
		context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
		context->CSSetConstantBuffers(0, 1, nullCB);
		context->CSSetShader(nullptr, nullptr, 0);
	}
}

int32_t GetJitterPhaseCount(int32_t renderWidth, int32_t displayWidth)
{
	const float basePhaseCount = 8.0f;
	const int32_t jitterPhaseCount = int32_t(basePhaseCount * pow((float(displayWidth) / renderWidth), 2.0f));
	return jitterPhaseCount;
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

	auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
	GET_INSTANCE_MEMBER(BSImagespaceShaderISTemporalAA, imageSpaceManager);

	// Force enable TAA if needed
	BSImagespaceShaderISTemporalAA->taaEnabled = upscaleMethod != UpscaleMethod::kNONE;
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
	auto screenSize = state->screenSize;

	auto screenWidth = static_cast<int>(screenSize.x);
	auto screenHeight = static_cast<int>(screenSize.y);

	if (upscaleMethod != UpscaleMethod::kNONE && upscaleMethod != UpscaleMethod::kTAA) {
		float resolutionScaleBase = 1.0f / ffxFsr3GetUpscaleRatioFromQualityMode((FfxFsr3QualityMode)settings.qualityMode);

		auto renderWidth = static_cast<int>(screenWidth * resolutionScaleBase);
		auto renderHeight = static_cast<int>(screenHeight * resolutionScaleBase);

		resolutionScale.x = static_cast<float>(renderWidth) / static_cast<float>(screenWidth);
		resolutionScale.y = static_cast<float>(renderHeight) / static_cast<float>(screenHeight);

		auto phaseCount = GetJitterPhaseCount(renderWidth, screenWidth);

		GetJitterOffset(&jitter.x, &jitter.y, state->frameCount, phaseCount);

		if (globals::game::isVR)
			a_viewport->projectionPosScaleX = -jitter.x / renderWidth;
		else
			a_viewport->projectionPosScaleX = -2.0f * jitter.x / renderWidth;

		a_viewport->projectionPosScaleY = 2.0f * jitter.y / renderHeight;
	} else {
		resolutionScale = { 1.0f, 1.0f };

		if (globals::game::isVR)
			jitter.x = -a_viewport->projectionPosScaleX * screenWidth;
		else
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
	if (!globals::game::isVR)
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

	if (globals::game::isVR) {
		depthStencilDesc.StencilEnable = true;     // Enable stencil testing
		depthStencilDesc.StencilReadMask = 0xFF;   // Read all stencil bits
		depthStencilDesc.StencilWriteMask = 0xFF;  // Write to all stencil bits

		// Configure front-facing stencil operations
		depthStencilDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;       // Replace on stencil fail
		depthStencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;  // Replace on depth fail
		depthStencilDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;    // Replace on pass
		depthStencilDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;       // Always pass stencil test

		// Configure back-facing stencil operations (same as front)
		depthStencilDesc.BackFace.StencilFailOp = depthStencilDesc.FrontFace.StencilFailOp;
		depthStencilDesc.BackFace.StencilDepthFailOp = depthStencilDesc.FrontFace.StencilDepthFailOp;
		depthStencilDesc.BackFace.StencilPassOp = depthStencilDesc.FrontFace.StencilPassOp;
		depthStencilDesc.BackFace.StencilFunc = depthStencilDesc.FrontFace.StencilFunc;
	} else {
		depthStencilDesc.StencilEnable = false;  // Disable stencil testing
	}

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

	// Setup HDR resources only when the HDR Display feature is loaded
	if (globals::features::hdrDisplay.loaded) {
		globals::features::hdrDisplay.SetupResources();
	}
}

void Upscaling::ClearShaderCache()
{
	for (int i = 0; i < 5; ++i) {
		encodeTexturesCS[i] = nullptr;  // com_ptr automatically releases
	}
	encodeTexturesCSDepthOutput = nullptr;

	depthRefractionUpscalePS = nullptr;  // com_ptr automatically releases
	underwaterMaskUpscalePS = nullptr;   // com_ptr automatically releases
	upscaleVS = nullptr;                 // com_ptr automatically releases
}

void Upscaling::CopySharedD3D12Resources()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Upscaling - Copy Shared D3D12 Resources");
	globals::state->BeginPerfEvent("Copy Shared D3D12 Resources");

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	context->CopyResource(dx12SwapChain.motionVectorBufferShared12->resource11, motionVector.texture);

	auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

	{
		// Set up viewport for fullscreen rendering
		auto screenSize = globals::state->screenSize;

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

		context->Draw(3, 0);
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
	return d3d12SwapChainActive && !globals::game::isVR;
}

bool Upscaling::IsFrameGenerationActive() const
{
	return IsFrameGenerationDx12PathActive() && settings.frameGenerationMode && fidelityFX.isFrameGenActive;
}

bool Upscaling::ShouldUseFrameGenerationThisFrame() const
{
	auto* ui = globals::game::ui;
	return IsFrameGenerationDx12PathActive() &&
	       settings.frameGenerationMode &&
	       ui && !ui->GameIsPaused() &&
	       !globals::state->IsMainOrLoadingMenuOpen(ui);
}

bool Upscaling::IsUpscalingActive() const
{
	auto method = GetUpscaleMethod();

	// Only consider vendor upscalers (FSR/DLSS) as "active" when the
	// selected method actually produces a downscale. If the renderer is
	// currently running at 1:1 (no downscale), treat upscaling as inactive.
	if (!(method == UpscaleMethod::kFSR || method == UpscaleMethod::kDLSS)) {
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
	return fidelityFX.featureFSR3FG;
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

void Upscaling::CreateProxySwapChain(IDXGIAdapter* adapter, DXGI_SWAP_CHAIN_DESC swapChainDesc)
{
	dx12SwapChain.CreateSwapChain(adapter, swapChainDesc);
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
		state->BeginPerfEvent("Encode Upscaling Textures");
		TracyD3D11Zone(globals::state->tracyCtx, "Encode Upscaling Textures");

		auto& temporalAAMask = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kTEMPORAL_AA_MASK];
		auto& normals = renderer->GetRuntimeData().renderTargets[globals::deferred->forwardRenderTargets[2]];
		auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

		// VR: ensure per-eye intermediate textures exist before the dispatch writes into them
		if (globals::game::isVR)
			EnsureVRIntermediateTextures();

		auto renderSize = Util::ConvertToDynamic(globals::state->screenSize);
		uint32_t numEyes = globals::game::isVR ? 2 : 1;
		uint32_t eyeRenderWidth = (uint32_t)(renderSize.x / numEyes);
		uint32_t eyeRenderHeight = (uint32_t)renderSize.y;

		// Sources are the same combined stereo buffers for both VR and non-VR.
		// The shader applies EyeOffsetX to sample the correct half.
		ID3D11ShaderResourceView* views[4] = { temporalAAMask.SRV, normals.SRV, motionVector.SRV, depth.depthSRV };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);
		context->CSSetShader(GetEncodeTexturesCS(), nullptr, 0);

		for (uint32_t i = 0; i < numEyes; ++i) {
			uint32_t offsetX = i * eyeRenderWidth;

			UpscalingDataCB upscalingData;
			upscalingData.trueSamplingDim = float2((float)eyeRenderWidth, (float)eyeRenderHeight);
			upscalingData.eyeOffsetX = offsetX;
			upscalingDataCB->Update(upscalingData);
			auto upscalingBuffer = upscalingDataCB->CB();
			context->CSSetConstantBuffers(0, 1, &upscalingBuffer);

			// u2 (MotionVectorOutput): DLSS only — 5x5 dilated MVec for ghosting reduction.
			// u3 (DepthOutput): VR FSR only — converts R24G8_TYPELESS to R32_FLOAT so
			//   GetFfxResourceDescriptionDX11() returns a valid format. DLSS depth is copied in Streamline.cpp.
			ID3D11UnorderedAccessView* uavs[4] = {
				globals::game::isVR ? vrIntermediateReactiveMask[i]->uav.get() : reactiveMaskTexture->uav.get(),
				globals::game::isVR ? vrIntermediateTransparencyMask[i]->uav.get() : transparencyCompositionMaskTexture->uav.get(),
				(upscaleMethod == UpscaleMethod::kDLSS) ? (globals::game::isVR ? vrIntermediateMotionVectors[i]->uav.get() : motionVectorCopyTexture->uav.get()) : nullptr,
				(upscaleMethod == UpscaleMethod::kFSR && globals::game::isVR) ? vrIntermediateLinearDepth[i]->uav.get() : nullptr
			};
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			context->Dispatch((eyeRenderWidth + 7) / 8, (eyeRenderHeight + 7) / 8, 1);
		}

		ID3D11ShaderResourceView* nullViews[4] = { nullptr, nullptr, nullptr, nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(nullViews), nullViews);

		ID3D11UnorderedAccessView* nullUAVs[4] = { nullptr, nullptr, nullptr, nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAVs), nullUAVs, nullptr);

		ID3D11Buffer* nullBuffer = nullptr;
		context->CSSetConstantBuffers(0, 1, &nullBuffer);

		ID3D11ComputeShader* shader = nullptr;
		context->CSSetShader(shader, nullptr, 0);

		state->EndPerfEvent();
	}

	{
		state->BeginPerfEvent("Upscaling");
		TracyD3D11Zone(globals::state->tracyCtx, "Upscaling Dispatch");

		if (upscaleMethod == UpscaleMethod::kDLSS) {
			// VR-only workaround: a worldspace/cell transition causes ~2-3ms persistent GPU-time
			// regression in the DLSS feature that only clears on a manual mode/preset toggle.
			// Mirror that toggle by tearing down the DLSS feature on LoadingMenu close — the next
			// SetDLSSOptions/slEvaluateFeature call below recreates it with current per-eye extents.
			if (globals::game::isVR && pendingDLSSReset.exchange(false, std::memory_order_relaxed)) {
				logger::debug("[Upscaling] LoadingMenu close detected — rebuilding DLSS feature");
				streamline.DestroyDLSSResources();
			}
			streamline.Upscale(main.texture, reactiveMaskTexture->resource.get(), transparencyCompositionMaskTexture->resource.get(), motionVectorCopyTexture->resource.get());
		} else if (upscaleMethod == UpscaleMethod::kFSR) {
			fidelityFX.Upscale(main.texture, reactiveMaskTexture->resource.get(), transparencyCompositionMaskTexture->resource.get(), motionVector.texture, settings.sharpnessFSR);
		}

		state->EndPerfEvent();
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

	auto screenSize = state->screenSize;
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
	if (globals::game::isVR && (!depthCopy.views[0] || !depthCopy.stencilSRV)) {
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

		// Engine copies kMAIN→kMAIN_COPY during 3D scene rendering.
		// In non-3D contexts (map, main menu, loading, pause) the engine skips its copy.
		auto* ui = globals::game::ui;
		const bool inMenuContext = globals::state->isMapMenuOpen ||
		                           globals::state->isMainMenuOpen ||
		                           globals::state->isLoadingMenuOpen ||
		                           (ui && ui->GameIsPaused());
		if (inMenuContext) {
			copyIfNonAliased(depthCopy.texture, depth.texture);
		}

		// Clear stencil to be 0xFF
		if (globals::game::isVR) {
			context->ClearDepthStencilView(depthCopy.views[0], D3D11_CLEAR_STENCIL, 1.0f, 0xFF);
		}

		// Set depth stencil state to write 0x00
		context->OMSetDepthStencilState(upscaleDepthStencilState.get(), 0x00);

		copyIfNonAliased(refractionNormals.textureCopy, refractionNormals.texture);

		ID3D11ShaderResourceView* srvs[] = { refractionNormals.SRVCopy, depthCopy.depthSRV, depthCopy.stencilSRV };
		context->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		// kSAO_CAMERAZ is at quarter-stereo resolution in VR; the full-stereo viewport would
		// corrupt only the top-left quarter. The engine's ISSAOCameraZ pass populates it correctly.
		ID3D11RenderTargetView* rtvs[] = { refractionNormals.RTV,
			globals::game::isVR ? nullptr : saoCameraZ.RTV };
		context->OMSetRenderTargets(2, rtvs, depth.views[0]);

		context->PSSetShader(depthUpscalePS, nullptr, 0);
		context->Draw(3, 0);
	}

	{
		TracyD3D11Zone(globals::state->tracyCtx, "Upscaling - Underwater Mask");

		viewport.Width = screenSize.x * 0.5f;
		viewport.Height = screenSize.y * 0.5f;
		context->RSSetViewports(1, &viewport);

		copyIfNonAliased(underwaterMask.textureCopy, underwaterMask.texture);

		context->OMSetDepthStencilState(nullptr, 0x00);

		// t0: vanilla mask copy, t1: original depth (for VR per-eye analytical mask).
		// depthCopy still holds the original pre-upscale depth here (VR re-copy deferred).
		ID3D11ShaderResourceView* srvs[] = { underwaterMask.SRVCopy, depthCopy.depthSRV };
		context->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11RenderTargetView* rtvs[] = { underwaterMask.RTV };
		context->OMSetRenderTargets(ARRAYSIZE(rtvs), rtvs, nullptr);

		context->PSSetShader(underwaterMaskPS, nullptr, 0);
		context->Draw(3, 0);
	}

	// Now propagate the upscaled depth to kMAIN_COPY so downstream VR passes see it.
	if (globals::game::isVR) {
		TracyD3D11Zone(globals::state->tracyCtx, "Upscaling - Depth VR Propagate");
		copyIfNonAliased(depthCopy.texture, depth.texture);
	}

	ID3D11ShaderResourceView* nullPSResources[3] = { nullptr, nullptr, nullptr };
	context->PSSetShaderResources(0, ARRAYSIZE(nullPSResources), nullPSResources);

	state->EndPerfEvent();
}

void Upscaling::ApplySharpening()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Upscaling - Sharpening");

	if (settings.sharpnessDLSS <= 0.0f)
		return;

	if (!sharpenerTexture)
		return;

	float currentSharpness = (-2.0f * settings.sharpnessDLSS) + 2.0f;
	currentSharpness = exp2(-currentSharpness);

	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	if (!main.UAV)
		return;

	context->OMSetRenderTargets(0, nullptr, nullptr);

	// Zero-copy path: DLSS has already written to sharpenerTexture; sharpen directly into kMAIN.UAV.
	rcas.ApplySharpen(sharpenerTexture->srv.get(), main.UAV, currentSharpness);

	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
}

void Upscaling::Main_UpdateJitter::thunk(RE::BSGraphics::State* a_state)
{
	globals::features::upscaling.ConfigureTAA();
	func(a_state);
	globals::features::upscaling.ConfigureUpscaling(a_state);
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

	auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
	GET_INSTANCE_MEMBER(BSImagespaceShaderISTemporalAA, imageSpaceManager);

	BSImagespaceShaderISTemporalAA->taaEnabled = upscaleMethod == UpscaleMethod::kTAA;

	// Redirect kFRAMEBUFFER to float texture before ISHDR runs so HDR values >1.0 survive
	// When HDR Display is not loaded, ISHDR writes to vanilla kFRAMEBUFFER (SDR path)
	bool hdrLoaded = globals::features::hdrDisplay.loaded;
	if (hdrLoaded)
		globals::features::hdrDisplay.RedirectFramebuffer();

	func(a_this, a3, a_target, a_4, a_5);

	// Restore kFRAMEBUFFER after ISHDR — hdrTexture now has the HDR scene
	if (hdrLoaded)
		globals::features::hdrDisplay.RestoreFramebuffer();

	BSImagespaceShaderISTemporalAA->taaEnabled = false;
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
