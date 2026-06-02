#include "GrassLighting.h"

#include "I18n/I18n.h"

#define I18N_KEY_PREFIX "feature.grass_lighting."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	GrassLighting::Settings,
	Glossiness,
	SpecularStrength,
	SubsurfaceScatteringAmount,
	OverrideComplexGrassSettings,
	BasicGrassBrightness,
	ComplexGrassThreshold)

void GrassLighting::DrawSettings()
{
	if (ImGui::TreeNodeEx(T(TKEY("complex_grass"), "Complex Grass"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::TextWrapped("%s", T(TKEY("specular_desc"), "Specular highlights for complex grass"));
		ImGui::SliderFloat(T(TKEY("glossiness"), "Glossiness"), &settings.Glossiness, 1.0f, 100.0f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("glossiness_tooltip"), "Specular highlight glossiness."));
		}

		ImGui::SliderFloat(T(TKEY("specular_strength"), "Specular Strength"), &settings.SpecularStrength, 0.0f, 1.0f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("specular_strength_tooltip"), "Specular highlight strength."));
		}

		ImGui::Spacing();
		ImGui::TextWrapped("%s", T(TKEY("detection_header"), "Complex Grass Detection"));
		ImGui::SliderFloat(T(TKEY("detection_threshold"), "Detection Threshold"), &settings.ComplexGrassThreshold, 0.001f, 0.1f, "%.3f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("detection_threshold_tooltip"),
								  "Threshold for detecting complex grass textures. Lower values are more strict."));
		}

		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx(T(TKEY("effects"), "Effects"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat(T(TKEY("sss_amount"), "SSS Amount"), &settings.SubsurfaceScatteringAmount, 0.0f, 1.0f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("sss_tooltip"),
								  "Subsurface Scattering (SSS) amount. "
								  "Soft lighting controls how evenly lit an object is. "
								  "Back lighting illuminates the back face of an object. "
								  "Combined to model the transport of light through the surface."));
		}

		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx(T(TKEY("lighting"), "Lighting"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox(T(TKEY("override_complex"), "Override Complex Grass Lighting Settings"), (bool*)&settings.OverrideComplexGrassSettings);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("override_complex_tooltip"),
								  "Override the settings set by the grass mesh author. "
								  "Complex grass authors can define the brightness for their grass meshes. "
								  "However, some authors may not account for the extra lights available from Community Shaders. "
								  "This option will treat their grass settings like non-complex grass. "
								  "This was the default in Community Shaders < 0.7.0"));
		}
		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TextWrapped("%s", T(TKEY("basic_grass"), "Basic Grass"));
		ImGui::SliderFloat(T(TKEY("brightness"), "Brightness"), &settings.BasicGrassBrightness, 0.0f, 1.0f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("brightness_tooltip"), "Darkens the grass textures to look better with the new lighting"));
		}

		ImGui::TreePop();
	}
}

#undef I18N_KEY_PREFIX

void GrassLighting::LoadSettings(json& o_json)
{
	settings = o_json;
}

void GrassLighting::SaveSettings(json& o_json)
{
	o_json = settings;
}

void GrassLighting::RestoreDefaultSettings()
{
	settings = {};
}
