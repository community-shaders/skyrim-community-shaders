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
	ImGui::SetNextWindowSizeConstraints(ImVec2(600, 0), ImVec2(FLT_MAX, FLT_MAX));
	if (ImGui::Begin(GetEditorID().c_str(), &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar)) {
		DrawMenu();

		auto editorWindow = EditorWindow::GetSingleton();

		if (!editorWindow->settings.autoApplyChanges) {
			auto menu = globals::menu;
			bool useIcons = menu && menu->GetSettings().Theme.ShowActionIcons &&
			                menu->uiIcons.saveSettings.texture &&
			                menu->uiIcons.featureSettingRevert.texture;

			if (useIcons) {
				const float iconSize = ImGui::GetFrameHeight();
				const ImVec2 buttonSize(iconSize, iconSize);

				ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.8f, 0.8f, 0.25f));

				if (ImGui::ImageButton("##ApplyLightingTemplate", menu->uiIcons.saveSettings.texture, buttonSize)) {
					ApplyChanges();
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Apply changes to the game");
				}

				ImGui::SameLine();

				if (ImGui::ImageButton("##RevertLightingTemplate", menu->uiIcons.featureSettingRevert.texture, buttonSize)) {
					RevertChanges();
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Revert to saved values");
				}

				ImGui::PopStyleColor(2);
				ImGui::PopStyleVar();
			} else {
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.9f, 1.0f));
				if (ImGui::Button("Apply", ImVec2(ImGui::GetContentRegionAvail().x * 0.49f, 0))) {
					ApplyChanges();
				}
				ImGui::PopStyleColor();
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Apply changes to the game");
				}
				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.3f, 0.2f, 1.0f));
				if (ImGui::Button("Revert", ImVec2(-1, 0))) {
					RevertChanges();
				}
				ImGui::PopStyleColor();
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Revert to saved values");
				}
			}
		}
		ImGui::Separator();

		if (ImGui::BeginTabBar("LightingTemplateSettingsTabs", ImGuiTabBarFlags_None)) {
			if (ImGui::BeginTabItem("Basic")) {
				DrawBasicSettings();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Fog")) {
				DrawFogSettings();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("DALC")) {
				DrawDALCSettings();
				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}
	}
	ImGui::End();
}

void LightingTemplateWidget::DrawBasicSettings()
{
	bool changed = false;

	if (ImGui::CollapsingHeader("Ambient & Directional", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Spacing();
		if (DrawColorEdit("Ambient Color", settings.ambient))
			changed = true;
		ImGui::Spacing();
		if (DrawColorEdit("Directional Color", settings.directional))
			changed = true;
		ImGui::Spacing();
	}

	if (ImGui::CollapsingHeader("Directional Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Spacing();
		if (DrawSliderFloat("Directional XY", settings.directionalXY))
			changed = true;
		ImGui::Spacing();
		if (DrawSliderFloat("Directional Z", settings.directionalZ))
			changed = true;
		ImGui::Spacing();
		if (DrawSliderFloat("Directional Fade", settings.directionalFade))
			changed = true;
		ImGui::Spacing();
	}

	if (ImGui::CollapsingHeader("Light Fade", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Spacing();
		if (DrawSliderFloat("Light Fade Start", settings.lightFadeStart))
			changed = true;
		ImGui::Spacing();
		if (DrawSliderFloat("Light Fade End", settings.lightFadeEnd))
			changed = true;
		ImGui::Spacing();
	}

	if (ImGui::CollapsingHeader("Other", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Spacing();
		if (DrawSliderFloat("Clip Distance", settings.clipDist))
			changed = true;
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
	if (DrawColorEdit("Fog Color Near", settings.fogColorNear))
		changed = true;
	ImGui::Spacing();
	if (DrawColorEdit("Fog Color Far", settings.fogColorFar))
		changed = true;
	ImGui::Spacing();

	ImGui::Spacing();
	if (DrawSliderFloat("Fog Near", settings.fogNear))
		changed = true;
	ImGui::Spacing();
	if (DrawSliderFloat("Fog Far", settings.fogFar))
		changed = true;
	ImGui::Spacing();

	ImGui::Spacing();
	if (DrawSliderFloat("Fog Power", settings.fogPower))
		changed = true;
	ImGui::Spacing();
	if (DrawSliderFloat("Fog Clamp", settings.fogClamp))
		changed = true;
	ImGui::Spacing();

	if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
		ApplyChanges();
	}
}

void LightingTemplateWidget::DrawDALCSettings()
{
	bool changed = false;

	ImGui::Spacing();
	if (DrawColorEdit("Specular", settings.dalc.specular))
		changed = true;
	ImGui::Spacing();
	if (DrawSliderFloat("Fresnel Power", settings.dalc.fresnelPower))
		changed = true;
	ImGui::Spacing();

	for (int j = 0; j < 3; j++) {
		const char* labels[] = { "X", "Y", "Z" };
		ImGui::Separator();
		ImGui::Text("Directional %s", labels[j]);
		if (DrawColorEdit(std::format("Max##{}", labels[j]), settings.dalc.directional[j].max))
			changed = true;
		if (DrawColorEdit(std::format("Min##{}", labels[j]), settings.dalc.directional[j].min))
			changed = true;
		ImGui::Spacing();
	}

	if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
		ApplyChanges();
	}
}

void LightingTemplateWidget::ApplyChanges()
{
	SetLightingTemplateValues();
	SaveSettings();
}

void LightingTemplateWidget::RevertChanges()
{
	LoadLightingTemplateValues();
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

void LightingTemplateWidget::LoadSettings()
{
	if (!js.empty()) {
		settings = js;
	}
}

void LightingTemplateWidget::SaveSettings()
{
	js = settings;
}
