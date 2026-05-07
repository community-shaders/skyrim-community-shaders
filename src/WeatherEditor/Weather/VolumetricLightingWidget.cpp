#include "VolumetricLightingWidget.h"
#include "../EditorWindow.h"
#include "../WeatherUtils.h"

namespace
{
	namespace VolumetricLightingTab
	{
		constexpr const char* kBasic = "Basic";
		constexpr const char* kDensity = "Density";
		constexpr const char* kAdvanced = "Advanced";
	}

	namespace VolumetricLightingSetting
	{
		constexpr const char* kIntensity = "Intensity";
		constexpr const char* kContribution = "Contribution";
		constexpr const char* kColor = "Color";
		constexpr const char* kSize = "Size";
		constexpr const char* kWindSpeed = "Wind Speed";
		constexpr const char* kFallingSpeed = "Falling Speed";
		constexpr const char* kScattering = "Scattering";
		constexpr const char* kRangeFactor = "Range Factor";
	}
}

void VolumetricLightingWidget::DrawWidget()
{
	WeatherUtils::SetCurrentWidget(this);
	if (BeginWidgetWindow()) {
		DrawWidgetHeader("##VolumetricLightingSearch", true, true);
		DrawSearchDropdown();
	}
	bool changed = false;

	if (ImGui::BeginTabBar("VolumetricLightingTabs")) {
		const ImGuiTabItemFlags basicFlags = GetTabFlagsForOverride(VolumetricLightingTab::kBasic);
		const ImGuiTabItemFlags densityFlags = GetTabFlagsForOverride(VolumetricLightingTab::kDensity);
		const ImGuiTabItemFlags advancedFlags = GetTabFlagsForOverride(VolumetricLightingTab::kAdvanced);
		auto drawSection = [&](const char* settingId, const char* heading, auto draw) {
			DrawSearchSectionIfMatches(settingId, [&](const char*) {
				ImGui::SeparatorText(heading);
				draw();
			});
		};

		if (ImGui::BeginTabItem(VolumetricLightingTab::kBasic, nullptr, basicFlags)) {
			BeginScrollableContent("##BasicScroll");
			drawSection(VolumetricLightingSetting::kIntensity, "Intensity", [&]() {
				changed |= WeatherUtils::DrawSliderFloat(VolumetricLightingSetting::kIntensity, settings.intensity, 0.0f, 50.0f);
			});
			drawSection(VolumetricLightingSetting::kContribution, "Custom Color", [&]() {
				changed |= WeatherUtils::DrawSliderFloat(VolumetricLightingSetting::kContribution, settings.customColorContribution, 0.0f, 1.0f);
			});
			drawSection(VolumetricLightingSetting::kColor, "RGB Color", [&]() {
				float3 rgbColor{ settings.red, settings.green, settings.blue };
				if (WeatherUtils::DrawColorEdit(VolumetricLightingSetting::kColor, rgbColor)) {
					settings.red = rgbColor.x;
					settings.green = rgbColor.y;
					settings.blue = rgbColor.z;
					changed = true;
				}
			});
			EndScrollableContent();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem(VolumetricLightingTab::kDensity, nullptr, densityFlags)) {
			BeginScrollableContent("##DensityScroll");
			if (MatchesAnySearch({ VolumetricLightingSetting::kContribution, VolumetricLightingSetting::kSize, VolumetricLightingSetting::kWindSpeed, VolumetricLightingSetting::kFallingSpeed })) {
				ImGui::SeparatorText("Density Settings");
				changed |= WeatherUtils::DrawSliderFloat(VolumetricLightingSetting::kContribution, settings.densityContribution, 0.0f, 1.0f);
				changed |= WeatherUtils::DrawSliderFloat(VolumetricLightingSetting::kSize, settings.densitySize, 0.1f, 10000.0f);
				changed |= WeatherUtils::DrawSliderFloat(VolumetricLightingSetting::kWindSpeed, settings.densityWindSpeed, 0.0f, 100.0f);
				changed |= WeatherUtils::DrawSliderFloat(VolumetricLightingSetting::kFallingSpeed, settings.densityFallingSpeed, 0.0f, 100.0f);
			}
			EndScrollableContent();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem(VolumetricLightingTab::kAdvanced, nullptr, advancedFlags)) {
			BeginScrollableContent("##AdvancedScroll");
			if (MatchesAnySearch({ VolumetricLightingSetting::kContribution, VolumetricLightingSetting::kScattering })) {
				ImGui::SeparatorText("Phase Function");
				changed |= WeatherUtils::DrawSliderFloat(VolumetricLightingSetting::kContribution, settings.phaseFunctionContribution, 0.0f, 1.0f);
				changed |= WeatherUtils::DrawSliderFloat(VolumetricLightingSetting::kScattering, settings.phaseFunctionScattering, -1.0f, 1.0f);
			}
			drawSection(VolumetricLightingSetting::kRangeFactor, "Sampling", [&]() {
				changed |= WeatherUtils::DrawSliderFloat(VolumetricLightingSetting::kRangeFactor, settings.samplingRangeFactor, 0.0f, 160.0f);
			});
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
		{ VolumetricLightingSetting::kIntensity, VolumetricLightingTab::kBasic, VolumetricLightingSetting::kIntensity },
		{ "Custom Color Contribution", VolumetricLightingTab::kBasic, VolumetricLightingSetting::kContribution },
		{ VolumetricLightingSetting::kColor, VolumetricLightingTab::kBasic, VolumetricLightingSetting::kColor },
		{ "Density Contribution", VolumetricLightingTab::kDensity, VolumetricLightingSetting::kContribution },
		{ "Density Size", VolumetricLightingTab::kDensity, VolumetricLightingSetting::kSize },
		{ VolumetricLightingSetting::kWindSpeed, VolumetricLightingTab::kDensity, VolumetricLightingSetting::kWindSpeed },
		{ VolumetricLightingSetting::kFallingSpeed, VolumetricLightingTab::kDensity, VolumetricLightingSetting::kFallingSpeed },
		{ "Phase Function Contribution", VolumetricLightingTab::kAdvanced, VolumetricLightingSetting::kContribution },
		{ "Phase Function Scattering", VolumetricLightingTab::kAdvanced, VolumetricLightingSetting::kScattering },
		{ "Sampling Range Factor", VolumetricLightingTab::kAdvanced, VolumetricLightingSetting::kRangeFactor },
	};
}
