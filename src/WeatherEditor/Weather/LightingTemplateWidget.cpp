#include "LightingTemplateWidget.h"

#include "../EditorWindow.h"
#include "../WeatherUtils.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LightingTemplateWidget::DirectionalColor, max, min)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LightingTemplateWidget::DALC, specular, fresnelPower, directional)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(LightingTemplateWidget::Settings,
	ambient,
	directional,
	fogColorNear,
	fogColorFar,
	fogNear,
	fogFar,
	directionalXY,
	directionalZ,
	directionalFade,
	clipDist,
	fogPower,
	fogClamp,
	lightFadeStart,
	lightFadeEnd,
	dalc)

void LightingTemplateWidget::DrawWidget()
{
	ImGui::SetNextWindowSizeConstraints(ImVec2(600, 0), ImVec2(FLT_MAX, FLT_MAX));
	if (!ImGui::Begin(GetEditorID().c_str(), &open, ImGuiWindowFlags_NoSavedSettings | kStickyHeaderFlags)) {
		ImGui::End();
		return;
	}
	WeatherUtils::SetCurrentWidget(this);
	DrawWidgetHeader("##LightingTemplateSearch", false, true);

	bool changed = false;

	if (ImGui::BeginTabBar("LightingTemplateSettingsTabs")) {
		if (ImGui::BeginTabItem("Basic")) {
			BeginScrollableContent("##BasicScroll");
			changed |= DrawBasicSettings();
			EndScrollableContent();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Fog")) {
			BeginScrollableContent("##FogScroll");
			changed |= DrawFogSettings();
			EndScrollableContent();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("DALC")) {
			BeginScrollableContent("##DALCScroll");
			changed |= DrawDALCSettings();
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

bool LightingTemplateWidget::DrawBasicSettings()
{
	bool changed = false;

	ImGui::SeparatorText("Ambient & Directional");
	if (WeatherUtils::DrawColorEdit("Ambient Color", settings.ambient))
		changed = true;
	if (WeatherUtils::DrawColorEdit("Directional Color", settings.directional))
		changed = true;

	ImGui::SeparatorText("Directional Settings");
	if (WeatherUtils::DrawSliderFloat("Directional XY", settings.directionalXY))
		changed = true;
	if (WeatherUtils::DrawSliderFloat("Directional Z", settings.directionalZ))
		changed = true;
	if (WeatherUtils::DrawSliderFloat("Directional Fade", settings.directionalFade))
		changed = true;

	ImGui::SeparatorText("Light Fade");
	if (WeatherUtils::DrawSliderFloat("Light Fade Start", settings.lightFadeStart))
		changed = true;
	if (WeatherUtils::DrawSliderFloat("Light Fade End", settings.lightFadeEnd))
		changed = true;

	ImGui::SeparatorText("Other");
	if (WeatherUtils::DrawSliderFloat("Clip Distance", settings.clipDist))
		changed = true;

	return changed;
}

bool LightingTemplateWidget::DrawFogSettings()
{
	bool changed = false;

	ImGui::SeparatorText("Fog Colors");
	if (WeatherUtils::DrawColorEdit("Fog Color Near", settings.fogColorNear))
		changed = true;
	if (WeatherUtils::DrawColorEdit("Fog Color Far", settings.fogColorFar))
		changed = true;

	ImGui::SeparatorText("Fog Distance");
	if (WeatherUtils::DrawSliderFloat("Fog Near", settings.fogNear))
		changed = true;
	if (WeatherUtils::DrawSliderFloat("Fog Far", settings.fogFar))
		changed = true;

	ImGui::SeparatorText("Fog Properties");
	if (WeatherUtils::DrawSliderFloat("Fog Power", settings.fogPower))
		changed = true;
	if (WeatherUtils::DrawSliderFloat("Fog Clamp", settings.fogClamp))
		changed = true;

	return changed;
}

bool LightingTemplateWidget::DrawDALCSettings()
{
	bool changed = false;

	ImGui::SeparatorText("Basic DALC");
	if (WeatherUtils::DrawColorEdit("Specular", settings.dalc.specular))
		changed = true;
	if (WeatherUtils::DrawSliderFloat("Fresnel Power", settings.dalc.fresnelPower))
		changed = true;

	ImGui::SeparatorText("Directional Colors (Time of Day)");
	if (TOD::BeginTODTable("DALCDirectionalTable")) {
		TOD::RenderTODHeader();
		TOD::DrawTODSeparator();

		// Prepare arrays for TOD rendering (map X,Y,Z to Sunrise,Day,Sunset,Night)
		float3 maxColors[4];
		float3 minColors[4];

		// Map X (index 0) to Sunrise and Day
		maxColors[TOD::Sunrise] = settings.dalc.directional[0].max;
		maxColors[TOD::Day] = settings.dalc.directional[0].max;
		minColors[TOD::Sunrise] = settings.dalc.directional[0].min;
		minColors[TOD::Day] = settings.dalc.directional[0].min;

		// Map Y (index 1) to Sunset
		maxColors[TOD::Sunset] = settings.dalc.directional[1].max;
		minColors[TOD::Sunset] = settings.dalc.directional[1].min;

		// Map Z (index 2) to Night
		maxColors[TOD::Night] = settings.dalc.directional[2].max;
		minColors[TOD::Night] = settings.dalc.directional[2].min;

		if (TOD::DrawTODColorRow("Positive Direction (+)", maxColors)) {
			// Sunrise and Day both map to directional[0] - detect which column was actually edited
			bool sunriseEdited = !(maxColors[TOD::Sunrise] == settings.dalc.directional[0].max);
			settings.dalc.directional[0].max = sunriseEdited ? maxColors[TOD::Sunrise] : maxColors[TOD::Day];
			settings.dalc.directional[1].max = maxColors[TOD::Sunset];
			settings.dalc.directional[2].max = maxColors[TOD::Night];
			changed = true;
		}

		if (TOD::DrawTODColorRow("Negative Direction (-)", minColors)) {
			bool sunriseEdited = !(minColors[TOD::Sunrise] == settings.dalc.directional[0].min);
			settings.dalc.directional[0].min = sunriseEdited ? minColors[TOD::Sunrise] : minColors[TOD::Day];
			settings.dalc.directional[1].min = minColors[TOD::Sunset];
			settings.dalc.directional[2].min = minColors[TOD::Night];
			changed = true;
		}

		TOD::EndTODTable();
	}

	return changed;
}

void LightingTemplateWidget::ApplyChanges()
{
	if (!lightingTemplate)
		return;

	auto& data = lightingTemplate->data;
	auto& dalc = lightingTemplate->directionalAmbientLightingColors;

	Float3ToColor(settings.ambient, data.ambient);
	Float3ToColor(settings.directional, data.directional);
	Float3ToColor(settings.fogColorNear, data.fogColorNear);
	Float3ToColor(settings.fogColorFar, data.fogColorFar);

	data.fogNear = settings.fogNear;
	data.fogFar = settings.fogFar;
	data.directionalXY = static_cast<std::uint32_t>(std::clamp(std::round(settings.directionalXY), 0.0f, static_cast<float>(std::numeric_limits<std::uint32_t>::max())));
	data.directionalZ = static_cast<std::uint32_t>(std::clamp(std::round(settings.directionalZ), 0.0f, static_cast<float>(std::numeric_limits<std::uint32_t>::max())));
	data.directionalFade = settings.directionalFade;
	data.clipDist = settings.clipDist;
	data.fogPower = settings.fogPower;
	data.fogClamp = settings.fogClamp;
	data.lightFadeStart = settings.lightFadeStart;
	data.lightFadeEnd = settings.lightFadeEnd;

	dalc.fresnelPower = settings.dalc.fresnelPower;
	Float3ToColor(settings.dalc.specular, dalc.specular);

	Float3ToColor(settings.dalc.directional[0].max, dalc.directional.x.max);
	Float3ToColor(settings.dalc.directional[0].min, dalc.directional.x.min);
	Float3ToColor(settings.dalc.directional[1].max, dalc.directional.y.max);
	Float3ToColor(settings.dalc.directional[1].min, dalc.directional.y.min);
	Float3ToColor(settings.dalc.directional[2].max, dalc.directional.z.max);
	Float3ToColor(settings.dalc.directional[2].min, dalc.directional.z.min);
}

void LightingTemplateWidget::RevertChanges()
{
	settings = originalSettings;
	ApplyChanges();
}

void LightingTemplateWidget::LoadFromGameSettings()
{
	if (!lightingTemplate)
		return;

	auto& data = lightingTemplate->data;
	auto& dalc = lightingTemplate->directionalAmbientLightingColors;

	ColorToFloat3(data.ambient, settings.ambient);
	ColorToFloat3(data.directional, settings.directional);
	ColorToFloat3(data.fogColorNear, settings.fogColorNear);
	ColorToFloat3(data.fogColorFar, settings.fogColorFar);

	settings.fogNear = data.fogNear;
	settings.fogFar = data.fogFar;
	settings.directionalXY = static_cast<float>(data.directionalXY);
	settings.directionalZ = static_cast<float>(data.directionalZ);
	settings.directionalFade = data.directionalFade;
	settings.clipDist = data.clipDist;
	settings.fogPower = data.fogPower;
	settings.fogClamp = data.fogClamp;
	settings.lightFadeStart = data.lightFadeStart;
	settings.lightFadeEnd = data.lightFadeEnd;

	settings.dalc.fresnelPower = dalc.fresnelPower;
	ColorToFloat3(dalc.specular, settings.dalc.specular);

	ColorToFloat3(dalc.directional.x.max, settings.dalc.directional[0].max);
	ColorToFloat3(dalc.directional.x.min, settings.dalc.directional[0].min);
	ColorToFloat3(dalc.directional.y.max, settings.dalc.directional[1].max);
	ColorToFloat3(dalc.directional.y.min, settings.dalc.directional[1].min);
	ColorToFloat3(dalc.directional.z.max, settings.dalc.directional[2].max);
	ColorToFloat3(dalc.directional.z.min, settings.dalc.directional[2].min);
}

static void ValidateSettings(LightingTemplateWidget::Settings& s, const LightingTemplateWidget::Settings& vanilla)
{
	auto safeF = [](float v, float fallback) { return std::isfinite(v) ? v : fallback; };
	auto safeF3 = [](float3 v, float3 fallback) {
		return (std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z)) ? v : fallback;
	};
	s.ambient         = safeF3(s.ambient,         vanilla.ambient);
	s.directional     = safeF3(s.directional,     vanilla.directional);
	s.fogColorNear    = safeF3(s.fogColorNear,    vanilla.fogColorNear);
	s.fogColorFar     = safeF3(s.fogColorFar,     vanilla.fogColorFar);
	s.fogNear         = safeF(s.fogNear,           vanilla.fogNear);
	s.fogFar          = safeF(s.fogFar,            vanilla.fogFar);
	s.directionalXY   = safeF(s.directionalXY,     vanilla.directionalXY);
	s.directionalZ    = safeF(s.directionalZ,      vanilla.directionalZ);
	s.directionalFade = safeF(s.directionalFade,   vanilla.directionalFade);
	s.clipDist        = safeF(s.clipDist,           vanilla.clipDist);
	s.fogPower        = safeF(s.fogPower,           vanilla.fogPower);
	s.fogClamp        = safeF(s.fogClamp,           vanilla.fogClamp);
	s.lightFadeStart  = safeF(s.lightFadeStart,     vanilla.lightFadeStart);
	s.lightFadeEnd    = safeF(s.lightFadeEnd,       vanilla.lightFadeEnd);
	s.dalc.fresnelPower = safeF(s.dalc.fresnelPower, vanilla.dalc.fresnelPower);
	s.dalc.specular     = safeF3(s.dalc.specular,    vanilla.dalc.specular);
	for (int i = 0; i < 3; i++) {
		s.dalc.directional[i].max = safeF3(s.dalc.directional[i].max, vanilla.dalc.directional[i].max);
		s.dalc.directional[i].min = safeF3(s.dalc.directional[i].min, vanilla.dalc.directional[i].min);
	}
}

void LightingTemplateWidget::LoadSettings()
{
	settings = vanillaSettings;
	if (!js.empty()) {
		try {
			nlohmann::json merged = vanillaSettings;
			merged.merge_patch(js);
			settings = merged.get<Settings>();
			ValidateSettings(settings, vanillaSettings);
		} catch (const nlohmann::json::exception& e) {
			logger::error("LightingTemplate {}: Failed to load from JSON: {}", GetEditorID(), e.what());
			settings = vanillaSettings;
		}
	}
	originalSettings = settings;
	ApplyChanges();
}

void LightingTemplateWidget::SaveSettings()
{
	js = settings;
	originalSettings = settings;
}

bool LightingTemplateWidget::HasUnsavedChanges() const
{
	return !(settings == originalSettings);
}
