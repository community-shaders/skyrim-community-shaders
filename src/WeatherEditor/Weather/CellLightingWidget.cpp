#include "CellLightingWidget.h"
#include "../EditorWindow.h"
#include "../WeatherUtils.h"

namespace
{
	namespace CellLightingTab
	{
		constexpr const char* kColors = "Colors";
		constexpr const char* kFog = "Fog";
		constexpr const char* kDirectionalAmbient = "Directional Ambient";
		constexpr const char* kAdvanced = "Advanced";
		constexpr const char* kInheritance = "Inheritance";
	}

	namespace CellLightingSetting
	{
		constexpr const char* kAmbientColor = "Ambient Color";
		constexpr const char* kDirectionalColor = "Directional Color";
		constexpr const char* kDirectionalFade = "Directional Fade";
		constexpr const char* kFogNearColor = "Fog Near Color";
		constexpr const char* kFogFarColor = "Fog Far Color";
		constexpr const char* kFogNear = "Fog Near";
		constexpr const char* kFogFar = "Fog Far";
		constexpr const char* kFogPower = "Fog Power";
		constexpr const char* kFogClampMax = "Fog Clamp (Max)";
		constexpr const char* kXPlus = "X+ (Right)";
		constexpr const char* kXMinus = "X- (Left)";
		constexpr const char* kYPlus = "Y+ (Front)";
		constexpr const char* kYMinus = "Y- (Back)";
		constexpr const char* kZPlus = "Z+ (Up)";
		constexpr const char* kZMinus = "Z- (Down)";
		constexpr const char* kSpecular = "Specular";
		constexpr const char* kFresnelPower = "Fresnel Power";
		constexpr const char* kLightFadeStart = "Light Fade Start";
		constexpr const char* kLightFadeEnd = "Light Fade End";
		constexpr const char* kClipDistance = "Clip Distance";
		constexpr const char* kXYRotation = "XY Rotation";
		constexpr const char* kZRotation = "Z Rotation";
		constexpr const char* kInheritAmbientColor = "Inherit Ambient Color";
		constexpr const char* kInheritDirectionalColor = "Inherit Directional Color";
		constexpr const char* kInheritFogColor = "Inherit Fog Color";
		constexpr const char* kInheritFogNear = "Inherit Fog Near";
		constexpr const char* kInheritFogFar = "Inherit Fog Far";
		constexpr const char* kInheritDirectionalRotation = "Inherit Directional Rotation";
		constexpr const char* kInheritDirectionalFade = "Inherit Directional Fade";
		constexpr const char* kInheritClipDistance = "Inherit Clip Distance";
		constexpr const char* kInheritFogPower = "Inherit Fog Power";
		constexpr const char* kInheritFogMaxClamp = "Inherit Fog Max (Clamp)";
		constexpr const char* kInheritLightFadeDistances = "Inherit Light Fade Distances";
	}
}

void CellLightingWidget::DrawWidget()
{
	WeatherUtils::SetCurrentWidget(this);
	if (BeginWidgetWindow()) {
		DrawWidgetHeader("##CellLightingSearch", true, true);
		DrawSearchDropdown();
	}

	if (!cell || !cell->IsInteriorCell()) {
		ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.0f, 1.0f), "This cell is not an interior cell.");
		ImGui::TextWrapped("Cell lighting properties only apply to interior cells.");
	} else if (!cell->GetLighting()) {
		ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "No lighting data available for this cell.");
	} else {
		bool changed = false;

		if (ImGui::BeginTabBar("CellLightingTabs")) {
			const ImGuiTabItemFlags colorsFlags = GetTabFlagsForOverride(CellLightingTab::kColors);
			const ImGuiTabItemFlags fogFlags = GetTabFlagsForOverride(CellLightingTab::kFog);
			const ImGuiTabItemFlags dalcFlags = GetTabFlagsForOverride(CellLightingTab::kDirectionalAmbient);
			const ImGuiTabItemFlags advancedFlags = GetTabFlagsForOverride(CellLightingTab::kAdvanced);
			const ImGuiTabItemFlags inheritFlags = GetTabFlagsForOverride(CellLightingTab::kInheritance);
			if (ImGui::BeginTabItem(CellLightingTab::kColors, nullptr, colorsFlags)) {
				BeginScrollableContent("##ColorsScroll");
				if (MatchesAnySearch({ CellLightingSetting::kAmbientColor, CellLightingSetting::kDirectionalColor, CellLightingSetting::kDirectionalFade })) {
					ImGui::SeparatorText("Ambient & Directional");
					changed |= WeatherUtils::DrawColorEdit(CellLightingSetting::kAmbientColor, settings.ambient);
					changed |= WeatherUtils::DrawColorEdit(CellLightingSetting::kDirectionalColor, settings.directional);
					changed |= WeatherUtils::DrawSliderFloat(CellLightingSetting::kDirectionalFade, settings.directionalFade, 0.0f, 1.0f);
				}
				if (MatchesAnySearch({ CellLightingSetting::kFogNearColor, CellLightingSetting::kFogFarColor })) {
					ImGui::SeparatorText("Fog Colors");
					changed |= WeatherUtils::DrawColorEdit(CellLightingSetting::kFogNearColor, settings.fogColorNear);
					changed |= WeatherUtils::DrawColorEdit(CellLightingSetting::kFogFarColor, settings.fogColorFar);
				}
				EndScrollableContent();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem(CellLightingTab::kFog, nullptr, fogFlags)) {
				BeginScrollableContent("##FogScroll");
				if (MatchesAnySearch({ CellLightingSetting::kFogNear, CellLightingSetting::kFogFar })) {
					ImGui::SeparatorText("Fog Distance");
					changed |= WeatherUtils::DrawSliderFloat(CellLightingSetting::kFogNear, settings.fogNear, 0.0f, 163840.0f);
					changed |= WeatherUtils::DrawSliderFloat(CellLightingSetting::kFogFar, settings.fogFar, 0.0f, 163840.0f);
				}
				if (MatchesAnySearch({ CellLightingSetting::kFogPower, CellLightingSetting::kFogClampMax })) {
					ImGui::SeparatorText("Fog Properties");
					changed |= WeatherUtils::DrawSliderFloat(CellLightingSetting::kFogPower, settings.fogPower, 0.0f, 10.0f);
					changed |= WeatherUtils::DrawSliderFloat(CellLightingSetting::kFogClampMax, settings.fogClamp, 0.0f, 1.0f);
				}
				EndScrollableContent();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem(CellLightingTab::kDirectionalAmbient, nullptr, dalcFlags)) {
				BeginScrollableContent("##DAmbientScroll");
				ImGui::SeparatorText("Directional Ambient Lighting (DALC)");
				changed |= WeatherUtils::DrawColorEdit(CellLightingSetting::kXPlus, settings.directionalXPlus);
				changed |= WeatherUtils::DrawColorEdit(CellLightingSetting::kXMinus, settings.directionalXMinus);
				changed |= WeatherUtils::DrawColorEdit(CellLightingSetting::kYPlus, settings.directionalYPlus);
				changed |= WeatherUtils::DrawColorEdit(CellLightingSetting::kYMinus, settings.directionalYMinus);
				changed |= WeatherUtils::DrawColorEdit(CellLightingSetting::kZPlus, settings.directionalZPlus);
				changed |= WeatherUtils::DrawColorEdit(CellLightingSetting::kZMinus, settings.directionalZMinus);
				changed |= WeatherUtils::DrawColorEdit(CellLightingSetting::kSpecular, settings.directionalSpecular);
				changed |= WeatherUtils::DrawSliderFloat(CellLightingSetting::kFresnelPower, settings.fresnelPower, 0.0f, 10.0f);
				EndScrollableContent();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem(CellLightingTab::kAdvanced, nullptr, advancedFlags)) {
				BeginScrollableContent("##AdvancedScroll");
				if (MatchesAnySearch({ CellLightingSetting::kLightFadeStart, CellLightingSetting::kLightFadeEnd, CellLightingSetting::kClipDistance })) {
					ImGui::SeparatorText("Light Fade Distances");
					changed |= WeatherUtils::DrawSliderFloat(CellLightingSetting::kLightFadeStart, settings.lightFadeStart, 0.0f, 163840.0f);
					changed |= WeatherUtils::DrawSliderFloat(CellLightingSetting::kLightFadeEnd, settings.lightFadeEnd, 0.0f, 163840.0f);
					changed |= WeatherUtils::DrawSliderFloat(CellLightingSetting::kClipDistance, settings.clipDist, 0.0f, 163840.0f);
				}
				if (MatchesAnySearch({ CellLightingSetting::kXYRotation, CellLightingSetting::kZRotation })) {
					ImGui::SeparatorText("Directional Rotation");
					int xyDegrees = settings.directionalXY;
					int zDegrees = settings.directionalZ;
					if (DrawIfMatchesSearch(CellLightingSetting::kXYRotation, [&](const char* label) {
						    return DrawWithHighlight(label, [&]() {
							    return ImGui::SliderInt(label, &xyDegrees, 0, 360);
						    });
					    })) {
						settings.directionalXY = static_cast<uint32_t>(xyDegrees);
						changed = true;
					}
					if (DrawIfMatchesSearch(CellLightingSetting::kZRotation, [&](const char* label) {
						    return DrawWithHighlight(label, [&]() {
							    return ImGui::SliderInt(label, &zDegrees, 0, 360);
						    });
					    })) {
						settings.directionalZ = static_cast<uint32_t>(zDegrees);
						changed = true;
					}
				}
				EndScrollableContent();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem(CellLightingTab::kInheritance, nullptr, inheritFlags)) {
				BeginScrollableContent("##InheritanceScroll");
				ImGui::TextWrapped("These flags control which lighting properties are inherited from the cell's lighting template.");
				ImGui::Separator();
				changed |= WeatherUtils::DrawCheckbox(CellLightingSetting::kInheritAmbientColor, settings.inheritAmbientColor);
				changed |= WeatherUtils::DrawCheckbox(CellLightingSetting::kInheritDirectionalColor, settings.inheritDirectionalColor);
				changed |= WeatherUtils::DrawCheckbox(CellLightingSetting::kInheritFogColor, settings.inheritFogColor);
				changed |= WeatherUtils::DrawCheckbox(CellLightingSetting::kInheritFogNear, settings.inheritFogNear);
				changed |= WeatherUtils::DrawCheckbox(CellLightingSetting::kInheritFogFar, settings.inheritFogFar);
				changed |= WeatherUtils::DrawCheckbox(CellLightingSetting::kInheritDirectionalRotation, settings.inheritDirectionalRotation);
				changed |= WeatherUtils::DrawCheckbox(CellLightingSetting::kInheritDirectionalFade, settings.inheritDirectionalFade);
				changed |= WeatherUtils::DrawCheckbox(CellLightingSetting::kInheritClipDistance, settings.inheritClipDistance);
				changed |= WeatherUtils::DrawCheckbox(CellLightingSetting::kInheritFogPower, settings.inheritFogPower);
				changed |= WeatherUtils::DrawCheckbox(CellLightingSetting::kInheritFogMaxClamp, settings.inheritFogMax);
				changed |= WeatherUtils::DrawCheckbox(CellLightingSetting::kInheritLightFadeDistances, settings.inheritLightFadeDistances);
				EndScrollableContent();
				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}

		if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
			ApplyChanges();
		}
	}
	ImGui::End();
}

void CellLightingWidget::LoadSettings()
{
	if (!cell || !cell->IsInteriorCell())
		return;

	auto lighting = cell->GetLighting();
	if (!lighting)
		return;

	// Try to load from JSON first
	if (!js.empty()) {
		settings = vanillaSettings;
		try {
			if (js.contains("ambient")) {
				auto arr = js["ambient"];
				if (arr.is_array() && arr.size() == 3) {
					settings.ambient = { arr[0], arr[1], arr[2] };
				}
			}
			if (js.contains("directional")) {
				auto arr = js["directional"];
				if (arr.is_array() && arr.size() == 3) {
					settings.directional = { arr[0], arr[1], arr[2] };
				}
			}
			if (js.contains("fogColorNear")) {
				auto arr = js["fogColorNear"];
				if (arr.is_array() && arr.size() == 3) {
					settings.fogColorNear = { arr[0], arr[1], arr[2] };
				}
			}
			if (js.contains("fogColorFar")) {
				auto arr = js["fogColorFar"];
				if (arr.is_array() && arr.size() == 3) {
					settings.fogColorFar = { arr[0], arr[1], arr[2] };
				}
			}
			if (js.contains("fogNear"))
				settings.fogNear = js["fogNear"];
			if (js.contains("fogFar"))
				settings.fogFar = js["fogFar"];
			if (js.contains("fogPower"))
				settings.fogPower = js["fogPower"];
			if (js.contains("fogClamp"))
				settings.fogClamp = js["fogClamp"];
			if (js.contains("directionalFade"))
				settings.directionalFade = js["directionalFade"];
			if (js.contains("clipDist"))
				settings.clipDist = js["clipDist"];
			if (js.contains("lightFadeStart"))
				settings.lightFadeStart = js["lightFadeStart"];
			if (js.contains("lightFadeEnd"))
				settings.lightFadeEnd = js["lightFadeEnd"];
			if (js.contains("directionalXY"))
				settings.directionalXY = js["directionalXY"];
			if (js.contains("directionalZ"))
				settings.directionalZ = js["directionalZ"];

			if (js.contains("dalc")) {
				auto& dalc = js["dalc"];
				if (dalc.contains("xPlus") && dalc["xPlus"].is_array() && dalc["xPlus"].size() == 3) {
					settings.directionalXPlus = { dalc["xPlus"][0], dalc["xPlus"][1], dalc["xPlus"][2] };
				}
				if (dalc.contains("xMinus") && dalc["xMinus"].is_array() && dalc["xMinus"].size() == 3) {
					settings.directionalXMinus = { dalc["xMinus"][0], dalc["xMinus"][1], dalc["xMinus"][2] };
				}
				if (dalc.contains("yPlus") && dalc["yPlus"].is_array() && dalc["yPlus"].size() == 3) {
					settings.directionalYPlus = { dalc["yPlus"][0], dalc["yPlus"][1], dalc["yPlus"][2] };
				}
				if (dalc.contains("yMinus") && dalc["yMinus"].is_array() && dalc["yMinus"].size() == 3) {
					settings.directionalYMinus = { dalc["yMinus"][0], dalc["yMinus"][1], dalc["yMinus"][2] };
				}
				if (dalc.contains("zPlus") && dalc["zPlus"].is_array() && dalc["zPlus"].size() == 3) {
					settings.directionalZPlus = { dalc["zPlus"][0], dalc["zPlus"][1], dalc["zPlus"][2] };
				}
				if (dalc.contains("zMinus") && dalc["zMinus"].is_array() && dalc["zMinus"].size() == 3) {
					settings.directionalZMinus = { dalc["zMinus"][0], dalc["zMinus"][1], dalc["zMinus"][2] };
				}
				if (dalc.contains("specular") && dalc["specular"].is_array() && dalc["specular"].size() == 3) {
					settings.directionalSpecular = { dalc["specular"][0], dalc["specular"][1], dalc["specular"][2] };
				}
				if (dalc.contains("fresnelPower"))
					settings.fresnelPower = dalc["fresnelPower"];
			}

			if (js.contains("inherit")) {
				auto& inherit = js["inherit"];
				if (inherit.contains("ambientColor"))
					settings.inheritAmbientColor = inherit["ambientColor"];
				if (inherit.contains("directionalColor"))
					settings.inheritDirectionalColor = inherit["directionalColor"];
				if (inherit.contains("fogColor"))
					settings.inheritFogColor = inherit["fogColor"];
				if (inherit.contains("fogNear"))
					settings.inheritFogNear = inherit["fogNear"];
				if (inherit.contains("fogFar"))
					settings.inheritFogFar = inherit["fogFar"];
				if (inherit.contains("directionalRotation"))
					settings.inheritDirectionalRotation = inherit["directionalRotation"];
				if (inherit.contains("directionalFade"))
					settings.inheritDirectionalFade = inherit["directionalFade"];
				if (inherit.contains("clipDistance"))
					settings.inheritClipDistance = inherit["clipDistance"];
				if (inherit.contains("fogPower"))
					settings.inheritFogPower = inherit["fogPower"];
				if (inherit.contains("fogMax"))
					settings.inheritFogMax = inherit["fogMax"];
				if (inherit.contains("lightFadeDistances"))
					settings.inheritLightFadeDistances = inherit["lightFadeDistances"];
			}

		} catch (const std::exception& e) {
			logger::error("CellLighting {}: Failed to load from JSON: {}", GetEditorID(), e.what());
			settings = vanillaSettings;
		}
	} else {
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
	ColorToFloat3(dalc.directional.x.max, settings.directionalXPlus);
	ColorToFloat3(dalc.directional.x.min, settings.directionalXMinus);
	ColorToFloat3(dalc.directional.y.max, settings.directionalYPlus);
	ColorToFloat3(dalc.directional.y.min, settings.directionalYMinus);
	ColorToFloat3(dalc.directional.z.max, settings.directionalZPlus);
	ColorToFloat3(dalc.directional.z.min, settings.directionalZMinus);
	ColorToFloat3(dalc.specular, settings.directionalSpecular);
	settings.fresnelPower = dalc.fresnelPower;

	auto flags = lighting->lightingTemplateInheritanceFlags;
	settings.inheritAmbientColor = flags.any(RE::INTERIOR_DATA::Inherit::kAmbientColor);
	settings.inheritDirectionalColor = flags.any(RE::INTERIOR_DATA::Inherit::kDirectionalColor);
	settings.inheritFogColor = flags.any(RE::INTERIOR_DATA::Inherit::kFogColor);
	settings.inheritFogNear = flags.any(RE::INTERIOR_DATA::Inherit::kFogNear);
	settings.inheritFogFar = flags.any(RE::INTERIOR_DATA::Inherit::kFogFar);
	settings.inheritDirectionalRotation = flags.any(RE::INTERIOR_DATA::Inherit::kDirectionalRotation);
	settings.inheritDirectionalFade = flags.any(RE::INTERIOR_DATA::Inherit::kDirectionalFade);
	settings.inheritClipDistance = flags.any(RE::INTERIOR_DATA::Inherit::kClipDistance);
	settings.inheritFogPower = flags.any(RE::INTERIOR_DATA::Inherit::kFogPower);
	settings.inheritFogMax = flags.any(RE::INTERIOR_DATA::Inherit::kFogMax);
	settings.inheritLightFadeDistances = flags.any(RE::INTERIOR_DATA::Inherit::kLightFadeDistances);
}

void CellLightingWidget::SaveSettings()
{
	js["ambient"] = { settings.ambient.x, settings.ambient.y, settings.ambient.z };
	js["directional"] = { settings.directional.x, settings.directional.y, settings.directional.z };
	js["fogColorNear"] = { settings.fogColorNear.x, settings.fogColorNear.y, settings.fogColorNear.z };
	js["fogColorFar"] = { settings.fogColorFar.x, settings.fogColorFar.y, settings.fogColorFar.z };
	js["fogNear"] = settings.fogNear;
	js["fogFar"] = settings.fogFar;
	js["fogPower"] = settings.fogPower;
	js["fogClamp"] = settings.fogClamp;
	js["directionalFade"] = settings.directionalFade;
	js["clipDist"] = settings.clipDist;
	js["lightFadeStart"] = settings.lightFadeStart;
	js["lightFadeEnd"] = settings.lightFadeEnd;
	js["directionalXY"] = settings.directionalXY;
	js["directionalZ"] = settings.directionalZ;

	js["dalc"]["xPlus"] = { settings.directionalXPlus.x, settings.directionalXPlus.y, settings.directionalXPlus.z };
	js["dalc"]["xMinus"] = { settings.directionalXMinus.x, settings.directionalXMinus.y, settings.directionalXMinus.z };
	js["dalc"]["yPlus"] = { settings.directionalYPlus.x, settings.directionalYPlus.y, settings.directionalYPlus.z };
	js["dalc"]["yMinus"] = { settings.directionalYMinus.x, settings.directionalYMinus.y, settings.directionalYMinus.z };
	js["dalc"]["zPlus"] = { settings.directionalZPlus.x, settings.directionalZPlus.y, settings.directionalZPlus.z };
	js["dalc"]["zMinus"] = { settings.directionalZMinus.x, settings.directionalZMinus.y, settings.directionalZMinus.z };
	js["dalc"]["specular"] = { settings.directionalSpecular.x, settings.directionalSpecular.y, settings.directionalSpecular.z };
	js["dalc"]["fresnelPower"] = settings.fresnelPower;

	js["inherit"]["ambientColor"] = settings.inheritAmbientColor;
	js["inherit"]["directionalColor"] = settings.inheritDirectionalColor;
	js["inherit"]["fogColor"] = settings.inheritFogColor;
	js["inherit"]["fogNear"] = settings.inheritFogNear;
	js["inherit"]["fogFar"] = settings.inheritFogFar;
	js["inherit"]["directionalRotation"] = settings.inheritDirectionalRotation;
	js["inherit"]["directionalFade"] = settings.inheritDirectionalFade;
	js["inherit"]["clipDistance"] = settings.inheritClipDistance;
	js["inherit"]["fogPower"] = settings.inheritFogPower;
	js["inherit"]["fogMax"] = settings.inheritFogMax;
	js["inherit"]["lightFadeDistances"] = settings.inheritLightFadeDistances;
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
	Float3ToColor(settings.directionalXPlus, dalc.directional.x.max);
	Float3ToColor(settings.directionalXMinus, dalc.directional.x.min);
	Float3ToColor(settings.directionalYPlus, dalc.directional.y.max);
	Float3ToColor(settings.directionalYMinus, dalc.directional.y.min);
	Float3ToColor(settings.directionalZPlus, dalc.directional.z.max);
	Float3ToColor(settings.directionalZMinus, dalc.directional.z.min);
	Float3ToColor(settings.directionalSpecular, dalc.specular);
	dalc.fresnelPower = settings.fresnelPower;

	// Apply inheritance flags
	lighting->lightingTemplateInheritanceFlags.reset();
	if (settings.inheritAmbientColor)
		lighting->lightingTemplateInheritanceFlags.set(RE::INTERIOR_DATA::Inherit::kAmbientColor);
	if (settings.inheritDirectionalColor)
		lighting->lightingTemplateInheritanceFlags.set(RE::INTERIOR_DATA::Inherit::kDirectionalColor);
	if (settings.inheritFogColor)
		lighting->lightingTemplateInheritanceFlags.set(RE::INTERIOR_DATA::Inherit::kFogColor);
	if (settings.inheritFogNear)
		lighting->lightingTemplateInheritanceFlags.set(RE::INTERIOR_DATA::Inherit::kFogNear);
	if (settings.inheritFogFar)
		lighting->lightingTemplateInheritanceFlags.set(RE::INTERIOR_DATA::Inherit::kFogFar);
	if (settings.inheritDirectionalRotation)
		lighting->lightingTemplateInheritanceFlags.set(RE::INTERIOR_DATA::Inherit::kDirectionalRotation);
	if (settings.inheritDirectionalFade)
		lighting->lightingTemplateInheritanceFlags.set(RE::INTERIOR_DATA::Inherit::kDirectionalFade);
	if (settings.inheritClipDistance)
		lighting->lightingTemplateInheritanceFlags.set(RE::INTERIOR_DATA::Inherit::kClipDistance);
	if (settings.inheritFogPower)
		lighting->lightingTemplateInheritanceFlags.set(RE::INTERIOR_DATA::Inherit::kFogPower);
	if (settings.inheritFogMax)
		lighting->lightingTemplateInheritanceFlags.set(RE::INTERIOR_DATA::Inherit::kFogMax);
	if (settings.inheritLightFadeDistances)
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

std::vector<Widget::SearchResult> CellLightingWidget::CollectSearchableSettings() const
{
	const std::vector<std::pair<std::string, std::vector<std::string>>> entries = {
		{ CellLightingTab::kColors, { CellLightingSetting::kAmbientColor, CellLightingSetting::kDirectionalColor, CellLightingSetting::kDirectionalFade,
										 CellLightingSetting::kFogNearColor, CellLightingSetting::kFogFarColor } },
		{ CellLightingTab::kFog, { CellLightingSetting::kFogNear, CellLightingSetting::kFogFar, CellLightingSetting::kFogPower, CellLightingSetting::kFogClampMax } },
		{ CellLightingTab::kDirectionalAmbient, { CellLightingSetting::kXPlus, CellLightingSetting::kXMinus, CellLightingSetting::kYPlus, CellLightingSetting::kYMinus,
													CellLightingSetting::kZPlus, CellLightingSetting::kZMinus, CellLightingSetting::kSpecular, CellLightingSetting::kFresnelPower } },
		{ CellLightingTab::kAdvanced, { CellLightingSetting::kLightFadeStart, CellLightingSetting::kLightFadeEnd, CellLightingSetting::kClipDistance,
										   CellLightingSetting::kXYRotation, CellLightingSetting::kZRotation } },
		{ CellLightingTab::kInheritance, { CellLightingSetting::kInheritAmbientColor, CellLightingSetting::kInheritDirectionalColor, CellLightingSetting::kInheritFogColor,
											  CellLightingSetting::kInheritFogNear, CellLightingSetting::kInheritFogFar, CellLightingSetting::kInheritDirectionalRotation,
											  CellLightingSetting::kInheritDirectionalFade, CellLightingSetting::kInheritClipDistance, CellLightingSetting::kInheritFogPower,
											  CellLightingSetting::kInheritFogMaxClamp, CellLightingSetting::kInheritLightFadeDistances } },
	};

	std::vector<SearchResult> results;
	for (const auto& [tab, names] : entries) {
		for (const auto& name : names) {
			results.push_back({ name, tab, name });
		}
	}
	return results;
}
