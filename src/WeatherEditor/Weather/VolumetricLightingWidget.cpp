#include "VolumetricLightingWidget.h"
#include "../EditorWindow.h"
#include "../WeatherUtils.h"

void VolumetricLightingWidget::DrawWidget()
{
	WeatherUtils::SetCurrentWidget(this);
	if (BeginWidgetWindow()) {
		DrawWidgetHeader("##VolumetricLightingSearch", true, true);
		DrawSearchDropdown();
	}
	bool changed = false;

	if (ImGui::BeginTabBar("VolumetricLightingTabs")) {
		const ImGuiTabItemFlags basicFlags = GetTabFlagsForOverride("Basic");
		const ImGuiTabItemFlags densityFlags = GetTabFlagsForOverride("Density");
		const ImGuiTabItemFlags advancedFlags = GetTabFlagsForOverride("Advanced");

		if (ImGui::BeginTabItem("Basic", nullptr, basicFlags)) {
			BeginScrollableContent("##BasicScroll");
			if (MatchesSearch("Intensity")) {
				ImGui::SeparatorText("Intensity");
				if (WeatherUtils::DrawSliderFloat("Intensity", settings.intensity, 0.0f, 50.0f))
					changed = true;
			}
			if (MatchesSearch("Contribution")) {
				ImGui::SeparatorText("Custom Color");
				if (WeatherUtils::DrawSliderFloat("Contribution", settings.customColorContribution, 0.0f, 1.0f))
					changed = true;
			}
			if (MatchesSearch("Color")) {
				ImGui::SeparatorText("RGB Color");
				float3 rgbColor{ settings.red, settings.green, settings.blue };
				if (WeatherUtils::DrawColorEdit("Color", rgbColor)) {
					settings.red = rgbColor.x;
					settings.green = rgbColor.y;
					settings.blue = rgbColor.z;
					changed = true;
				}
			}
			EndScrollableContent();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Density", nullptr, densityFlags)) {
			BeginScrollableContent("##DensityScroll");
			ImGui::SeparatorText("Density Settings");
			if (MatchesSearch("Contribution")) {
				if (WeatherUtils::DrawSliderFloat("Contribution", settings.densityContribution, 0.0f, 1.0f))
					changed = true;
			}
			if (MatchesSearch("Size")) {
				if (WeatherUtils::DrawSliderFloat("Size", settings.densitySize, 0.1f, 10000.0f))
					changed = true;
			}
			if (MatchesSearch("Wind Speed")) {
				if (WeatherUtils::DrawSliderFloat("Wind Speed", settings.densityWindSpeed, 0.0f, 100.0f))
					changed = true;
			}
			if (MatchesSearch("Falling Speed")) {
				if (WeatherUtils::DrawSliderFloat("Falling Speed", settings.densityFallingSpeed, 0.0f, 100.0f))
					changed = true;
			}
			EndScrollableContent();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Advanced", nullptr, advancedFlags)) {
			BeginScrollableContent("##AdvancedScroll");
			ImGui::SeparatorText("Phase Function");
			if (MatchesSearch("Contribution")) {
				if (WeatherUtils::DrawSliderFloat("Contribution", settings.phaseFunctionContribution, 0.0f, 1.0f))
					changed = true;
			}
			if (MatchesSearch("Scattering")) {
				if (WeatherUtils::DrawSliderFloat("Scattering", settings.phaseFunctionScattering, 0.0f, 1.0f))
					changed = true;
			}
			if (MatchesSearch("Range Factor")) {
				ImGui::SeparatorText("Sampling");
				if (WeatherUtils::DrawSliderFloat("Range Factor", settings.samplingRangeFactor, 0.0f, 160.0f))
					changed = true;
			}
			EndScrollableContent();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
		ApplyChanges();
	}
	ImGui::End();
}

void VolumetricLightingWidget::LoadSettings()
{
	if (!volumetricLighting)
		return;

	if (!js.empty()) {
		settings = vanillaSettings;
		try {
			if (js.contains("intensity"))
				settings.intensity = js["intensity"];
			if (js.contains("customColorContribution"))
				settings.customColorContribution = js["customColorContribution"];
			if (js.contains("red"))
				settings.red = js["red"];
			if (js.contains("green"))
				settings.green = js["green"];
			if (js.contains("blue"))
				settings.blue = js["blue"];
			if (js.contains("densityContribution"))
				settings.densityContribution = js["densityContribution"];
			if (js.contains("densitySize"))
				settings.densitySize = js["densitySize"];
			if (js.contains("densityWindSpeed"))
				settings.densityWindSpeed = js["densityWindSpeed"];
			if (js.contains("densityFallingSpeed"))
				settings.densityFallingSpeed = js["densityFallingSpeed"];
			if (js.contains("phaseFunctionContribution"))
				settings.phaseFunctionContribution = js["phaseFunctionContribution"];
			if (js.contains("phaseFunctionScattering"))
				settings.phaseFunctionScattering = js["phaseFunctionScattering"];
			if (js.contains("samplingRangeFactor"))
				settings.samplingRangeFactor = js["samplingRangeFactor"];
		} catch (const std::exception& e) {
			logger::error("VolumetricLighting {}: Failed to load from JSON: {}", GetEditorID(), e.what());
			settings = vanillaSettings;
		}
	} else {
		settings = vanillaSettings;
	}

	originalSettings = settings;
	ApplyChanges();
}

void VolumetricLightingWidget::LoadFromGameSettings()
{
	if (!volumetricLighting)
		return;
	settings.intensity = volumetricLighting->intensity;
	settings.customColorContribution = volumetricLighting->customColor.contribution;
	settings.red = volumetricLighting->red;
	settings.green = volumetricLighting->green;
	settings.blue = volumetricLighting->blue;
	settings.densityContribution = volumetricLighting->density.contribution;
	settings.densitySize = volumetricLighting->density.size;
	settings.densityWindSpeed = volumetricLighting->density.windSpeed;
	settings.densityFallingSpeed = volumetricLighting->density.fallingSpeed;
	settings.phaseFunctionContribution = volumetricLighting->phaseFunction.contribution;
	settings.phaseFunctionScattering = volumetricLighting->phaseFunction.scattering;
	settings.samplingRangeFactor = volumetricLighting->samplingRepartition.rangeFactor;
}

void VolumetricLightingWidget::SaveSettings()
{
	js["intensity"] = settings.intensity;
	js["customColorContribution"] = settings.customColorContribution;
	js["red"] = settings.red;
	js["green"] = settings.green;
	js["blue"] = settings.blue;
	js["densityContribution"] = settings.densityContribution;
	js["densitySize"] = settings.densitySize;
	js["densityWindSpeed"] = settings.densityWindSpeed;
	js["densityFallingSpeed"] = settings.densityFallingSpeed;
	js["phaseFunctionContribution"] = settings.phaseFunctionContribution;
	js["phaseFunctionScattering"] = settings.phaseFunctionScattering;
	js["samplingRangeFactor"] = settings.samplingRangeFactor;
	originalSettings = settings;
}

void VolumetricLightingWidget::ApplyChanges()
{
	if (!volumetricLighting)
		return;

	volumetricLighting->intensity = settings.intensity;
	volumetricLighting->customColor.contribution = settings.customColorContribution;
	volumetricLighting->red = settings.red;
	volumetricLighting->green = settings.green;
	volumetricLighting->blue = settings.blue;
	volumetricLighting->density.contribution = settings.densityContribution;
	volumetricLighting->density.size = settings.densitySize;
	volumetricLighting->density.windSpeed = settings.densityWindSpeed;
	volumetricLighting->density.fallingSpeed = settings.densityFallingSpeed;
	volumetricLighting->phaseFunction.contribution = settings.phaseFunctionContribution;
	volumetricLighting->phaseFunction.scattering = settings.phaseFunctionScattering;
	volumetricLighting->samplingRepartition.rangeFactor = settings.samplingRangeFactor;
}

void VolumetricLightingWidget::RevertChanges()
{
	settings = vanillaSettings;
	ApplyChanges();
}

bool VolumetricLightingWidget::HasUnsavedChanges() const
{
	return !(settings == originalSettings);
}

std::vector<Widget::SearchResult> VolumetricLightingWidget::CollectSearchableSettings() const
{
	// Many tabs share the same inner label ("Contribution"); display names are
	// disambiguated for the dropdown while the inner id matches the ImGui label.
	return {
		{ "Intensity", "Basic", "Intensity" },
		{ "Custom Color Contribution", "Basic", "Contribution" },
		{ "Color", "Basic", "Color" },
		{ "Density Contribution", "Density", "Contribution" },
		{ "Density Size", "Density", "Size" },
		{ "Wind Speed", "Density", "Wind Speed" },
		{ "Falling Speed", "Density", "Falling Speed" },
		{ "Phase Function Contribution", "Advanced", "Contribution" },
		{ "Phase Function Scattering", "Advanced", "Scattering" },
		{ "Sampling Range Factor", "Advanced", "Range Factor" },
	};
}
