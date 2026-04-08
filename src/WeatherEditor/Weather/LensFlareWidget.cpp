#include "LensFlareWidget.h"
#include "../EditorWindow.h"
#include "../WeatherUtils.h"

void LensFlareWidget::DrawWidget()
{
	if (BeginWidgetWindow()) {
		DrawWidgetHeader("##LensFlareSearch", true, true);
	}
	BeginScrollableContent("##LFScroll");
	{
		bool changed = false;

		ImGui::SeparatorText("Fade Distance");
		if (ImGui::SliderFloat("Fade Dist Radius Scale", &settings.fadeDistRadiusScale, 0.0f, 10.0f))
			changed = true;

		ImGui::SeparatorText("Color");
		if (ImGui::SliderFloat("Color Influence", &settings.colorInfluence, 0.0f, 1.0f))
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
