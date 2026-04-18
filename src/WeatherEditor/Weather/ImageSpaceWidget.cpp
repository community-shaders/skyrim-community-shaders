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

	if (BeginWidgetWindow()) {
		DrawWidgetHeader("##ImageSpaceSearch", false, true);
		DrawSearchDropdown();
	}
	BeginScrollableContent("##ISScroll");
	{
		if (PropertyDrawer::BeginTable("ImageSpaceSettings")) {
			bool changed = false;
			const bool showHdr = MatchesSearch("Eye Adapt Speed") || MatchesSearch("Bloom Blur Radius") || MatchesSearch("Bloom Threshold") ||
			                     MatchesSearch("Bloom Scale") || MatchesSearch("White") || MatchesSearch("Sunlight Scale") || MatchesSearch("Sky Scale");
			const bool showCinematic = MatchesSearch("Saturation") || MatchesSearch("Brightness") || MatchesSearch("Contrast");
			const bool showTint = MatchesSearch("Tint Color") || MatchesSearch("Tint Amount");
			const bool showDOF = MatchesSearch("DOF Strength") || MatchesSearch("DOF Distance") || MatchesSearch("DOF Range");

			// HDR Settings
			if (MatchesSearch("Eye Adapt Speed"))
				changed |= PropertyDrawer::DrawFloat("Eye Adapt Speed", settings.hdrEyeAdaptSpeed, 0.0f, 100.0f);
			if (MatchesSearch("Bloom Blur Radius"))
				changed |= PropertyDrawer::DrawFloat("Bloom Blur Radius", settings.hdrBloomBlurRadius, 0.0f, 10.0f);
			if (MatchesSearch("Bloom Threshold"))
				changed |= PropertyDrawer::DrawFloat("Bloom Threshold", settings.hdrBloomThreshold, 0.0f, 10.0f);
			if (MatchesSearch("Bloom Scale"))
				changed |= PropertyDrawer::DrawFloat("Bloom Scale", settings.hdrBloomScale, 0.0f, 10.0f);
			if (MatchesSearch("White"))
				changed |= PropertyDrawer::DrawFloat("White", settings.hdrWhite, 0.0f, 10.0f);
			if (MatchesSearch("Sunlight Scale"))
				changed |= PropertyDrawer::DrawFloat("Sunlight Scale", settings.hdrSunlightScale, 0.0f, 50.0f);
			if (MatchesSearch("Sky Scale"))
				changed |= PropertyDrawer::DrawFloat("Sky Scale", settings.hdrSkyScale, 0.0f, 10.0f);

			if (showHdr && (showCinematic || showTint || showDOF))
				PropertyDrawer::DrawSeparator();

			// Cinematic Settings
			if (MatchesSearch("Saturation"))
				changed |= PropertyDrawer::DrawFloat("Saturation", settings.cinematicSaturation, 0.0f, 2.0f);
			if (MatchesSearch("Brightness"))
				changed |= PropertyDrawer::DrawFloat("Brightness", settings.cinematicBrightness, 0.0f, 2.0f);
			if (MatchesSearch("Contrast"))
				changed |= PropertyDrawer::DrawFloat("Contrast", settings.cinematicContrast, 0.0f, 2.0f);

			if (showCinematic && (showTint || showDOF))
				PropertyDrawer::DrawSeparator();

			// Tint Settings
			float3 tintColor{ settings.tintColor.x, settings.tintColor.y, settings.tintColor.z };
			if (MatchesSearch("Tint Color") && PropertyDrawer::DrawColor("Tint Color", tintColor)) {
				settings.tintColor = tintColor;
				changed = true;
			}
			if (MatchesSearch("Tint Amount"))
				changed |= PropertyDrawer::DrawFloat("Tint Amount", settings.tintAmount, 0.0f, 1.0f);

			if (showTint && showDOF)
				PropertyDrawer::DrawSeparator();

			// Depth of Field
			if (MatchesSearch("DOF Strength"))
				changed |= PropertyDrawer::DrawFloat("DOF Strength", settings.dofStrength, 0.0f, 10.0f);
			if (MatchesSearch("DOF Distance"))
				changed |= PropertyDrawer::DrawFloat("DOF Distance", settings.dofDistance, 0.0f, 50000.0f, "%.1f");
			if (MatchesSearch("DOF Range"))
				changed |= PropertyDrawer::DrawFloat("DOF Range", settings.dofRange, 0.0f, 50000.0f, "%.1f");

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

std::vector<Widget::SearchResult> ImageSpaceWidget::CollectSearchableSettings() const
{
	const std::vector<std::string> names = {
		"Eye Adapt Speed", "Bloom Blur Radius", "Bloom Threshold", "Bloom Scale",
		"White", "Sunlight Scale", "Sky Scale",
		"Saturation", "Brightness", "Contrast",
		"Tint Color", "Tint Amount",
		"DOF Strength", "DOF Distance", "DOF Range"
	};

	std::vector<SearchResult> results;
	results.reserve(names.size());
	for (const auto& name : names) {
		results.push_back({ name, "", name });
	}
	return results;
}
