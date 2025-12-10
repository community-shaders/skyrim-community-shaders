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
	WeatherUtils::SetCurrentWidget(this);
	auto editorWindow = EditorWindow::GetSingleton();

	ImGui::SetNextWindowSizeConstraints(ImVec2(600, 0), ImVec2(FLT_MAX, FLT_MAX));
	if (ImGui::Begin(GetEditorID().c_str(), &open, ImGuiWindowFlags_NoSavedSettings)) {
		// Draw header with search and Save/Load/Delete buttons
		DrawWidgetHeader("##ImageSpaceSearch", false, true);

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
				if (WeatherUtils::DrawColorEdit("Tint Color", settings.tintColor))
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
	}
	ImGui::End();
}

void ImageSpaceWidget::LoadSettings()
{
	try {
		if (!js.empty() && js.contains("Settings") && js["Settings"].is_object()) {
			settings = js["Settings"];
		} else {
			LoadImageSpaceValues();
		}
	} catch (const std::exception& e) {
		logger::error("Failed to load ImageSpace settings for {}: {}", GetEditorID(), e.what());
		LoadImageSpaceValues();
	}
}

void ImageSpaceWidget::SaveSettings()
{
	js["Settings"] = settings;
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
