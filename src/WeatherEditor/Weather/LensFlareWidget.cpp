#include "LensFlareWidget.h"
#include "../EditorWindow.h"
#include "../WeatherUtils.h"

namespace
{
	namespace LensFlareSetting
	{
		constexpr const char* kFadeDistRadiusScale = "Fade Dist Radius Scale";
		constexpr const char* kColorInfluence = "Color Influence";
	}
}

void LensFlareWidget::DrawWidget()
{
	if (BeginWidgetWindow()) {
		DrawWidgetHeader("##LensFlareSearch", true, true);
		DrawSearchDropdown();
	}
	BeginScrollableContent("##LFScroll");
	{
		bool changed = false;

		if (DrawIfMatchesSearch(LensFlareSetting::kFadeDistRadiusScale, [&](const char* label) {
			ImGui::SeparatorText("Fade Distance");
			return ImGui::SliderFloat(label, &settings.fadeDistRadiusScale, 0.0f, 1.0f);
		}))
			changed = true;
		if (DrawIfMatchesSearch(LensFlareSetting::kColorInfluence, [&](const char* label) {
			ImGui::SeparatorText("Color");
			return ImGui::SliderFloat(label, &settings.colorInfluence, 0.0f, 1.0f);
		}))
			changed = true;

		if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
			ApplyChanges();
		}
	}
	EndScrollableContent();
	ImGui::End();
}

void LensFlareWidget::LoadSettings()
{
	if (!lensFlare)
		return;

	if (!js.empty()) {
		settings = vanillaSettings;
		try {
			if (js.contains("fadeDistRadiusScale"))
				settings.fadeDistRadiusScale = js["fadeDistRadiusScale"];
			if (js.contains("colorInfluence"))
				settings.colorInfluence = js["colorInfluence"];
		} catch (const std::exception& e) {
			logger::error("LensFlare {}: Failed to load from JSON: {}", GetEditorID(), e.what());
			settings = vanillaSettings;
		}
	} else {
		settings = vanillaSettings;
	}
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
	js["fadeDistRadiusScale"] = settings.fadeDistRadiusScale;
	js["colorInfluence"] = settings.colorInfluence;
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

std::vector<Widget::SearchResult> LensFlareWidget::CollectSearchableSettings() const
{
	return {
		{ LensFlareSetting::kFadeDistRadiusScale, "", LensFlareSetting::kFadeDistRadiusScale },
		{ LensFlareSetting::kColorInfluence, "", LensFlareSetting::kColorInfluence },
	};
}
