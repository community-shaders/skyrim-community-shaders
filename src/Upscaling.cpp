#include "Upscaling.h"

#include "DX12SwapChain.h"
#include "Hooks.h"
#include "State.h"
#include <Windows.h>
#include <reshade/reshade.hpp>
#include "Deferred.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Upscaling::Settings,
	upscaleMethod,
	upscaleMethodNoDLSS,
	upscaleMethodNoFSR,
	upscaleMethodNoXeSS,
	sharpness,
	frameLimitMode,
	frameGenerationMode,
	frameGenerationForceEnable,
	streamlineLogLevel);

void Upscaling::DrawSettings()
{
	// Skyrim settings control whether any upscaling is possible

	auto state = globals::state;
	auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
	auto streamline = globals::streamline;
	GET_INSTANCE_MEMBER(BSImagespaceShaderISTemporalAA, imageSpaceManager);
	auto& bTAA = BSImagespaceShaderISTemporalAA->taaEnabled;  // Setting used by shaders

	// Update upscale mode based on TAA setting
	settings.upscaleMethod = bTAA ? (settings.upscaleMethod == (uint)UpscaleMethod::kNONE ? (uint)UpscaleMethod::kTAA : settings.upscaleMethod) : (uint)UpscaleMethod::kNONE;

	// Display upscaling options in the UI
	const char* upscaleModes[] = { "Disabled", "Temporal Anti-Aliasing", "AMD FSR 3.1", "NVIDIA DLSS", "Intel XeSS" };

	// Determine available modes
	bool featureDLSS = streamline->featureDLSS;
	bool featureXeSS = globals::xess->featureXeSS;
	uint* currentUpscaleMode = &settings.upscaleMethod;
	
	// Fallback based on available features
	if (!featureDLSS && !featureXeSS) {
		currentUpscaleMode = &settings.upscaleMethodNoFSR;
	} else if (!featureDLSS) {
		currentUpscaleMode = &settings.upscaleMethodNoDLSS;
	} else if (!featureXeSS) {
		currentUpscaleMode = &settings.upscaleMethodNoXeSS;
	}
	
	uint availableModes = 4; // All methods available for non-VR
	//if (globals::game::isVR) {
	//	if (featureDLSS || featureXeSS) availableModes = 2;
	//	else availableModes = 1;
	//}

	if (state->featureLevel != D3D_FEATURE_LEVEL_11_1)
		availableModes = 1;

	// Slider for method selection
	ImGui::SliderInt("Method", (int*)currentUpscaleMode, 0, availableModes, std::format("{}", upscaleModes[(uint)*currentUpscaleMode]).c_str());
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Disabled:\n"
			"Disable all methods. Same as disabling Skyrim's TAA.\n"
			"\n"
			"Temporal Anti-Aliasing:\n"
			"Uses Skyrim's TAA which uses frame history to smooth out jagged edges, reducing flickering and improving image stability.\n"
			"\n"
			"AMD FSR 3.1:\n"
			"AMD FSR 3.1 is an open-source upscaling algorithm compatible with all GPUs.\n"
			"\n"
			"NVIDIA DLSS:\n"
			"NVIDIA's Deep Learning Super Sampling is an upscaling algorithm using AI. Requires an NVIDIA RTX GPU.\n"
			"\n"
			"Intel XeSS:\n"
			"Intel's Xe Super Sampling uses machine learning to upscale rendered frames. Compatible with DirectX 11/12 GPUs supporting Shader Model 6.4.");
	}

	*currentUpscaleMode = std::min(availableModes, (uint)*currentUpscaleMode);
	bTAA = *currentUpscaleMode != (uint)UpscaleMethod::kNONE;

	// settings for scaleform/ini
	if (auto iniSettingCollection = globals::game::iniPrefSettingCollection) {
		if (auto setting = iniSettingCollection->GetSetting("bUseTAA:Display")) {
			setting->data.b = bTAA;
		}
	}

	// Check the current upscale method
	auto upscaleMethod = GetUpscaleMethod();

	// Display upscaling settings if applicable
	if (upscaleMethod != UpscaleMethod::kNONE && upscaleMethod != UpscaleMethod::kTAA) {
		const char* upscalePresetsDLSS[] = { "Performance", "Balanced", "Quality", "DLAA" };
		const char* upscalePresets[] = { "Performance", "Balanced", "Quality", "Native AA" };

		if (upscaleMethod == UpscaleMethod::kDLSS)
			ImGui::SliderInt("Upscale Preset", (int*)&settings.upscalePreset, 0, 3, std::format("{}", upscalePresetsDLSS[3 - settings.upscalePreset]).c_str());
		else if (upscaleMethod == UpscaleMethod::kXESS)
			ImGui::SliderInt("Upscale Preset", (int*)&settings.upscalePreset, 0, 3, std::format("{}", upscalePresets[3 - settings.upscalePreset]).c_str());
		else
			ImGui::SliderInt("Upscale Preset", (int*)&settings.upscalePreset, 0, 3, std::format("{}", upscalePresets[3 - settings.upscalePreset]).c_str());
		
		ImGui::SliderFloat("Sharpness", &settings.sharpness, 0.0f, 1.0f, "%.1f");
		settings.sharpness = std::clamp(settings.sharpness, 0.0f, 1.0f);
	}

	if (globals::fidelityFX->featureFSR3FG) {
		if (ImGui::TreeNodeEx("Frame Generation", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Text("Frame Generation interpolates real frames with generated ones for a smoother experience");
			ImGui::Text("Uses AMD FSR 3.1 Frame Generation technology");
			if (globals::fidelityFX && globals::fidelityFX->featureFSR3FG)
				ImGui::Text("AMD FSR 3.1 Frame Generation is available.");
			ImGui::Text("Requires a D3D11 to D3D12 proxy which can create compatibility issues");
			ImGui::Text("Toggling this setting requires a restart to work correctly");

			bool onlyRequiresRestart = true;

			if (!isWindowed) {
				ImGui::Text("Warning: Requires windowed mode");
				onlyRequiresRestart = false;
			}

			if (lowRefreshRate && !settings.frameGenerationForceEnable) {
				ImGui::Text("Warning: Requires a high refresh rate monitor or Force Enable Frame Generation");
				onlyRequiresRestart = false;
			}

			if (fidelityFXMissing) {
				ImGui::Text("Warning: amd_fidelityfx_dx12.dll is not loaded");
				onlyRequiresRestart = false;
			}

			if (onlyRequiresRestart && settings.frameGenerationMode && !d3d12Interop)
				ImGui::Text("Warning: Requires restart");

			std::string backendLabel = globals::fidelityFX && globals::fidelityFX->isFrameGenActive ? "FSR3" : "None";
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
		for (const auto& [name, version] : FidelityFX::dllVersions)
			ffRows.push_back({ name, version });
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
		for (const auto& [name, version] : Streamline::dllVersions)
			slRows.push_back({ name, version });
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

Upscaling::UpscaleMethod Upscaling::GetUpscaleMethod()
{
	if (globals::state->featureLevel != D3D_FEATURE_LEVEL_11_1)
		return (Upscaling::UpscaleMethod)settings.upscaleMethodNoFSR;

	bool featureDLSS = globals::streamline->featureDLSS;
	bool featureXeSS = globals::xess->featureXeSS;

	if (featureDLSS && featureXeSS)
		return (Upscaling::UpscaleMethod)settings.upscaleMethod;
	else if (featureDLSS)
		return (Upscaling::UpscaleMethod)settings.upscaleMethodNoXeSS;
	else if (featureXeSS)
		return (Upscaling::UpscaleMethod)settings.upscaleMethodNoDLSS;
	else
		return (Upscaling::UpscaleMethod)settings.upscaleMethodNoFSR;
}

void Upscaling::CheckResources()
{
	static auto previousUpscaleMode = UpscaleMethod::kTAA;
	auto currentUpscaleMode = GetUpscaleMethod();

	auto streamline = globals::streamline;
	auto fidelityFX = globals::fidelityFX;
	auto xess = globals::xess;

	if (previousUpscaleMode != currentUpscaleMode) {
		if (previousUpscaleMode == UpscaleMethod::kDLSS)
			streamline->DestroyDLSSResources();
		else if (previousUpscaleMode == UpscaleMethod::kFSR)
			fidelityFX->DestroyFSRResources();
		else if (previousUpscaleMode == UpscaleMethod::kXESS)
			xess->DestroyXeSSResources();

		if (currentUpscaleMode == UpscaleMethod::kFSR)
			fidelityFX->CreateFSRResources();
		else if (currentUpscaleMode == UpscaleMethod::kXESS)
			xess->CreateXeSSResources();

		previousUpscaleMode = currentUpscaleMode;
	}
}

ID3D11ComputeShader* Upscaling::GetEncodeTexturesCS()
{
	if (!encodeTexturesCS) {
		logger::debug("Compiling EncodeTexturesCS.hlsl");
		encodeTexturesCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data/Shaders/Upscaling/EncodeTexturesCS.hlsl", {}, "cs_5_0");
	}
	return encodeTexturesCS;
}

ID3D11ComputeShader* Upscaling::GetEncodeTexturesTransparencyCS()
{
	if (!encodeTexturesTransparencyCS) {
		logger::debug("Compiling EncodeTexturesCS.hlsl TRANSPARENCY_MASK");
		encodeTexturesTransparencyCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data/Shaders/Upscaling/EncodeTexturesCS.hlsl", { { "TRANSPARENCY_MASK", "" } }, "cs_5_0");
	}
	return encodeTexturesTransparencyCS;
}

ID3D11ComputeShader* Upscaling::GetRCASCS()
{
	float sharpnessRemapped = (-2.0f * settings.sharpness) + 2.0f;
	sharpnessRemapped = exp2(-sharpnessRemapped);

	static auto previousSharpness = sharpnessRemapped;
	auto currentSharpness = sharpnessRemapped;

	if (previousSharpness != currentSharpness) {
		previousSharpness = currentSharpness;

		if (rcasCS) {
			rcasCS->Release();
			rcasCS = nullptr;
		}
	}

	if (!rcasCS) {
		logger::debug("Compiling RCAS.hlsl");
		rcasCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data/Shaders/Upscaling/RCAS/RCAS.hlsl", { { "SHARPNESS", std::format("{}", currentSharpness).c_str() } }, "cs_5_0");
	}

	return rcasCS;
}

ID3D11PixelShader* Upscaling::GetDepthUpscalePS()
{
	if (!depthUpscalePS) {
		logger::debug("Compiling DepthUpscalePS.hlsl");
		std::vector<std::pair<const char*, const char*>> defines = { { "PSHADER", "" } };
		if (globals::game::isVR) {
			defines.push_back({ "VR", "" });
		}
		depthUpscalePS = (ID3D11PixelShader*)Util::CompileShader(L"Data/Shaders/Upscaling/DepthUpscale.hlsl", defines, "ps_5_0");
	}

	return depthUpscalePS;
}

ID3D11VertexShader* Upscaling::GetDepthUpscaleVS()
{
	if (!depthUpscaleVS) {
		logger::debug("Compiling DepthUpscaleVS.hlsl");
		depthUpscaleVS = (ID3D11VertexShader*)Util::CompileShader(L"Data/Shaders/Upscaling/DepthUpscale.hlsl", { { "VSHADER", "" } }, "vs_5_0");
	}

	return depthUpscaleVS;
}

void Upscaling::ConfigureUpscaling(RE::BSGraphics::State* a_viewport)
{
	auto upscaleMethod = GetUpscaleMethod();

	// The game defaults this to a non-zero value
	static float* clampOffset = (float*)REL::RelocationID(512203, 389038).address();
	*clampOffset = 0;

	if (upscaleMethod != UpscaleMethod::kNONE) {		

		if (allowUpscaling && upscaleMethod != UpscaleMethod::kTAA)
			resolutionScale = 1.0f / ffxFsr3GetUpscaleRatioFromQualityMode((FfxFsr3QualityMode)settings.upscalePreset);
		else
			resolutionScale = 1.0f;

		auto state = globals::state;
		auto screenSize = globals::state->screenSize;

		auto screenWidth = static_cast<int>(screenSize.x);
		auto renderWidth = static_cast<int>(screenWidth * resolutionScale);

		auto screenHeight = static_cast<int>(screenSize.y);
		auto renderHeight = static_cast<int>(screenHeight * resolutionScale);

		auto phaseCount = ffxFsr3GetJitterPhaseCount(renderWidth, screenWidth);

		ffxFsr3UpscalerGetJitterOffset(&jitter.x, &jitter.y, state->frameCount, phaseCount);

		if (globals::game::isVR)
			a_viewport->projectionPosScaleX = -jitter.x / renderWidth;
		else
			a_viewport->projectionPosScaleX = -2.0f * jitter.x / renderWidth;

		a_viewport->projectionPosScaleY = 2.0f * jitter.y / renderHeight;

	} else {
		resolutionScale = 1.0f;
	}

	auto& runtimeData = a_viewport->GetRuntimeData();

	runtimeData.dynamicResolutionPreviousWidthRatio = resolutionScale;
	runtimeData.dynamicResolutionPreviousHeightRatio = resolutionScale;
	runtimeData.dynamicResolutionWidthRatio = resolutionScale;
	runtimeData.dynamicResolutionHeightRatio = resolutionScale;

	allowUpscaling = false;
}

void Upscaling::CreateUpscalingResources()
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

	upscalingTexture = new Texture2D(texDesc);
	upscalingTexture->CreateSRV(srvDesc);
	upscalingTexture->CreateUAV(uavDesc);

	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	texDesc.Format = DXGI_FORMAT_R8_UNORM;
	srvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	reactiveMaskTexture = new Texture2D(texDesc);
	reactiveMaskTexture->CreateSRV(srvDesc);
	reactiveMaskTexture->CreateUAV(uavDesc);

	transparencyCompositionMaskTexture = new Texture2D(texDesc);
	transparencyCompositionMaskTexture->CreateSRV(srvDesc);
	transparencyCompositionMaskTexture->CreateUAV(uavDesc);

	//if (d3d12Interop)
	CreateFrameGenerationResources();

	resolutionScaleCB = new ConstantBuffer(ConstantBufferDesc<ResolutionScaleCB>());

	D3D11_DEPTH_STENCIL_DESC depthStencilDesc = {};
	depthStencilDesc.DepthEnable = true;							// Enable depth testing
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;	// Write to all depth bits
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;			// Always pass depth test (write all depths)
	depthStencilDesc.StencilEnable = false;							// Disable stencil testing
	DX::ThrowIfFailed(globals::d3d::device->CreateDepthStencilState(&depthStencilDesc, &depthUpscaleState));

	// Create blend state for depth upscaling (disable color writes, depth only)
	D3D11_BLEND_DESC blendDesc = {};
	blendDesc.AlphaToCoverageEnable = false;
	blendDesc.IndependentBlendEnable = false;
	blendDesc.RenderTarget[0].BlendEnable = false;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = 0;  // No color writes
	DX::ThrowIfFailed(globals::d3d::device->CreateBlendState(&blendDesc, &depthUpscaleBlendState));

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
	DX::ThrowIfFailed(globals::d3d::device->CreateRasterizerState(&rasterizerDesc, &depthUpscaleRasterizerState));
}

void Upscaling::DestroyUpscalingResources()
{
	upscalingTexture->srv = nullptr;
	upscalingTexture->uav = nullptr;
	upscalingTexture->resource = nullptr;
	delete upscalingTexture;

	reactiveMaskTexture->srv = nullptr;
	reactiveMaskTexture->uav = nullptr;
	reactiveMaskTexture->resource = nullptr;
	delete reactiveMaskTexture;

	transparencyCompositionMaskTexture->srv = nullptr;
	transparencyCompositionMaskTexture->uav = nullptr;
	transparencyCompositionMaskTexture->resource = nullptr;
	delete transparencyCompositionMaskTexture;

	if (encodeTexturesCS) {
		encodeTexturesCS->Release();
		encodeTexturesCS = nullptr;
	}

	// Clean up depth upscaling states
	if (depthUpscaleState) {
		depthUpscaleState->Release();
		depthUpscaleState = nullptr;
	}
	if (depthUpscaleBlendState) {
		depthUpscaleBlendState->Release();
		depthUpscaleBlendState = nullptr;
	}
	if (depthUpscaleRasterizerState) {
		depthUpscaleRasterizerState->Release();
		depthUpscaleRasterizerState = nullptr;
	}

	// Clean up shared D3D12 resources
	if (HUDLessBufferShared12) {
		delete HUDLessBufferShared12;
		HUDLessBufferShared12 = nullptr;
	}
	if (depthBufferShared12) {
		delete depthBufferShared12;
		depthBufferShared12 = nullptr;
	}
	if (motionVectorBufferShared12) {
		delete motionVectorBufferShared12;
		motionVectorBufferShared12 = nullptr;
	}
	if (reactiveMaskBufferShared12) {
		delete reactiveMaskBufferShared12;
		reactiveMaskBufferShared12 = nullptr;
	}

	// Clean up shared D3D12 device resources
	if (sharedFenceEvent) {
		CloseHandle(sharedFenceEvent);
		sharedFenceEvent = nullptr;
	}
	sharedD3D12Fence = nullptr;
	sharedD3D12CommandList = nullptr;
	sharedD3D12CommandAllocator = nullptr;
	sharedD3D12CommandQueue = nullptr;
	sharedD3D12Device = nullptr;
	sharedFenceValue = 0;
}

void Upscaling::CreateSharedD3D12Device(IDXGIAdapter* a_dxgiAdapter)
{
	// Create D3D12 device on same adapter
	DX::ThrowIfFailed(D3D12CreateDevice(a_dxgiAdapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&sharedD3D12Device)));

	// Create command queue
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.NodeMask = 0;

	DX::ThrowIfFailed(sharedD3D12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&sharedD3D12CommandQueue)));

	// Create command allocator
	DX::ThrowIfFailed(sharedD3D12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&sharedD3D12CommandAllocator)));

	// Create command list
	DX::ThrowIfFailed(sharedD3D12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, sharedD3D12CommandAllocator.get(), nullptr, IID_PPV_ARGS(&sharedD3D12CommandList)));

	// Create fence for synchronization
	DX::ThrowIfFailed(sharedD3D12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&sharedD3D12Fence)));

	sharedFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (sharedFenceEvent == nullptr) {
		throw std::runtime_error("Failed to create shared fence event");
	}

	// Close initial command list
	sharedD3D12CommandList->Close();

	logger::info("[Upscaling] Shared D3D12 device and interop resources created successfully");
}


void Upscaling::CreateFrameGenerationResources()
{
	logger::info("[Frame Generation] Creating resources");

	// Get D3D11 device5 interface for WrappedResource creation
	winrt::com_ptr<ID3D11Device5> d3d11Device5;
	if (FAILED(globals::d3d::device->QueryInterface(IID_PPV_ARGS(&d3d11Device5)))) {
		logger::error("[Upscaling] Failed to get ID3D11Device5 interface");
		return;
	}

	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC texDesc{};
	main.texture->GetDesc(&texDesc);

	texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
	inputColorBufferShared12 = new WrappedResource(texDesc, d3d11Device5.get(), sharedD3D12Device.get());
	outputColorBufferShared12 = new WrappedResource(texDesc, d3d11Device5.get(), sharedD3D12Device.get());

	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	HUDLessBufferShared12 = new WrappedResource(texDesc, d3d11Device5.get(), sharedD3D12Device.get());
	reactiveMaskBufferShared12 = new WrappedResource(texDesc, d3d11Device5.get(), sharedD3D12Device.get());

	texDesc.Format = DXGI_FORMAT_R32_FLOAT;
	depthBufferShared12 = new WrappedResource(texDesc, d3d11Device5.get(), sharedD3D12Device.get());

	auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	motionVector.texture->GetDesc(&texDesc);
	motionVectorBufferShared12 = new WrappedResource(texDesc, d3d11Device5.get(), sharedD3D12Device.get());

	copyDepthToSharedBufferCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\FrameGeneration\\CopyDepthToSharedBufferCS.hlsl", {}, "cs_5_0");
}

void Upscaling::CopyFrameGenerationResources()
{
	if (!d3d12Interop || !settings.frameGenerationMode)
		return;

	CopySharedResources();

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	if (!useHUDLess) {
		auto& swapChain = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER];
		ID3D11Resource* swapChainResource;
		swapChain.SRV->GetResource(&swapChainResource);
		context->CopyResource(HUDLessBufferShared12->resource11, swapChainResource);
	}

	useHUDLess = false;
}

void Upscaling::CopySharedResources()
{
	// Only copy once per frame for all upscaling systems (XeSS, Frame Generation, etc.)
	if (!sharedResourcesFrameChecker.IsNewFrame())
		return;

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	{
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

	{
		auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

		{
			auto dispatchCount = Util::GetScreenDispatchCount(true);

			ID3D11ShaderResourceView* views[1] = { depth.depthSRV };
			context->CSSetShaderResources(0, ARRAYSIZE(views), views);

			ID3D11UnorderedAccessView* uavs[1] = { depthBufferShared12->uav };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			context->CSSetShader(copyDepthToSharedBufferCS, nullptr, 0);

			context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
		}

		ID3D11ShaderResourceView* views[1] = { nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[1] = { nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11ComputeShader* shader = nullptr;
		context->CSSetShader(shader, nullptr, 0);
	}
}

void Upscaling::PostDisplay()
{
	globals::state->RenderReShade();

	if (!d3d12Interop || !settings.frameGenerationMode)
		return;

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	auto& swapChain = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER];
	ID3D11Resource* swapChainResource;
	swapChain.SRV->GetResource(&swapChainResource);
	context->CopyResource(HUDLessBufferShared12->resource11, swapChainResource);

	useHUDLess = true;
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
	if (d3d12Interop && settings.frameLimitMode) {
		double bestRefreshRate = refreshRate - (refreshRate * refreshRate) / 3600.0;

		LARGE_INTEGER qpf;
		QueryPerformanceFrequency(&qpf);

		int64_t targetFrameTicks = int64_t(double(qpf.QuadPart) / (bestRefreshRate * (settings.frameGenerationMode ? 0.5 : 1.0)));

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
	return d3d12Interop && settings.frameGenerationMode;
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
	if (globals::dx12SwapChain && globals::dx12SwapChain->swapChain) {
		// Get frame time from the D3D12 SwapChain
		return globals::dx12SwapChain->GetFrameTime();
	}

	return 0.0f;
}

void Upscaling::Upscale()
{
	auto upscaleMethod = GetUpscaleMethod();

	CheckResources();

	auto state = globals::state;

	auto context = globals::d3d::context;
	
	auto renderer = globals::game::renderer;

	context->OMSetRenderTargets(0, nullptr, nullptr);  // Unbind all bound render targets

	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	auto dispatchCount = Util::GetScreenDispatchCount(true);

	{
		state->BeginPerfEvent("Alpha Mask");

		auto& temporalAAMask = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kTEMPORAL_AA_MASK];
		auto& depthPreWater = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
		auto& depthPostWater = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

		{
			bool useTransparencyMask = upscaleMethod != UpscaleMethod::kXESS;

			ID3D11ShaderResourceView* views[3] = { temporalAAMask.SRV, depthPreWater.depthSRV, depthPostWater.depthSRV };
			context->CSSetShaderResources(0, ARRAYSIZE(views), views);

			ID3D11UnorderedAccessView* uavs[2] = { reactiveMaskTexture->uav.get(), useTransparencyMask ? transparencyCompositionMaskTexture->uav.get() : nullptr };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			context->CSSetShader(useTransparencyMask ? GetEncodeTexturesTransparencyCS() : GetEncodeTexturesCS(), nullptr, 0);

			context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
		}

		ID3D11ShaderResourceView* views[3] = { nullptr, nullptr, nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[2] = { nullptr, nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11ComputeShader* shader = nullptr;
		context->CSSetShader(shader, nullptr, 0);

		state->EndPerfEvent();
	}

	{
		state->BeginPerfEvent("Upscaling");

		if (upscaleMethod == UpscaleMethod::kDLSS)
			globals::streamline->Upscale(main.texture, upscalingTexture->resource.get(), reactiveMaskTexture->resource.get(), transparencyCompositionMaskTexture->resource.get(), (sl::DLSSPreset)11u);
		else if (upscaleMethod == UpscaleMethod::kFSR)
			globals::fidelityFX->Upscale(main.texture, reactiveMaskTexture->resource.get(), transparencyCompositionMaskTexture->resource.get(), jitter, settings.sharpness);
		else if (upscaleMethod == UpscaleMethod::kXESS)
			globals::xess->Upscale(main.texture, upscalingTexture->resource.get(), reactiveMaskTexture->resource.get(), jitter);

		state->EndPerfEvent();
	}

	if (upscaleMethod != UpscaleMethod::kFSR) {
		state->BeginPerfEvent("Sharpening");

		{
			{
				dispatchCount = Util::GetScreenDispatchCount(false);

				ID3D11ShaderResourceView* views[1] = { upscalingTexture->srv.get() };
				context->CSSetShaderResources(0, ARRAYSIZE(views), views);

				ID3D11UnorderedAccessView* uavs[1] = { main.UAV };
				context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

				context->CSSetShader(GetRCASCS(), nullptr, 0);

				context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
			}

			ID3D11ShaderResourceView* views[1] = { nullptr };
			context->CSSetShaderResources(0, ARRAYSIZE(views), views);

			ID3D11UnorderedAccessView* uavs[1] = { nullptr };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			ID3D11ComputeShader* shader = nullptr;
			context->CSSetShader(shader, nullptr, 0);
		}

		state->EndPerfEvent();
	}
}

void UpdateCameraData();

void Upscaling::PerformUpscaling()
{
	Upscale();
	UpscaleDepth();

	auto& runtimeData = globals::game::graphicsState->GetRuntimeData();

	// The game uses these values at multiple points, setting to 1.0 disables all checks
	runtimeData.dynamicResolutionPreviousWidthRatio = 1.0f;
	runtimeData.dynamicResolutionPreviousHeightRatio = 1.0f;
	runtimeData.dynamicResolutionWidthRatio = 1.0f;
	runtimeData.dynamicResolutionHeightRatio = 1.0f;
	
	// Updates the PerFrame constant buffer so that dynamic resolution settings are disabled
	using func_t = decltype(&UpdateCameraData);
	static REL::Relocation<func_t> func{ RELOCATION_ID(75472, 77258) };
	func();
	
	allowUpscaling = true;
}

void Upscaling::UpscaleDepth()
{
	if (resolutionScale != 1.0f) {
		globals::state->BeginPerfEvent("Depth Upscaling");

		auto& renderer = globals::game::renderer;
		auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
		auto& depthCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN_COPY];

		auto context = globals::d3d::context;

		context->CopyResource(depthCopy.texture, depth.texture);

		ResolutionScaleCB updateData{};
		updateData.ResolutionScale.x = resolutionScale;
		resolutionScaleCB->Update(updateData);

		// Set up Input Assembler for fullscreen triangle (no vertex/index buffers needed)
		context->IASetInputLayout(nullptr);
		context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
		context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// Set up vertex shader that generates fullscreen triangle using SV_VertexID
		context->VSSetShader(GetDepthUpscaleVS(), nullptr, 0);

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
		context->RSSetState(depthUpscaleRasterizerState);

		// Set blend state (no color writes, depth only)
		context->OMSetBlendState(nullptr, nullptr, 0xffffffff);

		// Set depth stencil state
		context->OMSetDepthStencilState(depthUpscaleState, 0);

		// Set render targets (no color target, depth only)
		context->OMSetRenderTargets(0, nullptr, depth.views[0]);

		// Set up pixel shader resources
		auto constantBuffer = resolutionScaleCB->CB();
		context->PSSetConstantBuffers(0, 1, &constantBuffer);
		
		if (globals::game::isVR) {
			// For VR, bind both depth and stencil textures
			ID3D11ShaderResourceView* views[2] = { depthCopy.depthSRV, depthCopy.stencilSRV };
			context->PSSetShaderResources(0, 2, views);
		} else {
			// For non-VR, bind only depth texture
			context->PSSetShaderResources(0, 1, &depthCopy.depthSRV);
		}

		context->PSSetSamplers(0, 1, &globals::deferred->linearSampler);

		context->PSSetShader(GetDepthUpscalePS(), nullptr, 0);

		context->Draw(3, 0);

		// Clean up pixel shader resources
		if (globals::game::isVR) {
			ID3D11ShaderResourceView* nullPSResources[2] = { nullptr, nullptr };
			context->PSSetShaderResources(0, 2, nullPSResources);
		} else {
			ID3D11ShaderResourceView* nullPSResources[1] = { nullptr };
			context->PSSetShaderResources(0, 1, nullPSResources);
		}

		globals::state->EndPerfEvent();
	}
}

/**
 * @brief Installs hooks on the Map and Unmap methods of the provided D3D11 device context.
 *
 * This enables interception of resource mapping and unmapping operations for frame buffer caching.
 */
void Upscaling::InstallD3DHooks(ID3D11DeviceContext* a_context)
{
	stl::detour_vfunc<14, ID3D11DeviceContext_Map>(a_context);
	stl::detour_vfunc<15, ID3D11DeviceContext_Unmap>(a_context);
}

/**
 * @brief Hooks the ID3D11DeviceContext::Map method to track mapping of the per-frame resource.
 *
 * Calls the original Map function and, if the mapped resource matches the current per-frame buffer, stores the mapped subresource pointer for later use.
 *
 * @return HRESULT Result of the original Map call.
 */
HRESULT Upscaling::ID3D11DeviceContext_Map::thunk(ID3D11DeviceContext* This, ID3D11Resource* pResource, UINT Subresource, D3D11_MAP MapType, UINT MapFlags, D3D11_MAPPED_SUBRESOURCE* pMappedResource)
{
	HRESULT hr = func(This, pResource, Subresource, MapType, MapFlags, pMappedResource);
	if (hr == S_OK) {
		if (*globals::game::perFrame.get() == pResource)
			globals::upscaling->mappedFrameBuffer = pMappedResource;
	}
	return hr;
}

/**
 * @brief Hooked implementation of ID3D11DeviceContext::Unmap that caches the frame buffer if applicable.
 *
 * If the resource being unmapped matches the current per-frame buffer and a mapped frame buffer is present, caches the frame buffer data before calling the original Unmap function.
 */
void Upscaling::ID3D11DeviceContext_Unmap::thunk(ID3D11DeviceContext* This, ID3D11Resource* pResource, UINT Subresource)
{
	if (*globals::game::perFrame.get() == pResource && globals::upscaling->mappedFrameBuffer)
		globals::upscaling->CacheFramebuffer();
	func(This, pResource, Subresource);
}

/**
 * @brief Caches the current frame buffer data and clears the mapped pointer.
 *
 * Copies the contents of the mapped frame buffer into an internal cache and resets the mapped frame buffer pointer.
 */
void Upscaling::CacheFramebuffer()
{
	auto frameBuffer = (FrameBuffer*)mappedFrameBuffer->pData;
	frameBufferCached = *frameBuffer;
	mappedFrameBuffer = nullptr;
}
