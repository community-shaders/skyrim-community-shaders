#include "LightingTemplateWidget.h"

#include "../../I18n/I18n.h"
#include "../EditorWindow.h"
#include "../WeatherUtils.h"

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
		constexpr const char* kSpecular = "Specular";
		constexpr const char* kFresnelPower = "Fresnel Power";
		constexpr const char* kXPlus = "X+ (Right)";
		constexpr const char* kXMinus = "X- (Left)";
		constexpr const char* kYPlus = "Y+ (Front)";
		constexpr const char* kYMinus = "Y- (Back)";
		constexpr const char* kZPlus = "Z+ (Up)";
		constexpr const char* kZMinus = "Z- (Down)";
	}
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LightingTemplateWidget::DirectionalColor, max, min)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LightingTemplateWidget::DALC, specular, fresnelPower, directional)
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
	if (BeginWidgetWindow()) {
		DrawWidgetHeader("##LightingTemplateSearch", false, true);
		DrawSearchDropdown();
	}
	if (ImGui::BeginTabBar("LightingTemplateSettingsTabs", ImGuiTabBarFlags_None)) {
		const ImGuiTabItemFlags basicFlags = GetTabFlagsForOverride(LightingTemplateTab::kBasic);
		const ImGuiTabItemFlags fogFlags = GetTabFlagsForOverride(LightingTemplateTab::kFog);
		const ImGuiTabItemFlags dalcFlags = GetTabFlagsForOverride(LightingTemplateTab::kDalc);

		if (ImGui::BeginTabItem(T(TKEY("tab_basic"), "Basic"), nullptr, basicFlags)) {
			BeginScrollableContent("##BasicScroll");
			DrawBasicSettings();
			EndScrollableContent();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem(T(TKEY("tab_fog"), "Fog"), nullptr, fogFlags)) {
			BeginScrollableContent("##FogScroll");
			DrawFogSettings();
			EndScrollableContent();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem(T(TKEY("tab_dalc"), "DALC"), nullptr, dalcFlags)) {
			BeginScrollableContent("##DALCScroll");
			DrawDALCSettings();
			EndScrollableContent();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}
	ImGui::End();
}

void LightingTemplateWidget::DrawBasicSettings()
{
	bool changed = false;
	auto drawMatchedHeader = [&](bool matches, const char* label, auto draw) {
		if (!matches)
			return;
		if (ShouldOpenSearchSection())
			ImGui::SetNextItemOpen(true, ImGuiCond_Always);
		if (ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Spacing();
			draw();
			ImGui::Spacing();
		}
	};

	drawMatchedHeader(MatchesAnySearch({ LightingTemplateSetting::kAmbientColor, LightingTemplateSetting::kDirectionalColor }), T(TKEY("ambient_directional"), "Ambient & Directional"), [&]() {
		changed |= WeatherUtils::DrawColorEdit(LightingTemplateSetting::kAmbientColor, settings.ambient);
		ImGui::Spacing();
		changed |= WeatherUtils::DrawColorEdit(LightingTemplateSetting::kDirectionalColor, settings.directional);
	});

	drawMatchedHeader(MatchesAnySearch({ LightingTemplateSetting::kDirectionalXY, LightingTemplateSetting::kDirectionalZ, LightingTemplateSetting::kDirectionalFade }), T(TKEY("directional_settings"), "Directional Settings"), [&]() {
		changed |= WeatherUtils::DrawSliderFloat(LightingTemplateSetting::kDirectionalXY, settings.directionalXY, 0.0f, 360.0f);
		ImGui::Spacing();
		changed |= WeatherUtils::DrawSliderFloat(LightingTemplateSetting::kDirectionalZ, settings.directionalZ, 0.0f, 360.0f);
		ImGui::Spacing();
		changed |= WeatherUtils::DrawSliderFloat(LightingTemplateSetting::kDirectionalFade, settings.directionalFade, 0.0f, 10.0f);
	});

	drawMatchedHeader(MatchesAnySearch({ LightingTemplateSetting::kLightFadeStart, LightingTemplateSetting::kLightFadeEnd }), T(TKEY("light_fade"), "Light Fade"), [&]() {
		changed |= WeatherUtils::DrawSliderFloat(LightingTemplateSetting::kLightFadeStart, settings.lightFadeStart, 0.0f, 163840.0f);
		ImGui::Spacing();
		changed |= WeatherUtils::DrawSliderFloat(LightingTemplateSetting::kLightFadeEnd, settings.lightFadeEnd, 0.0f, 163840.0f);
	});

	drawMatchedHeader(MatchesAnySearch({ LightingTemplateSetting::kClipDistance }), T(TKEY("other"), "Other"), [&]() {
		changed |= WeatherUtils::DrawSliderFloat(LightingTemplateSetting::kClipDistance, settings.clipDist, 0.0f, 163840.0f);
	});

	if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
		ApplyChanges();
	}
}

void LightingTemplateWidget::DrawFogSettings()
{
	bool changed = false;

	DrawSearchSectionIfMatches(LightingTemplateSetting::kFogColorNear, [&](const char*) {
		ImGui::Spacing();
		changed |= WeatherUtils::DrawColorEdit(LightingTemplateSetting::kFogColorNear, settings.fogColorNear);
	});

	DrawSearchSectionIfMatches(LightingTemplateSetting::kFogColorFar, [&](const char*) {
		ImGui::Spacing();
		changed |= WeatherUtils::DrawColorEdit(LightingTemplateSetting::kFogColorFar, settings.fogColorFar);
	});

	DrawSearchSectionIfMatches(LightingTemplateSetting::kFogNear, [&](const char*) {
		ImGui::Spacing();
		changed |= WeatherUtils::DrawSliderFloat(LightingTemplateSetting::kFogNear, settings.fogNear, 0.0f, 163840.0f);
	});

	DrawSearchSectionIfMatches(LightingTemplateSetting::kFogFar, [&](const char*) {
		ImGui::Spacing();
		changed |= WeatherUtils::DrawSliderFloat(LightingTemplateSetting::kFogFar, settings.fogFar, 0.0f, 163840.0f);
	});

	DrawSearchSectionIfMatches(LightingTemplateSetting::kFogPower, [&](const char*) {
		ImGui::Spacing();
		changed |= WeatherUtils::DrawSliderFloat(LightingTemplateSetting::kFogPower, settings.fogPower, 0.0f, 10.0f);
	});

	DrawSearchSectionIfMatches(LightingTemplateSetting::kFogClamp, [&](const char*) {
		ImGui::Spacing();
		changed |= WeatherUtils::DrawSliderFloat(LightingTemplateSetting::kFogClamp, settings.fogClamp, 0.0f, 1.0f);
	});

	if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
		ApplyChanges();
	}
}

void LightingTemplateWidget::DrawDALCSettings()
{
	bool changed = false;

	if (MatchesAnySearch({ LightingTemplateSetting::kSpecular, LightingTemplateSetting::kFresnelPower })) {
		ImGui::SeparatorText(T(TKEY("dalc_header"), "Directional Ambient Lighting (DALC)"));
		changed |= WeatherUtils::DrawColorEdit(LightingTemplateSetting::kSpecular, settings.dalc.specular);
		changed |= WeatherUtils::DrawSliderFloat(LightingTemplateSetting::kFresnelPower, settings.dalc.fresnelPower, 0.0f, 10.0f);
	}

	if (MatchesAnySearch({ LightingTemplateSetting::kXPlus, LightingTemplateSetting::kXMinus, LightingTemplateSetting::kYPlus,
			LightingTemplateSetting::kYMinus, LightingTemplateSetting::kZPlus, LightingTemplateSetting::kZMinus })) {
		ImGui::SeparatorText(T(TKEY("directional_colors"), "Directional Colors"));
		changed |= WeatherUtils::DrawColorEdit(LightingTemplateSetting::kXPlus, settings.dalc.directional[0].max);
		changed |= WeatherUtils::DrawColorEdit(LightingTemplateSetting::kXMinus, settings.dalc.directional[0].min);
		changed |= WeatherUtils::DrawColorEdit(LightingTemplateSetting::kYPlus, settings.dalc.directional[1].max);
		changed |= WeatherUtils::DrawColorEdit(LightingTemplateSetting::kYMinus, settings.dalc.directional[1].min);
		changed |= WeatherUtils::DrawColorEdit(LightingTemplateSetting::kZPlus, settings.dalc.directional[2].max);
		changed |= WeatherUtils::DrawColorEdit(LightingTemplateSetting::kZMinus, settings.dalc.directional[2].min);
	}

	if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
		ApplyChanges();
	}
}

#undef I18N_KEY_PREFIX

void LightingTemplateWidget::ApplyChanges()
{
	SetLightingTemplateValues();
}

void LightingTemplateWidget::RevertChanges()
{
	settings = vanillaSettings;
	ApplyChanges();
}

void LightingTemplateWidget::SetLightingTemplateValues()
{
	auto& data = lightingTemplate->data;
	auto& dalc = lightingTemplate->directionalAmbientLightingColors;

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

	dalc.fresnelPower = settings.dalc.fresnelPower;
	Float3ToColor(settings.dalc.specular, dalc.specular);

	Float3ToColor(settings.dalc.directional[0].max, dalc.directional.x.max);
	Float3ToColor(settings.dalc.directional[0].min, dalc.directional.x.min);

	Float3ToColor(settings.dalc.directional[1].max, dalc.directional.y.max);
	Float3ToColor(settings.dalc.directional[1].min, dalc.directional.y.min);

	Float3ToColor(settings.dalc.directional[2].max, dalc.directional.z.max);
	Float3ToColor(settings.dalc.directional[2].min, dalc.directional.z.min);
}

void LightingTemplateWidget::LoadLightingTemplateValues()
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

void LightingTemplateWidget::LoadFromGameSettings()
{
	LoadLightingTemplateValues();
}

void LightingTemplateWidget::LoadSettings()
{
	if (!js.empty()) {
		settings = js;
	} else {
		settings = vanillaSettings;
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

std::vector<Widget::SearchResult> LightingTemplateWidget::CollectSearchableSettings() const
{
	const std::vector<std::pair<std::string, std::vector<std::string>>> entries = {
		{ LightingTemplateTab::kBasic, { LightingTemplateSetting::kAmbientColor, LightingTemplateSetting::kDirectionalColor,
										   LightingTemplateSetting::kDirectionalXY, LightingTemplateSetting::kDirectionalZ, LightingTemplateSetting::kDirectionalFade,
										   LightingTemplateSetting::kLightFadeStart, LightingTemplateSetting::kLightFadeEnd, LightingTemplateSetting::kClipDistance } },
		{ LightingTemplateTab::kFog, { LightingTemplateSetting::kFogColorNear, LightingTemplateSetting::kFogColorFar,
										 LightingTemplateSetting::kFogNear, LightingTemplateSetting::kFogFar, LightingTemplateSetting::kFogPower, LightingTemplateSetting::kFogClamp } },
		{ LightingTemplateTab::kDalc, { LightingTemplateSetting::kSpecular, LightingTemplateSetting::kFresnelPower,
										  LightingTemplateSetting::kXPlus, LightingTemplateSetting::kXMinus, LightingTemplateSetting::kYPlus, LightingTemplateSetting::kYMinus,
										  LightingTemplateSetting::kZPlus, LightingTemplateSetting::kZMinus } },
	};

	std::vector<SearchResult> results;
	for (const auto& [tab, names] : entries) {
		for (const auto& name : names) {
			results.push_back({ WeatherUtils::TranslateControlLabel(name), tab, name });
		}
	}
	return results;
}
