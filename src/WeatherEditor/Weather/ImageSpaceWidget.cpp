#include "ImageSpaceWidget.h"

#include "../EditorWindow.h"
#include "../WeatherUtils.h"
#include "Util.h"

#include <filesystem>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ImageSpaceWidget::Settings,
	hdrEyeAdaptSpeed,
	hdrBloomBlurRadius,
	hdrBloomThreshold,
	hdrBloomScale,
	hdrSunlightScale,
	hdrSkyScale,
	cinematicSaturation,
	cinematicBrightness,
	cinematicContrast,
	tintColor,
	tintAmount,
	dofStrength,
	dofDistance,
	dofRange)

ImageSpaceWidget::~ImageSpaceWidget()
{
}

void ImageSpaceWidget::DrawWidget()
{
	if (!open)
		return;

	auto editorWindow = EditorWindow::GetSingleton();

	ImGui::SetNextWindowSize(ImVec2(600, 800), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(std::format("ImageSpace Editor - {}", GetEditorID()).c_str(), &open)) {
		ImGui::Text("Form ID: %s", GetFormID().c_str());
		ImGui::Text("Plugin: %s", GetFilename().c_str());
		ImGui::Separator();

		// Search bar (activatable with Ctrl+F)
		BeginWidgetSearchBar(searchBuffer, sizeof(searchBuffer), searchActive);

		// Draw all settings in a unified table
		if (ImGui::BeginTable("ImageSpaceSettings", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthFixed, 200.0f);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

			bool changed = false;

			// HDR Settings
			if (MatchesSearch("Eye Adapt Speed")) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Eye Adapt Speed");
				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1);
				if (ImGui::SliderFloat("##EyeAdaptSpeed", &settings.hdrEyeAdaptSpeed, 0.0f, 10.0f, "%.3f"))
					changed = true;
			}

			if (MatchesSearch("Bloom Blur Radius")) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Bloom Blur Radius");
				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1);
				if (ImGui::SliderFloat("##BloomBlurRadius", &settings.hdrBloomBlurRadius, 0.0f, 10.0f, "%.3f"))
					changed = true;
			}

			if (MatchesSearch("Bloom Threshold")) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Bloom Threshold");
				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1);
				if (ImGui::SliderFloat("##BloomThreshold", &settings.hdrBloomThreshold, 0.0f, 10.0f, "%.3f"))
					changed = true;
			}

			if (MatchesSearch("Bloom Scale")) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Bloom Scale");
				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1);
				if (ImGui::SliderFloat("##BloomScale", &settings.hdrBloomScale, 0.0f, 10.0f, "%.3f"))
					changed = true;
			}

			if (MatchesSearch("Sunlight Scale")) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Sunlight Scale");
				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1);
				if (ImGui::SliderFloat("##SunlightScale", &settings.hdrSunlightScale, 0.0f, 10.0f, "%.3f"))
					changed = true;
			}

			if (MatchesSearch("Sky Scale")) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Sky Scale");
				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1);
				if (ImGui::SliderFloat("##SkyScale", &settings.hdrSkyScale, 0.0f, 10.0f, "%.3f"))
					changed = true;
			}

			// Separator between sections
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Separator();
			ImGui::TableSetColumnIndex(1);
			ImGui::Separator();

			// Cinematic Settings
			if (MatchesSearch("Saturation")) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Saturation");
				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1);
				if (ImGui::SliderFloat("##Saturation", &settings.cinematicSaturation, 0.0f, 2.0f, "%.3f"))
					changed = true;
			}

			if (MatchesSearch("Brightness")) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Brightness");
				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1);
				if (ImGui::SliderFloat("##Brightness", &settings.cinematicBrightness, 0.0f, 2.0f, "%.3f"))
					changed = true;
			}

			if (MatchesSearch("Contrast")) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Contrast");
				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1);
				if (ImGui::SliderFloat("##Contrast", &settings.cinematicContrast, 0.0f, 2.0f, "%.3f"))
					changed = true;
			}

			// Separator
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Separator();
			ImGui::TableSetColumnIndex(1);
			ImGui::Separator();

			// Tint Settings
			if (MatchesSearch("Tint Color")) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Tint Color");
				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1);
				if (ImGui::ColorEdit3("##TintColor", &settings.tintColor.x))
					changed = true;
			}

			if (MatchesSearch("Tint Amount")) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Tint Amount");
				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1);
				if (ImGui::SliderFloat("##TintAmount", &settings.tintAmount, 0.0f, 1.0f, "%.3f"))
					changed = true;
			}

			// Separator
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Separator();
			ImGui::TableSetColumnIndex(1);
			ImGui::Separator();

			// Depth of Field
			if (MatchesSearch("DOF Strength")) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("DOF Strength");
				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1);
				if (ImGui::SliderFloat("##DOFStrength", &settings.dofStrength, 0.0f, 10.0f, "%.3f"))
					changed = true;
			}

			if (MatchesSearch("DOF Distance")) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("DOF Distance");
				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1);
				if (ImGui::SliderFloat("##DOFDistance", &settings.dofDistance, 0.0f, 10000.0f, "%.1f"))
					changed = true;
			}

			if (MatchesSearch("DOF Range")) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("DOF Range");
				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1);
				if (ImGui::SliderFloat("##DOFRange", &settings.dofRange, 0.0f, 10000.0f, "%.1f"))
					changed = true;
			}

			ImGui::EndTable();

			if (changed && editorWindow->settings.autoApplyChanges) {
				ApplyChanges();
			}
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// Action buttons
		ImGui::BeginDisabled(!editorWindow->settings.autoApplyChanges);
		if (ImGui::Button("Apply", ImVec2(-1, 0))) {
			ApplyChanges();
		}
		ImGui::EndDisabled();

		if (ImGui::Button("Revert", ImVec2(-1, 0))) {
			RevertChanges();
		}

		// Save/Load buttons (always visible)
		if (ImGui::Button("Save to File", ImVec2(-1, 0))) {
			Save();
		}

		if (ImGui::Button("Load from File", ImVec2(-1, 0))) {
			Load();
		}

		// Delete button with confirmation (only show if file exists)
		std::string filePath = std::format("{}\\{}\\{}.json", Util::PathHelpers::GetCommunityShaderPath().string(), GetFolderName(), GetEditorID());
		if (std::filesystem::exists(filePath)) {
			static bool showDeleteConfirm = false;
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.3f, 0.2f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.4f, 0.3f, 1.0f));
			if (ImGui::ImageButton("##DeleteImageSpace", globals::menu->uiIcons.deleteSettings.texture, ImVec2(32, 32))) {
				if (editorWindow->settings.suppressDeleteWarning) {
					Delete();
				} else {
					showDeleteConfirm = true;
				}
			}
			ImGui::PopStyleColor(2);
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Delete saved file and revert to defaults");
			}

			if (showDeleteConfirm) {
				ImGui::OpenPopup("Delete Confirmation##ImageSpace");
			}

			if (ImGui::BeginPopupModal("Delete Confirmation##ImageSpace", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::Text("Are you sure you want to delete the settings file?");
				ImGui::Text("This will reload default values from the game.");
				ImGui::Spacing();

				ImGui::Checkbox("Don't ask again", &editorWindow->settings.suppressDeleteWarning);

				ImGui::Spacing();
				if (ImGui::Button("Delete", ImVec2(120, 0))) {
					Delete();
					showDeleteConfirm = false;
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel", ImVec2(120, 0))) {
					showDeleteConfirm = false;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
		}
	}
	ImGui::End();
}

void ImageSpaceWidget::LoadSettings()
{
	try {
		if (!js.empty() && js.contains("Settings")) {
			auto settingsJson = js["Settings"];

			// Validate that we have actual data
			bool hasValidData = false;
			for (const auto& [key, value] : settingsJson.items()) {
				if (value.is_number() && value.get<float>() != 0.0f) {
					hasValidData = true;
					break;
				}
				if (value.is_array() && !value.empty()) {
					hasValidData = true;
					break;
				}
			}

			if (hasValidData) {
				settings = settingsJson;
				logger::info("ImageSpace settings loaded successfully for {}", GetEditorID());
			} else {
				logger::warn("ImageSpace settings contained only zero values for {}, loading from form", GetEditorID());
				LoadImageSpaceValues();
				EditorWindow::GetSingleton()->ShowNotification(
					std::format("Failed to load {} - using default values", GetEditorID()),
					ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
			}
		} else {
			LoadImageSpaceValues();
		}
	} catch (const std::exception& e) {
		logger::error("Failed to load ImageSpace settings for {}: {}", GetEditorID(), e.what());
		LoadImageSpaceValues();
		EditorWindow::GetSingleton()->ShowNotification(
			std::format("Error loading {} - using default values", GetEditorID()),
			ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
	}
}

void ImageSpaceWidget::SaveSettings()
{
	try {
		js["Settings"] = settings;
		logger::info("ImageSpace settings saved for {}", GetEditorID());
	} catch (const std::exception& e) {
		logger::error("Failed to save ImageSpace settings for {}: {}", GetEditorID(), e.what());
		EditorWindow::GetSingleton()->ShowNotification(
			std::format("Failed to save {}", GetEditorID()),
			ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
	}
}

void ImageSpaceWidget::SetImageSpaceValues()
{
	if (!imageSpace)
		return;

	auto& data = imageSpace->data;

	// HDR
	data.hdr.eyeAdaptSpeed = settings.hdrEyeAdaptSpeed;
	data.hdr.bloomBlurRadius = settings.hdrBloomBlurRadius;
	data.hdr.bloomThreshold = settings.hdrBloomThreshold;
	data.hdr.bloomScale = settings.hdrBloomScale;
	data.hdr.sunlightScale = settings.hdrSunlightScale;
	data.hdr.skyScale = settings.hdrSkyScale;

	// Cinematic
	data.cinematic.saturation = settings.cinematicSaturation;
	data.cinematic.brightness = settings.cinematicBrightness;
	data.cinematic.contrast = settings.cinematicContrast;

	// Tint
	data.tint.color.red = settings.tintColor.x;
	data.tint.color.green = settings.tintColor.y;
	data.tint.color.blue = settings.tintColor.z;
	data.tint.amount = settings.tintAmount;

	// Depth of Field
	data.depthOfField.strength = settings.dofStrength;
	data.depthOfField.distance = settings.dofDistance;
	data.depthOfField.range = settings.dofRange;
}

void ImageSpaceWidget::LoadImageSpaceValues()
{
	if (!imageSpace)
		return;

	auto& data = imageSpace->data;

	// HDR
	settings.hdrEyeAdaptSpeed = data.hdr.eyeAdaptSpeed;
	settings.hdrBloomBlurRadius = data.hdr.bloomBlurRadius;
	settings.hdrBloomThreshold = data.hdr.bloomThreshold;
	settings.hdrBloomScale = data.hdr.bloomScale;
	settings.hdrSunlightScale = data.hdr.sunlightScale;
	settings.hdrSkyScale = data.hdr.skyScale;

	// Cinematic
	settings.cinematicSaturation = data.cinematic.saturation;
	settings.cinematicBrightness = data.cinematic.brightness;
	settings.cinematicContrast = data.cinematic.contrast;

	// Tint
	settings.tintColor.x = data.tint.color.red;
	settings.tintColor.y = data.tint.color.green;
	settings.tintColor.z = data.tint.color.blue;
	settings.tintAmount = data.tint.amount;

	// Depth of Field
	settings.dofStrength = data.depthOfField.strength;
	settings.dofDistance = data.depthOfField.distance;
	settings.dofRange = data.depthOfField.range;
}

void ImageSpaceWidget::ApplyChanges()
{
	SetImageSpaceValues();
}

void ImageSpaceWidget::RevertChanges()
{
	LoadImageSpaceValues();
}
