#include "Upscaling.h"

#include "Hooks.h"
#include "State.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Upscaling::Settings,
	upscaleMethod,
	upscaleMethodNoDLSS,
	upscaleMethodNoFSR,
	sharpness,
	dlssPreset);

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
		const char* dlssPresets[] = { "Default", "Preset A", "Preset B", "Preset C", "Preset D", "Preset E", "Preset F" };
		ImGui::SliderInt("DLSS Preset", (int*)&settings.dlssPreset, 0, 6, std::format("{}", dlssPresets[(uint)settings.dlssPreset]).c_str());
		settings.dlssPreset = std::min(6u, (uint)settings.dlssPreset);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Default:\n"
				"Preset E\n\n"
				"Preset A (intended for Perf/Balanced/Quality modes):\n"
				"An older variant best suited to combat ghosting for elements with missing inputs (such as motion vectors)\n\n"
				"Preset B (intended for Ultra Perf mode):\n"
				"Similar to Preset A but for Ultra Performance mode\n\n"
				"The CS default preset. Preset C (intended for Perf/Balanced/Quality modes):\n"
				"Preset which generally favors current frame information. Generally well-suited for fast-paced game content\n\n"
				"Preset D (intended for Perf/Balanced/Quality modes):\n"
				"Similar to Preset E. Preset E is generally recommended over Preset D.\n\n"
				"Preset E (intended for Perf/Balanced/Quality modes):\n"
				"Default preset for Perf/Balanced/Quality mode. Generally favors image stability\n\n"
				"Preset F (intended for Ultra Perf/DLAA modes):\n"
				"The default preset for Ultra Perf and DLAA modes.");
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

ID3D11ComputeShader* Upscaling::GetRCASCS()
{
	static auto previousSharpness = settings.sharpness;
	auto currentSharpness = settings.sharpness;

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

ID3D11ComputeShader* Upscaling::GetEncodeTexturesCS()
{
	if (!encodeTexturesCS) {
		logger::debug("Compiling EncodeTexturesCS.hlsl");
		encodeTexturesCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data/Shaders/Upscaling/EncodeTexturesCS.hlsl", {}, "cs_5_0");
	}
	return encodeTexturesCS;
}

ID3D11ComputeShader* Upscaling::GetEncodeTexturesDLSSCS()
{
	if (!encodeTexturesDLSSCS) {
		logger::debug("Compiling EncodeTexturesCS.hlsl DLSS");
		encodeTexturesDLSSCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data/Shaders/Upscaling/EncodeTexturesCS.hlsl", { { "DLSS", "" } }, "cs_5_0");
	}
	return encodeTexturesDLSSCS;
}

void Upscaling::UpdateJitter()
{
	auto upscaleMethod = GetUpscaleMethod();
	if (upscaleMethod == UpscaleMethod::kFSR || upscaleMethod == UpscaleMethod::kDLSS) {
		static auto gameViewport = globals::game::graphicsState;

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

	inputTextureSRV->Release();

	ID3D11RenderTargetView* outputTextureRTV;
	ID3D11DepthStencilView* dsv;
	context->OMGetRenderTargets(1, &outputTextureRTV, &dsv);
	context->OMSetRenderTargets(0, nullptr, nullptr);

	outputTextureRTV->Release();

	if (dsv)
		dsv->Release();

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

			context->CSSetShader(upscaleMethod == UpscaleMethod::kDLSS ? GetEncodeTexturesDLSSCS() : GetEncodeTexturesCS(), nullptr, 0);

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
			globals::streamline->Upscale(upscalingTexture, alphaMaskTexture, (sl::DLSSPreset)settings.dlssPreset);
		else if (upscaleMethod == UpscaleMethod::kFSR)
			FidelityFX::GetSingleton()->Upscale(upscalingTexture, alphaMaskTexture, jitter, reset, settings.sharpness);

		reset = false;

		state->EndPerfEvent();
	}

	if (upscaleMethod != UpscaleMethod::kFSR && settings.sharpness > 0.0f) {
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
}

void Upscaling::SharpenTAA()
{
	std::lock_guard<std::mutex> lock(settingsMutex);  // Lock for the duration of this function

	CheckResources();

	auto state = globals::state;
	auto context = globals::d3d::context;

	ID3D11ShaderResourceView* inputTextureSRV;
	context->PSGetShaderResources(0, 1, &inputTextureSRV);

	inputTextureSRV->Release();

	ID3D11RenderTargetView* outputTextureRTV;
	ID3D11DepthStencilView* dsv;
	context->OMGetRenderTargets(1, &outputTextureRTV, &dsv);
	context->OMSetRenderTargets(0, nullptr, nullptr);

	outputTextureRTV->Release();

	if (dsv)
		dsv->Release();

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

void Upscaling::InstallHooks()
{
	if (!globals::state->upscalerLoaded) {
		bool isGOG = !GetModuleHandle(L"steam_api64.dll");

		stl::write_thunk_call<Main_UpdateJitter>(REL::RelocationID(75460, 77245).address() + REL::Relocate(0xE5, isGOG ? 0x133 : 0xE2, 0x104));
		stl::write_thunk_call<TAA_BeginTechnique>(REL::RelocationID(100540, 107270).address() + REL::Relocate(0x3E9, 0x3EA, 0x448));
		stl::write_thunk_call<TAA_EndTechnique>(REL::RelocationID(100540, 107270).address() + REL::Relocate(0x3F3, 0x3F4, 0x452));
		stl::write_thunk_call<BSImageSpacerShader_RenderPassImmediately>(REL::RelocationID(100951, 107733).address() + REL::Relocate(0x82, 0x78, 0x7E));

		logger::info("[Upscaling] Installed hooks");

		globals::game::ui->GetEventSource<RE::MenuOpenCloseEvent>()->AddEventSink(globals::upscaling);
		logger::info("[Upscaling] Registered for MenuOpenCloseEvent");
	} else {
		logger::info("[Upscaling] Not installing hooks due to Skyrim Upscaler");
	}
}
