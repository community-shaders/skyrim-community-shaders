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
	}
	if (ImGui::BeginTabBar("LightingTemplateSettingsTabs", ImGuiTabBarFlags_None)) {
		if (ImGui::BeginTabItem("Basic")) {
			BeginScrollableContent("##BasicScroll");
			DrawBasicSettings();
			EndScrollableContent();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Fog")) {
			BeginScrollableContent("##FogScroll");
			DrawFogSettings();
			EndScrollableContent();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("DALC")) {
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

	if (ImGui::CollapsingHeader("Ambient & Directional", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Spacing();
		if (MatchesSearch("Ambient Color") && WeatherUtils::DrawColorEdit("Ambient Color", settings.ambient))
			changed = true;
		if (MatchesSearch("Ambient Color"))
			ImGui::Spacing();
		if (MatchesSearch("Directional Color") && WeatherUtils::DrawColorEdit("Directional Color", settings.directional))
			changed = true;
		if (MatchesSearch("Directional Color"))
			ImGui::Spacing();
	}

	if (ImGui::CollapsingHeader("Directional Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Spacing();
		if (MatchesSearch("Directional XY") && WeatherUtils::DrawSliderFloat("Directional XY", settings.directionalXY, 0.0f, 360.0f))
			changed = true;
		if (MatchesSearch("Directional XY"))
			ImGui::Spacing();
		if (MatchesSearch("Directional Z") && WeatherUtils::DrawSliderFloat("Directional Z", settings.directionalZ, 0.0f, 360.0f))
			changed = true;
		if (MatchesSearch("Directional Z"))
			ImGui::Spacing();
		if (MatchesSearch("Directional Fade") && WeatherUtils::DrawSliderFloat("Directional Fade", settings.directionalFade, 0.0f, 10.0f))
			changed = true;
		if (MatchesSearch("Directional Fade"))
			ImGui::Spacing();
	}

	if (ImGui::CollapsingHeader("Light Fade", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Spacing();
		if (MatchesSearch("Light Fade Start") && WeatherUtils::DrawSliderFloat("Light Fade Start", settings.lightFadeStart, 0.0f, 163840.0f))
			changed = true;
		if (MatchesSearch("Light Fade Start"))
			ImGui::Spacing();
		if (MatchesSearch("Light Fade End") && WeatherUtils::DrawSliderFloat("Light Fade End", settings.lightFadeEnd, 0.0f, 163840.0f))
			changed = true;
		if (MatchesSearch("Light Fade End"))
			ImGui::Spacing();
	}

	if (ImGui::CollapsingHeader("Other", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Spacing();
		if (MatchesSearch("Clip Distance") && WeatherUtils::DrawSliderFloat("Clip Distance", settings.clipDist, 0.0f, 163840.0f))
			changed = true;
		if (MatchesSearch("Clip Distance"))
			ImGui::Spacing();
	}

	if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
		ApplyChanges();
	}
}

void LightingTemplateWidget::DrawFogSettings()
{
	bool changed = false;

	ImGui::Spacing();
	if (MatchesSearch("Fog Color Near") && WeatherUtils::DrawColorEdit("Fog Color Near", settings.fogColorNear))
		changed = true;
	if (MatchesSearch("Fog Color Near"))
		ImGui::Spacing();
	if (MatchesSearch("Fog Color Far") && WeatherUtils::DrawColorEdit("Fog Color Far", settings.fogColorFar))
		changed = true;
	if (MatchesSearch("Fog Color Far"))
		ImGui::Spacing();

	ImGui::Spacing();
	if (MatchesSearch("Fog Near") && WeatherUtils::DrawSliderFloat("Fog Near", settings.fogNear, 0.0f, 163840.0f))
		changed = true;
	if (MatchesSearch("Fog Near"))
		ImGui::Spacing();
	if (MatchesSearch("Fog Far") && WeatherUtils::DrawSliderFloat("Fog Far", settings.fogFar, 0.0f, 163840.0f))
		changed = true;
	if (MatchesSearch("Fog Far"))
		ImGui::Spacing();

	ImGui::Spacing();
	if (MatchesSearch("Fog Power") && WeatherUtils::DrawSliderFloat("Fog Power", settings.fogPower, 0.0f, 10.0f))
		changed = true;
	if (MatchesSearch("Fog Power"))
		ImGui::Spacing();
	if (MatchesSearch("Fog Clamp") && WeatherUtils::DrawSliderFloat("Fog Clamp", settings.fogClamp, 0.0f, 1.0f))
		changed = true;
	if (MatchesSearch("Fog Clamp"))
		ImGui::Spacing();

	if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
		ApplyChanges();
	}
}

void LightingTemplateWidget::DrawDALCSettings()
{
	bool changed = false;

	ImGui::SeparatorText("Directional Ambient Lighting (DALC)");
	if (MatchesSearch("Specular") && WeatherUtils::DrawColorEdit("Specular", settings.dalc.specular))
		changed = true;
	if (MatchesSearch("Fresnel Power") && WeatherUtils::DrawSliderFloat("Fresnel Power", settings.dalc.fresnelPower, 0.0f, 10.0f))
		changed = true;

	ImGui::SeparatorText("Directional Colors");
	if ((MatchesSearch("Directional") || MatchesSearch("X+ (Right)")) && WeatherUtils::DrawColorEdit("X+ (Right)", settings.dalc.directional[0].max))
		changed = true;
	if ((MatchesSearch("Directional") || MatchesSearch("X- (Left)")) && WeatherUtils::DrawColorEdit("X- (Left)", settings.dalc.directional[0].min))
		changed = true;
	if ((MatchesSearch("Directional") || MatchesSearch("Y+ (Front)")) && WeatherUtils::DrawColorEdit("Y+ (Front)", settings.dalc.directional[1].max))
		changed = true;
	if ((MatchesSearch("Directional") || MatchesSearch("Y- (Back)")) && WeatherUtils::DrawColorEdit("Y- (Back)", settings.dalc.directional[1].min))
		changed = true;
	if ((MatchesSearch("Directional") || MatchesSearch("Z+ (Up)")) && WeatherUtils::DrawColorEdit("Z+ (Up)", settings.dalc.directional[2].max))
		changed = true;
	if ((MatchesSearch("Directional") || MatchesSearch("Z- (Down)")) && WeatherUtils::DrawColorEdit("Z- (Down)", settings.dalc.directional[2].min))
		changed = true;

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
