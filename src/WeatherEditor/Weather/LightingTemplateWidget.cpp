#include "LightingTemplateWidget.h"

#include "../EditorWindow.h"
#include "../WeatherUtils.h"

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
		const ImGuiTabItemFlags basicFlags = GetTabFlagsForOverride("Basic");
		const ImGuiTabItemFlags fogFlags = GetTabFlagsForOverride("Fog");
		const ImGuiTabItemFlags dalcFlags = GetTabFlagsForOverride("DALC");

		if (ImGui::BeginTabItem("Basic", nullptr, basicFlags)) {
			BeginScrollableContent("##BasicScroll");
			DrawBasicSettings();
			EndScrollableContent();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Fog", nullptr, fogFlags)) {
			BeginScrollableContent("##FogScroll");
			DrawFogSettings();
			EndScrollableContent();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("DALC", nullptr, dalcFlags)) {
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

	if (MatchesSearch("Ambient Color") || MatchesSearch("Directional Color")) {
		if (ImGui::CollapsingHeader("Ambient & Directional", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Spacing();
			if (MatchesSearch("Ambient Color") && WeatherUtils::DrawColorEdit("Ambient Color", settings.ambient))
				changed = true;
			ImGui::Spacing();
			if (MatchesSearch("Directional Color") && WeatherUtils::DrawColorEdit("Directional Color", settings.directional))
				changed = true;
			ImGui::Spacing();
		}
	}

	if (MatchesSearch("Directional XY") || MatchesSearch("Directional Z") || MatchesSearch("Directional Fade")) {
		if (ImGui::CollapsingHeader("Directional Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Spacing();
			if (MatchesSearch("Directional XY") && WeatherUtils::DrawSliderFloat("Directional XY", settings.directionalXY, 0.0f, 360.0f))
				changed = true;
			ImGui::Spacing();
			if (MatchesSearch("Directional Z") && WeatherUtils::DrawSliderFloat("Directional Z", settings.directionalZ, 0.0f, 360.0f))
				changed = true;
			ImGui::Spacing();
			if (MatchesSearch("Directional Fade") && WeatherUtils::DrawSliderFloat("Directional Fade", settings.directionalFade, 0.0f, 10.0f))
				changed = true;
			ImGui::Spacing();
		}
	}

	if (MatchesSearch("Light Fade Start") || MatchesSearch("Light Fade End")) {
		if (ImGui::CollapsingHeader("Light Fade", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Spacing();
			if (MatchesSearch("Light Fade Start") && WeatherUtils::DrawSliderFloat("Light Fade Start", settings.lightFadeStart, 0.0f, 163840.0f))
				changed = true;
			ImGui::Spacing();
			if (MatchesSearch("Light Fade End") && WeatherUtils::DrawSliderFloat("Light Fade End", settings.lightFadeEnd, 0.0f, 163840.0f))
				changed = true;
			ImGui::Spacing();
		}
	}

	if (MatchesSearch("Clip Distance")) {
		if (ImGui::CollapsingHeader("Other", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Spacing();
			if (WeatherUtils::DrawSliderFloat("Clip Distance", settings.clipDist, 0.0f, 163840.0f))
				changed = true;
			ImGui::Spacing();
		}
	}

	if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
		ApplyChanges();
	}
}

void LightingTemplateWidget::DrawFogSettings()
{
	bool changed = false;

	if (MatchesSearch("Fog Color Near")) {
		ImGui::Spacing();
		if (WeatherUtils::DrawColorEdit("Fog Color Near", settings.fogColorNear))
			changed = true;
	}

	if (MatchesSearch("Fog Color Far")) {
		ImGui::Spacing();
		if (WeatherUtils::DrawColorEdit("Fog Color Far", settings.fogColorFar))
			changed = true;
	}

	if (MatchesSearch("Fog Near")) {
		ImGui::Spacing();
		if (WeatherUtils::DrawSliderFloat("Fog Near", settings.fogNear, 0.0f, 163840.0f))
			changed = true;
	}

	if (MatchesSearch("Fog Far")) {
		ImGui::Spacing();
		if (WeatherUtils::DrawSliderFloat("Fog Far", settings.fogFar, 0.0f, 163840.0f))
			changed = true;
	}

	if (MatchesSearch("Fog Power")) {
		ImGui::Spacing();
		if (WeatherUtils::DrawSliderFloat("Fog Power", settings.fogPower, 0.0f, 10.0f))
			changed = true;
	}

	if (MatchesSearch("Fog Clamp")) {
		ImGui::Spacing();
		if (WeatherUtils::DrawSliderFloat("Fog Clamp", settings.fogClamp, 0.0f, 1.0f))
			changed = true;
	}

	if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
		ApplyChanges();
	}
}

void LightingTemplateWidget::DrawDALCSettings()
{
	bool changed = false;

	if (MatchesSearch("Specular") || MatchesSearch("Fresnel Power")) {
		ImGui::SeparatorText("Directional Ambient Lighting (DALC)");
		if (MatchesSearch("Specular") && WeatherUtils::DrawColorEdit("Specular", settings.dalc.specular))
			changed = true;
		if (MatchesSearch("Fresnel Power") && WeatherUtils::DrawSliderFloat("Fresnel Power", settings.dalc.fresnelPower, 0.0f, 10.0f))
			changed = true;
	}

	if (MatchesSearch("X+ (Right)") || MatchesSearch("X- (Left)") || MatchesSearch("Y+ (Front)") ||
		MatchesSearch("Y- (Back)") || MatchesSearch("Z+ (Up)") || MatchesSearch("Z- (Down)")) {
		ImGui::SeparatorText("Directional Colors");
		if (MatchesSearch("X+ (Right)") && WeatherUtils::DrawColorEdit("X+ (Right)", settings.dalc.directional[0].max))
			changed = true;
		if (MatchesSearch("X- (Left)") && WeatherUtils::DrawColorEdit("X- (Left)", settings.dalc.directional[0].min))
			changed = true;
		if (MatchesSearch("Y+ (Front)") && WeatherUtils::DrawColorEdit("Y+ (Front)", settings.dalc.directional[1].max))
			changed = true;
		if (MatchesSearch("Y- (Back)") && WeatherUtils::DrawColorEdit("Y- (Back)", settings.dalc.directional[1].min))
			changed = true;
		if (MatchesSearch("Z+ (Up)") && WeatherUtils::DrawColorEdit("Z+ (Up)", settings.dalc.directional[2].max))
			changed = true;
		if (MatchesSearch("Z- (Down)") && WeatherUtils::DrawColorEdit("Z- (Down)", settings.dalc.directional[2].min))
			changed = true;
	}

	if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
		ApplyChanges();
	}
}

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
		{ "Basic", { "Ambient Color", "Directional Color",
					   "Directional XY", "Directional Z", "Directional Fade",
					   "Light Fade Start", "Light Fade End", "Clip Distance" } },
		{ "Fog", { "Fog Color Near", "Fog Color Far",
					 "Fog Near", "Fog Far", "Fog Power", "Fog Clamp" } },
		{ "DALC", { "Specular", "Fresnel Power",
					  "X+ (Right)", "X- (Left)", "Y+ (Front)", "Y- (Back)", "Z+ (Up)", "Z- (Down)" } },
	};

	std::vector<SearchResult> results;
	for (const auto& [tab, names] : entries) {
		for (const auto& name : names) {
			results.push_back({ name, tab, name });
		}
	}
	return results;
}
