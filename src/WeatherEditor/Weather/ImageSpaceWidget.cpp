#include "ImageSpaceWidget.h"

#include "../EditorWindow.h"
#include "../WeatherUtils.h"
#include "Util.h"

#include <filesystem>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ImageSpaceWidget::Settings,
	hdrEyeAdaptSpeed,
	hdrEyeAdaptStrength,
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

	if (BeginWidgetWindow()) {
		DrawWidgetHeader("##ImageSpaceSearch", false, true);
	}
	BeginScrollableContent("##ISScroll");
	{
		if (PropertyDrawer::BeginTable("ImageSpaceSettings")) {
			bool changed = false;
			const char* search = searchBuffer[0] ? searchBuffer : nullptr;

			// HDR Settings
			changed |= PropertyDrawer::DrawFloat("Eye Adapt Speed", settings.hdrEyeAdaptSpeed, 0.0f, 100.0f, search);
			changed |= PropertyDrawer::DrawFloat("Eye Adapt Strength", settings.hdrEyeAdaptStrength, 0.0f, 50.0f, search);
			changed |= PropertyDrawer::DrawFloat("Bloom Blur Radius", settings.hdrBloomBlurRadius, 0.0f, 10.0f, search);
			changed |= PropertyDrawer::DrawFloat("Bloom Threshold", settings.hdrBloomThreshold, 0.0f, 10.0f, search);
			changed |= PropertyDrawer::DrawFloat("Bloom Scale", settings.hdrBloomScale, 0.0f, 30.0f, search);
			changed |= PropertyDrawer::DrawFloat("White", settings.hdrWhite, 0.0f, 30.0f, search);
			changed |= PropertyDrawer::DrawFloat("Sunlight Scale", settings.hdrSunlightScale, 0.0f, 50.0f, search);
			changed |= PropertyDrawer::DrawFloat("Sky Scale", settings.hdrSkyScale, 0.0f, 30.0f, search);

			PropertyDrawer::DrawSeparator();

			// Cinematic Settings
			changed |= PropertyDrawer::DrawFloat("Saturation", settings.cinematicSaturation, 0.0f, 10.0f, search);
			changed |= PropertyDrawer::DrawFloat("Brightness", settings.cinematicBrightness, 0.0f, 10.0f, search);
			changed |= PropertyDrawer::DrawFloat("Contrast", settings.cinematicContrast, 0.0f, 10.0f, search);

			PropertyDrawer::DrawSeparator();

			// Tint Settings
			float3 tintColor{ settings.tintColor.x, settings.tintColor.y, settings.tintColor.z };
			if (PropertyDrawer::DrawColor("Tint Color", tintColor, search)) {
				settings.tintColor = tintColor;
				changed = true;
			}
			changed |= PropertyDrawer::DrawFloat("Tint Amount", settings.tintAmount, 0.0f, 1.0f, search);

			PropertyDrawer::DrawSeparator();

			// Depth of Field
			changed |= PropertyDrawer::DrawFloat("DOF Strength", settings.dofStrength, 0.0f, 1.0f, search);
			changed |= PropertyDrawer::DrawFloat("DOF Distance", settings.dofDistance, 0.0f, 50000.0f, search, "%.1f");
			changed |= PropertyDrawer::DrawFloat("DOF Range", settings.dofRange, 0.0f, 50000.0f, search, "%.1f");

			PropertyDrawer::EndTable();

			if (changed && editorWindow->settings.autoApplyChanges) {
				ApplyChanges();
			}
		}
	}
	EndScrollableContent();
	ImGui::End();
}

void ImageSpaceWidget::LoadSettings()
{
	try {
		if (!js.empty() && js.contains("Settings") && js["Settings"].is_object()) {
			settings = js["Settings"];
		} else {
			settings = vanillaSettings;
		}
	} catch (const std::exception& e) {
		logger::error("Failed to load ImageSpace settings for {}: {}", GetEditorID(), e.what());
		settings = vanillaSettings;
	}
	originalSettings = settings;
	ApplyChanges();
}

void ImageSpaceWidget::SaveSettings()
{
	js["Settings"] = settings;
	originalSettings = settings;
}

void ImageSpaceWidget::SetImageSpaceValues()
{
	if (!imageSpace)
		return;

	auto& data = imageSpace->data;

	// HDR
	data.hdr.eyeAdaptSpeed = settings.hdrEyeAdaptSpeed;
	data.hdr.eyeAdaptStrength = settings.hdrEyeAdaptStrength;
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
	settings.hdrEyeAdaptStrength = data.hdr.eyeAdaptStrength;
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

void ImageSpaceWidget::LoadFromGameSettings()
{
	LoadImageSpaceValues();
}

void ImageSpaceWidget::ApplyChanges()
{
	SetImageSpaceValues();
}

void ImageSpaceWidget::RevertChanges()
{
	settings = vanillaSettings;
	ApplyChanges();
}

bool ImageSpaceWidget::HasUnsavedChanges() const
{
	return !(settings == originalSettings);
}
