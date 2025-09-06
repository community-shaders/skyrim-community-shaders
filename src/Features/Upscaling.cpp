#include "Upscaling.h"

#include "Deferred.h"
#include "Hooks.h"
#include "State.h"
#include "Upscaling/DX12SwapChain.h"
#include "Upscaling/FidelityFX.h"
#include "Upscaling/Streamline.h"
#include "Upscaling/XeSS.h"
#include <Windows.h>
#include <directx/d3dx12.h>
#include <reshade/reshade.hpp>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Upscaling::Settings,
	upscaleMethod,
	upscaleMethodNoDLSS,
	qualityMode,
	frameLimitMode,
	frameGenerationMode,
	frameGenerationForceEnable,
	streamlineLogLevel);

// D3D hook function pointers and implementations
decltype(&CreateDXGIFactory) ptrCreateDXGIFactory;

HRESULT WINAPI hk_CreateDXGIFactory(REFIID, void** ppFactory)
{
	return ptrCreateDXGIFactory(__uuidof(IDXGIFactory4), ppFactory);
}

decltype(&D3D11CreateDeviceAndSwapChain) ptrD3D11CreateDeviceAndSwapChain;

/**
 * @brief Creates a Direct3D 11 device and swap chain, with support for advanced upscaling and frame generation features.
 *
 * This function intercepts the standard D3D11 device and swap chain creation process to enable integration with Streamline and FidelityFX technologies, as well as optional D3D12 proxying for frame generation. It adjusts swap chain flags for tearing support, manages feature checks, and conditionally routes device creation through Streamline or FidelityFX proxies based on runtime settings and hardware capabilities. If frame generation is enabled and supported, a D3D12 proxy is used; otherwise, the standard D3D11 creation path is followed.
 *
 * @return HRESULT indicating the success or failure of device and swap chain creation.
 */
HRESULT WINAPI hk_D3D11CreateDeviceAndSwapChain(
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

	if (!globals::game::isVR) {
		// Use better swap effect to prevent tearing and improve performance
		pSwapChainDesc->SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

		// Set new more precise format
		pSwapChainDesc->BufferDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
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

	upscaling.CreateSharedD3D12Device(pAdapter);

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

			upscaling.d3d12Interop = true;

			return S_OK;
		} else {
			logger::warn("[Frame Generation] FidelityFX DLLs are not loaded, skipping proxy");
			upscaling.fidelityFXMissing = true;
		}
	}

	auto ret = ptrD3D11CreateDeviceAndSwapChain(pAdapter,
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

	return ret;
}

void Upscaling::DrawSettings()
{
	// Display upscaling options in the UI - build labels with version info
	std::vector<std::string> upscaleModes = { "None", "TAA" };

	std::string fsrLabel = "AMD FSR";
	if (!fidelityFX.versionInfo.empty()) {
		fsrLabel += " " + fidelityFX.versionInfo;
	}
	upscaleModes.push_back(fsrLabel);

	std::string xessLabel = "Intel XeSS";
	if (!xess.versionInfo.empty()) {
		xessLabel += " " + xess.versionInfo;
	}
	upscaleModes.push_back(xessLabel);

	std::string dlssLabel = "NVIDIA DLSS 4 Preset K";
	upscaleModes.push_back(dlssLabel);

	// Determine available modes
	bool featureDLSS = streamline.featureDLSS;

	uint* currentUpscaleMode = &settings.upscaleMethod;
	uint availableModes = 4;

	if (featureDLSS) {
		// All modes available including DLSS
	} else {
		currentUpscaleMode = &settings.upscaleMethodNoDLSS;
		availableModes = 3;
	}

	// Slider for method selection
	std::string currentLabel = upscaleModes[(uint)*currentUpscaleMode];
	ImGui::SliderInt("Method", (int*)currentUpscaleMode, 0, availableModes, currentLabel.c_str());

	*currentUpscaleMode = std::min(availableModes, (uint)*currentUpscaleMode);

	// Check the current upscale method
	auto upscaleMethod = GetUpscaleMethod();

	// Display upscaling settings if applicable
	if (!globals::game::isVR) {
		if (upscaleMethod != UpscaleMethod::kNONE && upscaleMethod != UpscaleMethod::kTAA) {
			const char* upscalePresetsDLSS[] = { "Ultra Performance", "Performance", "Balanced", "Quality", "DLAA" };
			const char* upscalePresets[] = { "Ultra Performance", "Performance", "Balanced", "Quality", "Native AA" };

			if (upscaleMethod == UpscaleMethod::kDLSS)
				ImGui::SliderInt("Upscale Preset", (int*)&settings.qualityMode, 0, 4, std::format("{}", upscalePresetsDLSS[4 - settings.qualityMode]).c_str());
			else
				ImGui::SliderInt("Upscale Preset", (int*)&settings.qualityMode, 0, 4, std::format("{}", upscalePresets[4 - settings.qualityMode]).c_str());
		}
	} else {
		ImGui::Text("Upscaling from lower resolutions is not currently available for VR");
	}

	if (!globals::game::isVR) {
		if (ImGui::TreeNodeEx("Frame Generation", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Text("Frame Generation interpolates real frames with generated ones for a smoother experience");
			ImGui::Text("Uses AMD FSR Frame Generation technology");
			if (fidelityFX.featureFSR3FG)
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

			if (onlyRequiresRestart && settings.frameGenerationMode && !d3d12Interop) {
				ImGui::PushStyleColor(ImGuiCol_Text, Util::Colors::GetWarning());
				ImGui::Text("Warning: Requires restart");
				ImGui::PopStyleColor();
			}

			std::string backendLabel = fidelityFX.isFrameGenActive ? "FSR3" : "None";
			std::string enabledLabel = "Enabled (" + backendLabel + ")";
			const char* toggleModes[] = { "Disabled", "Enabled" };
			const char* toggleModesFG[] = { "Disabled", enabledLabel.c_str() };

			ImGui::SliderInt("Frame Generation", (int*)&settings.frameGenerationMode, 0, 1, toggleModesFG[settings.frameGenerationMode]);

			if (!d3d12Interop)
				ImGui::BeginDisabled();

			ImGui::SliderInt("Frame Limit (Variable Refresh Rate)", (int*)&settings.frameLimitMode, 0, 1, std::format("{}", toggleModes[settings.frameLimitMode]).c_str());

			if (!d3d12Interop)
				ImGui::EndDisabled();

			ImGui::Text("Allows frame generation to function on low refresh rate monitors");
			ImGui::SliderInt("Force Enable Frame Generation", (int*)&settings.frameGenerationForceEnable, 0, 1, std::format("{}", toggleModes[settings.frameGenerationForceEnable]).c_str());

			ImGui::TreePop();
		}
	} else {
		if (ImGui::TreeNodeEx("Frame Generation", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Text("Frame Generation is not available on your system.\nThis requires either NVIDIA DLSS-G or AMD FSR 3.1 Frame Generation support and D3D12 interop.");
			ImGui::TreePop();
		}
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
		ImGui::Separator();
		// FidelityFX section
		if (ImGui::Selectable("AMD FidelityFX DLLs (click to open folder)")) {
			ShellExecuteW(nullptr, L"open", FidelityFX::PluginDir, nullptr, nullptr, SW_SHOWNORMAL);
		}
		std::vector<std::string> headers = { "DLL Name", "Version" };
		std::vector<std::vector<std::string>> ffRows;
		for (const auto& [name, dllVersion] : FidelityFX::dllVersions)
			ffRows.push_back({ name, dllVersion });
		std::vector<Util::TableSortFunc> ffSorters = { nullptr, Util::VersionSortComparator };
		Util::ShowSortedStringTableStrings(
			"ffx_dll_versions",
			headers,
			ffRows,
			0,
			true,
			ffSorters);

		// Streamline section
		if (ImGui::Selectable("NVIDIA Streamline DLLs (click to open folder)")) {
			ShellExecuteW(nullptr, L"open", Streamline::PluginDir, nullptr, nullptr, SW_SHOWNORMAL);
		}
		std::vector<std::vector<std::string>> slRows;
		for (const auto& [name, dllVersion] : Streamline::dllVersions)
			slRows.push_back({ name, dllVersion });
		std::vector<Util::TableSortFunc> slSorters = { nullptr, Util::VersionSortComparator };
		Util::ShowSortedStringTableStrings(
			"sl_dll_versions",
			headers,
			slRows,
			0,
			true,
			slSorters);
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

void Upscaling::Load()
{
	logger::info("[Upscaling] Load: Installing D3D IAT hooks and loading upscaling SDKs");
	*(uintptr_t*)&ptrD3D11CreateDeviceAndSwapChain = SKSE::PatchIAT(hk_D3D11CreateDeviceAndSwapChain, "d3d11.dll", "D3D11CreateDeviceAndSwapChain");
	*(uintptr_t*)&ptrCreateDXGIFactory = SKSE::PatchIAT(hk_CreateDXGIFactory, "dxgi.dll", !REL::Module::IsVR() ? "CreateDXGIFactory" : "CreateDXGIFactory1");
}

struct CreateRenderTarget_LDR1
{
	static void thunk(RE::BSGraphics::Renderer* This, RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties)
	{
		RE::BSGraphics::RenderTargetProperties properties = *a_properties;
		properties.format.set(RE::BSGraphics::Format::kR10G10B10A2_UNORM);
		func(This, a_target, &properties);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct CreateRenderTarget_LDR2
{
	static void thunk(RE::BSGraphics::Renderer* This, RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties)
	{
		RE::BSGraphics::RenderTargetProperties properties = *a_properties;
		properties.format.set(RE::BSGraphics::Format::kR10G10B10A2_UNORM);
		func(This, a_target, &properties);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct CreateRenderTarget_LDR3
{
	static void thunk(RE::BSGraphics::Renderer* This, RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties)
	{
		RE::BSGraphics::RenderTargetProperties properties = *a_properties;
		properties.format.set(RE::BSGraphics::Format::kR10G10B10A2_UNORM);
		func(This, a_target, &properties);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct CreateRenderTarget_LDR4
{
	static void thunk(RE::BSGraphics::Renderer* This, RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties)
	{
		RE::BSGraphics::RenderTargetProperties properties = *a_properties;
		properties.format.set(RE::BSGraphics::Format::kR10G10B10A2_UNORM);
		func(This, a_target, &properties);
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

	if (!REL::Module::IsVR()) {
		// Patches render target creation to use higher precision format
		stl::write_thunk_call<CreateRenderTarget_LDR1>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0x529, 0x528));
		stl::write_thunk_call<CreateRenderTarget_LDR2>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0xB2E, 0xB2E));
		stl::write_thunk_call<CreateRenderTarget_LDR3>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0x62F, 0x62E));
		stl::write_thunk_call<CreateRenderTarget_LDR4>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0x642, 0x641));

		// Patches RSSetScissorRect calls to use dynamic resolution
		// This is a PC-specific function hence it was missing
		stl::detour_thunk<SetScissorRect>(REL::RelocationID(75564, 77365));

		// Patches facegen texture generation to not use dynamic resolution
		stl::detour_thunk<BSFaceGenManager_UpdatePendingCustomizationTextures>(REL::RelocationID(26455, 27041));

		// Patches precipitation camera to not use dynamic resolution
		stl::write_thunk_call<Main_RenderPrecipitation>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x3A1, 0x3A1, 0x2FA));
	}

	logger::info("[Upscaling] Installed hooks");
}

Upscaling::UpscaleMethod Upscaling::GetUpscaleMethod()
{
	if (streamline.featureDLSS) {
		settings.upscaleMethod = std::clamp(settings.upscaleMethod, (uint)UpscaleMethod::kNONE, (uint)UpscaleMethod::kDLSS);
		settings.qualityMode = std::clamp(settings.qualityMode, 0u, 4u);
		return (UpscaleMethod)settings.upscaleMethod;
	}

	settings.upscaleMethodNoDLSS = std::clamp(settings.upscaleMethodNoDLSS, (uint)UpscaleMethod::kNONE, (uint)UpscaleMethod::kXESS);
	settings.qualityMode = std::clamp(settings.qualityMode, 0u, 4u);
	return (UpscaleMethod)settings.upscaleMethodNoDLSS;
}

void Upscaling::CreateUpscalingTextureResources(UpscaleMethod a_upscalemethod)
{
	logger::debug("[Upscaling] Creating texture resources for method {}", (int)a_upscalemethod);

	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC texDesc{};
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

	main.texture->GetDesc(&texDesc);
	main.SRV->GetDesc(&srvDesc);
	main.UAV->GetDesc(&uavDesc);

	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	texDesc.Format = DXGI_FORMAT_R8_UNORM;
	srvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	// DLSS uses D3D11 textures (not shared D3D12)
	if (a_upscalemethod == UpscaleMethod::kDLSS) {
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

		if (!motionVectorCopyTexture) {
			auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

			D3D11_TEXTURE2D_DESC motionTexDesc{};
			motionVector.texture->GetDesc(&motionTexDesc);

			texDesc.Format = motionTexDesc.Format;
			srvDesc.Format = texDesc.Format;
			uavDesc.Format = texDesc.Format;

			motionVectorCopyTexture = new Texture2D(texDesc);
			motionVectorCopyTexture->CreateSRV(srvDesc);
			motionVectorCopyTexture->CreateUAV(uavDesc);
		}
	}

	// Update shared D3D12 resources based on current requirements
	UpdateSharedResources();
}

void Upscaling::DestroyUpscalingTextureResources(UpscaleMethod a_upscalemethod)
{
	logger::debug("[Upscaling] Destroying texture resources for method {}", (int)a_upscalemethod);

	// Clean up D3D11 textures that are no longer needed
	// Only destroy DLSS textures when switching away from DLSS
	if (a_upscalemethod != UpscaleMethod::kDLSS) {
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

		if (motionVectorCopyTexture) {
			motionVectorCopyTexture->srv = nullptr;
			motionVectorCopyTexture->uav = nullptr;
			motionVectorCopyTexture->resource = nullptr;

			delete motionVectorCopyTexture;
			motionVectorCopyTexture = nullptr;
		}
	}

	// Update shared resources to clean up what's no longer needed
	UpdateSharedResources();
}

void Upscaling::CheckResources(UpscaleMethod a_upscalemethod)
{
	static auto previousUpscaleMode = UpscaleMethod::kTAA;
	static bool previousFrameGenMode = false;

	bool frameGenModeChanged = (settings.frameGenerationMode && d3d12Interop) != previousFrameGenMode;
	bool upscaleModeChanged = (previousUpscaleMode != a_upscalemethod);

	if (upscaleModeChanged || frameGenModeChanged) {
		logger::debug("[Upscaling] Resource change detected - Upscale: {} -> {}, FrameGen: {} -> {}",
			(int)previousUpscaleMode, (int)a_upscalemethod, previousFrameGenMode, (settings.frameGenerationMode && d3d12Interop));

		// Synchronise all pending GPU work before destroying contexts
		// Otherwise resources will be destroyed whilst in use, causing the device to crash
		if (previousUpscaleMode == UpscaleMethod::kFSR || previousUpscaleMode == UpscaleMethod::kXESS) {
			UINT64 fenceValue = sharedInteropFenceValue++;
			DX::ThrowIfFailed(sharedD3D12CommandQueue->Signal(sharedD3D12Fence.get(), fenceValue));
			if (sharedD3D12Fence->GetCompletedValue() < fenceValue) {
				sharedD3D12Fence->SetEventOnCompletion(fenceValue, sharedFenceEvent);
				WaitForSingleObject(sharedFenceEvent, INFINITE);
			}
		}

		// Destroy previous upscaling method resources (this will intelligently clean up based on what's still needed)
		if (upscaleModeChanged) {
			DestroyUpscalingTextureResources(a_upscalemethod);

			if (previousUpscaleMode == UpscaleMethod::kDLSS)
				streamline.DestroyDLSSResources();
			else if (previousUpscaleMode == UpscaleMethod::kFSR)
				fidelityFX.DestroyFSRResources();
			else if (previousUpscaleMode == UpscaleMethod::kXESS)
				xess.DestroyXeSSResources();
		}

		// Handle frame generation resource changes
		if (frameGenModeChanged) {
			// Update shared resources based on new frame generation state
			UpdateSharedResources();
		}

		// Create new upscaling method resources
		if (upscaleModeChanged) {
			CreateUpscalingTextureResources(a_upscalemethod);

			if (a_upscalemethod == UpscaleMethod::kFSR)
				fidelityFX.CreateFSRResources();
			else if (a_upscalemethod == UpscaleMethod::kXESS)
				xess.CreateXeSSResources();
		}

		previousUpscaleMode = a_upscalemethod;
		previousFrameGenMode = (settings.frameGenerationMode && d3d12Interop);
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
		case UpscaleMethod::kFSR:
			defines.push_back({ "FSR", "" });
			break;
		case UpscaleMethod::kXESS:
			defines.push_back({ "XESS", "" });
			break;
		case UpscaleMethod::kDLSS:
			defines.push_back({ "DLSS", "" });
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

void Upscaling::ConfigureUpscaling(RE::BSGraphics::State* a_viewport)
{
	auto upscaleMethod = GetUpscaleMethod();

	// Delete or create resources as necessary
	CheckResources(upscaleMethod);

	// The game defaults this to a non-zero value
	if (!globals::game::isVR) {
		auto fDRClampOffset = RE::GetINISetting("fDRClampOffset:Display");
		fDRClampOffset->data.f = 0.0f;
	}

	auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
	GET_INSTANCE_MEMBER(BSImagespaceShaderISTemporalAA, imageSpaceManager);

	// Disable water TAA when upscaling is enabled
	bool* enableWaterTAA = reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(BSImagespaceShaderISTemporalAA) + 0x38LL);
	*enableWaterTAA = upscaleMethod == UpscaleMethod::kNONE || upscaleMethod == UpscaleMethod::kTAA;

	// Force enable TAA if needed
	BSImagespaceShaderISTemporalAA->taaEnabled = upscaleMethod != UpscaleMethod::kNONE;

	// Cache original TAA values for UI
	projectionPosScaleX = a_viewport->projectionPosScaleX;
	projectionPosScaleY = a_viewport->projectionPosScaleY;

	// Get full screen size
	auto state = globals::state;
	auto screenSize = state->screenSize;

	auto screenWidth = static_cast<int>(screenSize.x);
	auto screenHeight = static_cast<int>(screenSize.y);

	if (upscaleMethod != UpscaleMethod::kNONE && upscaleMethod != UpscaleMethod::kTAA) {
		if (globals::game::isVR) {
			resolutionScale = 1.0f;
		} else if (upscaleMethod == UpscaleMethod::kXESS) {
			resolutionScale = xess.GetInputResolutionScale((uint32_t)screenSize.x, (uint32_t)screenSize.y, settings.qualityMode);
		} else if (upscaleMethod == UpscaleMethod::kDLSS) {
			resolutionScale = streamline.GetInputResolutionScale((uint32_t)screenSize.x, (uint32_t)screenSize.y, settings.qualityMode);
		} else if (upscaleMethod == UpscaleMethod::kFSR) {
			resolutionScale = fidelityFX.GetInputResolutionScale((uint32_t)screenSize.x, (uint32_t)screenSize.y, settings.qualityMode);
		}

		auto renderWidth = static_cast<int>(screenWidth * resolutionScale);

		auto renderHeight = static_cast<int>(screenHeight * resolutionScale);

		auto phaseCount = GetJitterPhaseCount(renderWidth, screenWidth);

		GetJitterOffset(&jitter.x, &jitter.y, state->frameCount, phaseCount);

		if (globals::game::isVR)
			a_viewport->projectionPosScaleX = -jitter.x / renderWidth;
		else
			a_viewport->projectionPosScaleX = -2.0f * jitter.x / renderWidth;

		a_viewport->projectionPosScaleY = 2.0f * jitter.y / renderHeight;
	} else {
		resolutionScale = 1.0f;

		if (globals::game::isVR)
			jitter.x = -a_viewport->projectionPosScaleX * screenWidth;
		else
			jitter.x = -a_viewport->projectionPosScaleX * screenWidth / 2.0f;

		jitter.y = a_viewport->projectionPosScaleY * screenHeight / 2.0f;
	}

	auto& runtimeData = a_viewport->GetRuntimeData();

	runtimeData.dynamicResolutionPreviousWidthRatio = dynamicResolutionWidthRatio;
	runtimeData.dynamicResolutionPreviousHeightRatio = dynamicResolutionHeightRatio;
	runtimeData.dynamicResolutionWidthRatio = resolutionScale;
	runtimeData.dynamicResolutionHeightRatio = resolutionScale;

	dynamicResolutionWidthRatio = resolutionScale;
	dynamicResolutionHeightRatio = resolutionScale;

	// Disable dynamic resolution unless the game explictly enables it
	if (!globals::game::isVR)
		runtimeData.dynamicResolutionLock = 1;

	if (upscaleMethod == UpscaleMethod::kTAA)
		resolutionScale = 1.0f;
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

	// Initial resource allocation will be handled by CheckResources() during first upscaling call
	// This avoids creating unnecessary resources at startup

	// Initialize all shared resources based on current settings
	UpdateSharedResources();

	D3D11_DEPTH_STENCIL_DESC depthStencilDesc = {};
	depthStencilDesc.DepthEnable = true;                           // Enable depth testing
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;  // Write to all depth bits
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;          // Always pass depth test (write all depths)

	if (REL::Module::IsVR()) {
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

	// Create shared D3D11/D3D12 fences for synchronization
	winrt::com_ptr<ID3D11Device5> d3d11Device5;
	DX::ThrowIfFailed(globals::d3d::device->QueryInterface(IID_PPV_ARGS(d3d11Device5.put())));

	HANDLE sharedFenceHandle;
	DX::ThrowIfFailed(sharedD3D12Device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(sharedD3D12Fence.put())));
	DX::ThrowIfFailed(sharedD3D12Device->CreateSharedHandle(sharedD3D12Fence.get(), nullptr, GENERIC_ALL, nullptr, &sharedFenceHandle));
	DX::ThrowIfFailed(d3d11Device5->OpenSharedFence(sharedFenceHandle, IID_PPV_ARGS(sharedD3D11Fence.put())));
	CloseHandle(sharedFenceHandle);

	auto upscaleMethod = GetUpscaleMethod();

	// Delete or create resources as necessary
	CheckResources(upscaleMethod);
}

void Upscaling::ClearShaderCache()
{
	for (int i = 0; i < 5; ++i) {
		encodeTexturesCS[i] = nullptr;  // com_ptr automatically releases
	}

	depthRefractionUpscalePS = nullptr;  // com_ptr automatically releases
	underwaterMaskUpscalePS = nullptr;   // com_ptr automatically releases
	upscaleVS = nullptr;                 // com_ptr automatically releases
}

void Upscaling::CreateSharedD3D12Device(IDXGIAdapter* a_dxgiAdapter)
{
	// Create D3D12 device on same adapter
	DX::ThrowIfFailed(D3D12CreateDevice(a_dxgiAdapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(sharedD3D12Device.put())));

	// Create command queue
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.NodeMask = 0;

	DX::ThrowIfFailed(sharedD3D12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(sharedD3D12CommandQueue.put())));

	// Create command allocator
	DX::ThrowIfFailed(sharedD3D12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(sharedD3D12CommandAllocator.put())));

	// Create command list
	DX::ThrowIfFailed(sharedD3D12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, sharedD3D12CommandAllocator.get(), nullptr, IID_PPV_ARGS(sharedD3D12CommandList.put())));

	// Create fence for synchronization
	DX::ThrowIfFailed(sharedD3D12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(sharedD3D12Fence.put())));

	sharedFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (sharedFenceEvent == nullptr) {
		throw std::runtime_error("Failed to create shared fence event");
	}

	// Close initial command list
	sharedD3D12CommandList->Close();

	logger::info("[Upscaling] Shared D3D12 device and interop resources created successfully");
}

void Upscaling::UpdateSharedResources()
{
	logger::info("[Upscaling] Updating shared D3D12 resources");

	auto currentMethod = GetUpscaleMethod();

	// Determine current feature requirements
	bool needsUpscalingResources = (currentMethod == UpscaleMethod::kFSR || currentMethod == UpscaleMethod::kXESS);
	bool needsFSRSpecific = (currentMethod == UpscaleMethod::kFSR);
	bool needsFrameGenResources = (settings.frameGenerationMode && d3d12Interop);
	bool needsSharedBasics = needsUpscalingResources || needsFrameGenResources;

	if (!needsSharedBasics) {
		// Clean up all resources when nothing is needed
		if (inputColorBufferShared12) {
			delete inputColorBufferShared12;
			inputColorBufferShared12 = nullptr;
		}
		if (outputColorBufferShared12) {
			delete outputColorBufferShared12;
			outputColorBufferShared12 = nullptr;
		}
		if (reactiveMaskShared12) {
			delete reactiveMaskShared12;
			reactiveMaskShared12 = nullptr;
		}
		if (transparencyCompositionMaskShared12) {
			delete transparencyCompositionMaskShared12;
			transparencyCompositionMaskShared12 = nullptr;
		}
		if (!d3d12Interop) {
			if (depthBufferShared12) {
				delete depthBufferShared12;
				depthBufferShared12 = nullptr;
			}
			if (motionVectorBufferShared12) {
				delete motionVectorBufferShared12;
				motionVectorBufferShared12 = nullptr;
			}
		}
		copyDepthToSharedBufferPS = nullptr;
		return;
	}

	// Get required interfaces
	winrt::com_ptr<ID3D11Device5> d3d11Device5;
	DX::ThrowIfFailed(globals::d3d::device->QueryInterface(IID_PPV_ARGS(d3d11Device5.put())));

	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC texDesc{};
	main.texture->GetDesc(&texDesc);
	texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

	// Upscaling-specific resources (FSR/XeSS)
	if (needsUpscalingResources) {
		if (!inputColorBufferShared12) {
			inputColorBufferShared12 = new WrappedResource(texDesc, d3d11Device5.get(), sharedD3D12Device.get());
		}
		if (!outputColorBufferShared12) {
			outputColorBufferShared12 = new WrappedResource(texDesc, d3d11Device5.get(), sharedD3D12Device.get());
		}

		texDesc.Format = DXGI_FORMAT_R8_UNORM;
		if (!reactiveMaskShared12) {
			reactiveMaskShared12 = new WrappedResource(texDesc, d3d11Device5.get(), sharedD3D12Device.get());
		}
	} else {
		// Clean up upscaling-only resources
		if (inputColorBufferShared12) {
			delete inputColorBufferShared12;
			inputColorBufferShared12 = nullptr;
		}
		if (outputColorBufferShared12) {
			delete outputColorBufferShared12;
			outputColorBufferShared12 = nullptr;
		}
		if (reactiveMaskShared12) {
			delete reactiveMaskShared12;
			reactiveMaskShared12 = nullptr;
		}
	}

	// FSR-specific resources
	if (needsFSRSpecific) {
		texDesc.Format = DXGI_FORMAT_R8_UNORM;
		if (!transparencyCompositionMaskShared12) {
			transparencyCompositionMaskShared12 = new WrappedResource(texDesc, d3d11Device5.get(), sharedD3D12Device.get());
		}
	} else {
		if (transparencyCompositionMaskShared12) {
			delete transparencyCompositionMaskShared12;
			transparencyCompositionMaskShared12 = nullptr;
		}
	}

	// Shared resources (depth/motion - needed by both upscaling and frame generation)
	if (needsSharedBasics) {
		texDesc.Format = DXGI_FORMAT_R32_FLOAT;
		if (!depthBufferShared12) {
			depthBufferShared12 = new WrappedResource(texDesc, d3d11Device5.get(), sharedD3D12Device.get());
		}

		auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
		motionVector.texture->GetDesc(&texDesc);
		if (!motionVectorBufferShared12) {
			motionVectorBufferShared12 = new WrappedResource(texDesc, d3d11Device5.get(), sharedD3D12Device.get());
		}

		if (!copyDepthToSharedBufferPS) {
			copyDepthToSharedBufferPS.attach((ID3D11PixelShader*)Util::CompileShader(L"Data\\Shaders\\Upscaling\\CopyDepthToSharedBufferPS.hlsl", { { "PSHADER", "" } }, "ps_5_0"));
		}
	} else if (!d3d12Interop) {
		if (depthBufferShared12) {
			delete depthBufferShared12;
			depthBufferShared12 = nullptr;
		}
		if (motionVectorBufferShared12) {
			delete motionVectorBufferShared12;
			motionVectorBufferShared12 = nullptr;
		}
		copyDepthToSharedBufferPS = nullptr;
	}

	logger::info("[Upscaling] Shared resource update complete - Upscaling: {}, FSR: {}, FrameGen: {}",
		needsUpscalingResources, needsFSRSpecific, needsFrameGenResources);
}

void Upscaling::CopySharedD3D12Resources()
{
	// Only copy once per frame for all upscaling systems (XeSS, Frame Generation, etc.)
	if (!sharedResourcesFrameChecker.IsNewFrame())
		return;

	auto upscaleMethod = GetUpscaleMethod();

	globals::state->BeginPerfEvent("Copy Shared D3D12 Resources");

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	// Not required by XeSS
	if (upscaleMethod == UpscaleMethod::kFSR || (d3d12Interop && settings.frameGenerationMode && upscaleMethod != UpscaleMethod::kXESS)) {
		auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

		// Copy only the dynamic resolution area
		auto renderSize = Util::ConvertToDynamic(globals::state->screenSize);
		D3D11_BOX srcBox = {};
		srcBox.left = 0;
		srcBox.top = 0;
		srcBox.front = 0;
		srcBox.right = (UINT)renderSize.x;
		srcBox.bottom = (UINT)renderSize.y;
		srcBox.back = 1;

		context->CopySubresourceRegion(motionVectorBufferShared12->resource11, 0, 0, 0, 0, motionVector.texture, 0, &srcBox);
	}

	if (upscaleMethod == UpscaleMethod::kFSR || upscaleMethod == UpscaleMethod::kXESS || d3d12Interop && settings.frameGenerationMode) {
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
			ID3D11RenderTargetView* rtvs[1] = { depthBufferShared12->rtv };
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
	}

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
	auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
	GET_INSTANCE_MEMBER(BSImagespaceShaderISTemporalAA, imageSpaceManager);

	auto gameIsPaused = globals::game::ui->GameIsPaused();

	if (d3d12Interop && settings.frameGenerationMode)
		BSImagespaceShaderISTemporalAA->taaEnabled = GetUpscaleMethod() != UpscaleMethod::kNONE && gameIsPaused;
	else
		BSImagespaceShaderISTemporalAA->taaEnabled = GetUpscaleMethod() != UpscaleMethod::kNONE;

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

	globals::state->RenderReShade();

	if (d3d12Interop)
		SetUIBuffer();
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
	if (d3d12Interop) {
		// Use frame latency waitable object if available for better frame pacing
		HANDLE waitableObject = GetFrameLatencyWaitableObject();

		// Wait for the next frame presentation slot
		WaitForSingleObject(waitableObject, INFINITE);

		if (settings.frameLimitMode) {
			// Fall back to the original timing method
			// Use integer arithmetic for more precise timing
			int64_t targetFrameTimeNS = int64_t(1000000000.0
				/ (refreshRate
				   * (settings.frameGenerationMode && !globals::game::ui->GameIsPaused()
					  ? 0.5
					  : 1.0)));
			int64_t targetFrameTicks = (targetFrameTimeNS * qpf.QuadPart)
				/ 1000000000LL;

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
					if (DisplayConfigGetDeviceInfo(&sourceName.header) == ERROR_SUCCESS) {
						// find the matched device which is associated with current device
						// there may be the possibility that display may be duplicated and windows may be one of them in such scenario
						// there may be two callback because source is same target will be different
						// as window is on both the display so either selecting either one is ok
						if (wcscmp(info.szDevice, sourceName.viewGdiDeviceName) == 0) {
							// get the refresh rate
							UINT numerator = p.targetInfo.refreshRate.Numerator;
							UINT denominator = p.targetInfo.refreshRate.Denominator;
							return (double)numerator / (double)denominator;
						}
					}
				}
			}
		}
	}
	logger::error("Failed to retrieve refresh rate from swap chain");
	return 60;
}

bool Upscaling::IsFrameGenerationActive() const
{
	return d3d12Interop && settings.frameGenerationMode && fidelityFX.isFrameGenActive;
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
	// Initialize all upscaling SDK components during plugin startup
	// This ensures all SDKs are available before any D3D device creation
	streamline.LoadInterposer();
	fidelityFX.LoadFFX();
	xess.LoadXeSS();
}

void Upscaling::CheckFrameConstants()
{
	streamline.CheckFrameConstants();
}

void Upscaling::SetUIBuffer()
{
	dx12SwapChain.SetUIBuffer();
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

void Upscaling::Upscale()
{
	auto upscaleMethod = GetUpscaleMethod();

	auto state = globals::state;
	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;

	context->OMSetRenderTargets(0, nullptr, nullptr);  // Unbind all bound render targets

	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	auto dispatchCount = Util::GetScreenDispatchCount(true);

	{
		state->BeginPerfEvent("Encode Upscaling Textures");

		auto& temporalAAMask = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kTEMPORAL_AA_MASK];
		auto& normals = renderer->GetRuntimeData().renderTargets[globals::deferred->forwardRenderTargets[2]];
		auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
		auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

		{
			ID3D11ShaderResourceView* views[4] = { temporalAAMask.SRV, normals.SRV, motionVector.SRV, depth.depthSRV };
			context->CSSetShaderResources(0, ARRAYSIZE(views), views);

			// Use shared D3D12 textures for FSR/XeSS, regular D3D11 textures for DLSS
			ID3D11UnorderedAccessView* reactiveMaskUAV = upscaleMethod == UpscaleMethod::kDLSS ? reactiveMaskTexture->uav.get() : reactiveMaskShared12->uav;

			ID3D11UnorderedAccessView* transparencyUAV = nullptr;
			if (upscaleMethod == UpscaleMethod::kDLSS) {
				transparencyUAV = transparencyCompositionMaskTexture->uav.get();
			} else if (upscaleMethod == UpscaleMethod::kFSR) {
				transparencyUAV = transparencyCompositionMaskShared12->uav;
			}

			ID3D11UnorderedAccessView* motionVectorUAV = nullptr;
			if (upscaleMethod == UpscaleMethod::kDLSS) {
				motionVectorUAV = motionVectorCopyTexture->uav.get();
			} else if (upscaleMethod == UpscaleMethod::kXESS) {
				motionVectorUAV = motionVectorBufferShared12->uav;
			}

			ID3D11UnorderedAccessView* uavs[3] = { reactiveMaskUAV, transparencyUAV, motionVectorUAV };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			context->CSSetShader(GetEncodeTexturesCS(), nullptr, 0);

			context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
		}

		ID3D11ShaderResourceView* views[4] = { nullptr, nullptr, nullptr, nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[3] = { nullptr, nullptr, nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11ComputeShader* shader = nullptr;
		context->CSSetShader(shader, nullptr, 0);

		state->EndPerfEvent();
	}

	{
		state->BeginPerfEvent("Upscaling");

		if (upscaleMethod == UpscaleMethod::kDLSS)
			streamline.Upscale(main.texture, reactiveMaskTexture->resource.get(), transparencyCompositionMaskTexture->resource.get(), motionVectorCopyTexture->resource.get(), sl::DLSSPreset::ePresetK);
		else {
			// Copy input color texture to shared D3D12 resource (only dynamic resolution area)
			auto renderSize = Util::ConvertToDynamic(globals::state->screenSize);
			D3D11_BOX srcBox = {};
			srcBox.left = 0;
			srcBox.top = 0;
			srcBox.front = 0;
			srcBox.right = (UINT)renderSize.x;
			srcBox.bottom = (UINT)renderSize.y;
			srcBox.back = 1;

			context->CopySubresourceRegion(inputColorBufferShared12->resource11, 0, 0, 0, 0, main.texture, 0, &srcBox);

			// Wait for D3D11 to finish
			winrt::com_ptr<ID3D11DeviceContext4> d3d11Context4;
			DX::ThrowIfFailed(context->QueryInterface(IID_PPV_ARGS(d3d11Context4.put())));
			DX::ThrowIfFailed(d3d11Context4->Signal(sharedD3D11Fence.get(), sharedInteropFenceValue));
			DX::ThrowIfFailed(sharedD3D12CommandQueue->Wait(sharedD3D12Fence.get(), sharedInteropFenceValue));
			sharedInteropFenceValue++;

			// Reset command allocator and list
			DX::ThrowIfFailed(sharedD3D12CommandAllocator->Reset());
			DX::ThrowIfFailed(sharedD3D12CommandList->Reset(sharedD3D12CommandAllocator.get(), nullptr));

			if (upscaleMethod == UpscaleMethod::kFSR) {
				fidelityFX.Upscale(
					inputColorBufferShared12->resource.get(),
					motionVectorBufferShared12->resource.get(),
					depthBufferShared12->resource.get(),
					reactiveMaskShared12->resource.get(),
					transparencyCompositionMaskShared12->resource.get(),
					outputColorBufferShared12->resource.get(),
					sharedD3D12CommandList.get(),
					(uint32_t)renderSize.x,
					(uint32_t)renderSize.y,
					jitter);
			} else {
				xess.Upscale(
					inputColorBufferShared12->resource.get(),
					motionVectorBufferShared12->resource.get(),
					depthBufferShared12->resource.get(),
					reactiveMaskShared12->resource.get(),
					outputColorBufferShared12->resource.get(),
					sharedD3D12CommandList.get(),
					(uint32_t)renderSize.x,
					(uint32_t)renderSize.y,
					jitter);
			}

			// Close and execute command list
			DX::ThrowIfFailed(sharedD3D12CommandList->Close());

			ID3D12CommandList* commandLists[] = { sharedD3D12CommandList.get() };
			sharedD3D12CommandQueue->ExecuteCommandLists(1, commandLists);

			// Wait for D3D12 to finish
			DX::ThrowIfFailed(sharedD3D12CommandQueue->Signal(sharedD3D12Fence.get(), sharedInteropFenceValue));
			DX::ThrowIfFailed(d3d11Context4->Wait(sharedD3D11Fence.get(), sharedInteropFenceValue));
			sharedInteropFenceValue++;

			// Copy back to main buffer
			context->CopyResource(main.texture, outputColorBufferShared12->resource11);
		}

		state->EndPerfEvent();
	}
}

void Upscaling::PerformUpscaling()
{
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
	if (resolutionScale != 1.0f) {
		globals::state->BeginPerfEvent("Render Target Upscaling");

		auto& renderer = globals::game::renderer;
		auto context = globals::d3d::context;

		// Set up Input Assembler for fullscreen triangle (no vertex/index buffers needed)
		context->IASetInputLayout(nullptr);
		context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
		context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// Set up vertex shader that generates fullscreen triangle using SV_VertexID
		context->VSSetShader(GetUpscaleVS(), nullptr, 0);

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

		// Set rasterizer state
		context->RSSetState(upscaleRasterizerState.get());

		// Set blend state
		context->OMSetBlendState(upscaleBlendState.get(), nullptr, 0xffffffff);

		// Set up pixel shader resources
		auto deferred = globals::deferred;

		ID3D11SamplerState* samplers[] = { deferred->linearSampler };
		context->PSSetSamplers(0, ARRAYSIZE(samplers), samplers);

		// Set up jitter constant buffer for upscaling
		JitterCB jitterData;
		jitterData.jitter = jitter;

		jitterCB->Update(jitterData);
		auto bufferArray = jitterCB->CB();
		context->PSSetConstantBuffers(0, 1, &bufferArray);

		auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

		{
			auto& refractionNormals = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kREFRACTION_NORMALS];
			auto& saoCameraZ = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kSAO_CAMERAZ];

			auto& depthCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN_COPY];

			// Sometimes this is not already copied e.g. map menu
			context->CopyResource(depthCopy.texture, depth.texture);

			// Clear stencil to be 0xFF
			if (globals::game::isVR)
				context->ClearDepthStencilView(depthCopy.views[0], D3D11_CLEAR_STENCIL, 1.0f, 0xFF);

			// Set depth stencil state to write 0x00
			context->OMSetDepthStencilState(upscaleDepthStencilState.get(), 0x00);

			context->CopyResource(refractionNormals.textureCopy, refractionNormals.texture);

			ID3D11ShaderResourceView* srvs[] = { refractionNormals.SRVCopy, depthCopy.depthSRV, depthCopy.stencilSRV };
			context->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

			ID3D11RenderTargetView* rtvs[] = { refractionNormals.RTV, saoCameraZ.RTV };
			context->OMSetRenderTargets(ARRAYSIZE(rtvs), rtvs, depth.views[0]);

			context->PSSetShader(GetDepthRefractionUpscalePS(), nullptr, 0);
			context->Draw(3, 0);

			// Depth copy is also used on VR
			if (globals::game::isVR)
				context->CopyResource(depthCopy.texture, depth.texture);
		}

		{
			viewport.Width = screenSize.x * 0.5f;
			viewport.Height = screenSize.y * 0.5f;
			context->RSSetViewports(1, &viewport);

			auto& underwaterMask = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kUNDERWATER_MASK];

			context->CopyResource(underwaterMask.textureCopy, underwaterMask.texture);

			context->OMSetDepthStencilState(nullptr, 0x00);

			ID3D11ShaderResourceView* srvs[] = { underwaterMask.SRVCopy };
			context->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

			ID3D11RenderTargetView* rtvs[] = { underwaterMask.RTV };
			context->OMSetRenderTargets(ARRAYSIZE(rtvs), rtvs, nullptr);

			context->PSSetShader(GetUnderwaterMaskUpscalePS(), nullptr, 0);
			context->Draw(3, 0);
		}

		ID3D11ShaderResourceView* nullPSResources[3] = { nullptr, nullptr, nullptr };
		context->PSSetShaderResources(0, ARRAYSIZE(nullPSResources), nullPSResources);

		globals::state->EndPerfEvent();
	}
}

void Upscaling::Main_UpdateJitter::thunk(RE::BSGraphics::State* a_state)
{
	func(a_state);
	globals::features::upscaling.ConfigureUpscaling(a_state);
}

void Upscaling::MenuManagerDrawInterfaceStartHook::thunk(int64_t a1)
{
	globals::features::upscaling.PostDisplay();
	func(a1);
}

void Upscaling::Main_PostProcessing::thunk(RE::ImageSpaceManager* a1, uint32_t a3, uint32_t er8_)
{
	auto& upscaling = globals::features::upscaling;
	auto upscaleMethod = upscaling.GetUpscaleMethod();

	upscaling.CopySharedD3D12Resources();

	if (upscaleMethod != UpscaleMethod::kNONE && upscaleMethod != UpscaleMethod::kTAA)
		upscaling.PerformUpscaling();

	auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
	GET_INSTANCE_MEMBER(BSImagespaceShaderISTemporalAA, imageSpaceManager);

	BSImagespaceShaderISTemporalAA->taaEnabled = upscaleMethod == UpscaleMethod::kTAA;

	func(a1, a3, er8_);

	BSImagespaceShaderISTemporalAA->taaEnabled = upscaleMethod != UpscaleMethod::kNONE;
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