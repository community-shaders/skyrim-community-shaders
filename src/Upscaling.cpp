#include "Upscaling.h"

#include "SwapChain.h"
#include "Hooks.h"
#include "State.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Upscaling::Settings,
	upscaleMethod,
	upscaleMethodNoDLSS,
	upscaleMethodNoFSR,
	sharpness,
	dlssPreset,
	vsyncMode,
	frameLimitMode,
	frameGenerationMode,
	frameGenerationForceEnable);

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
			setting->data.b = false;
		}
	}

	// Check the current upscale method
	auto upscaleMethod = GetUpscaleMethod();

	// Display sharpness slider if applicable
	if (upscaleMethod != UpscaleMethod::kNONE) {
		ImGui::SliderFloat("Sharpness", &settings.sharpness, 0.0f, 1.0f, "%.1f");
		settings.sharpness = std::clamp(settings.sharpness, 0.0f, 1.0f);
	}

	// Display upscaling preset if applicable

	if (upscaleMethod == UpscaleMethod::kFSR || upscaleMethod == UpscaleMethod::kDLSS) {
		const char* upscalePresets[] = { "Ultra Performance", "Performance", "Balanced", "Quality" };
		ImGui::SliderInt("Upscale Preset", (int*)&settings.upscalePreset, 1, 4, std::format("{}", upscalePresets[4 - settings.upscalePreset]).c_str());
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
		if (ImGui::TreeNodeEx("Frame Generation", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Text("Frame Generation interpolates real frames with generated ones for a smoother experience");
			ImGui::Text("Uses AMD FSR 3.1 Frame Generation technology");
			ImGui::Text("Requires a D3D11 to D3D12 proxy which can create compatibility issues");
			ImGui::Text("Toggling this setting requires a restart to work correctly.");

			bool onlyRequiresRestart = true;

			if (!isWindowed) {
				ImGui::Text("Warning: Requires windowed mode");
				onlyRequiresRestart = false;
			}

			if (lowRefreshRate && !settings.frameGenerationForceEnable) {
				ImGui::Text("Warning: Requires a high refresh rate monitor or Force Enable Frame Generation");
				onlyRequiresRestart = false;
			}

			if (!FidelityFX::GetSingleton()->module) {
				ImGui::Text("Warning: Requires amd_fidelityfx_dx12.dll to be loaded");
				onlyRequiresRestart = false;
			}

			auto dx12Interop = SwapChain::GetSingleton()->dx12Interop;

			if (onlyRequiresRestart && settings.frameGenerationMode && !dx12Interop)
				ImGui::Text("Warning: Requires restart");

			const char* toggleModes[] = { "Disabled", "Enabled" };

			ImGui::SliderInt("Frame Generation", (int*)&settings.frameGenerationMode, 0, 1, std::format("{}", toggleModes[settings.frameGenerationMode]).c_str());

			if (!settings.frameGenerationMode && dx12Interop)
				ImGui::BeginDisabled();

			ImGui::SliderInt("V-Sync", (int*)&settings.vsyncMode, 0, 1, std::format("{}", toggleModes[settings.vsyncMode]).c_str());

			if (!settings.frameGenerationMode && dx12Interop)
				ImGui::EndDisabled();

			if ((settings.vsyncMode || !settings.frameGenerationMode) && dx12Interop)
				ImGui::BeginDisabled();

			ImGui::SliderInt("Frame Limit (Variable Refresh Rate)", (int*)&settings.frameLimitMode, 0, 1, std::format("{}", toggleModes[settings.frameLimitMode]).c_str());

			if ((settings.vsyncMode || !settings.frameGenerationMode) && dx12Interop)
				ImGui::EndDisabled();

			ImGui::Text("Allows frame generation to function on low refresh rate monitors");
			ImGui::SliderInt("Force Enable Frame Generation", (int*)&settings.frameGenerationForceEnable, 0, 1, std::format("{}", toggleModes[settings.frameGenerationForceEnable]).c_str());

			ImGui::TreePop();
		}
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
	auto fidelityFX = FidelityFX::GetSingleton();

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

	auto context = globals::d3d::context;
	auto state = globals::state;
	auto swapChain = globals::swapChain;

	auto inputTexture = swapChain->swapChainBuffer->resource.get();
	auto outputTexture = swapChain->dx12Interop ? swapChain->upscaledSwapChainBufferWrapped->resource11 : swapChain->upscaledSwapChainBuffer->resource.get();

	//context->CopySubresourceRegion(
	//	outputTexture,
	//	0,
	//	0,
	//	0,
	//	0,
	//	inputTexture,
	//	0,
	//	nullptr
	//);

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

		if (upscaleMethod == UpscaleMethod::kDLSS)
			globals::streamline->Upscale(inputTexture, outputTexture, alphaMaskTexture, settings.dlssPreset == 0 ? (sl::DLSSPreset)11u : sl::DLSSPreset::ePresetE);
		else if (upscaleMethod == UpscaleMethod::kFSR)
			FidelityFX::GetSingleton()->Upscale(inputTexture, outputTexture, alphaMaskTexture, jitter, reset);

		reset = false;

		state->EndPerfEvent();
	}

	if (settings.sharpness > 0.0f) {
		state->BeginPerfEvent("Sharpening");

		globals::streamline->Sharpen(outputTexture, settings.sharpness);

		state->EndPerfEvent();
	}
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

	texDesc.Format = DXGI_FORMAT_R8_UNORM;
	srvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	alphaMaskTexture = new Texture2D(texDesc);
	alphaMaskTexture->CreateSRV(srvDesc);
	alphaMaskTexture->CreateUAV(uavDesc);

	if (globals::swapChain->dx12Interop)
		CreateFrameGenerationResources();
}

void Upscaling::DestroyUpscalingResources()
{
	alphaMaskTexture->srv = nullptr;
	alphaMaskTexture->uav = nullptr;
	alphaMaskTexture->resource = nullptr;
	delete alphaMaskTexture;
}

void Upscaling::CreateFrameGenerationResources()
{
	logger::info("[Frame Generation] Creating resources");

	auto renderer = RE::BSGraphics::Renderer::GetSingleton();
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

	colorBufferShared = new Texture2D(texDesc);
	colorBufferShared->CreateSRV(srvDesc);
	colorBufferShared->CreateRTV(rtvDesc);
	colorBufferShared->CreateUAV(uavDesc);

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
		DX::ThrowIfFailed(colorBufferShared->resource->QueryInterface(IID_PPV_ARGS(&dxgiResource)));

		HANDLE sharedHandle = nullptr;
		DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(
			nullptr,
			DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
			nullptr,
			&sharedHandle));

		DX::ThrowIfFailed(globals::swapChain->d3d12Device->OpenSharedHandle(
			sharedHandle,
			IID_PPV_ARGS(&colorBufferShared12)));

		CloseHandle(sharedHandle);
	}

	{
		IDXGIResource1* dxgiResource = nullptr;
		DX::ThrowIfFailed(depthBufferShared->resource->QueryInterface(IID_PPV_ARGS(&dxgiResource)));

		HANDLE sharedHandle = nullptr;
		DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(
			nullptr,
			DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
			nullptr,
			&sharedHandle));

		DX::ThrowIfFailed(globals::swapChain->d3d12Device->OpenSharedHandle(
			sharedHandle,
			IID_PPV_ARGS(&depthBufferShared12)));

		CloseHandle(sharedHandle);
	}

	{
		IDXGIResource1* dxgiResource = nullptr;
		DX::ThrowIfFailed(motionVectorBufferShared->resource->QueryInterface(IID_PPV_ARGS(&dxgiResource)));

		HANDLE sharedHandle = nullptr;
		DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(
			nullptr,
			DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
			nullptr,
			&sharedHandle));

		DX::ThrowIfFailed(globals::swapChain->d3d12Device->OpenSharedHandle(
			sharedHandle,
			IID_PPV_ARGS(&motionVectorBufferShared12)));

		CloseHandle(sharedHandle);
	}

	copyDepthToSharedBufferCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\FrameGeneration\\CopyDepthToSharedBufferCS.hlsl", {}, "cs_5_0");
}

void Upscaling::CopyResourcesToSharedBuffers()
{
	if (!globals::swapChain->swapChain || !settings.frameGenerationMode || RE::UI::GetSingleton()->GameIsPaused())
		return;

	auto& context = globals::d3d::context;
	auto renderer = RE::BSGraphics::Renderer::GetSingleton();

	ID3D11RenderTargetView* backupViews[8];
	ID3D11DepthStencilView* backupDsv;
	context->OMGetRenderTargets(8, backupViews, &backupDsv);  // Backup bound render targets
	context->OMSetRenderTargets(0, nullptr, nullptr);         // Unbind all bound render targets

	auto& swapChain = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER];

	ID3D11Resource* swapChainResource;
	swapChain.SRV->GetResource(&swapChainResource);

	context->CopyResource(colorBufferShared->resource.get(), swapChainResource);

	auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	context->CopyResource(motionVectorBufferShared->resource.get(), motionVector.texture);

	{
		auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

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

	context->OMSetRenderTargets(8, backupViews, backupDsv);  // Restore all bound render targets

	for (int i = 0; i < 8; i++) {
		if (backupViews[i])
			backupViews[i]->Release();
	}

	if (backupDsv)
		backupDsv->Release();
}
