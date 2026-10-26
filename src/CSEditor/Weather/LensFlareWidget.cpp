#include "LensFlareWidget.h"
#include "../EditorWindow.h"
#include "../WeatherUtils.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LensFlareSettings, fadeDistRadiusScale, colorInfluence)

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
	WeatherUtils::SetCurrentWidget(this);
	if (BeginWidgetWindow(true, true, false, nullptr, "##LensFlareSearch")) {
		DrawWidgetHeader("##LensFlareSearch", true, true);
		DrawSearchDropdown();
	}
	BeginScrollableContent("##LFScroll");
	{
		bool changed = false;
		auto drawSection = [&](const char* settingId, const char* sectionLabel, float& value) {
			DrawSearchSectionIfMatches(settingId, [&](const char* label) {
				ImGui::SeparatorText(sectionLabel);
				if (WeatherUtils::DrawSliderFloat(label, value, 0.0f, 1.0f))
					changed = true;
			});
		};

		drawSection(LensFlareSetting::kFadeDistRadiusScale, "Fade Distance", settings.fadeDistRadiusScale);
		drawSection(LensFlareSetting::kColorInfluence, "Color", settings.colorInfluence);

		AutoApply(changed);
	}
	EndScrollableContent();
	ImGui::End();
}

void LensFlareWidget::LoadSettings()
{
	if (lensFlare)
		LoadSavedSettings();
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
	SaveSettingsToJson();
}

void LensFlareWidget::ApplyChanges()
{
	if (!lensFlare)
		return;

	lensFlare->fadeDistRadiusScale = settings.fadeDistRadiusScale;
	lensFlare->colorInfluence = settings.colorInfluence;
}

std::vector<Widget::SearchResult> LensFlareWidget::CollectSearchableSettings() const
{
	return WeatherUtils::LabelSearchResults({ { "", { LensFlareSetting::kFadeDistRadiusScale, LensFlareSetting::kColorInfluence } } });
}
