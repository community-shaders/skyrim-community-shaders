#include "LensFlareWidget.h"
#include "../EditorWindow.h"
#include "../WeatherUtils.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LensFlareWidget::Settings,
	fadeDistRadiusScale,
	colorInfluence)

void LensFlareWidget::DrawWidget()
{
	ImGui::SetNextWindowSizeConstraints(ImVec2(600, 0), ImVec2(FLT_MAX, FLT_MAX));
	if (!ImGui::Begin(GetEditorID().c_str(), &open, ImGuiWindowFlags_NoSavedSettings | kStickyHeaderFlags)) {
		ImGui::End();
		return;
	}
	WeatherUtils::SetCurrentWidget(this);
	DrawWidgetHeader("##LensFlareSearch", true, true);

	BeginScrollableContent("##LFScroll");
	bool changed = false;

	ImGui::SeparatorText("Fade Distance");
	if (WeatherUtils::DrawSliderFloat("Fade Dist Radius Scale", settings.fadeDistRadiusScale, 0.0f, 10.0f))
		changed = true;

	ImGui::SeparatorText("Color");
	if (WeatherUtils::DrawSliderFloat("Color Influence", settings.colorInfluence, 0.0f, 1.0f))
		changed = true;

	if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
		ApplyChanges();
	}
	EndScrollableContent();
	ImGui::End();
}

void LensFlareWidget::LoadSettings()
{
	if (!lensFlare)
		return;

	settings = vanillaSettings;
	if (!js.empty()) {
		try {
			nlohmann::json merged = vanillaSettings;
			merged.merge_patch(js);
			settings = merged.get<Settings>();
		} catch (const std::exception& e) {
			logger::error("LensFlare {}: Failed to load from JSON: {}", GetEditorID(), e.what());
			settings = vanillaSettings;
		}
	}

	// Validate and clamp fields to safe ranges
	settings.fadeDistRadiusScale = std::clamp(settings.fadeDistRadiusScale, 0.0f, 10.0f);
	settings.colorInfluence = std::clamp(settings.colorInfluence, 0.0f, 1.0f);

	originalSettings = settings;
	ApplyChanges();
}

void LensFlareWidget::LoadFromGameSettings()
{
	if (!lensFlare)
		return;
	settings.fadeDistRadiusScale = lensFlare->fadeDistRadiusScale;
	settings.colorInfluence = lensFlare->colorInfluence;
}

void LensFlareWidget::SaveSettings()
{
	js = settings;
	originalSettings = settings;
}

void LensFlareWidget::ApplyChanges()
{
	if (!lensFlare)
		return;

	lensFlare->fadeDistRadiusScale = settings.fadeDistRadiusScale;
	lensFlare->colorInfluence = settings.colorInfluence;
}

void LensFlareWidget::RevertChanges()
{
	settings = vanillaSettings;
	ApplyChanges();
}

bool LensFlareWidget::HasUnsavedChanges() const
{
	return !(settings == originalSettings);
}
