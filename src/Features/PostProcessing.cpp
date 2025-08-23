#include "PostProcessing.h"

#include "IconsFontAwesome5.h"
#include "imgui_stdlib.h"

#include "State.h"
#include "Upscaling.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	PostProcessing::Settings,
	DisableVanillaTonemapping)

void PostProcessing::DrawSettings()
{
	// 0 for list of feats
	// 1 for feat settings
	// static int pageNum = 0;
	// static int featIdx = 0;
	static int pipelinePageNum = 0;
	static int pipelineFeatIdx = 0;
	static int presetIdx = -1;
	// const float _iconButtonSize = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().FramePadding.x;
	// const ImVec2 iconButtonSize{ _iconButtonSize, _iconButtonSize };

	ImGui::BeginGroup();
	std::string currentPreset = (presetIdx >= 0 && presetIdx < presets.size()) ? presets[presetIdx] : "Select a preset";

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
	if (ImGui::Button("Load")) {
		if (presetIdx >= 0 && presetIdx < presets.size()) {
			LoadPresetFrom(presets[presetIdx]);
		}
	}

	ImGui::EndGroup();
	ImGui::BeginGroup();
	static std::string newPresetName = "";
	ImGui::InputText("##NewPresetName", &newPresetName);

	ImGui::SameLine();
	if (ImGui::Button("Save")) {
		if (!newPresetName.empty())
			SavePresetTo(newPresetName);
	}

	ImGui::EndGroup();

	ImGui::Separator();
	ImGui::Checkbox("Bypass", &bypass);
	ImGui::SameLine();
	ImGui::Checkbox("Disable Vanilla Tonemapping", (bool*)&settings.DisableVanillaTonemapping);

	ImGui::Separator();

	if (pipelinePageNum == 0) {
		for (int i = 0; i < pipeline.size(); ++i) {
			auto& feat = pipeline[i];
			if (feat) {
				ImGui::PushID(feat->GetType().c_str());
				ImGui::Checkbox("##Enabled", &feat->enabled);
				ImGui::SameLine();
				if (ImGui::Button(ICON_FA_BARS)) {
					pipelineFeatIdx = i;
					pipelinePageNum = 1;
				}
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text("Edit settings for this feature.");
				ImGui::SameLine();
				ImGui::Text("%s", feat->GetType().c_str());
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text(feat->GetDesc().c_str());
				ImGui::PopID();
			}
		}
	} else if (pipelinePageNum == 1) {
		if (ImGui::Button(ICON_FA_ARROW_LEFT " Back to Pipeline")) {
			pipelinePageNum = 0;
		}
		ImGui::Separator();
		if (pipelineFeatIdx >= 0 && pipelineFeatIdx < pipeline.size()) {
			auto& feat = pipeline[pipelineFeatIdx];
			if (feat) {
				ImGui::PushID(feat->GetType().c_str());
				
				ImGui::SeparatorText(feat->GetType().c_str());
				ImGui::TextWrapped(feat->GetDesc().c_str());
				
				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();
				ImGui::Checkbox("Enabled", &feat->enabled);
				if (feat->enabled)
				{
					ImGui::Indent();
					feat->DrawSettings();
					ImGui::Unindent();
				} else {
					ImGui::TextDisabled("Enable the feature to see its settings.");
				}
				
				ImGui::PopID();
			} else {
				ImGui::TextDisabled("Selected feature is not valid.");
				pipelinePageNum = 0;
			}
		} else {
			ImGui::TextDisabled("Invalid feature selected. Returning to list.");
			pipelinePageNum = 0;
		}
	}

	ImGui::Separator();

	if (ImGui::TreeNode("Debug")) {
		ImGui::Text("In Interior: %s", imageSpaceManager->inInterior ? "Yes" : "No");
		ImGui::Text("Time of Day:");
		ImGui::Text("Dawn: %.2f\nSunrise: %.2f\nDay: %.2f\nSunset: %.2f\nDusk: %.2f\nNight: %.2f",
			imageSpaceManager->timeOfDay[0],
			imageSpaceManager->timeOfDay[1],
			imageSpaceManager->timeOfDay[2],
			imageSpaceManager->timeOfDay[3],
			imageSpaceManager->timeOfDay[4],
			imageSpaceManager->timeOfDay[5]);
		if (ImGui::TreeNode("Game ImageSpace Values")) {
			ImGui::Text("Base Amount: %.3f", imageSpaceManager->gameISData.baseAmount);
			ImGui::Text("Base Data:");
			ImGui::Text("Cinematic Values:");
			ImGui::Text("Saturation: %.3f\nBrightness: %.3f\nContrast: %.3f",
				imageSpaceManager->gameISData.baseData.cinematic.saturation,
				imageSpaceManager->gameISData.baseData.cinematic.brightness,
				imageSpaceManager->gameISData.baseData.cinematic.contrast);

			ImGui::Text("HDR Values:");
			ImGui::Text("Eye Adapt Speed: %.3f\nBloom Blur Radius: %.3f\nBloom Threshold: %.3f\nBloom Scale: %.3f\nReceive Bloom Threshold: %.3f\nWhite: %.3f\nSunlight Scale: %.3f\nSky Scale: %.3f\nEye Adapt Strength: %.3f",
				imageSpaceManager->gameISData.baseData.hdr.eyeAdaptSpeed,
				imageSpaceManager->gameISData.baseData.hdr.bloomBlurRadius,
				imageSpaceManager->gameISData.baseData.hdr.bloomThreshold,
				imageSpaceManager->gameISData.baseData.hdr.bloomScale,
				imageSpaceManager->gameISData.baseData.hdr.receiveBloomThreshold,
				imageSpaceManager->gameISData.baseData.hdr.white,
				imageSpaceManager->gameISData.baseData.hdr.sunlightScale,
				imageSpaceManager->gameISData.baseData.hdr.skyScale,
				imageSpaceManager->gameISData.baseData.hdr.eyeAdaptStrength);

			ImGui::Text("Tint Values:");
			ImGui::Text("Tint Amount: %.3f\nTint Color: (%.3f, %.3f, %.3f)",
				imageSpaceManager->gameISData.baseData.tint.amount,
				imageSpaceManager->gameISData.baseData.tint.color.red,
				imageSpaceManager->gameISData.baseData.tint.color.green,
				imageSpaceManager->gameISData.baseData.tint.color.blue);

			ImGui::Text("Depth of Field Values:");
			ImGui::Text("DOF Strength: %.3f\nDOF Distance: %.3f\nDOF Range: %.3f\nDOF Flags: %d\nDOF Sky Blur Radius: %d",
				imageSpaceManager->gameISData.baseData.depthOfField.strength,
				imageSpaceManager->gameISData.baseData.depthOfField.distance,
				imageSpaceManager->gameISData.baseData.depthOfField.range,
				imageSpaceManager->gameISData.baseData.depthOfField.flags,
				static_cast<int>(imageSpaceManager->gameISData.baseData.depthOfField.skyBlurRadius.get()));

			ImGui::Text("Mod Amount: %.3f", imageSpaceManager->gameISData.modAmount);
			ImGui::Text("Mod Data:");
			ImGui::Text("Fade Amount: %.3f\nFade Color: (%.3f, %.3f, %.3f)\nBlur Radius: %.3f\nDouble Vision Strength: %.3f\n",
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kFadeAmount],
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kFadeR],
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kFadeG],
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kFadeB],
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kBlurRadius],
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kDoubleVisionStrength]);
			ImGui::Text("Radial Blur Strength: %.3f\nRadial Blur Rampup: %.3f\nRadial Blur Start: %.3f\nRadial Blur Rampdown: %.3f\nRadial Blur Down Start: %.3f\nRadial Blur Center: (%.3f, %.3f)",
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kRadialBlurStrength],
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kRadialBlurRampup],
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kRadialBlurStart],
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kRadialBlurRampdown],
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kRadialBlurDownStart],
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kRadialBlurCenterX],
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kRadialBlurCenterY]);
			ImGui::Text("DOF Strength: %.3f\nDOF Distance: %.3f\nDOF Range: %.3f\nDOF Mode: %d",
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kDOFStrength],
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kDOFDistance],
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kDOFRange],
				imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kDOFMode]);
			ImGui::Text("Motion Blur Strength: %.3f", imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kMotionBlurStrength]);
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
	json a_presets = {};
	SaveSettings(a_presets);
	a_presets["preset_name"] = a_name;

	// Check if the name is valid
	if (a_name.empty()) {
		logger::warn("Invalid preset name.");
		return;
	}

	try {
		std::filesystem::create_directories(ppPresetPath);
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("Error creating preset directory during Save ({}) : {}\n", ppPresetPath, e.what());
		return;
	}

	std::string presetPath = std::format("{}\\{}.json", ppPresetPath, a_name);
	std::ofstream o{ presetPath };

	try {
		o << std::setw(4) << a_presets;
		logger::info("Saving preset to {}", presetPath);
	} catch (const std::exception& e) {
		logger::warn("Failed to write preset to file: {}. Error: {}", presetPath, e.what());
	}
}

void PostProcessing::RestoreDefaultSettings()
{	
	try {
		LoadSettings("default");
	} catch (const std::exception& e) {
		settings = {};
		pipeline[static_cast<size_t>(FeaturePipelineIndex::AutoExposure)].get()->enabled = true;
		pipeline[static_cast<size_t>(FeaturePipelineIndex::ColorGrading)].get()->enabled = true;
		pipeline[static_cast<size_t>(FeaturePipelineIndex::LUT)].get()->enabled = false;

		if (!REL::Module::IsVR()) {
			pipeline[static_cast<size_t>(FeaturePipelineIndex::MotionBlur)].get()->enabled = false;
			pipeline[static_cast<size_t>(FeaturePipelineIndex::DoF)].get()->enabled = false;
			pipeline[static_cast<size_t>(FeaturePipelineIndex::CODBloom)].get()->enabled = true;
			pipeline[static_cast<size_t>(FeaturePipelineIndex::LensFlare)].get()->enabled = false;
			pipeline[static_cast<size_t>(FeaturePipelineIndex::Vignette)].get()->enabled = true;
			pipeline[static_cast<size_t>(FeaturePipelineIndex::Camera)].get()->enabled = false;
		}

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
		auto gameTexMainCopy = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_COPY];

		D3D11_TEXTURE2D_DESC texDesc;
		gameTexMainCopy.texture->GetDesc(&texDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		texDesc.MipLevels = srvDesc.Texture2D.MipLevels = 1;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		texDesc.MiscFlags = 0;

		texCopy = eastl::make_unique<Texture2D>(texDesc);
		texCopy->CreateUAV(uavDesc);

		texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.Format = texDesc.Format;
		uavDesc.Format = texDesc.Format;

		texAfterTAA = eastl::make_unique<Texture2D>(texDesc);
		texAfterTAA->CreateSRV(srvDesc);
		texAfterTAA->CreateUAV(uavDesc);
	}

	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\PostProcessing\\copy.cs.hlsl", {}, "cs_5_0")))
		copyCS.attach(rawPtr);

	pipeline[static_cast<size_t>(FeaturePipelineIndex::AutoExposure)] = std::make_unique<HistogramAutoExposure>();
	pipeline[static_cast<size_t>(FeaturePipelineIndex::AutoExposure)].get()->enabled = true;
	pipeline[static_cast<size_t>(FeaturePipelineIndex::ColorGrading)] = std::make_unique<ColorGrading>();
	pipeline[static_cast<size_t>(FeaturePipelineIndex::ColorGrading)].get()->enabled = true;
	pipeline[static_cast<size_t>(FeaturePipelineIndex::LUT)] = std::make_unique<LUT>();
	pipeline[static_cast<size_t>(FeaturePipelineIndex::LUT)].get()->enabled = false;

	if (!REL::Module::IsVR()) {
		pipeline[static_cast<size_t>(FeaturePipelineIndex::MotionBlur)] = std::make_unique<MotionBlur>();
		pipeline[static_cast<size_t>(FeaturePipelineIndex::MotionBlur)].get()->enabled = false;
		pipeline[static_cast<size_t>(FeaturePipelineIndex::DoF)] = std::make_unique<DoF>();
		pipeline[static_cast<size_t>(FeaturePipelineIndex::DoF)].get()->enabled = false;
		pipeline[static_cast<size_t>(FeaturePipelineIndex::CODBloom)] = std::make_unique<CODBloom>();
		pipeline[static_cast<size_t>(FeaturePipelineIndex::CODBloom)].get()->enabled = true;
		pipeline[static_cast<size_t>(FeaturePipelineIndex::LensFlare)] = std::make_unique<LensFlare>();
		pipeline[static_cast<size_t>(FeaturePipelineIndex::LensFlare)].get()->enabled = false;
		pipeline[static_cast<size_t>(FeaturePipelineIndex::Vignette)] = std::make_unique<Vignette>();
		pipeline[static_cast<size_t>(FeaturePipelineIndex::Vignette)].get()->enabled = true;
		pipeline[static_cast<size_t>(FeaturePipelineIndex::Camera)] = std::make_unique<Camera>();
		pipeline[static_cast<size_t>(FeaturePipelineIndex::Camera)].get()->enabled = false;
	}

	for (auto& pipe : pipeline) {
		if (pipe) {
			pipe->SetupResources();
		}
	}

	ProcessSettings(pendingSettings);
	pendingSettings = {};
}

void PostProcessing::Reset()
{
	for (auto& pipe : pipeline) {
		if (pipe)
			pipe->Reset();
	}
}

// from doodlum
void PostProcessing::UpdateToD()
{
	if (bypass)
		return;

	auto sky = globals::game::sky;
	if (!sky)
		return;

	imageSpaceManager->inInterior = true;
	if (auto player = RE::PlayerCharacter::GetSingleton()) {
		if (auto parentCell = player->GetParentCell()) {
			imageSpaceManager->inInterior = parentCell->IsInteriorCell();
		}
	}

	float currentTime = sky->currentGameHour;

	float sunriseBegin = sky->GetSunriseBegin();
	float sunriseEnd = sky->GetSunriseEnd();
	float sunsetBegin = sky->GetSunsetBegin();
	float sunsetEnd = sky->GetSunsetEnd();

	float dawnMid = sunriseBegin + (sunriseEnd - sunriseBegin) * 0.5f;
	float duskMid = sunsetBegin + (sunsetEnd - sunsetBegin) * 0.5f;

	auto range01 = [](float t, float a, float b) {
		// Handles wrap-around if b < a
		float range = b - a;
		if (range < 0.0f)
			range += 24.0f;
		float value = t - a;
		if (value < 0.0f)
			value += 24.0f;
		return std::clamp(value / range, 0.0f, 1.0f);
	};

	for (int i = 0; i < 6; ++i) {
		imageSpaceManager->timeOfDay[i] = 0.0f;
	}

	// Dawn → Sunrise
	if (currentTime >= sunriseBegin && currentTime < dawnMid) {
		float f = range01(currentTime, sunriseBegin, dawnMid);
		imageSpaceManager->timeOfDay[0] = 1.0f - f;  // dawn
		imageSpaceManager->timeOfDay[1] = f;         // sunrise
	} else if (currentTime >= dawnMid && currentTime < sunriseEnd) {
		float f = range01(currentTime, dawnMid, sunriseEnd);
		imageSpaceManager->timeOfDay[1] = 1.0f - f;  // sunrise
		imageSpaceManager->timeOfDay[2] = f;         // day
	}
	// Day → Sunset
	else if (currentTime >= sunriseEnd && currentTime < sunsetBegin) {
		float f = range01(currentTime, sunriseEnd, sunsetBegin);
		imageSpaceManager->timeOfDay[2] = 1.0f - f;  // day
		imageSpaceManager->timeOfDay[3] = f;         // sunset
	}
	// Sunset → Dusk
	else if (currentTime >= sunsetBegin && currentTime < duskMid) {
		float f = range01(currentTime, sunsetBegin, duskMid);
		imageSpaceManager->timeOfDay[3] = 1.0f - f;  // sunset
		imageSpaceManager->timeOfDay[4] = f;         // dusk
	} else if (currentTime >= duskMid && currentTime < sunsetEnd) {
		float f = range01(currentTime, duskMid, sunsetEnd);
		imageSpaceManager->timeOfDay[4] = 1.0f - f;  // dusk
		imageSpaceManager->timeOfDay[5] = f;         // night
	}
	// Night → Dawn (wrap)
	else {
		float f = range01(currentTime, sunsetEnd, sunriseBegin);
		imageSpaceManager->timeOfDay[5] = 1.0f - f;  // night
		imageSpaceManager->timeOfDay[0] = f;         // dawn
	}
}

void PostProcessing::PreProcess()
{
	if (bypass)
		return;

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	bool inMainLoadingMenu = globals::game::ui && (globals::game::ui->IsMenuOpen(RE::MainMenu::MENU_NAME) || globals::game::ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME));

	auto gameTexMain = isrefraction ? renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_COPY] : renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	PostProcessFeature::TextureInfo lastTexColor = { gameTexMain.texture, gameTexMain.SRV };
	auto gameTexMainAlt = isrefraction ? renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN] : renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_COPY];

	// go through each fx
	for (auto& pipe : pipeline) {
		if (pipe && pipe->enabled && !pipe->DrawAfterColorGrading() && !(inMainLoadingMenu && pipe->DisableInMainLoadingMenu())) {
			pipe->Draw(lastTexColor);
		}
	}

	for (auto& pipe : pipeline) {
		if (pipe && pipe->enabled && pipe->DrawAfterColorGrading() && !(inMainLoadingMenu && pipe->DisableInMainLoadingMenu())) {
			pipe->Draw(lastTexColor);
		}
	}

	D3D11_TEXTURE2D_DESC desc;
	lastTexColor.tex->GetDesc(&desc);
	if (desc.Format == texCopy->desc.Format) {
		// either MAIN_COPY or MAIN is used as input for HDR pass
		// so we copy to both so whatever the game wants we're not failing it
		context->CopySubresourceRegion(gameTexMain.texture, 0, 0, 0, 0, lastTexColor.tex, 0, nullptr);
		context->CopySubresourceRegion(gameTexMainAlt.texture, 0, 0, 0, 0, lastTexColor.tex, 0, nullptr);
	} else {
		ID3D11ShaderResourceView* srv = lastTexColor.srv;
		ID3D11UnorderedAccessView* uav = texCopy->uav.get();

		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShaderResources(0, 1, &srv);
		context->CSSetShader(copyCS.get(), nullptr, 0);
		context->Dispatch((texCopy->desc.Width + 7) >> 3, (texCopy->desc.Height + 7) >> 3, 1);

		srv = nullptr;
		uav = nullptr;

		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShaderResources(0, 1, &srv);
		context->CSSetShader(nullptr, nullptr, 0);

		context->CopySubresourceRegion(gameTexMain.texture, 0, 0, 0, 0, texCopy->resource.get(), 0, nullptr);
		context->CopySubresourceRegion(gameTexMainAlt.texture, 0, 0, 0, 0, texCopy->resource.get(), 0, nullptr);
	}

	isrefraction = false;
}

void PostProcessing::Prepass()
{
	if (!pendingSettings.empty()) {
		logger::info("Processing pending post processing settings...");
		ProcessSettings(pendingSettings);
		pendingSettings = {};
	}

	UpdateToD();

	// Update gameISData
	const auto ImageSpace = RE::ImageSpaceManager::GetSingleton();
	if (globals::game::isVR) {
		const auto& iSRuntimeData = ImageSpace->GetVRRuntimeData();
		imageSpaceManager->gameISData = iSRuntimeData.data;
		if (const auto& overrideBaseData = iSRuntimeData.overrideBaseData) {
			imageSpaceManager->gameISData.baseData = *overrideBaseData;
		} else {
			imageSpaceManager->gameISData.baseData = *iSRuntimeData.currentBaseData;
		}
	} else {
		const auto& iSRuntimeData = ImageSpace->GetRuntimeData();
		imageSpaceManager->gameISData = iSRuntimeData.data;
		if (const auto& overrideBaseData = iSRuntimeData.overrideBaseData) {
			imageSpaceManager->gameISData.baseData = *overrideBaseData;
		} else {
			imageSpaceManager->gameISData.baseData = *iSRuntimeData.currentBaseData;
		}
	}
}

void PostProcessing::PostPostLoad()
{
	logger::info("Hooking preprocess passes");
	stl::write_vfunc<0x2, BSImagespaceShaderRefraction_SetupTechnique>(RE::VTABLE_BSImagespaceShaderRefraction[0]);
	stl::write_vfunc<0x2, BSImagespaceShaderHDRTonemapBlendCinematic_SetupTechnique>(RE::VTABLE_BSImagespaceShaderHDRTonemapBlendCinematic[0]);
	stl::write_vfunc<0x2, BSImagespaceShaderHDRTonemapBlendCinematicFade_SetupTechnique>(RE::VTABLE_BSImagespaceShaderHDRTonemapBlendCinematicFade[0]);
}