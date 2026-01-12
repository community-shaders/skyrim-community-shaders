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
	hdrWhite,
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
		if (PropertyDrawer::BeginTable("ImageSpaceSettings", 200.0f)) {
			bool changed = false;
			const char* search = searchBuffer[0] ? searchBuffer : nullptr;

			// HDR Settings
			changed |= PropertyDrawer::DrawFloat("Eye Adapt Speed", settings.hdrEyeAdaptSpeed, 0.0f, 10.0f, search);
			changed |= PropertyDrawer::DrawFloat("Bloom Blur Radius", settings.hdrBloomBlurRadius, 0.0f, 10.0f, search);
			changed |= PropertyDrawer::DrawFloat("Bloom Threshold", settings.hdrBloomThreshold, 0.0f, 10.0f, search);
			changed |= PropertyDrawer::DrawFloat("Bloom Scale", settings.hdrBloomScale, 0.0f, 10.0f, search);
			changed |= PropertyDrawer::DrawFloat("White", settings.hdrWhite, 0.0f, 10.0f, search);
			changed |= PropertyDrawer::DrawFloat("Sunlight Scale", settings.hdrSunlightScale, 0.0f, 10.0f, search);
			changed |= PropertyDrawer::DrawFloat("Sky Scale", settings.hdrSkyScale, 0.0f, 10.0f, search);

			PropertyDrawer::DrawSeparator();

			// Cinematic Settings
			changed |= PropertyDrawer::DrawFloat("Saturation", settings.cinematicSaturation, 0.0f, 2.0f, search);
			changed |= PropertyDrawer::DrawFloat("Brightness", settings.cinematicBrightness, 0.0f, 2.0f, search);
			changed |= PropertyDrawer::DrawFloat("Contrast", settings.cinematicContrast, 0.0f, 2.0f, search);

			PropertyDrawer::DrawSeparator();

			// Tint Settings
			changed |= PropertyDrawer::DrawColor("Tint Color", settings.tintColor, search);
			changed |= PropertyDrawer::DrawFloat("Tint Amount", settings.tintAmount, 0.0f, 1.0f, search);

			PropertyDrawer::DrawSeparator();

			// Depth of Field
			changed |= PropertyDrawer::DrawFloat("DOF Strength", settings.dofStrength, 0.0f, 10.0f, search);
			changed |= PropertyDrawer::DrawFloat("DOF Distance", settings.dofDistance, 0.0f, 10000.0f, search, "%.1f");
			changed |= PropertyDrawer::DrawFloat("DOF Range", settings.dofRange, 0.0f, 10000.0f, search, "%.1f");

			PropertyDrawer::EndTable();

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
	data.hdr.white = settings.hdrWhite;
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
	settings.hdrWhite = data.hdr.white;
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
