#include "PostProcessing.h"

#include "IconsFontAwesome5.h"
#include "imgui_stdlib.h"

#include "Menu.h"
#include "Profiler.h"
#include "State.h"
#include "Util.h"

#include "PostProcessing/RasterPass.h"

#include "Features/Upscaling.h"

#include <format>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	PostProcessing::Settings,
	DisableVanillaTonemapping)

void PostProcessing::DrawSettings()
{
	static int pipelinePageNum = 0;
	static int pipelineFeatIdx = 0;
	static int presetIdx = -1;

	ImGui::BeginGroup();
	std::string currentPreset = (presetIdx >= 0 && presetIdx < presets.size()) ? presets[presetIdx] : T("feature.post_processing.select_a_preset", "Select a preset");

	if (ImGui::BeginCombo("##PresetCombo", currentPreset.c_str())) {
		presets = LoadPresets();

		for (int i = 0; i < presets.size(); ++i) {
			bool isSelected = presetIdx == i;
			if (ImGui::Selectable(presets[i].c_str(), isSelected))
				presetIdx = i;
			if (isSelected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	ImGui::SameLine();
	if (ImGui::Button(T("feature.post_processing.load", "Load"))) {
		if (presetIdx >= 0 && presetIdx < presets.size()) {
			LoadPresetFrom(presets[presetIdx]);
		}
	}

	ImGui::EndGroup();
	ImGui::BeginGroup();
	static std::string newPresetName = "";
	ImGui::InputText("##NewPresetName", &newPresetName);

	ImGui::SameLine();
	if (ImGui::Button(T("feature.post_processing.save", "Save"))) {
		if (!newPresetName.empty())
			SavePresetTo(newPresetName);
	}

	ImGui::EndGroup();

	ImGui::Separator();

	// Effects11 replaces the whole tonemap pass, so these toggles would have no effect while
	// it owns the frame. Disable them rather than let them silently do nothing.
	const bool tonemapTakenByEffects11 = IsTonemapOwnedByEffects11();

	ImGui::BeginDisabled(tonemapTakenByEffects11);

	// A disabled checkbox never reports a click, so bypass keeps its stored value while forced on.
	bool bypassDisplay = bypass || tonemapTakenByEffects11;
	if (ImGui::Checkbox(T("feature.post_processing.bypass", "Bypass"), &bypassDisplay))
		bypass = bypassDisplay;

	ImGui::SameLine();
	ImGui::Checkbox(T("feature.post_processing.disable_vanilla_tonemapping", "Disable Vanilla Tonemapping"), (bool*)&settings.DisableVanillaTonemapping);
	ImGui::EndDisabled();

	if (tonemapTakenByEffects11) {
		ImGui::PushStyleColor(ImGuiCol_Text, Menu::GetSingleton()->GetTheme().StatusPalette.Warning);
		ImGui::TextWrapped("%s", T("feature.post_processing.tonemap_owned_by_effects11",
									 "Tonemapping is currently handled by Effects 11. Post Processing effects that run "
									 "before tonemapping still apply. To use Post Processing tonemapping instead, either "
									 "disable Effects 11 or enable its \"UseOriginalPostProcessing\" setting."));
		ImGui::PopStyleColor();
	}

	ImGui::Separator();

	if (pipelinePageNum == 0) {
		for (int i = 0; i < pipeline.size(); ++i) {
			auto& feat = pipeline[i];
			if (feat && feat->IsVisible()) {
				auto displayName = feat->GetDisplayName();
				auto description = feat->GetDesc();
				ImGui::PushID(feat->GetType().c_str());
				ImGui::Checkbox("##Enabled", &feat->enabled);
				ImGui::SameLine();
				if (ImGui::Button(ICON_FA_BARS)) {
					pipelineFeatIdx = i;
					pipelinePageNum = 1;
				}
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text("%s", T("feature.post_processing.edit_settings_for_this_feature", "Edit settings for this feature."));
				ImGui::SameLine();
				ImGui::Text("%s", displayName.c_str());
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text("%s", description.c_str());
				ImGui::PopID();
			}
		}
	} else if (pipelinePageNum == 1) {
		auto backLabel = std::format("{} {}", ICON_FA_ARROW_LEFT, T("feature.post_processing.back_to_pipeline", "Back to Pipeline"));
		if (ImGui::Button(backLabel.c_str())) {
			pipelinePageNum = 0;
		}
		ImGui::Separator();
		if (pipelineFeatIdx >= 0 && pipelineFeatIdx < pipeline.size()) {
			auto& feat = pipeline[pipelineFeatIdx];
			if (feat) {
				auto displayName = feat->GetDisplayName();
				auto description = feat->GetDesc();
				ImGui::PushID(feat->GetType().c_str());

				ImGui::SeparatorText(displayName.c_str());
				ImGui::TextWrapped("%s", description.c_str());

				ImGui::Spacing();
				auto recompileLabel = std::format("{} {}", ICON_FA_SYNC, T("feature.post_processing.recompile_shaders", "Recompile Shaders"));
				if (ImGui::Button(recompileLabel.c_str())) {
					feat->ClearShaderCache();
				}
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text("%s", T("feature.post_processing.recompile_shaders_for_this_sub_feature_only", "Recompile shaders for this sub-feature only."));
				ImGui::Separator();
				ImGui::Spacing();
				ImGui::Checkbox(T("feature.post_processing.enabled", "Enabled"), &feat->enabled);
				if (feat->enabled) {
					ImGui::Indent();
					feat->DrawSettings();
					ImGui::Unindent();
				} else {
					ImGui::TextDisabled("%s", T("feature.post_processing.enable_the_feature_to_see_its_settings", "Enable the feature to see its settings."));
				}

				ImGui::PopID();
			} else {
				ImGui::TextDisabled("%s", T("feature.post_processing.selected_feature_is_not_valid", "Selected feature is not valid."));
				pipelinePageNum = 0;
			}
		} else {
			ImGui::TextDisabled("%s", T("feature.post_processing.invalid_feature_selected_returning_to_list", "Invalid feature selected. Returning to list."));
			pipelinePageNum = 0;
		}
	}

	ImGui::Separator();

	if (ImGui::TreeNode(T("feature.post_processing.debug", "Debug"))) {
		if (ImGui::TreeNode(T("feature.post_processing.game_imagespace_values", "Game ImageSpace Values"))) {
			ImGui::Text(T("feature.post_processing.base_amount", "Base Amount: %.3f"), imageSpaceManager->gameISData.baseAmount);
			ImGui::Text("%s", T("feature.post_processing.base_data", "Base Data:"));
			ImGui::Text("%s", T("feature.post_processing.cinematic_values", "Cinematic Values:"));
			ImGui::Text(T("feature.post_processing.saturation_brightness_contrast_values", "Saturation: %.3f\nBrightness: %.3f\nContrast: %.3f"),
				imageSpaceManager->gameISData.baseData.cinematic.saturation,
				imageSpaceManager->gameISData.baseData.cinematic.brightness,
				imageSpaceManager->gameISData.baseData.cinematic.contrast);

			ImGui::Text("%s", T("feature.post_processing.hdr_values", "HDR Values:"));
			ImGui::Text(T("feature.post_processing.hdr_values_detail", "Eye Adapt Speed: %.3f\nBloom Blur Radius: %.3f\nBloom Threshold: %.3f\nBloom Scale: %.3f\nReceive Bloom Threshold: %.3f\nWhite: %.3f\nSunlight Scale: %.3f\nSky Scale: %.3f\nEye Adapt Strength: %.3f"),
				imageSpaceManager->gameISData.baseData.hdr.eyeAdaptSpeed,
				imageSpaceManager->gameISData.baseData.hdr.bloomBlurRadius,
				imageSpaceManager->gameISData.baseData.hdr.bloomThreshold,
				imageSpaceManager->gameISData.baseData.hdr.bloomScale,
				imageSpaceManager->gameISData.baseData.hdr.receiveBloomThreshold,
				imageSpaceManager->gameISData.baseData.hdr.white,
				imageSpaceManager->gameISData.baseData.hdr.sunlightScale,
				imageSpaceManager->gameISData.baseData.hdr.skyScale,
				imageSpaceManager->gameISData.baseData.hdr.eyeAdaptStrength);

			ImGui::Text("%s", T("feature.post_processing.tint_values", "Tint Values:"));
			ImGui::Text(T("feature.post_processing.tint_values_detail", "Tint Amount: %.3f\nTint Color: (%.3f, %.3f, %.3f)"),
				imageSpaceManager->gameISData.baseData.tint.amount,
				imageSpaceManager->gameISData.baseData.tint.color.red,
				imageSpaceManager->gameISData.baseData.tint.color.green,
				imageSpaceManager->gameISData.baseData.tint.color.blue);

			ImGui::Text("%s", T("feature.post_processing.depth_of_field_values", "Depth of Field Values:"));
			ImGui::Text(T("feature.post_processing.depth_of_field_values_detail", "DOF Strength: %.3f\nDOF Distance: %.3f\nDOF Range: %.3f\nDOF Flags: %d\nDOF Sky Blur Radius: %d"),
				imageSpaceManager->gameISData.baseData.depthOfField.strength,
				imageSpaceManager->gameISData.baseData.depthOfField.distance,
				imageSpaceManager->gameISData.baseData.depthOfField.range,
				imageSpaceManager->gameISData.baseData.depthOfField.flags,
				static_cast<int>(imageSpaceManager->gameISData.baseData.depthOfField.skyBlurRadius.get()));

			ImGui::Text(T("feature.post_processing.mod_amount", "Mod Amount: %.3f"), imageSpaceManager->gameISData.modAmount);
			ImGui::Text("%s", T("feature.post_processing.mod_data", "Mod Data:"));
			ImGui::Text(T("feature.post_processing.mod_fade_values_detail", "Fade Amount: %.3f\nFade Color: (%.3f, %.3f, %.3f)\nBlur Radius: %.3f\nDouble Vision Strength: %.3f\n"),
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kFadeAmount],
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kFadeR],
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kFadeG],
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kFadeB],
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kBlurRadius],
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kDoubleVisionStrength]);
			ImGui::Text(T("feature.post_processing.radial_blur_values_detail", "Radial Blur Strength: %.3f\nRadial Blur Rampup: %.3f\nRadial Blur Start: %.3f\nRadial Blur Rampdown: %.3f\nRadial Blur Down Start: %.3f\nRadial Blur Center: (%.3f, %.3f)"),
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kRadialBlurStrength],
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kRadialBlurRampup],
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kRadialBlurStart],
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kRadialBlurRampdown],
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kRadialBlurDownStart],
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kRadialBlurCenterX],
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kRadialBlurCenterY]);
			ImGui::Text(T("feature.post_processing.mod_dof_values_detail", "DOF Strength: %.3f\nDOF Distance: %.3f\nDOF Range: %.3f\nDOF Mode: %d"),
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kDOFStrength],
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kDOFDistance],
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kDOFRange],
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kDOFMode]);
			ImGui::Text(T("feature.post_processing.motion_blur_strength", "Motion Blur Strength: %.3f"), imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kMotionBlurStrength]);
			ImGui::TreePop();
		}
		ImGui::TreePop();
	}
}

void PostProcessing::LoadSettings(json& o_json)
{
	pendingSettings = o_json;
}

void PostProcessing::ProcessSettings(json& o_json)
{
	logger::info("Loading post processing settings...");

	for (auto& feat : pipeline) {
		if (feat && o_json.contains(feat->GetType())) {
			if (!feat->IsAutoEnabled())
				feat->enabled = o_json.value(feat->GetType(), json::object()).value("enabled", true);
			json featSettings = o_json.value(feat->GetType(), json::object()).value("settings", json::object());
			feat->LoadSettings(featSettings);
			if (loaded)
				feat->SetupResources();
		}
	}

	if (o_json.contains("ppsettings"))
		settings = o_json["ppsettings"];
}

void PostProcessing::SaveSettings(json& o_json)
{
	if (!pendingSettings.empty()) {
		o_json = pendingSettings;
		return;
	}

	for (auto& pipe : pipeline) {
		if (pipe) {
			json featureSetting{};
			pipe->SaveSettings(featureSetting);
			o_json[pipe->GetType()] = {
				{ "enabled", pipe->enabled },
				{ "settings", featureSetting }
			};
		}
	}

	o_json["ppsettings"] = settings;
}

std::vector<std::string> PostProcessing::LoadPresets()
{
	std::vector<std::string> o_presets = {};

	try {
		std::filesystem::create_directories(ppPresetPath);
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("Error creating preset directory during Load ({}) : {}\n", ppPresetPath, e.what());
		return o_presets;
	}

	for (const auto& entry : std::filesystem::directory_iterator(ppPresetPath)) {
		if (entry.is_regular_file() && entry.path().extension() == ".json") {
			o_presets.push_back(entry.path().stem().string());
		}
	}

	return o_presets;
}

void PostProcessing::LoadPresetFrom(std::string a_name)
{
	json a_presets = {};

	// if the name has .json, remove it
	if (a_name.ends_with(".json"))
		a_name = a_name.substr(0, a_name.size() - 5);

	try {
		logger::info("Loading preset: {}", a_name);
		std::ifstream i{ std::format("{}\\{}.json", ppPresetPath, a_name) };
		i >> a_presets;
	} catch (const std::exception& e) {
		logger::warn("Failed to load preset: {}. Error: {}", a_name, e.what());
		return;
	}

	ProcessSettings(a_presets);
}

void PostProcessing::SavePresetTo(std::string a_name)
{
	// Check if the name is valid
	if (a_name.empty()) {
		logger::warn("Invalid preset name.");
		return;
	}

	json a_presets = {};
	SaveSettings(a_presets);
	a_presets["preset_name"] = a_name;

	try {
		std::filesystem::create_directories(ppPresetPath);
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("Error creating preset directory during Save ({}) : {}\n", ppPresetPath, e.what());
		return;
	}

	std::string presetPath = std::format("{}\\{}.json", ppPresetPath, a_name);
	std::ofstream o{ presetPath };
	if (!o.is_open() || !o.good()) {
		logger::warn("Failed to open preset file for writing: {}", presetPath);
		return;
	}

	try {
		o << std::setw(4) << a_presets;
		logger::info("Saving preset to {}", presetPath);
	} catch (const std::exception& e) {
		logger::warn("Failed to write preset to file: {}. Error: {}", presetPath, e.what());
	}
}

void PostProcessing::RestoreDefaultSettings()
{
	// If pipeline isn't initialized yet (called during early loading before SetupResources),
	// load default.json into pendingSettings for deferred application in SetupResources.
	// This ensures first-startup defaults match what "Restore Defaults" produces later.
	bool pipelineReady = pipeline[static_cast<size_t>(FeaturePipelineIndex::AutoExposure)] != nullptr;
	if (!pipelineReady) {
		try {
			std::ifstream i{ std::format("{}\\{}.json", ppPresetPath, "default") };
			json defaultPreset;
			i >> defaultPreset;
			pendingSettings = defaultPreset;
			logger::info("Pipeline not ready, loaded default preset into pending settings");
		} catch (const std::exception& e) {
			logger::info("No default preset available during early load, C++ defaults will be used. Error: {}", e.what());
			pendingSettings = {};
		}
		return;
	}

	try {
		LoadPresetFrom("default");
	} catch (const std::exception& e) {
		logger::warn("Failed to load default preset. Error: {}", e.what());
		settings = {};
		pipeline[static_cast<size_t>(FeaturePipelineIndex::AutoExposure)].get()->enabled = true;
		pipeline[static_cast<size_t>(FeaturePipelineIndex::ColorGrading)].get()->enabled = true;
		pipeline[static_cast<size_t>(FeaturePipelineIndex::LUT)].get()->enabled = false;

		pipeline[static_cast<size_t>(FeaturePipelineIndex::MotionBlur)].get()->enabled = false;
		pipeline[static_cast<size_t>(FeaturePipelineIndex::DoF)].get()->enabled = false;
		pipeline[static_cast<size_t>(FeaturePipelineIndex::CODBloom)].get()->enabled = true;
		pipeline[static_cast<size_t>(FeaturePipelineIndex::LensFlare)].get()->enabled = false;
		pipeline[static_cast<size_t>(FeaturePipelineIndex::Vignette)].get()->enabled = true;
		pipeline[static_cast<size_t>(FeaturePipelineIndex::Camera)].get()->enabled = false;

		for (auto& pipe : pipeline) {
			if (pipe) {
				pipe->RestoreDefaultSettings();
			}
		}
	}
}

void PostProcessing::ClearShaderCache()
{
	for (auto& pipe : pipeline) {
		if (pipe)
			pipe->ClearShaderCache();
	}
}

void PostProcessing::SetupResources()
{
	{
		auto renderer = globals::game::renderer;
		auto gameTexMain = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
		auto gameTexMainCopy = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_COPY];

		D3D11_TEXTURE2D_DESC texDesc;
		D3D11_TEXTURE2D_DESC texMainDesc;
		D3D11_TEXTURE2D_DESC texMainCopyDesc;
		gameTexMain.texture->GetDesc(&texMainDesc);
		gameTexMainCopy.texture->GetDesc(&texMainCopyDesc);
		texDesc = texMainDesc;

		texDesc.MipLevels = 1;
		texDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
		texDesc.MiscFlags = 0;

		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		texCopyMain = eastl::make_unique<Texture2D>(texDesc);
		texCopyMain->CreateRTV(rtvDesc);

		if (texMainCopyDesc.Format != texMainDesc.Format) {
			texDesc = texMainCopyDesc;
			rtvDesc.Format = texDesc.Format;
			texDesc.MipLevels = 1;
			texDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
			texDesc.MiscFlags = 0;

			texCopyMainCopy = eastl::make_unique<Texture2D>(texDesc);
			texCopyMainCopy->CreateRTV(rtvDesc);
		} else {
			texCopyMainCopy = nullptr;
		}
	}

	if (auto rawPtr = reinterpret_cast<ID3D11VertexShader*>(Util::CompileShader(L"Data\\Shaders\\PostProcessing\\fullscreen.hlsli", {}, "vs_5_0", "FullscreenTriangleVS")))
		fullscreenVS.attach(rawPtr);
	if (auto rawPtr = reinterpret_cast<ID3D11PixelShader*>(Util::CompileShader(L"Data\\Shaders\\PostProcessing\\copy.ps.hlsl", {}, "ps_5_0")))
		copyPS.attach(rawPtr);

	pipeline[static_cast<size_t>(FeaturePipelineIndex::LocalExposure)] = std::make_shared<LocalExposure>();
	pipeline[static_cast<size_t>(FeaturePipelineIndex::LocalExposure)].get()->enabled = false;
	pipeline[static_cast<size_t>(FeaturePipelineIndex::AutoExposure)] = std::make_shared<HistogramAutoExposure>();
	pipeline[static_cast<size_t>(FeaturePipelineIndex::AutoExposure)].get()->enabled = true;
	pipeline[static_cast<size_t>(FeaturePipelineIndex::ColorGrading)] = std::make_shared<ColorGrading>();
	pipeline[static_cast<size_t>(FeaturePipelineIndex::ColorGrading)].get()->enabled = true;
	pipeline[static_cast<size_t>(FeaturePipelineIndex::LUT)] = std::make_shared<LUT>();
	pipeline[static_cast<size_t>(FeaturePipelineIndex::LUT)].get()->enabled = false;

	pipeline[static_cast<size_t>(FeaturePipelineIndex::MotionBlur)] = std::make_shared<MotionBlur>();
	pipeline[static_cast<size_t>(FeaturePipelineIndex::MotionBlur)].get()->enabled = false;
	pipeline[static_cast<size_t>(FeaturePipelineIndex::DoF)] = std::make_shared<DoF>();
	pipeline[static_cast<size_t>(FeaturePipelineIndex::DoF)].get()->enabled = false;
	pipeline[static_cast<size_t>(FeaturePipelineIndex::PhysicalGlare)] = std::make_shared<PhysicalGlare>();
	pipeline[static_cast<size_t>(FeaturePipelineIndex::PhysicalGlare)].get()->enabled = false;
	pipeline[static_cast<size_t>(FeaturePipelineIndex::CODBloom)] = std::make_shared<CODBloom>();
	pipeline[static_cast<size_t>(FeaturePipelineIndex::CODBloom)].get()->enabled = true;
	pipeline[static_cast<size_t>(FeaturePipelineIndex::LensFlare)] = std::make_shared<LensFlare>();
	pipeline[static_cast<size_t>(FeaturePipelineIndex::LensFlare)].get()->enabled = false;
	pipeline[static_cast<size_t>(FeaturePipelineIndex::Composite)] = std::make_shared<Composite>();
	pipeline[static_cast<size_t>(FeaturePipelineIndex::Composite)].get()->enabled = true;
	pipeline[static_cast<size_t>(FeaturePipelineIndex::Vignette)] = std::make_shared<Vignette>();
	pipeline[static_cast<size_t>(FeaturePipelineIndex::Vignette)].get()->enabled = true;
	pipeline[static_cast<size_t>(FeaturePipelineIndex::Camera)] = std::make_shared<Camera>();
	pipeline[static_cast<size_t>(FeaturePipelineIndex::Camera)].get()->enabled = false;
	pipeline[static_cast<size_t>(FeaturePipelineIndex::Border)] = std::make_shared<Border>();
	pipeline[static_cast<size_t>(FeaturePipelineIndex::Border)].get()->enabled = false;

	for (auto& pipe : pipeline) {
		if (pipe) {
			pipe->owner = this;
			pipe->SetupResources();
		}
	}

	bokehResources.Setup();

	ProcessSettings(pendingSettings);
	pendingSettings = {};
}

void PostProcessing::Reset()
{
	// Cleared per frame rather than only at the end of PreProcess: when Effects11 owns the
	// tonemap (or the pipeline is bypassed) PreProcess never runs, and a stale flag would
	// make the next frame we do run read from the wrong buffer.
	isrefraction = false;

	for (auto& pipe : pipeline) {
		if (pipe)
			pipe->Reset();
	}
}

void PostProcessing::CopyToRenderTarget(
	RE::BSGraphics::RenderTargetData& targetRT,
	Texture2D* convertTex,
	ID3D11Texture2D* srcTex,
	ID3D11ShaderResourceView* srcSRV)
{
	// D3D11 rejects a copy whose source and destination are the same resource, which happens
	// whenever the pipeline left the image in the buffer we are writing back to.
	if (targetRT.texture == srcTex)
		return;

	auto context = globals::d3d::context;

	D3D11_TEXTURE2D_DESC srcDesc;
	srcTex->GetDesc(&srcDesc);

	D3D11_TEXTURE2D_DESC targetDesc;
	targetRT.texture->GetDesc(&targetDesc);

	if (srcDesc.Format == targetDesc.Format) {
		context->CopySubresourceRegion(targetRT.texture, 0, 0, 0, 0, srcTex, 0, nullptr);
		return;
	}

	if (!copyPS || !fullscreenVS || !convertTex || !convertTex->rtv || !convertTex->resource)
		return;

	{
		PostProcessingRaster::RasterPass pass(context);

		ID3D11ShaderResourceView* srv = srcSRV;
		context->PSSetShaderResources(0, 1, &srv);
		pass.SetTargets({ convertTex->rtv.get() }, (float)convertTex->desc.Width, (float)convertTex->desc.Height);
		pass.SetShaders(fullscreenVS.get(), copyPS.get());
		pass.Draw();

		srv = nullptr;
		context->PSSetShaderResources(0, 1, &srv);
	}

	context->CopySubresourceRegion(targetRT.texture, 0, 0, 0, 0, convertTex->resource.get(), 0, nullptr);
}

void PostProcessing::DrawFeature(PostProcessFeature& feature, PostProcessFeature::TextureInfo& lastTexColor)
{
	if (feature.WritesToMainTexture()) {
		feature.Draw(lastTexColor);
	} else {
		PostProcessFeature::TextureInfo inTex = lastTexColor;
		feature.Draw(inTex);
	}
}

void PostProcessing::DrawBeforeUpscaling()
{
	if (bypass || IsTonemapOwnedByEffects11())
		return;

	auto& upscaling = globals::features::upscaling;
	if (!upscaling.loaded)
		return;

	auto renderer = globals::game::renderer;
	auto state = globals::state;

	bool inMainLoadingMenu = state->IsMainOrLoadingMenuOpen();
	auto gameTexMain = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	PostProcessFeature::TextureInfo lastTexColor = { gameTexMain.texture, gameTexMain.SRV };

	state->BeginPerfEvent("[Post Processing] Pre-Upscale");

	// update auto-enabled features
	for (auto& pipe : pipeline) {
		if (pipe && pipe->IsAutoEnabled())
			pipe->UpdateAutoEnabled();
	}

	// go through each fx
	for (auto& pipe : pipeline) {
		if (pipe && pipe->enabled && !pipe->DrawAfterColorGrading() && !(inMainLoadingMenu && pipe->DisableInMainLoadingMenu()) && pipe->DrawBeforeUpscaling()) {
			DrawFeature(*pipe, lastTexColor);
		}
	}

	CopyToRenderTarget(gameTexMain, texCopyMain.get(), lastTexColor.tex, lastTexColor.srv);

	state->EndPerfEvent();
}

void PostProcessing::PreProcess(RE::RENDER_TARGET a_input)
{
	if (bypass)
		return;

	auto renderer = globals::game::renderer;

	auto& upscaling = globals::features::upscaling;

	// This runs before the HDR chain, so ISRefraction still has kMAIN_COPY bound as a render
	// target. D3D11 silently nulls any SRV of a resource that is also an output, which would
	// make the pipeline sample black instead of the scene.
	globals::d3d::context->OMSetRenderTargets(0, nullptr, nullptr);
	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);

	bool inMainLoadingMenu = globals::state->IsMainOrLoadingMenuOpen();

	auto& gameTexMainRT = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	auto& gameTexMainCopyRT = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_COPY];

	// The tonemap hook hands us the pass input directly, so no need to probe the bound RTV.
	// Refraction still routes through kMAIN_COPY without that being reflected in a_input.
	bool useMainCopy = isrefraction || a_input == RE::RENDER_TARGETS::kMAIN_COPY;

	auto gameTexMain = useMainCopy ? gameTexMainCopyRT : gameTexMainRT;
	PostProcessFeature::TextureInfo lastTexColor = { gameTexMain.texture, gameTexMain.SRV };
	auto gameTexMainAlt = useMainCopy ? gameTexMainRT : gameTexMainCopyRT;

	// update auto-enabled features
	for (auto& pipe : pipeline) {
		if (pipe && pipe->IsAutoEnabled())
			pipe->UpdateAutoEnabled();
	}

	// go through each fx
	for (auto& pipe : pipeline) {
		if (pipe && pipe->enabled && !pipe->DrawAfterColorGrading() && !(inMainLoadingMenu && pipe->DisableInMainLoadingMenu()) && (!pipe->DrawBeforeUpscaling() || !upscaling.loaded)) {
			DrawFeature(*pipe, lastTexColor);
		}
	}

	for (auto& pipe : pipeline) {
		if (pipe && pipe->enabled && pipe->DrawAfterColorGrading() && !(inMainLoadingMenu && pipe->DisableInMainLoadingMenu()) && (!pipe->DrawBeforeUpscaling() || !upscaling.loaded)) {
			DrawFeature(*pipe, lastTexColor);
		}
	}

	Texture2D* mainConvertTex = texCopyMain.get();
	Texture2D* mainCopyConvertTex = texCopyMainCopy ? texCopyMainCopy.get() : texCopyMain.get();

	CopyToRenderTarget(gameTexMain, useMainCopy ? mainCopyConvertTex : mainConvertTex, lastTexColor.tex, lastTexColor.srv);
	CopyToRenderTarget(gameTexMainAlt, useMainCopy ? mainConvertTex : mainCopyConvertTex, lastTexColor.tex, lastTexColor.srv);

	isrefraction = false;
}

void PostProcessing::ClearBorderMotionVectorsForFrameGen()
{
	// Effects11 owns the image, so no letterbox is drawn and zeroing its motion vectors
	// would hand frame generation a band of static pixels over live scene content.
	if (bypass || IsTonemapOwnedByEffects11())
		return;

	auto borderIdx = static_cast<size_t>(FeaturePipelineIndex::Border);
	auto& pipe = pipeline[borderIdx];
	if (pipe && pipe->enabled) {
		auto* border = static_cast<Border*>(pipe.get());
		border->ClearMotionVectorsForFrameGen();
	}
}

bool PostProcessing::WantsTonemapOwnership() const
{
	return !bypass && settings.DisableVanillaTonemapping != 0;
}

bool PostProcessing::IsTonemapOwnedByEffects11() const
{
	return globals::state->GetTonemapOwner() == State::TonemapOwner::kEffects11;
}

PostProcessing::Settings PostProcessing::GetCommonBufferData()
{
	Settings data = settings;

	// Effects11 outputs gamma-space SDR from its own tonemapper. Leaving this flag set would
	// make ISHDR take its passthrough branch and HDROutputCS treat the scene as linear and
	// already display-mapped, skipping AutoHDR and the BT.2020 conversion.
	if (globals::state->GetTonemapOwner() != State::TonemapOwner::kPostProcessing)
		data.DisableVanillaTonemapping = 0;

	return data;
}

void PostProcessing::Prepass()
{
	if (!pendingSettings.empty()) {
		logger::info("Processing pending post processing settings...");
		ProcessSettings(pendingSettings);
		pendingSettings = {};
	}

	// Update gameISData
	const auto ImageSpace = RE::ImageSpaceManager::GetSingleton();
	const auto& iSRuntimeData = ImageSpace->GetRuntimeData();
	imageSpaceManager->gameISData = iSRuntimeData.data;
	if (const auto& overrideBaseData = iSRuntimeData.overrideBaseData) {
		imageSpaceManager->gameISData.baseData = *overrideBaseData;
	} else {
		imageSpaceManager->gameISData.baseData = *iSRuntimeData.currentBaseData;
	}
}

void PostProcessing::PostPostLoad()
{
	logger::info("Hooking preprocess passes");
	stl::write_vfunc<0x2, BSImagespaceShaderRefraction_SetupTechnique>(RE::VTABLE_BSImagespaceShaderRefraction[0]);
}
