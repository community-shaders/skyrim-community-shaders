#include "DynamicCubemaps.h"

#include <DDSTextureLoader.h>
#include <DirectXTex.h>

#include "ShaderCache.h"
#include "State.h"

constexpr auto MIPLEVELS = 8;

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	DynamicCubemaps::Settings,
	EnabledSSR,
	EnabledCreator);

/**
 * @brief Gets the shader define options based on current settings.
 *
 * This function checks the current settings (e.g., if SSR is enabled)
 * and returns a vector of string pairs representing shader preprocessor defines.
 * These defines are used to compile shaders with specific features enabled or disabled.
 *
 * @return std::vector<std::pair<std::string_view, std::string_view>> A list of shader define options.
 *         Each pair consists of the define name and its value (often empty for boolean flags).
 */
std::vector<std::pair<std::string_view, std::string_view>> DynamicCubemaps::GetShaderDefineOptions()
{
	std::vector<std::pair<std::string_view, std::string_view>> result;
	if (settings.EnabledSSR) {
		result.push_back({ "ENABLESSR", "" });
	}

	return result;
}

/**
 * @brief Renders the settings UI for the Dynamic Cubemaps feature.
 *
 * This function uses ImGui to display and manage user-configurable settings
 * related to dynamic cubemaps, such as enabling/disabling features,
 * adjusting parameters, and triggering actions like exporting cubemaps.
 * It is typically called once per frame during the UI rendering phase.
 */
void DynamicCubemaps::DrawSettings()
{
	if (ImGui::TreeNodeEx("Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::TreeNodeEx("Screen Space Reflections", ImGuiTreeNodeFlags_DefaultOpen)) {
			recompileFlag |= ImGui::Checkbox("Enable Screen Space Reflections", reinterpret_cast<bool*>(&settings.EnabledSSR));
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Enable Screen Space Reflections on Water");
				if (REL::Module::IsVR() && !enabledAtBoot) {
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
					ImGui::Text(
						"A restart is required to enable in VR. "
						"Save Settings after enabling and restart the game.");
					ImGui::PopStyleColor();
				}
			}
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Dynamic Cubemap Creator", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Text("You must enable creator mode by adding the shader define CREATOR");
			ImGui::Checkbox("Enable Creator", reinterpret_cast<bool*>(&settings.EnabledCreator));
			if (settings.EnabledCreator) {
				ImGui::ColorEdit3("Color", reinterpret_cast<float*>(&settings.CubemapColor));
				ImGui::SliderFloat("Roughness", &settings.CubemapColor.w, 0.0f, 1.0f, "%.2f");
				if (ImGui::Button("Export")) {
					auto device = globals::d3d::device;
					auto context = globals::d3d::context;

					D3D11_TEXTURE2D_DESC texDesc{};
					texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
					texDesc.Height = 1;
					texDesc.Width = 1;
					texDesc.ArraySize = 6;
					texDesc.MipLevels = 1;
					texDesc.SampleDesc.Count = 1;
					texDesc.Usage = D3D11_USAGE_DEFAULT;
					texDesc.BindFlags = 0;
					texDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

					D3D11_SUBRESOURCE_DATA subresourceData[6];

					struct PixelData
					{
						uint8_t r, g, b, a;
					};

					static PixelData colorPixel{};

					colorPixel = { (uint8_t)((settings.CubemapColor.x * 255.0f) + 0.5f),
						(uint8_t)((settings.CubemapColor.y * 255.0f) + 0.5f),
						(uint8_t)((settings.CubemapColor.z * 255.0f) + 0.5f),
						std::min((uint8_t)254u, (uint8_t)((settings.CubemapColor.w * 255.0f) + 0.5f)) };

					static PixelData emptyPixel{};

					subresourceData[0].pSysMem = &colorPixel;
					subresourceData[0].SysMemPitch = sizeof(PixelData);
					subresourceData[0].SysMemSlicePitch = sizeof(PixelData);

					for (uint i = 1; i < 6; i++) {
						subresourceData[i].pSysMem = &emptyPixel;
						subresourceData[i].SysMemPitch = sizeof(PixelData);
						subresourceData[i].SysMemSlicePitch = sizeof(PixelData);
					}

					ID3D11Texture2D* tempTexture;
					DirectX::ScratchImage image;

					try {
						DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, subresourceData, &tempTexture));
						DX::ThrowIfFailed(CaptureTexture(device, context, tempTexture, image));

						if (std::filesystem::create_directories(defaultDynamicCubeMapSavePath)) {
							logger::info("Missing DynamicCubeMap Creator directory created: {}", defaultDynamicCubeMapSavePath);
						}

						std::filesystem::path DynamicCubeMapSavePath = defaultDynamicCubeMapSavePath;
						std::filesystem::path filename(std::format("R{:03d}G{:03d}B{:03d}A{:03d}.dds", colorPixel.r, colorPixel.g, colorPixel.b, colorPixel.a));
						DynamicCubeMapSavePath /= filename;

						if (std::filesystem::exists(DynamicCubeMapSavePath)) {
							logger::info("DynamicCubeMap Creator file for {} already exists, skipping.", filename.string());
						} else {
							DX::ThrowIfFailed(SaveToDDSFile(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::DDS_FLAGS::DDS_FLAGS_NONE, DynamicCubeMapSavePath.c_str()));
							logger::info("DynamicCubeMap Creator file for {} written", filename.string());
						}

					} catch (const std::exception& e) {
						logger::error("Failed in DynamicCubeMap Creator file: {} {}", defaultDynamicCubeMapSavePath, e.what());
					}

					image.Release();
					tempTexture->Release();
				}
			}
			ImGui::TreePop();
		}
		if (REL::Module::IsVR()) {
			if (ImGui::TreeNodeEx("Advanced VR Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
				Util::RenderImGuiSettingsTree(iniVRCubeMapSettings, "VR");
				Util::RenderImGuiSettingsTree(hiddenVRCubeMapSettings, "hiddenVR");
				ImGui::TreePop();
			}
		}

		ImGui::Spacing();
		ImGui::Spacing();

		ImGui::TreePop();
	}
}

/**
 * @brief Loads settings from a JSON object.
 *
 * Populates the internal settings structure from the provided JSON object.
 * Also loads specific game settings for VR if applicable and flags shaders for recompile.
 * @param o_json The JSON object containing the settings to load.
 */
void DynamicCubemaps::LoadSettings(json& o_json)
{
	settings = o_json;
	if (REL::Module::IsVR()) {
		Util::LoadGameSettings(iniVRCubeMapSettings);
	}
	recompileFlag = true;
}

/**
 * @brief Saves current settings to a JSON object.
 *
 * Serializes the internal settings structure into the provided JSON object.
 * Also saves specific game settings for VR if applicable.
 * @param o_json The JSON object where settings will be saved.
 */
void DynamicCubemaps::SaveSettings(json& o_json)
{
	o_json = settings;
	if (REL::Module::IsVR()) {
		Util::SaveGameSettings(iniVRCubeMapSettings);
	}
}

/**
 * @brief Restores all settings to their default values.
 *
 * Resets the internal settings structure to default.
 * Also resets specific game settings to their defaults for VR if applicable and flags shaders for recompile.
 */
void DynamicCubemaps::RestoreDefaultSettings()
{
	settings = {};
	if (REL::Module::IsVR()) {
		Util::ResetGameSettingsToDefaults(iniVRCubeMapSettings);
		Util::ResetGameSettingsToDefaults(hiddenVRCubeMapSettings);
	}
	recompileFlag = true;
}

/**
 * @brief Called after game data has been loaded.
 *
 * Enables necessary boolean game settings for VR if applicable.
 * Registers the menu open/close event handler.
 */
void DynamicCubemaps::DataLoaded()
{
	if (REL::Module::IsVR()) {
		// enable cubemap settings in VR
		Util::EnableBooleanSettings(iniVRCubeMapSettings, GetName());
		Util::EnableBooleanSettings(hiddenVRCubeMapSettings, GetName());
	}
	MenuOpenCloseEventHandler::Register();
}

/**
 * @brief Called after all game loading processes (post-post load).
 *
 * For VR and if SSR is enabled, this function ensures certain hidden game settings
 * required for dynamic cubemaps (like bScreenSpaceReflectionEnabled) are enabled.
 * It logs changes made to these settings.
 */
void DynamicCubemaps::PostPostLoad()
{
	if (REL::Module::IsVR() && settings.EnabledSSR) {
		std::map<std::string, uintptr_t> earlyhiddenVRCubeMapSettings{
			{ "bScreenSpaceReflectionEnabled:Display", 0x1ED5BC0 },
		};
		for (const auto& settingPair : earlyhiddenVRCubeMapSettings) {
			const auto& settingName = settingPair.first;
			const auto address = REL::Offset{ settingPair.second }.address();
			bool* setting = reinterpret_cast<bool*>(address);
			if (!*setting) {
				logger::info("[PostPostLoad] Changing {} from {} to {} to support Dynamic Cubemaps", settingName, *setting, true);
				*setting = true;
			}
		}
		enabledAtBoot = true;
	}
}

/**
 * @brief Processes menu open/close events.
 *
 * Resets the cubemap capture when a loading menu closes (i.e., when a new cell is entered).
 * @param a_event The menu open/close event.
 * @param Unused event source.
 * @return RE::BSEventNotifyControl kContinue to allow other handlers to process the event.
 */
RE::BSEventNotifyControl MenuOpenCloseEventHandler::ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	// When entering a new cell, reset the capture
	if (a_event->menuName == RE::LoadingMenu::MENU_NAME) {
		if (!a_event->opening) {
			auto dynamicCubemaps = DynamicCubemaps::GetSingleton();
			dynamicCubemaps->resetCapture[0] = true;
			dynamicCubemaps->resetCapture[1] = true;
		}
	}
	return RE::BSEventNotifyControl::kContinue;
}

/**
 * @brief Registers the menu open/close event handler.
 *
 * Adds this handler to the list of sinks for menu open/close events.
 * @return true if registration was successful, false otherwise.
 */
bool MenuOpenCloseEventHandler::Register()
{
	static MenuOpenCloseEventHandler singleton;
	auto ui = globals::game::ui;

	if (!ui) {
		logger::error("UI event source not found");
		return false;
	}

	ui->GetEventSource<RE::MenuOpenCloseEvent>()->AddEventSink(&singleton);

	logger::info("Registered {}", typeid(singleton).name());

	return true;
}

/**
 * @brief Clears the cached compute shaders.
 *
 * Releases and nullifies pointers to all compute shaders used for cubemap generation and processing.
 * This is typically called when shader recompilation is needed.
 */
void DynamicCubemaps::ClearShaderCache()
{
	if (updateCubemapCS) {
		updateCubemapCS->Release();
		updateCubemapCS = nullptr;
	}
	if (updateCubemapReflectionsCS) {
		updateCubemapReflectionsCS->Release();
		updateCubemapReflectionsCS = nullptr;
	}
	if (updateCubemapFakeReflectionsCS) {
		updateCubemapFakeReflectionsCS->Release();
		updateCubemapFakeReflectionsCS = nullptr;
	}
	if (inferCubemapCS) {
		inferCubemapCS->Release();
		inferCubemapCS = nullptr;
	}
	if (inferCubemapReflectionsCS) {
		inferCubemapReflectionsCS->Release();
		inferCubemapReflectionsCS = nullptr;
	}
	if (inferCubemapFakeReflectionsCS) {
		inferCubemapFakeReflectionsCS->Release();
		inferCubemapFakeReflectionsCS = nullptr;
	}
	if (specularIrradianceCS) {
		specularIrradianceCS->Release();
		specularIrradianceCS = nullptr;
	}
}

/**
 * @brief Gets or compiles the compute shader for updating the base cubemap.
 *
 * Lazily compiles the shader if it hasn't been loaded yet.
 * @return ID3D11ComputeShader* Pointer to the compiled compute shader.
 */
ID3D11ComputeShader* DynamicCubemaps::GetComputeShaderUpdate()
{
	if (!updateCubemapCS) {
		logger::debug("Compiling UpdateCubemapCS");
		updateCubemapCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\DynamicCubemaps\\UpdateCubemapCS.hlsl", {}, "cs_5_0"));
	}
	return updateCubemapCS;
}

/**
 * @brief Gets or compiles the compute shader for updating reflections cubemap.
 *
 * This version of the shader is compiled with the REFLECTIONS define.
 * Lazily compiles the shader if it hasn't been loaded yet.
 * @return ID3D11ComputeShader* Pointer to the compiled compute shader.
 */
ID3D11ComputeShader* DynamicCubemaps::GetComputeShaderUpdateReflections()
{
	if (!updateCubemapReflectionsCS) {
		logger::debug("Compiling UpdateCubemapCS REFLECTIONS");
		updateCubemapReflectionsCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\DynamicCubemaps\\UpdateCubemapCS.hlsl", { { "REFLECTIONS", "" } }, "cs_5_0"));
	}
	return updateCubemapReflectionsCS;
}

/**
 * @brief Gets or compiles the compute shader for updating fake reflections cubemap.
 *
 * This version of the shader is compiled with the FAKEREFLECTIONS define.
 * Used when the sky is hidden but reflections are still desired.
 * Lazily compiles the shader if it hasn't been loaded yet.
 * @return ID3D11ComputeShader* Pointer to the compiled compute shader.
 */
ID3D11ComputeShader* DynamicCubemaps::GetComputeShaderUpdateFakeReflections()
{
	if (!updateCubemapFakeReflectionsCS) {
		logger::debug("Compiling UpdateCubemapCS FAKEREFLECTIONS");
		updateCubemapFakeReflectionsCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\DynamicCubemaps\\UpdateCubemapCS.hlsl", { { "FAKEREFLECTIONS", "" } }, "cs_5_0"));
	}
	return updateCubemapFakeReflectionsCS;
}

/**
 * @brief Gets or compiles the compute shader for inferring cubemap details.
 *
 * Lazily compiles the shader if it hasn't been loaded yet.
 * @return ID3D11ComputeShader* Pointer to the compiled compute shader.
 */
ID3D11ComputeShader* DynamicCubemaps::GetComputeShaderInferrence()
{
	if (!inferCubemapCS) {
		logger::debug("Compiling InferCubemapCS");
		inferCubemapCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\DynamicCubemaps\\InferCubemapCS.hlsl", {}, "cs_5_0"));
	}
	return inferCubemapCS;
}

/**
 * @brief Gets or compiles the compute shader for inferring reflections cubemap details.
 *
 * This version of the shader is compiled with the REFLECTIONS define.
 * Lazily compiles the shader if it hasn't been loaded yet.
 * @return ID3D11ComputeShader* Pointer to the compiled compute shader.
 */
ID3D11ComputeShader* DynamicCubemaps::GetComputeShaderInferrenceReflections()
{
	if (!inferCubemapReflectionsCS) {
		logger::debug("Compiling InferCubemapCS REFLECTIONS");
		inferCubemapReflectionsCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\DynamicCubemaps\\InferCubemapCS.hlsl", { { "REFLECTIONS", "" } }, "cs_5_0"));
	}
	return inferCubemapReflectionsCS;
}

/**
 * @brief Gets or compiles the compute shader for inferring fake reflections cubemap details.
 *
 * This version of the shader is compiled with the FAKEREFLECTIONS define.
 * Lazily compiles the shader if it hasn't been loaded yet.
 * @return ID3D11ComputeShader* Pointer to the compiled compute shader.
 */
ID3D11ComputeShader* DynamicCubemaps::GetComputeShaderInferrenceFakeReflections()
{
	if (!inferCubemapFakeReflectionsCS) {
		logger::debug("Compiling InferCubemapCS FAKEREFLECTIONS");
		inferCubemapFakeReflectionsCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\DynamicCubemaps\\InferCubemapCS.hlsl", { { "FAKEREFLECTIONS", "" } }, "cs_5_0"));
	}
	return inferCubemapFakeReflectionsCS;
}

/**
 * @brief Gets or compiles the compute shader for calculating specular irradiance.
 *
 * This shader processes a cubemap to generate its specular irradiance map, used for image-based lighting.
 * Lazily compiles the shader if it hasn't been loaded yet.
 * @return ID3D11ComputeShader* Pointer to the compiled compute shader.
 */
ID3D11ComputeShader* DynamicCubemaps::GetComputeShaderSpecularIrradiance()
{
	if (!specularIrradianceCS) {
		logger::debug("Compiling SpecularIrradianceCS");
		specularIrradianceCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\DynamicCubemaps\\SpecularIrradianceCS.hlsl", {}, "cs_5_0"));
	}
	return specularIrradianceCS;
}

/**
 * @brief Updates the cubemap capture from the current scene view.
 *
 * This function dispatches a compute shader to capture the scene into a cubemap texture.
 * It handles both regular environment cubemaps and reflection cubemaps.
 * @param a_reflections True if capturing for reflections, false for standard environment cubemap.
 */
void DynamicCubemaps::UpdateCubemapCapture(bool a_reflections)
{
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	ID3D11ShaderResourceView* srvs[2] = { depth.depthSRV, main.SRV };
	context->CSSetShaderResources(0, 2, srvs);

	uint index = a_reflections ? 1 : 0;

	ID3D11UnorderedAccessView* uavs[3];
	if (a_reflections) {
		uavs[0] = envCaptureReflectionsTexture->uav.get();
		uavs[1] = envCaptureRawReflectionsTexture->uav.get();
		uavs[2] = envCapturePositionReflectionsTexture->uav.get();
	} else {
		uavs[0] = envCaptureTexture->uav.get();
		uavs[1] = envCaptureRawTexture->uav.get();
		uavs[2] = envCapturePositionTexture->uav.get();
	}

	if (resetCapture[index]) {
		float clearColor[4]{ 0, 0, 0, 0 };
		context->ClearUnorderedAccessViewFloat(uavs[0], clearColor);
		context->ClearUnorderedAccessViewFloat(uavs[1], clearColor);
		context->ClearUnorderedAccessViewFloat(uavs[2], clearColor);
		resetCapture[index] = false;
	}

	context->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);

	UpdateCubemapCB updateData{};

	static float3 cameraPreviousPosAdjust[2] = { { 0, 0, 0 }, { 0, 0, 0 } };
	updateData.CameraPreviousPosAdjust = cameraPreviousPosAdjust[index];

	auto eyePosition = Util::GetEyePosition(0);

	cameraPreviousPosAdjust[index] = { eyePosition.x, eyePosition.y, eyePosition.z };

	updateCubemapCB->Update(updateData);

	ID3D11Buffer* buffer = updateCubemapCB->CB();
	context->CSSetConstantBuffers(0, 1, &buffer);

	context->CSSetSamplers(0, 1, &computeSampler);

	context->CSSetShader(a_reflections ? (fakeReflections ? GetComputeShaderUpdateFakeReflections() : GetComputeShaderUpdateReflections()) : GetComputeShaderUpdate(), nullptr, 0);

	context->Dispatch((uint32_t)std::ceil(envCaptureTexture->desc.Width / 8.0f), (uint32_t)std::ceil(envCaptureTexture->desc.Height / 8.0f), 6);

	uavs[0] = nullptr;
	uavs[1] = nullptr;
	uavs[2] = nullptr;
	context->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);

	srvs[0] = nullptr;
	srvs[1] = nullptr;
	context->CSSetShaderResources(0, 2, srvs);

	buffer = nullptr;
	context->CSSetConstantBuffers(0, 1, &buffer);

	context->CSSetShader(nullptr, nullptr, 0);

	ID3D11SamplerState* nullSampler = { nullptr };
	context->CSSetSamplers(0, 1, &nullSampler);
}

/**
 * @brief Performs the inference step for dynamic cubemaps.
 *
 * This function uses compute shaders to infer local reflection information from a captured cubemap.
 * It processes either the standard environment cubemap or the reflections cubemap based on the input parameter.
 * Mipmaps are generated for the input cubemap, and a specific inference compute shader is dispatched.
 * The result is stored in the `envInferredTexture`.
 *
 * @param a_reflections True to process the reflections cubemap, false for the standard environment cubemap.
 */
void DynamicCubemaps::Inferrence(bool a_reflections)
{
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	// Infer local reflection information
	ID3D11UnorderedAccessView* uav = envInferredTexture->uav.get();

	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

	context->GenerateMips((a_reflections ? envCaptureReflectionsTexture : envCaptureTexture)->srv.get());

	auto& cubemap = renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGETS_CUBEMAP::kREFLECTIONS];

	ID3D11ShaderResourceView* srvs[3] = { (a_reflections ? envCaptureReflectionsTexture : envCaptureTexture)->srv.get(), cubemap.SRV, defaultCubemap };
	context->CSSetShaderResources(0, 3, srvs);

	context->CSSetSamplers(0, 1, &computeSampler);

	context->CSSetShader(a_reflections ? (fakeReflections ? GetComputeShaderInferrenceFakeReflections() : GetComputeShaderInferrenceReflections()) : GetComputeShaderInferrence(), nullptr, 0);

	context->Dispatch((uint32_t)std::ceil(envCaptureTexture->desc.Width / 8.0f), (uint32_t)std::ceil(envCaptureTexture->desc.Height / 8.0f), 6);

	srvs[0] = nullptr;
	srvs[1] = nullptr;
	srvs[2] = nullptr;
	context->CSSetShaderResources(0, 3, srvs);

	uav = nullptr;

	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

	context->CSSetShader(nullptr, 0, 0);

	ID3D11SamplerState* sampler = nullptr;
	context->CSSetSamplers(0, 1, &sampler);
}

/**
 * @brief Computes the pre-filtered specular environment map (irradiance) for a cubemap.
 *
 * This function first copies the content from `envInferredTexture` to the target cubemap
 * (`envReflectionsTexture` or `envTexture`). It then generates mipmaps for `envInferredTexture`
 * and dispatches a compute shader (`GetComputeShaderSpecularIrradiance`) iteratively for each mip level
 * to produce a pre-filtered specular map. The results are written into the mip levels of the target cubemap.
 *
 * @param a_reflections True to process the reflections cubemap, false for the standard environment cubemap.
 */
void DynamicCubemaps::Irradiance(bool a_reflections)
{
	auto context = globals::d3d::context;

	// Copy cubemap to other resources
	for (uint face = 0; face < 6; face++) {
		uint srcSubresourceIndex = D3D11CalcSubresource(0, face, MIPLEVELS);
		context->CopySubresourceRegion(a_reflections ? envReflectionsTexture->resource.get() : envTexture->resource.get(), D3D11CalcSubresource(0, face, MIPLEVELS), 0, 0, 0, envInferredTexture->resource.get(), srcSubresourceIndex, nullptr);
	}

	// Compute pre-filtered specular environment map.
	{
		auto srv = envInferredTexture->srv.get();
		context->GenerateMips(srv);

		context->CSSetShaderResources(0, 1, &srv);
		context->CSSetSamplers(0, 1, &computeSampler);
		context->CSSetShader(GetComputeShaderSpecularIrradiance(), nullptr, 0);

		ID3D11Buffer* buffer = spmapCB->CB();
		context->CSSetConstantBuffers(0, 1, &buffer);

		float const delta_roughness = 1.0f / std::max(float(MIPLEVELS - 1), 1.0f);

		std::uint32_t size = std::max(envTexture->desc.Width, envTexture->desc.Height) / 2;

		for (std::uint32_t level = 1; level < MIPLEVELS; level++, size /= 2) {
			const UINT numGroups = (UINT)std::max(1u, size / 8);

			const SpecularMapFilterSettingsCB spmapConstants = { level * delta_roughness };
			spmapCB->Update(spmapConstants);

			auto uav = a_reflections ? uavReflectionsArray[level - 1] : uavArray[level - 1];

			context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
			context->Dispatch(numGroups, numGroups, 6);
		}
	}

	ID3D11ShaderResourceView* nullSRV = { nullptr };
	ID3D11SamplerState* nullSampler = { nullptr };
	ID3D11Buffer* nullBuffer = { nullptr };
	ID3D11UnorderedAccessView* nullUAV = { nullptr };

	context->CSSetShaderResources(0, 1, &nullSRV);
	context->CSSetSamplers(0, 1, &nullSampler);
	context->CSSetShader(nullptr, 0, 0);
	context->CSSetConstantBuffers(0, 1, &nullBuffer);
	context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
}

/**
 * @brief Manages the multi-stage process of updating dynamic cubemaps.
 *
 * This function acts as a state machine, cycling through tasks such as capturing, inferring,
 * and calculating irradiance for both standard and reflection cubemaps.
 * It calls the appropriate helper functions (`UpdateCubemapCapture`, `Inferrence`, `Irradiance`)
 * based on the current `nextTask` state. It also handles shader recompilation if the `recompileFlag` is set.
 */
void DynamicCubemaps::UpdateCubemap()
{
	TracyD3D11Zone(globals::state->tracyCtx, "Cubemap Update");
	if (recompileFlag) {
		logger::debug("Recompiling for Dynamic Cubemaps");
		auto shaderCache = globals::shaderCache;
		if (!shaderCache->Clear("Data//Shaders//ISReflectionsRayTracing.hlsl"))
			// if can't find specific hlsl file cache, clear all image space files
			shaderCache->Clear(RE::BSShader::Types::ImageSpace);
		recompileFlag = false;
	}

	switch (nextTask) {
	case NextTask::kCapture:
		UpdateCubemapCapture(false);
		nextTask = NextTask::kInferrence;
		break;

	case NextTask::kInferrence:
		nextTask = NextTask::kIrradiance;
		Inferrence(false);
		break;

	case NextTask::kIrradiance:
		if (activeReflections)
			nextTask = NextTask::kCapture2;
		else
			nextTask = NextTask::kCapture;
		Irradiance(false);
		break;

	case NextTask::kCapture2:
		UpdateCubemapCapture(true);
		nextTask = NextTask::kInferrence2;
		break;

	case NextTask::kInferrence2:
		Inferrence(true);
		nextTask = NextTask::kIrradiance2;
		break;

	case NextTask::kIrradiance2:
		nextTask = NextTask::kCapture;
		Irradiance(true);
		break;
	}
}

/**
 * @brief Sets the generated cubemap textures for use in post-deferred rendering passes.
 *
 * Binds the active cubemap texture (`envReflectionsTexture` or `envTexture`, depending on `activeReflections`)
 * and the base `envTexture` to pixel shader resource slots (starting at slot 30).
 * This makes the cubemaps available for effects like screen-space reflections or image-based lighting.
 */
void DynamicCubemaps::PostDeferred()
{
	auto context = globals::d3d::context;

	ID3D11ShaderResourceView* views[2] = { (activeReflections ? envReflectionsTexture : envTexture)->srv.get(), envTexture->srv.get() };
	context->PSSetShaderResources(30, 2, views);
}

/**
 * @brief Initializes and sets up all D3D11 resources required for dynamic cubemap generation.
 *
 * This function ensures all necessary compute shaders are compiled. It creates sampler states,
 * and allocates various Texture2D resources for capturing, processing, and storing cubemaps
 * (e.g., `envCaptureTexture`, `envTexture`, `envReflectionsTexture`, `envInferredTexture`).
 * It also sets up their Shader Resource Views (SRVs) and Unordered Access Views (UAVs),
 * initializes constant buffers, and loads a default cubemap.
 */
void DynamicCubemaps::SetupResources()
{
	GetComputeShaderUpdate();
	GetComputeShaderUpdateReflections();
	GetComputeShaderInferrence();
	GetComputeShaderInferrenceReflections();
	GetComputeShaderSpecularIrradiance();

	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	{
		D3D11_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.MinLOD = 0;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, &computeSampler));
	}

	auto& cubemap = renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGETS_CUBEMAP::kREFLECTIONS];

	{
		D3D11_TEXTURE2D_DESC texDesc;
		cubemap.texture->GetDesc(&texDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		cubemap.SRV->GetDesc(&srvDesc);

		texDesc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

		// Create additional resources

		texDesc.MipLevels = MIPLEVELS;
		texDesc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
		srvDesc.TextureCube.MipLevels = MIPLEVELS;

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = texDesc.Format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
		uavDesc.Texture2DArray.MipSlice = 0;
		uavDesc.Texture2DArray.FirstArraySlice = 0;
		uavDesc.Texture2DArray.ArraySize = texDesc.ArraySize;

		envCaptureTexture = new Texture2D(texDesc);
		envCaptureTexture->CreateSRV(srvDesc);
		envCaptureTexture->CreateUAV(uavDesc);

		envCaptureRawTexture = new Texture2D(texDesc);
		envCaptureRawTexture->CreateSRV(srvDesc);
		envCaptureRawTexture->CreateUAV(uavDesc);

		envCapturePositionTexture = new Texture2D(texDesc);
		envCapturePositionTexture->CreateSRV(srvDesc);
		envCapturePositionTexture->CreateUAV(uavDesc);

		envCaptureReflectionsTexture = new Texture2D(texDesc);
		envCaptureReflectionsTexture->CreateSRV(srvDesc);
		envCaptureReflectionsTexture->CreateUAV(uavDesc);

		envCaptureRawReflectionsTexture = new Texture2D(texDesc);
		envCaptureRawReflectionsTexture->CreateSRV(srvDesc);
		envCaptureRawReflectionsTexture->CreateUAV(uavDesc);

		envCapturePositionReflectionsTexture = new Texture2D(texDesc);
		envCapturePositionReflectionsTexture->CreateSRV(srvDesc);
		envCapturePositionReflectionsTexture->CreateUAV(uavDesc);

		texDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
		srvDesc.Format = texDesc.Format;
		uavDesc.Format = texDesc.Format;

		envTexture = new Texture2D(texDesc);
		envTexture->CreateSRV(srvDesc);
		envTexture->CreateUAV(uavDesc);

		envReflectionsTexture = new Texture2D(texDesc);
		envReflectionsTexture->CreateSRV(srvDesc);
		envReflectionsTexture->CreateUAV(uavDesc);

		envInferredTexture = new Texture2D(texDesc);
		envInferredTexture->CreateSRV(srvDesc);
		envInferredTexture->CreateUAV(uavDesc);

		updateCubemapCB = new ConstantBuffer(ConstantBufferDesc<UpdateCubemapCB>());
	}

	{
		spmapCB = new ConstantBuffer(ConstantBufferDesc<SpecularMapFilterSettingsCB>());
	}

	{
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = envTexture->desc.Format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;

		uavDesc.Texture2DArray.FirstArraySlice = 0;
		uavDesc.Texture2DArray.ArraySize = envTexture->desc.ArraySize;

		for (std::uint32_t level = 1; level < MIPLEVELS; ++level) {
			uavDesc.Texture2DArray.MipSlice = level;
			DX::ThrowIfFailed(device->CreateUnorderedAccessView(envTexture->resource.get(), &uavDesc, &uavArray[level - 1]));
		}

		for (std::uint32_t level = 1; level < MIPLEVELS; ++level) {
			uavDesc.Texture2DArray.MipSlice = level;
			DX::ThrowIfFailed(device->CreateUnorderedAccessView(envReflectionsTexture->resource.get(), &uavDesc, &uavReflectionsArray[level - 1]));
		}
	}

	{
		DirectX::CreateDDSTextureFromFile(device, L"Data\\Shaders\\DynamicCubemaps\\defaultcubemap.dds", nullptr, &defaultCubemap);
	}
}

/**
 * @brief Resets the active state of reflections based on the current sky mode.
 *
 * This function checks the game's current sky object and its mode to determine
 * if reflections should be active. It also considers whether the sky is hidden,
 * which might imply using fake reflections.
 */
void DynamicCubemaps::Reset()
{
	if (auto sky = globals::game::sky) {
		activeReflections = sky->mode.get() == RE::Sky::Mode::kFull;
		fakeReflections = activeReflections && sky->flags.any(RE::Sky::Flags::kHideSky);
	} else {
		activeReflections = false;
		fakeReflections = false;
	}
}
