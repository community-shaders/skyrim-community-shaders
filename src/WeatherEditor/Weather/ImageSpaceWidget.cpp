#include "ImageSpaceWidget.h"

#include "../EditorWindow.h"
#include "../WeatherUtils.h"

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

void ImageSpaceWidget::DrawWidget()
{
	auto editorWindow = EditorWindow::GetSingleton();

	ImGui::SetNextWindowSizeConstraints(ImVec2(600, 0), ImVec2(FLT_MAX, FLT_MAX));
	if (!ImGui::Begin(GetEditorID().c_str(), &open, ImGuiWindowFlags_NoSavedSettings | kStickyHeaderFlags)) {
		ImGui::End();
		return;
	}
	WeatherUtils::SetCurrentWidget(this);
	// Draw header with search and Save/Load/Delete buttons
	DrawWidgetHeader("##ImageSpaceSearch", false, true);

	BeginScrollableContent("##ISScroll");
	{
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
			changed |= PropertyDrawer::DrawFloat("Sunlight Scale", settings.hdrSunlightScale, 0.0f, 50.0f, search);
			changed |= PropertyDrawer::DrawFloat("Sky Scale", settings.hdrSkyScale, 0.0f, 10.0f, search);

			PropertyDrawer::DrawSeparator();

			// Cinematic Settings
			changed |= PropertyDrawer::DrawFloat("Saturation", settings.cinematicSaturation, 0.0f, 2.0f, search);
			changed |= PropertyDrawer::DrawFloat("Brightness", settings.cinematicBrightness, 0.0f, 2.0f, search);
			changed |= PropertyDrawer::DrawFloat("Contrast", settings.cinematicContrast, 0.0f, 2.0f, search);

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
			changed |= PropertyDrawer::DrawFloat("DOF Strength", settings.dofStrength, 0.0f, 10.0f, search);
			changed |= PropertyDrawer::DrawFloat("DOF Distance", settings.dofDistance, 0.0f, 10000.0f, search, "%.1f");
			changed |= PropertyDrawer::DrawFloat("DOF Range", settings.dofRange, 0.0f, 10000.0f, search, "%.1f");

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
	if (!imageSpace) {
		logger::warn("ImageSpaceWidget {}: imageSpace is null, skipping LoadSettings", GetEditorID());
		return;
	}

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

void ImageSpaceWidget::LoadFromGameSettings()
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
	if (!imageSpace)
		return;

	auto& data = imageSpace->data;

	// Clamp/sanitize all fields; reject non-finite values by substituting the low bound
	auto safeClamp = [](float v, float lo, float hi) {
		return std::isfinite(v) ? std::clamp(v, lo, hi) : lo;
	};

	// HDR
	data.hdr.eyeAdaptSpeed = safeClamp(settings.hdrEyeAdaptSpeed, 0.0f, 10.0f);
	data.hdr.bloomBlurRadius = safeClamp(settings.hdrBloomBlurRadius, 0.0f, 10.0f);
	data.hdr.bloomThreshold = safeClamp(settings.hdrBloomThreshold, 0.0f, 10.0f);
	data.hdr.bloomScale = safeClamp(settings.hdrBloomScale, 0.0f, 10.0f);
	data.hdr.white = safeClamp(settings.hdrWhite, 0.0f, 10.0f);
	data.hdr.sunlightScale = safeClamp(settings.hdrSunlightScale, 0.0f, 50.0f);
	data.hdr.skyScale = safeClamp(settings.hdrSkyScale, 0.0f, 10.0f);

	// Cinematic
	data.cinematic.saturation = safeClamp(settings.cinematicSaturation, 0.0f, 2.0f);
	data.cinematic.brightness = safeClamp(settings.cinematicBrightness, 0.0f, 2.0f);
	data.cinematic.contrast = safeClamp(settings.cinematicContrast, 0.0f, 2.0f);

	// Tint
	data.tint.color.red = safeClamp(settings.tintColor.x, 0.0f, 1.0f);
	data.tint.color.green = safeClamp(settings.tintColor.y, 0.0f, 1.0f);
	data.tint.color.blue = safeClamp(settings.tintColor.z, 0.0f, 1.0f);
	data.tint.amount = safeClamp(settings.tintAmount, 0.0f, 1.0f);

	// Depth of Field
	data.depthOfField.strength = safeClamp(settings.dofStrength, 0.0f, 10.0f);
	data.depthOfField.distance = safeClamp(settings.dofDistance, 0.0f, 10000.0f);
	data.depthOfField.range = safeClamp(settings.dofRange, 0.0f, 10000.0f);
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
