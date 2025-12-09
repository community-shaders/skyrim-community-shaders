#include "LensFlareWidget.h"
#include "../EditorWindow.h"
#include "../WeatherUtils.h"

void LensFlareWidget::DrawWidget()
{
	ImGui::SetNextWindowSizeConstraints(ImVec2(600, 0), ImVec2(FLT_MAX, FLT_MAX));
	if (ImGui::Begin(GetEditorID().c_str(), &open, ImGuiWindowFlags_NoSavedSettings)) {
		DrawWidgetHeader("##LensFlareSearch", false, true);

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
	ImGui::End();
}

void LensFlareWidget::LoadSettings()
{
	if (!lensFlare)
		return;

	if (!js.empty()) {
		try {
			if (js.contains("fadeDistRadiusScale"))
				settings.fadeDistRadiusScale = js["fadeDistRadiusScale"];
			if (js.contains("colorInfluence"))
				settings.colorInfluence = js["colorInfluence"];
		} catch (const std::exception& e) {
			logger::error("LensFlare {}: Failed to load from JSON: {}", GetEditorID(), e.what());
		}
	} else {
		settings.fadeDistRadiusScale = lensFlare->fadeDistRadiusScale;
		settings.colorInfluence = lensFlare->colorInfluence;
	}
	originalSettings = settings;
}

void LensFlareWidget::SaveSettings()
{
	js["fadeDistRadiusScale"] = settings.fadeDistRadiusScale;
	js["colorInfluence"] = settings.colorInfluence;
}

void LensFlareWidget::ApplyChanges()
{
	if (!lensFlare)
		return;

	lensFlare->fadeDistRadiusScale = settings.fadeDistRadiusScale;
	lensFlare->colorInfluence = settings.colorInfluence;

	originalSettings = settings;
}

void LensFlareWidget::RevertChanges()
{
	settings = originalSettings;
}

bool LensFlareWidget::HasUnsavedChanges() const
{
	return settings.fadeDistRadiusScale != originalSettings.fadeDistRadiusScale ||
	       settings.colorInfluence != originalSettings.colorInfluence;
}
