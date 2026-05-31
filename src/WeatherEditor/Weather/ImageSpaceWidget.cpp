#include "ImageSpaceWidget.h"

#include "../EditorWindow.h"
#include "../WeatherUtils.h"
#include "Util.h"

#include <algorithm>
#include <array>
#include <filesystem>

namespace
{
	enum class ImageSpaceSection
	{
		Hdr,
		Cinematic,
		Tint,
		DepthOfField
	};

	struct ImageSpaceSettingEntry
	{
		const char* label;
		ImageSpaceSection section;
	};

	namespace ImageSpaceSetting
	{
		constexpr const char* kEyeAdaptSpeed = "Eye Adapt Speed";
		constexpr const char* kEyeAdaptStrength = "Eye Adapt Strength";
		constexpr const char* kBloomBlurRadius = "Bloom Blur Radius";
		constexpr const char* kBloomThreshold = "Bloom Threshold";
		constexpr const char* kBloomScale = "Bloom Scale";
		constexpr const char* kWhite = "White";
		constexpr const char* kSunlightScale = "Sunlight Scale";
		constexpr const char* kSkyScale = "Sky Scale";
		constexpr const char* kSaturation = "Saturation";
		constexpr const char* kBrightness = "Brightness";
		constexpr const char* kContrast = "Contrast";
		constexpr const char* kTintColor = "Tint Color";
		constexpr const char* kTintAmount = "Tint Amount";
		constexpr const char* kDofStrength = "DOF Strength";
		constexpr const char* kDofDistance = "DOF Distance";
		constexpr const char* kDofRange = "DOF Range";
	}

	constexpr std::array<ImageSpaceSettingEntry, 16> kImageSpaceSettings = { {
		{ ImageSpaceSetting::kEyeAdaptSpeed, ImageSpaceSection::Hdr },
		{ ImageSpaceSetting::kEyeAdaptStrength, ImageSpaceSection::Hdr },
		{ ImageSpaceSetting::kBloomBlurRadius, ImageSpaceSection::Hdr },
		{ ImageSpaceSetting::kBloomThreshold, ImageSpaceSection::Hdr },
		{ ImageSpaceSetting::kBloomScale, ImageSpaceSection::Hdr },
		{ ImageSpaceSetting::kWhite, ImageSpaceSection::Hdr },
		{ ImageSpaceSetting::kSunlightScale, ImageSpaceSection::Hdr },
		{ ImageSpaceSetting::kSkyScale, ImageSpaceSection::Hdr },
		{ ImageSpaceSetting::kSaturation, ImageSpaceSection::Cinematic },
		{ ImageSpaceSetting::kBrightness, ImageSpaceSection::Cinematic },
		{ ImageSpaceSetting::kContrast, ImageSpaceSection::Cinematic },
		{ ImageSpaceSetting::kTintColor, ImageSpaceSection::Tint },
		{ ImageSpaceSetting::kTintAmount, ImageSpaceSection::Tint },
		{ ImageSpaceSetting::kDofStrength, ImageSpaceSection::DepthOfField },
		{ ImageSpaceSetting::kDofDistance, ImageSpaceSection::DepthOfField },
		{ ImageSpaceSetting::kDofRange, ImageSpaceSection::DepthOfField },
	} };
}

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
		DrawSearchDropdown();
	}
	BeginScrollableContent("##ISScroll");
	{
		if (PropertyDrawer::BeginTable("ImageSpaceSettings")) {
			bool changed = false;
			auto sectionMatches = [&](ImageSpaceSection section) {
				return std::any_of(kImageSpaceSettings.begin(), kImageSpaceSettings.end(), [&](const ImageSpaceSettingEntry& setting) {
					return setting.section == section && MatchesSearch(setting.label);
				});
			};
			const bool showHdr = sectionMatches(ImageSpaceSection::Hdr);
			const bool showCinematic = sectionMatches(ImageSpaceSection::Cinematic);
			const bool showTint = sectionMatches(ImageSpaceSection::Tint);
			const bool showDOF = sectionMatches(ImageSpaceSection::DepthOfField);

			// HDR Settings
			changed |= PropertyDrawer::DrawFloat(ImageSpaceSetting::kEyeAdaptSpeed, settings.hdrEyeAdaptSpeed, 0.0f, 100.0f);
			changed |= PropertyDrawer::DrawFloat(ImageSpaceSetting::kEyeAdaptStrength, settings.hdrEyeAdaptStrength, 0.0f, 50.0f);
			changed |= PropertyDrawer::DrawFloat(ImageSpaceSetting::kBloomBlurRadius, settings.hdrBloomBlurRadius, 0.0f, 10.0f);
			changed |= PropertyDrawer::DrawFloat(ImageSpaceSetting::kBloomThreshold, settings.hdrBloomThreshold, 0.0f, 10.0f);
			changed |= PropertyDrawer::DrawFloat(ImageSpaceSetting::kBloomScale, settings.hdrBloomScale, 0.0f, 30.0f);
			changed |= PropertyDrawer::DrawFloat(ImageSpaceSetting::kWhite, settings.hdrWhite, 0.0f, 30.0f);
			changed |= PropertyDrawer::DrawFloat(ImageSpaceSetting::kSunlightScale, settings.hdrSunlightScale, 0.0f, 50.0f);
			changed |= PropertyDrawer::DrawFloat(ImageSpaceSetting::kSkyScale, settings.hdrSkyScale, 0.0f, 30.0f);

			if (showHdr && (showCinematic || showTint || showDOF))
				PropertyDrawer::DrawSeparator();

			// Cinematic Settings
			changed |= PropertyDrawer::DrawFloat(ImageSpaceSetting::kSaturation, settings.cinematicSaturation, 0.0f, 10.0f);
			changed |= PropertyDrawer::DrawFloat(ImageSpaceSetting::kBrightness, settings.cinematicBrightness, 0.0f, 10.0f);
			changed |= PropertyDrawer::DrawFloat(ImageSpaceSetting::kContrast, settings.cinematicContrast, 0.0f, 10.0f);

			if (showCinematic && (showTint || showDOF))
				PropertyDrawer::DrawSeparator();

			// Tint Settings
			float3 tintColor{ settings.tintColor.x, settings.tintColor.y, settings.tintColor.z };
			if (DrawIfMatchesSearch(ImageSpaceSetting::kTintColor, [&](const char* label) { return PropertyDrawer::DrawColor(label, tintColor); })) {
				settings.tintColor = tintColor;
				changed = true;
			}
			changed |= PropertyDrawer::DrawFloat(ImageSpaceSetting::kTintAmount, settings.tintAmount, 0.0f, 1.0f);

			if (showTint && showDOF)
				PropertyDrawer::DrawSeparator();

			// Depth of Field
			changed |= PropertyDrawer::DrawFloat(ImageSpaceSetting::kDofStrength, settings.dofStrength, 0.0f, 1.0f);
			changed |= PropertyDrawer::DrawFloat(ImageSpaceSetting::kDofDistance, settings.dofDistance, 0.0f, 50000.0f, "%.1f");
			changed |= PropertyDrawer::DrawFloat(ImageSpaceSetting::kDofRange, settings.dofRange, 0.0f, 50000.0f, "%.1f");

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

std::vector<Widget::SearchResult> ImageSpaceWidget::CollectSearchableSettings() const
{
	std::vector<SearchResult> results;
	results.reserve(kImageSpaceSettings.size());
	for (const auto& setting : kImageSpaceSettings) {
		results.push_back({ setting.label, "", setting.label });
	}
	return results;
}
