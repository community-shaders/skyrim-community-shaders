#include "CellLightingWidget.h"
#include "../EditorWindow.h"
#include "../WeatherUtils.h"
#include "Menu.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	CellLightingWidget::DALC,
	xPlus, xMinus,
	yPlus, yMinus,
	zPlus, zMinus,
	specular,
	fresnelPower)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	CellLightingWidget::Inherit,
	ambientColor,
	directionalColor,
	fogColor,
	fogNear,
	fogFar,
	directionalRotation,
	directionalFade,
	clipDistance,
	fogPower,
	fogMax,
	lightFadeDistances)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	CellLightingWidget::Settings,
	ambient,
	directional,
	fogColorNear,
	fogColorFar,
	fogNear,
	fogFar,
	fogPower,
	fogClamp,
	directionalFade,
	clipDist,
	lightFadeStart,
	lightFadeEnd,
	directionalXY,
	directionalZ,
	dalc,
	inherit)

void CellLightingWidget::DrawWidget()
{
	WeatherUtils::SetCurrentWidget(this);
	ImGui::SetNextWindowSizeConstraints(ImVec2(600, 0), ImVec2(FLT_MAX, FLT_MAX));
	if (!ImGui::Begin(GetEditorID().c_str(), &open, ImGuiWindowFlags_NoSavedSettings | kStickyHeaderFlags)) {
		ImGui::End();
		return;
	}
	DrawWidgetHeader("##CellLightingSearch", true, true);

	if (!cell || !cell->IsInteriorCell()) {
		auto& palette = Menu::GetSingleton()->GetTheme().StatusPalette;
		ImGui::TextColored(palette.Warning, "This cell is not an interior cell.");
		ImGui::TextWrapped("Cell lighting properties only apply to interior cells.");
	} else if (!cell->GetLighting()) {
		ImGui::TextColored(Menu::GetSingleton()->GetTheme().StatusPalette.Error, "No lighting data available for this cell.");
	} else {
		bool changed = false;

		if (ImGui::BeginTabBar("CellLightingTabs")) {
			if (ImGui::BeginTabItem("Colors")) {
				BeginScrollableContent("##ColorsScroll");
				ImGui::SeparatorText("Ambient & Directional");
				if (WeatherUtils::DrawColorEdit("Ambient Color", settings.ambient))
					changed = true;
				if (WeatherUtils::DrawColorEdit("Directional Color", settings.directional))
					changed = true;
				if (WeatherUtils::DrawSliderFloat("Directional Fade", settings.directionalFade, 0.0f, 1.0f))
					changed = true;

				ImGui::SeparatorText("Fog Colors");
				if (WeatherUtils::DrawColorEdit("Fog Near Color", settings.fogColorNear))
					changed = true;
				if (WeatherUtils::DrawColorEdit("Fog Far Color", settings.fogColorFar))
					changed = true;

				EndScrollableContent();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Fog")) {
				BeginScrollableContent("##FogScroll");
				ImGui::SeparatorText("Fog Distance");
				if (WeatherUtils::DrawSliderFloat("Fog Near", settings.fogNear, 0.0f, 10000.0f))
					changed = true;
				if (WeatherUtils::DrawSliderFloat("Fog Far", settings.fogFar, 0.0f, 50000.0f))
					changed = true;

				ImGui::SeparatorText("Fog Properties");
				if (WeatherUtils::DrawSliderFloat("Fog Power", settings.fogPower, 0.0f, 10.0f))
					changed = true;
				if (WeatherUtils::DrawSliderFloat("Fog Clamp (Max)", settings.fogClamp, 0.0f, 1.0f))
					changed = true;

				EndScrollableContent();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Directional Ambient")) {
				BeginScrollableContent("##DAmbientScroll");
				ImGui::SeparatorText("Directional Ambient Lighting (DALC)");

				if (WeatherUtils::DrawColorEdit("X+ (Right)", settings.dalc.xPlus))
					changed = true;
				if (WeatherUtils::DrawColorEdit("X- (Left)", settings.dalc.xMinus))
					changed = true;
				if (WeatherUtils::DrawColorEdit("Y+ (Front)", settings.dalc.yPlus))
					changed = true;
				if (WeatherUtils::DrawColorEdit("Y- (Back)", settings.dalc.yMinus))
					changed = true;
				if (WeatherUtils::DrawColorEdit("Z+ (Up)", settings.dalc.zPlus))
					changed = true;
				if (WeatherUtils::DrawColorEdit("Z- (Down)", settings.dalc.zMinus))
					changed = true;
				if (WeatherUtils::DrawColorEdit("Specular", settings.dalc.specular))
					changed = true;
				if (WeatherUtils::DrawSliderFloat("Fresnel Power", settings.dalc.fresnelPower, 0.0f, 10.0f))
					changed = true;

				EndScrollableContent();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Advanced")) {
				BeginScrollableContent("##AdvancedScroll");
				ImGui::SeparatorText("Light Fade Distances");
				if (WeatherUtils::DrawSliderFloat("Light Fade Start", settings.lightFadeStart, 0.0f, 10000.0f))
					changed = true;
				if (WeatherUtils::DrawSliderFloat("Light Fade End", settings.lightFadeEnd, 0.0f, 20000.0f))
					changed = true;
				if (WeatherUtils::DrawSliderFloat("Clip Distance", settings.clipDist, 0.0f, 50000.0f))
					changed = true;

				ImGui::SeparatorText("Directional Rotation");
				int xyDegrees = settings.directionalXY;
				int zDegrees = settings.directionalZ;
				if (ImGui::SliderInt("XY Rotation", &xyDegrees, 0, 360)) {
					settings.directionalXY = static_cast<uint32_t>(xyDegrees);
					changed = true;
				}
				if (ImGui::SliderInt("Z Rotation", &zDegrees, 0, 360)) {
					settings.directionalZ = static_cast<uint32_t>(zDegrees);
					changed = true;
				}

				EndScrollableContent();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Inheritance")) {
				BeginScrollableContent("##InheritanceScroll");
				ImGui::TextWrapped("These flags control which lighting properties are inherited from the cell's lighting template.");
				ImGui::Separator();

				if (ImGui::Checkbox("Inherit Ambient Color", &settings.inherit.ambientColor))
					changed = true;
				if (ImGui::Checkbox("Inherit Directional Color", &settings.inherit.directionalColor))
					changed = true;
				if (ImGui::Checkbox("Inherit Fog Color", &settings.inherit.fogColor))
					changed = true;
				if (ImGui::Checkbox("Inherit Fog Near", &settings.inherit.fogNear))
					changed = true;
				if (ImGui::Checkbox("Inherit Fog Far", &settings.inherit.fogFar))
					changed = true;
				if (ImGui::Checkbox("Inherit Directional Rotation", &settings.inherit.directionalRotation))
					changed = true;
				if (ImGui::Checkbox("Inherit Directional Fade", &settings.inherit.directionalFade))
					changed = true;
				if (ImGui::Checkbox("Inherit Clip Distance", &settings.inherit.clipDistance))
					changed = true;
				if (ImGui::Checkbox("Inherit Fog Power", &settings.inherit.fogPower))
					changed = true;
				if (ImGui::Checkbox("Inherit Fog Max (Clamp)", &settings.inherit.fogMax))
					changed = true;
				if (ImGui::Checkbox("Inherit Light Fade Distances", &settings.inherit.lightFadeDistances))
					changed = true;

				EndScrollableContent();
				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}

		if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
			ApplyChanges();
		}
	}  // end interior cell else-branch
	ImGui::End();
}

void CellLightingWidget::LoadSettings()
{
	if (!cell || !cell->IsInteriorCell())
		return;

	auto lighting = cell->GetLighting();
	if (!lighting)
		return;

	try {
		if (!js.empty()) {
			settings = js;
		} else {
			settings = vanillaSettings;
		}
	} catch (const std::exception& e) {
		logger::error("CellLighting {}: Failed to load from JSON: {}", GetEditorID(), e.what());
		settings = vanillaSettings;
	}

	originalSettings = settings;
	ApplyChanges();
}

void CellLightingWidget::LoadFromGameSettings()
{
	if (!cell || !cell->IsInteriorCell())
		return;

	auto lighting = cell->GetLighting();
	if (!lighting)
		return;

	ColorToFloat3(lighting->ambient, settings.ambient);
	ColorToFloat3(lighting->directional, settings.directional);
	ColorToFloat3(lighting->fogColorNear, settings.fogColorNear);
	ColorToFloat3(lighting->fogColorFar, settings.fogColorFar);

	settings.fogNear = lighting->fogNear;
	settings.fogFar = lighting->fogFar;
	settings.fogPower = lighting->fogPower;
	settings.fogClamp = lighting->fogClamp;

	settings.directionalFade = lighting->directionalFade;
	settings.clipDist = lighting->clipDist;
	settings.lightFadeStart = lighting->lightFadeStart;
	settings.lightFadeEnd = lighting->lightFadeEnd;
	settings.directionalXY = lighting->directionalXY;
	settings.directionalZ = lighting->directionalZ;

	auto& dalc = lighting->directionalAmbientLightingColors;
	ColorToFloat3(dalc.directional.x.max, settings.dalc.xPlus);
	ColorToFloat3(dalc.directional.x.min, settings.dalc.xMinus);
	ColorToFloat3(dalc.directional.y.max, settings.dalc.yPlus);
	ColorToFloat3(dalc.directional.y.min, settings.dalc.yMinus);
	ColorToFloat3(dalc.directional.z.max, settings.dalc.zPlus);
	ColorToFloat3(dalc.directional.z.min, settings.dalc.zMinus);
	ColorToFloat3(dalc.specular, settings.dalc.specular);
	settings.dalc.fresnelPower = dalc.fresnelPower;

	auto flags = lighting->lightingTemplateInheritanceFlags;
	settings.inherit.ambientColor = flags.any(RE::INTERIOR_DATA::Inherit::kAmbientColor);
	settings.inherit.directionalColor = flags.any(RE::INTERIOR_DATA::Inherit::kDirectionalColor);
	settings.inherit.fogColor = flags.any(RE::INTERIOR_DATA::Inherit::kFogColor);
	settings.inherit.fogNear = flags.any(RE::INTERIOR_DATA::Inherit::kFogNear);
	settings.inherit.fogFar = flags.any(RE::INTERIOR_DATA::Inherit::kFogFar);
	settings.inherit.directionalRotation = flags.any(RE::INTERIOR_DATA::Inherit::kDirectionalRotation);
	settings.inherit.directionalFade = flags.any(RE::INTERIOR_DATA::Inherit::kDirectionalFade);
	settings.inherit.clipDistance = flags.any(RE::INTERIOR_DATA::Inherit::kClipDistance);
	settings.inherit.fogPower = flags.any(RE::INTERIOR_DATA::Inherit::kFogPower);
	settings.inherit.fogMax = flags.any(RE::INTERIOR_DATA::Inherit::kFogMax);
	settings.inherit.lightFadeDistances = flags.any(RE::INTERIOR_DATA::Inherit::kLightFadeDistances);
}

void CellLightingWidget::SaveSettings()
{
	js = settings;
	originalSettings = settings;
}

void CellLightingWidget::ApplyChanges()
{
	if (!cell || !cell->IsInteriorCell())
		return;

	auto lighting = cell->GetLighting();
	if (!lighting)
		return;

	// Apply basic colors
	Float3ToColor(settings.ambient, lighting->ambient);
	Float3ToColor(settings.directional, lighting->directional);
	Float3ToColor(settings.fogColorNear, lighting->fogColorNear);
	Float3ToColor(settings.fogColorFar, lighting->fogColorFar);

	// Apply fog properties
	lighting->fogNear = settings.fogNear;
	lighting->fogFar = settings.fogFar;
	lighting->fogPower = settings.fogPower;
	lighting->fogClamp = settings.fogClamp;

	// Apply advanced properties
	lighting->directionalFade = settings.directionalFade;
	lighting->clipDist = settings.clipDist;
	lighting->lightFadeStart = settings.lightFadeStart;
	lighting->lightFadeEnd = settings.lightFadeEnd;
	lighting->directionalXY = settings.directionalXY;
	lighting->directionalZ = settings.directionalZ;

	// Apply directional ambient lighting colors
	auto& dalc = lighting->directionalAmbientLightingColors;
	Float3ToColor(settings.dalc.xPlus, dalc.directional.x.max);
	Float3ToColor(settings.dalc.xMinus, dalc.directional.x.min);
	Float3ToColor(settings.dalc.yPlus, dalc.directional.y.max);
	Float3ToColor(settings.dalc.yMinus, dalc.directional.y.min);
	Float3ToColor(settings.dalc.zPlus, dalc.directional.z.max);
	Float3ToColor(settings.dalc.zMinus, dalc.directional.z.min);
	Float3ToColor(settings.dalc.specular, dalc.specular);
	dalc.fresnelPower = settings.dalc.fresnelPower;

	// Apply inheritance flags
	lighting->lightingTemplateInheritanceFlags.reset();
	if (settings.inherit.ambientColor)
		lighting->lightingTemplateInheritanceFlags.set(RE::INTERIOR_DATA::Inherit::kAmbientColor);
	if (settings.inherit.directionalColor)
		lighting->lightingTemplateInheritanceFlags.set(RE::INTERIOR_DATA::Inherit::kDirectionalColor);
	if (settings.inherit.fogColor)
		lighting->lightingTemplateInheritanceFlags.set(RE::INTERIOR_DATA::Inherit::kFogColor);
	if (settings.inherit.fogNear)
		lighting->lightingTemplateInheritanceFlags.set(RE::INTERIOR_DATA::Inherit::kFogNear);
	if (settings.inherit.fogFar)
		lighting->lightingTemplateInheritanceFlags.set(RE::INTERIOR_DATA::Inherit::kFogFar);
	if (settings.inherit.directionalRotation)
		lighting->lightingTemplateInheritanceFlags.set(RE::INTERIOR_DATA::Inherit::kDirectionalRotation);
	if (settings.inherit.directionalFade)
		lighting->lightingTemplateInheritanceFlags.set(RE::INTERIOR_DATA::Inherit::kDirectionalFade);
	if (settings.inherit.clipDistance)
		lighting->lightingTemplateInheritanceFlags.set(RE::INTERIOR_DATA::Inherit::kClipDistance);
	if (settings.inherit.fogPower)
		lighting->lightingTemplateInheritanceFlags.set(RE::INTERIOR_DATA::Inherit::kFogPower);
	if (settings.inherit.fogMax)
		lighting->lightingTemplateInheritanceFlags.set(RE::INTERIOR_DATA::Inherit::kFogMax);
	if (settings.inherit.lightFadeDistances)
		lighting->lightingTemplateInheritanceFlags.set(RE::INTERIOR_DATA::Inherit::kLightFadeDistances);
}

void CellLightingWidget::RevertChanges()
{
	settings = vanillaSettings;
	ApplyChanges();
}

bool CellLightingWidget::HasUnsavedChanges() const
{
	return !(settings == originalSettings);
}
