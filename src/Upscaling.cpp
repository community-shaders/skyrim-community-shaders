#include "Upscaling.h"

#include "DX12SwapChain.h"
#include "Hooks.h"
#include "State.h"
#include <Windows.h>
#include <reshade/reshade.hpp>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Upscaling::Settings,
	upscaleMethod,
	upscaleMethodNoDLSS,
	upscaleMethodNoFSR,
	sharpness,
	dlssPreset,
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
	const char* upscaleModes[] = { "Disabled", "Temporal Anti-Aliasing", "AMD FSR 3.1", "NVIDIA DLAA" };

	// Determine available modes
	bool featureDLSS = streamline->featureDLSS;
	uint* currentUpscaleMode = featureDLSS ? &settings.upscaleMethod : &settings.upscaleMethodNoDLSS;
	uint availableModes = (globals::game::isVR && state->upscalerLoaded) ? (featureDLSS ? 2 : 1) : (featureDLSS ? 3 : 2);

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
			"AMD's open-source FSR spatial upscaling algorithm designed to enhance performance while maintaining high visual quality.\n"
			"\n"
			"NVIDIA DLAA:\n"
			"NVIDIA's Deep Learning Anti-Aliasing leverages AI to provide high-quality anti-aliasing without sacrificing performance. Requires NVIDIA RTX GPU.");
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

	// Display sharpness slider if applicable
	if (upscaleMethod != UpscaleMethod::kNONE) {
		ImGui::SliderFloat("Sharpness", &settings.sharpness, 0.0f, 1.0f, "%.1f");
		settings.sharpness = std::clamp(settings.sharpness, 0.0f, 1.0f);
	}

	// Display DLSS preset slider if using DLSS
	if (upscaleMethod == UpscaleMethod::kDLSS) {
		const char* dlssPresets[] = { "Transformer Model", "Convolutional Model" };
		settings.dlssPreset = std::clamp(settings.dlssPreset, 0u, 1u);
		ImGui::SliderInt("DLSS Super Resolution Preset", (int*)&settings.dlssPreset, 0, 1, std::format("{}", dlssPresets[settings.dlssPreset]).c_str());
		settings.dlssPreset = std::clamp(settings.dlssPreset, 0u, 1u);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("The new DLSS Transformer model offers more image stability, less ghosting and improved anti-aliasing in comparison with the original DLSS Convolutional Neural Network model.");
		}
	}

	if (!globals::game::isVR) {
		bool frameGenAvailable = ((globals::streamline && globals::streamline->featureDLSSG) ||
								  (globals::fidelityFX && globals::fidelityFX->featureFSR3FG));
		if (frameGenAvailable) {
			if (ImGui::TreeNodeEx("Frame Generation", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::Text("Frame Generation interpolates real frames with generated ones for a smoother experience");
				ImGui::Text("Uses AMD FSR 3.1 Frame Generation and NVIDIA DLSS Frame Generation technology");
				if (globals::streamline && globals::streamline->featureDLSSG)
					ImGui::Text("NVIDIA DLSS-G Frame Generation is available.");
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

				if (streamlineMissing) {
					ImGui::Text("Warning: Streamline is not loaded");
					onlyRequiresRestart = false;
				}

				if (fidelityFXMissing) {
					ImGui::Text("Warning: amd_fidelityfx_dx12.dll is not loaded");
					onlyRequiresRestart = false;
				}

				if (onlyRequiresRestart && settings.frameGenerationMode && !d3d12Interop)
					ImGui::Text("Warning: Requires restart");

				std::string backendLabel =
					(globals::streamline && globals::streamline->isFrameGenActive) ? "DLSSG" :
					(globals::fidelityFX && globals::fidelityFX->isFrameGenActive) ? "FSR3" :
																					 "None";
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
	std::lock_guard<std::mutex> lock(settingsMutex);

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
	std::lock_guard<std::mutex> lock(settingsMutex);
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

	if (globals::streamline->featureDLSS)
		return (Upscaling::UpscaleMethod)settings.upscaleMethod;

	return (Upscaling::UpscaleMethod)settings.upscaleMethodNoDLSS;
}

void Upscaling::CheckResources()
{
	static auto previousUpscaleMode = UpscaleMethod::kTAA;
	auto currentUpscaleMode = GetUpscaleMethod();

	auto streamline = globals::streamline;
	auto fidelityFX = globals::fidelityFX;

	if (previousUpscaleMode != currentUpscaleMode) {
		if (previousUpscaleMode == UpscaleMethod::kDLSS)
			streamline->DestroyDLSSResources();
		else if (previousUpscaleMode == UpscaleMethod::kFSR)
			fidelityFX->DestroyFSRResources();

		if (currentUpscaleMode == UpscaleMethod::kFSR)
			fidelityFX->CreateFSRResources();

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

void Upscaling::UpdateJitter()
{
	auto upscaleMethod = GetUpscaleMethod();
	if (upscaleMethod == UpscaleMethod::kFSR || upscaleMethod == UpscaleMethod::kDLSS) {
		auto gameViewport = globals::game::graphicsState;

		auto state = globals::state;

		ffxFsr3UpscalerGetJitterOffset(&jitter.x, &jitter.y, globals::state->frameCount, 8);

		if (globals::game::isVR)
			gameViewport->projectionPosScaleX = -jitter.x / state->screenSize.x;
		else
			gameViewport->projectionPosScaleX = -2.0f * jitter.x / state->screenSize.x;

		gameViewport->projectionPosScaleY = 2.0f * jitter.y / state->screenSize.y;
	}
}

void Upscaling::Upscale()
{
	std::lock_guard<std::mutex> lock(settingsMutex);  // Lock for the duration of this function

	auto upscaleMethod = GetUpscaleMethod();

	if (upscaleMethod == UpscaleMethod::kNONE || upscaleMethod == UpscaleMethod::kTAA)
		return;

	CheckResources();

	Hooks::BSGraphics_SetDirtyStates::func(false);

	auto state = globals::state;

	auto context = globals::d3d::context;

	ID3D11ShaderResourceView* inputTextureSRV;
	context->PSGetShaderResources(0, 1, &inputTextureSRV);

	ID3D11RenderTargetView* outputTextureRTV;
	ID3D11DepthStencilView* dsv;
	context->OMGetRenderTargets(1, &outputTextureRTV, &dsv);
	context->OMSetRenderTargets(0, nullptr, nullptr);

	// Get resources before releasing the views
	ID3D11Resource* inputTextureResource;
	inputTextureSRV->GetResource(&inputTextureResource);

	ID3D11Resource* outputTextureResource;
	outputTextureRTV->GetResource(&outputTextureResource);

	auto dispatchCount = Util::GetScreenDispatchCount(false);

	{
		state->BeginPerfEvent("Alpha Mask");

		static auto renderer = globals::game::renderer;
		static auto& temporalAAMask = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kTEMPORAL_AA_MASK];

		{
			ID3D11ShaderResourceView* views[1] = { temporalAAMask.SRV };
			context->CSSetShaderResources(0, ARRAYSIZE(views), views);

			ID3D11UnorderedAccessView* uavs[1] = { alphaMaskTexture->uav.get() };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			context->CSSetShader(GetEncodeTexturesCS(), nullptr, 0);

			context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
		}

		ID3D11ShaderResourceView* views[1] = { nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[1] = { nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11ComputeShader* shader = nullptr;
		context->CSSetShader(shader, nullptr, 0);

		state->EndPerfEvent();
	}

	{
		state->BeginPerfEvent("Upscaling");

		context->CopyResource(upscalingTexture->resource.get(), inputTextureResource);

		if (upscaleMethod == UpscaleMethod::kDLSS)
			globals::streamline->Upscale(upscalingTexture, alphaMaskTexture, settings.dlssPreset == 0 ? (sl::DLSSPreset)11u : sl::DLSSPreset::ePresetE);
		else if (upscaleMethod == UpscaleMethod::kFSR)
			globals::fidelityFX->Upscale(upscalingTexture, alphaMaskTexture, jitter, settings.sharpness);

		state->EndPerfEvent();
	}

	if (upscaleMethod != UpscaleMethod::kFSR) {
		state->BeginPerfEvent("Sharpening");

		context->CopyResource(inputTextureResource, upscalingTexture->resource.get());

		{
			{
				ID3D11ShaderResourceView* views[1] = { inputTextureSRV };
				context->CSSetShaderResources(0, ARRAYSIZE(views), views);

				ID3D11UnorderedAccessView* uavs[1] = { upscalingTexture->uav.get() };
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

	context->CopyResource(outputTextureResource, upscalingTexture->resource.get());

	// Release COM objects to prevent memory leaks after all usage is complete
	if (inputTextureSRV)
		inputTextureSRV->Release();
	if (outputTextureRTV)
		outputTextureRTV->Release();
	if (dsv)
		dsv->Release();
}

void Upscaling::SharpenTAA()
{
	std::lock_guard<std::mutex> lock(settingsMutex);  // Lock for the duration of this function

	CheckResources();

	auto state = globals::state;
	auto context = globals::d3d::context;

	ID3D11ShaderResourceView* inputTextureSRV;
	context->PSGetShaderResources(0, 1, &inputTextureSRV);

	ID3D11RenderTargetView* outputTextureRTV;
	ID3D11DepthStencilView* dsv;
	context->OMGetRenderTargets(1, &outputTextureRTV, &dsv);
	context->OMSetRenderTargets(0, nullptr, nullptr);

	// Get resources before releasing the views
	ID3D11Resource* inputTextureResource;
	inputTextureSRV->GetResource(&inputTextureResource);

	ID3D11Resource* outputTextureResource;
	outputTextureRTV->GetResource(&outputTextureResource);

	auto dispatchCount = Util::GetScreenDispatchCount(false);

	state->BeginPerfEvent("Sharpening");

	context->CopyResource(inputTextureResource, outputTextureResource);

	{
		{
			ID3D11ShaderResourceView* views[1] = { inputTextureSRV };
			context->CSSetShaderResources(0, ARRAYSIZE(views), views);

			ID3D11UnorderedAccessView* uavs[1] = { upscalingTexture->uav.get() };
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

	context->CopyResource(outputTextureResource, upscalingTexture->resource.get());

	// Release COM objects to prevent memory leaks after all usage is complete
	if (inputTextureSRV)
		inputTextureSRV->Release();
	if (outputTextureRTV)
		outputTextureRTV->Release();
	if (dsv)
		dsv->Release();

	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);  // Run OMSetRenderTargets again
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

	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	upscalingTexture = new Texture2D(texDesc);
	upscalingTexture->CreateSRV(srvDesc);
	upscalingTexture->CreateUAV(uavDesc);

	texDesc.Format = DXGI_FORMAT_R8_UNORM;
	srvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	alphaMaskTexture = new Texture2D(texDesc);
	alphaMaskTexture->CreateSRV(srvDesc);
	alphaMaskTexture->CreateUAV(uavDesc);

	if (d3d12Interop)
		CreateFrameGenerationResources();
}

void Upscaling::DestroyUpscalingResources()
{
	upscalingTexture->srv = nullptr;
	upscalingTexture->uav = nullptr;
	upscalingTexture->resource = nullptr;
	delete upscalingTexture;

	alphaMaskTexture->srv = nullptr;
	alphaMaskTexture->uav = nullptr;
	alphaMaskTexture->resource = nullptr;
	delete alphaMaskTexture;
}

void Upscaling::CreateFrameGenerationResources()
{
	logger::info("[Frame Generation] Creating resources");

	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC texDesc{};
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

	main.texture->GetDesc(&texDesc);
	main.SRV->GetDesc(&srvDesc);
	main.RTV->GetDesc(&rtvDesc);
	main.UAV->GetDesc(&uavDesc);

	texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.Format = texDesc.Format;
	rtvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	HUDLessBufferShared = new Texture2D(texDesc);
	HUDLessBufferShared->CreateSRV(srvDesc);
	HUDLessBufferShared->CreateRTV(rtvDesc);
	HUDLessBufferShared->CreateUAV(uavDesc);

	texDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.Format = texDesc.Format;
	rtvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	depthBufferShared = new Texture2D(texDesc);
	depthBufferShared->CreateSRV(srvDesc);
	depthBufferShared->CreateRTV(rtvDesc);
	depthBufferShared->CreateUAV(uavDesc);

	auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	D3D11_TEXTURE2D_DESC texDescMotionVector{};
	motionVector.texture->GetDesc(&texDescMotionVector);

	texDesc.Format = texDescMotionVector.Format;
	srvDesc.Format = texDesc.Format;
	rtvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	motionVectorBufferShared = new Texture2D(texDesc);
	motionVectorBufferShared->CreateSRV(srvDesc);
	motionVectorBufferShared->CreateRTV(rtvDesc);
	motionVectorBufferShared->CreateUAV(uavDesc);

	{
		IDXGIResource1* dxgiResource = nullptr;
		DX::ThrowIfFailed(HUDLessBufferShared->resource->QueryInterface(IID_PPV_ARGS(&dxgiResource)));

		if (globals::dx12SwapChain->swapChain) {
			HANDLE sharedHandle = nullptr;
			DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(
				nullptr,
				DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
				nullptr,
				&sharedHandle));

			DX::ThrowIfFailed(globals::dx12SwapChain->d3d12Device->OpenSharedHandle(
				sharedHandle,
				IID_PPV_ARGS(&HUDLessBufferShared12)));

			CloseHandle(sharedHandle);
		}
	}

	{
		IDXGIResource1* dxgiResource = nullptr;
		DX::ThrowIfFailed(depthBufferShared->resource->QueryInterface(IID_PPV_ARGS(&dxgiResource)));

		if (globals::dx12SwapChain->swapChain) {
			HANDLE sharedHandle = nullptr;
			DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(
				nullptr,
				DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
				nullptr,
				&sharedHandle));

			DX::ThrowIfFailed(globals::dx12SwapChain->d3d12Device->OpenSharedHandle(
				sharedHandle,
				IID_PPV_ARGS(&depthBufferShared12)));

			CloseHandle(sharedHandle);
		}
	}

	{
		IDXGIResource1* dxgiResource = nullptr;
		DX::ThrowIfFailed(motionVectorBufferShared->resource->QueryInterface(IID_PPV_ARGS(&dxgiResource)));

		if (globals::dx12SwapChain->swapChain) {
			HANDLE sharedHandle = nullptr;
			DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(
				nullptr,
				DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
				nullptr,
				&sharedHandle));

			DX::ThrowIfFailed(globals::dx12SwapChain->d3d12Device->OpenSharedHandle(
				sharedHandle,
				IID_PPV_ARGS(&motionVectorBufferShared12)));

			CloseHandle(sharedHandle);
		}
	}

	copyDepthToSharedBufferCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\FrameGeneration\\CopyDepthToSharedBufferCS.hlsl", {}, "cs_5_0");
}

void Upscaling::CopyBuffersToSharedResources()
{
	if (!d3d12Interop || !settings.frameGenerationMode)
		return;

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	{
		auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
		context->CopyResource(motionVectorBufferShared->resource.get(), motionVector.texture);
	}

	{
		auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];

		{
			auto dispatchCount = Util::GetScreenDispatchCount(true);

			ID3D11ShaderResourceView* views[1] = { depth.depthSRV };
			context->CSSetShaderResources(0, ARRAYSIZE(views), views);

			ID3D11UnorderedAccessView* uavs[1] = { depthBufferShared->uav.get() };
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

	if (!useHUDLess) {
		auto& swapChain = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER];
		ID3D11Resource* swapChainResource;
		swapChain.SRV->GetResource(&swapChainResource);
		context->CopyResource(HUDLessBufferShared->resource.get(), swapChainResource);
	}

	useHUDLess = false;
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
	context->CopyResource(HUDLessBufferShared->resource.get(), swapChainResource);

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
