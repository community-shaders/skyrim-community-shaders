#include "LightingTemplateWidget.h"

#include "../../I18n/I18n.h"
#include "../EditorWindow.h"
#include "../WeatherUtils.h"
#include "Utils/UI.h"

#define I18N_KEY_PREFIX "cs_editor."

namespace
{
	namespace LightingTemplateTab
	{
		constexpr const char* kBasic = "Basic";
		constexpr const char* kFog = "Fog";
		constexpr const char* kDalc = "DALC";
	}

	namespace LightingTemplateSetting
	{
		constexpr const char* kAmbientColor = "Ambient Color";
		constexpr const char* kDirectionalColor = "Directional Color";
		constexpr const char* kDirectionalXY = "Directional XY";
		constexpr const char* kDirectionalZ = "Directional Z";
		constexpr const char* kDirectionalFade = "Directional Fade";
		constexpr const char* kLightFadeStart = "Light Fade Start";
		constexpr const char* kLightFadeEnd = "Light Fade End";
		constexpr const char* kClipDistance = "Clip Distance";
		constexpr const char* kFogColorNear = "Fog Color Near";
		constexpr const char* kFogColorFar = "Fog Color Far";
		constexpr const char* kFogNear = "Fog Near";
		constexpr const char* kFogFar = "Fog Far";
		constexpr const char* kFogPower = "Fog Power";
		constexpr const char* kFogClamp = "Fog Clamp";
	}
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LightingTemplateWidget::Settings,
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

LightingTemplateWidget::~LightingTemplateWidget()
{
}

void LightingTemplateWidget::DrawWidget()
{
	WeatherUtils::SetCurrentWidget(this);
	if (BeginWidgetWindow(false, true, false, nullptr, "##LightingTemplateSearch")) {
		DrawWidgetHeader("##LightingTemplateSearch", false, true);
		DrawSearchDropdown();
	}
	if (Util::BeginPillTabBar("LightingTemplateSettingsTabs")) {
		const ImGuiTabItemFlags basicFlags = GetTabFlagsForOverride(LightingTemplateTab::kBasic);
		const ImGuiTabItemFlags fogFlags = GetTabFlagsForOverride(LightingTemplateTab::kFog);
		const ImGuiTabItemFlags dalcFlags = GetTabFlagsForOverride(LightingTemplateTab::kDalc);

		if (Util::BeginPillTabItem(T(TKEY("tab_basic"), "Basic"), nullptr, basicFlags)) {
			BeginScrollableContent("##BasicScroll");
			DrawBasicSettings();
			EndScrollableContent();
			Util::EndPillTabItem();
		}

		if (Util::BeginPillTabItem(T(TKEY("tab_fog"), "Fog"), nullptr, fogFlags)) {
			BeginScrollableContent("##FogScroll");
			DrawFogSettings();
			EndScrollableContent();
			Util::EndPillTabItem();
		}

		if (Util::BeginPillTabItem(T(TKEY("tab_dalc"), "DALC"), nullptr, dalcFlags)) {
			BeginScrollableContent("##DALCScroll");
			DrawDALCSettings();
			EndScrollableContent();
			Util::EndPillTabItem();
		}

		Util::EndPillTabBar();
	}
	ImGui::End();
}

void LightingTemplateWidget::DrawBasicSettings()
{
	bool changed = false;
	auto drawMatchedHeader = [&](bool matches, const char* label, auto draw) {
		if (matches && SearchHeader(label))
			draw();
	};

	drawMatchedHeader(MatchesAnySearch({ LightingTemplateSetting::kAmbientColor, LightingTemplateSetting::kDirectionalColor }), T(TKEY("ambient_directional"), "Ambient & Directional"), [&]() {
		changed |= WeatherUtils::DrawColorEdit(LightingTemplateSetting::kAmbientColor, settings.ambient);
		changed |= WeatherUtils::DrawColorEdit(LightingTemplateSetting::kDirectionalColor, settings.directional);
	});

	drawMatchedHeader(MatchesAnySearch({ LightingTemplateSetting::kDirectionalXY, LightingTemplateSetting::kDirectionalZ, LightingTemplateSetting::kDirectionalFade }), T(TKEY("directional_settings"), "Directional Settings"), [&]() {
		changed |= WeatherUtils::DrawSliderFloat(LightingTemplateSetting::kDirectionalXY, settings.directionalXY, 0.0f, 360.0f);
		changed |= WeatherUtils::DrawSliderFloat(LightingTemplateSetting::kDirectionalZ, settings.directionalZ, 0.0f, 360.0f);
		changed |= WeatherUtils::DrawSliderFloat(LightingTemplateSetting::kDirectionalFade, settings.directionalFade, 0.0f, 10.0f);
	});

	drawMatchedHeader(MatchesAnySearch({ LightingTemplateSetting::kLightFadeStart, LightingTemplateSetting::kLightFadeEnd }), T(TKEY("light_fade"), "Light Fade"), [&]() {
		changed |= WeatherUtils::DrawSliderFloat(LightingTemplateSetting::kLightFadeStart, settings.lightFadeStart, 0.0f, 163840.0f);
		changed |= WeatherUtils::DrawSliderFloat(LightingTemplateSetting::kLightFadeEnd, settings.lightFadeEnd, 0.0f, 163840.0f);
	});

	drawMatchedHeader(MatchesAnySearch({ LightingTemplateSetting::kClipDistance }), T(TKEY("other"), "Other"), [&]() {
		changed |= WeatherUtils::DrawSliderFloat(LightingTemplateSetting::kClipDistance, settings.clipDist, 0.0f, 163840.0f);
	});

	AutoApply(changed);
}

void LightingTemplateWidget::DrawFogSettings()
{
	bool changed = false;

	DrawSearchSectionIfMatches(LightingTemplateSetting::kFogColorNear, [&](const char*) {
		changed |= WeatherUtils::DrawColorEdit(LightingTemplateSetting::kFogColorNear, settings.fogColorNear);
	});

	DrawSearchSectionIfMatches(LightingTemplateSetting::kFogColorFar, [&](const char*) {
		changed |= WeatherUtils::DrawColorEdit(LightingTemplateSetting::kFogColorFar, settings.fogColorFar);
	});

	DrawSearchSectionIfMatches(LightingTemplateSetting::kFogNear, [&](const char*) {
		changed |= WeatherUtils::DrawSliderFloat(LightingTemplateSetting::kFogNear, settings.fogNear, 0.0f, 163840.0f);
	});

	DrawSearchSectionIfMatches(LightingTemplateSetting::kFogFar, [&](const char*) {
		changed |= WeatherUtils::DrawSliderFloat(LightingTemplateSetting::kFogFar, settings.fogFar, 0.0f, 163840.0f);
	});

	DrawSearchSectionIfMatches(LightingTemplateSetting::kFogPower, [&](const char*) {
		changed |= WeatherUtils::DrawSliderFloat(LightingTemplateSetting::kFogPower, settings.fogPower, 0.0f, 10.0f);
	});

	DrawSearchSectionIfMatches(LightingTemplateSetting::kFogClamp, [&](const char*) {
		changed |= WeatherUtils::DrawSliderFloat(LightingTemplateSetting::kFogClamp, settings.fogClamp, 0.0f, 1.0f);
	});

	AutoApply(changed);
}

void LightingTemplateWidget::DrawDALCSettings()
{
	AutoApply(DrawDALCControls(*this, settings.dalc));
}

#undef I18N_KEY_PREFIX

void LightingTemplateWidget::ApplyChanges()
{
	SetLightingTemplateValues();
}

void LightingTemplateWidget::SetLightingTemplateValues()
{
	auto& data = lightingTemplate->data;

	Float3ToColor(settings.ambient, data.ambient);
	Float3ToColor(settings.directional, data.directional);
	Float3ToColor(settings.fogColorNear, data.fogColorNear);
	Float3ToColor(settings.fogColorFar, data.fogColorFar);

	data.fogNear = settings.fogNear;
	data.fogFar = settings.fogFar;
	data.directionalXY = static_cast<std::uint32_t>(settings.directionalXY);
	data.directionalZ = static_cast<std::uint32_t>(settings.directionalZ);
	data.directionalFade = settings.directionalFade;
	data.clipDist = settings.clipDist;
	data.fogPower = settings.fogPower;
	data.fogClamp = settings.fogClamp;
	data.lightFadeStart = settings.lightFadeStart;
	data.lightFadeEnd = settings.lightFadeEnd;

	settings.dalc.Write(lightingTemplate->directionalAmbientLightingColors);
}

void LightingTemplateWidget::LoadLightingTemplateValues()
{
	if (!lightingTemplate)
		return;

	auto& data = lightingTemplate->data;

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

	settings.dalc.Read(lightingTemplate->directionalAmbientLightingColors);
}

void LightingTemplateWidget::LoadFromGameSettings()
{
	LoadLightingTemplateValues();
}

void LightingTemplateWidget::LoadSettings()
{
	LoadSavedSettings();
}

void LightingTemplateWidget::SaveSettings()
{
	SaveSettingsToJson();
}

std::vector<Widget::SearchResult> LightingTemplateWidget::CollectSearchableSettings() const
{
	return WeatherUtils::LabelSearchResults({
		{ LightingTemplateTab::kBasic, { LightingTemplateSetting::kAmbientColor, LightingTemplateSetting::kDirectionalColor,
										   LightingTemplateSetting::kDirectionalXY, LightingTemplateSetting::kDirectionalZ, LightingTemplateSetting::kDirectionalFade,
										   LightingTemplateSetting::kLightFadeStart, LightingTemplateSetting::kLightFadeEnd, LightingTemplateSetting::kClipDistance } },
		{ LightingTemplateTab::kFog, { LightingTemplateSetting::kFogColorNear, LightingTemplateSetting::kFogColorFar,
										 LightingTemplateSetting::kFogNear, LightingTemplateSetting::kFogFar, LightingTemplateSetting::kFogPower, LightingTemplateSetting::kFogClamp } },
		{ LightingTemplateTab::kDalc, { DALCSetting::kAll.begin(), DALCSetting::kAll.end() } },
	});
}
