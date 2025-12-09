#include "CellLightingWidget.h"
#include "../EditorWindow.h"
#include "../WeatherUtils.h"

void CellLightingWidget::DrawWidget()
{
	ImGui::SetNextWindowSizeConstraints(ImVec2(600, 0), ImVec2(FLT_MAX, FLT_MAX));
	if (ImGui::Begin(GetEditorID().c_str(), &open, ImGuiWindowFlags_NoSavedSettings)) {
		DrawWidgetHeader("##CellLightingSearch", false, true);

		bool changed = false;

		if (!cell || !cell->IsInteriorCell()) {
			ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.0f, 1.0f), "This cell is not an interior cell.");
			ImGui::TextWrapped("Cell lighting properties only apply to interior cells.");
			ImGui::End();
			return;
		}

		auto lighting = cell->GetLighting();
		if (!lighting) {
			ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "No lighting data available for this cell.");
			ImGui::End();
			return;
		}

		if (ImGui::BeginTabBar("CellLightingTabs")) {
			if (ImGui::BeginTabItem("Colors")) {
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

				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Fog")) {
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

				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Directional Ambient")) {
				ImGui::SeparatorText("Directional Ambient Lighting (DALC)");

				if (WeatherUtils::DrawColorEdit("X+ (Right)", settings.directionalXPlus))
					changed = true;
				if (WeatherUtils::DrawColorEdit("X- (Left)", settings.directionalXMinus))
					changed = true;
				if (WeatherUtils::DrawColorEdit("Y+ (Front)", settings.directionalYPlus))
					changed = true;
				if (WeatherUtils::DrawColorEdit("Y- (Back)", settings.directionalYMinus))
					changed = true;
				if (WeatherUtils::DrawColorEdit("Z+ (Up)", settings.directionalZPlus))
					changed = true;
				if (WeatherUtils::DrawColorEdit("Z- (Down)", settings.directionalZMinus))
					changed = true;
				if (WeatherUtils::DrawColorEdit("Specular", settings.directionalSpecular))
					changed = true;
				if (WeatherUtils::DrawSliderFloat("Fresnel Power", settings.fresnelPower, 0.0f, 10.0f))
					changed = true;

				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Advanced")) {
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

				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Inheritance")) {
				ImGui::TextWrapped("These flags control which lighting properties are inherited from the cell's lighting template.");
				ImGui::Separator();

				if (ImGui::Checkbox("Inherit Ambient Color", &settings.inheritAmbientColor))
					changed = true;
				if (ImGui::Checkbox("Inherit Directional Color", &settings.inheritDirectionalColor))
					changed = true;
				if (ImGui::Checkbox("Inherit Fog Color", &settings.inheritFogColor))
					changed = true;
				if (ImGui::Checkbox("Inherit Fog Near", &settings.inheritFogNear))
					changed = true;
				if (ImGui::Checkbox("Inherit Fog Far", &settings.inheritFogFar))
					changed = true;
				if (ImGui::Checkbox("Inherit Directional Rotation", &settings.inheritDirectionalRotation))
					changed = true;
				if (ImGui::Checkbox("Inherit Directional Fade", &settings.inheritDirectionalFade))
					changed = true;
				if (ImGui::Checkbox("Inherit Clip Distance", &settings.inheritClipDistance))
					changed = true;
				if (ImGui::Checkbox("Inherit Fog Power", &settings.inheritFogPower))
					changed = true;
				if (ImGui::Checkbox("Inherit Fog Max (Clamp)", &settings.inheritFogMax))
					changed = true;
				if (ImGui::Checkbox("Inherit Light Fade Distances", &settings.inheritLightFadeDistances))
					changed = true;

				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}

		if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
			ApplyChanges();
		}

		ImGui::End();
	}
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
			// Fall through to load from form
		}
	} else {
		// No JSON data, load from game form
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

	originalSettings = settings;
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

	originalSettings = settings;
}

void CellLightingWidget::RevertChanges()
{
	settings = originalSettings;
}

bool CellLightingWidget::HasUnsavedChanges() const
{
	return settings.ambient != originalSettings.ambient ||
	       settings.directional != originalSettings.directional ||
	       settings.fogColorNear != originalSettings.fogColorNear ||
	       settings.fogColorFar != originalSettings.fogColorFar ||
	       settings.fogNear != originalSettings.fogNear ||
	       settings.fogFar != originalSettings.fogFar ||
	       settings.fogPower != originalSettings.fogPower ||
	       settings.fogClamp != originalSettings.fogClamp ||
	       settings.directionalFade != originalSettings.directionalFade ||
	       settings.clipDist != originalSettings.clipDist ||
	       settings.lightFadeStart != originalSettings.lightFadeStart ||
	       settings.lightFadeEnd != originalSettings.lightFadeEnd ||
	       settings.directionalXY != originalSettings.directionalXY ||
	       settings.directionalZ != originalSettings.directionalZ ||
	       settings.directionalXPlus != originalSettings.directionalXPlus ||
	       settings.directionalXMinus != originalSettings.directionalXMinus ||
	       settings.directionalYPlus != originalSettings.directionalYPlus ||
	       settings.directionalYMinus != originalSettings.directionalYMinus ||
	       settings.directionalZPlus != originalSettings.directionalZPlus ||
	       settings.directionalZMinus != originalSettings.directionalZMinus ||
	       settings.directionalSpecular != originalSettings.directionalSpecular ||
	       settings.fresnelPower != originalSettings.fresnelPower ||
	       settings.inheritAmbientColor != originalSettings.inheritAmbientColor ||
	       settings.inheritDirectionalColor != originalSettings.inheritDirectionalColor ||
	       settings.inheritFogColor != originalSettings.inheritFogColor ||
	       settings.inheritFogNear != originalSettings.inheritFogNear ||
	       settings.inheritFogFar != originalSettings.inheritFogFar ||
	       settings.inheritDirectionalRotation != originalSettings.inheritDirectionalRotation ||
	       settings.inheritDirectionalFade != originalSettings.inheritDirectionalFade ||
	       settings.inheritClipDistance != originalSettings.inheritClipDistance ||
	       settings.inheritFogPower != originalSettings.inheritFogPower ||
	       settings.inheritFogMax != originalSettings.inheritFogMax ||
	       settings.inheritLightFadeDistances != originalSettings.inheritLightFadeDistances;
}
