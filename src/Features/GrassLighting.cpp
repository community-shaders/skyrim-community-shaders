#include "GrassLighting.h"

#include "GrassOptimizations.h"
#include "I18n/I18n.h"

#define I18N_KEY_PREFIX "feature.grass_lighting."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	GrassLighting::Settings,
	Glossiness,
	SpecularStrength,
	SubsurfaceScatteringAmount,
	OverrideComplexGrassSettings,
	BasicGrassBrightness,
	ComplexGrassThreshold,
	MidLODBrightness,
	FarLODBrightness,
	PBRWrappedLightingAmount,
	SubsurfaceBalance,
	CanopyOcclusion,
	ClumpShading)

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
			ImGui::TextWrapped("%s", T(TKEY("sss_tooltip"), "Subsurface scattering strength for non-PBR grass. Zero disables it. SSS Balance distributes the subsurface light between the two sides without changing its total. PBR subsurface scattering is controlled by the material and its texture."));
		}
		ImGui::SliderFloat(T(TKEY("sss_balance"), "SSS Balance"), &settings.SubsurfaceBalance, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextWrapped("%s", T(TKEY("sss_balance_tooltip"), "Share of non-PBR subsurface light sent to the side of a blade facing away from the light, shining through; the lit side receives the rest. 0 keeps it on the lit side, 0.5 splits it equally and 1 (default) sends all of it through. Does not change reflected light or the total subsurface strength. PBR balance is controlled by the material."));
		}

		ImGui::SliderFloat(T(TKEY("canopy_occlusion"), "Canopy Occlusion"), &settings.CanopyOcclusion, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextWrapped("%s", T(TKEY("canopy_occlusion_tooltip"), "Darkens grass toward the root of each plant, where the blades above block sunlight and sky. Zero disables it; 1 leaves roots almost unlit. Applies to PBR and non-PBR grass."));
		}
		ImGui::SliderFloat(T(TKEY("clump_shading"), "Clump Shading"), &settings.ClumpShading, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextWrapped("%s", T(TKEY("clump_shading_tooltip"), "Rounds grass lighting toward the plant's dome shape. Basic and PBR grass also receive extra shadowing on the side facing away from a light. Complex grass keeps its normal-map detail without that extra shadowing. Does not affect meshes with authored sphere normals. Zero disables it."));
		}

		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx(T(TKEY("pbr_grass"), "PBR Grass"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat(T(TKEY("pbr_wrapped_lighting_amount"), "PBR Wrapped Lighting Amount"), &settings.PBRWrappedLightingAmount, 0.0f, 1.0f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextWrapped("%s", T(TKEY("pbr_wrapped_lighting_tooltip"), "Softens lighting on PBR grass by overlapping the lit-side and shine-through lighting where a blade turns edge-on to the light. Zero uses plain Lambert lighting, which can leave normal-mapped streaks nearly black under a high sun. Default 0.5. Each side keeps its total energy."));
		}
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
		ImGui::SliderFloat(T(TKEY("brightness"), "Brightness"), &settings.BasicGrassBrightness, 0.0f, 1.0f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("brightness_tooltip"), "Darkens the grass textures to look better with the new lighting"));
		}

		if (globals::features::grassOptimizations.loaded) {
			ImGui::SliderFloat(T(TKEY("mid_lod_brightness"), "Middle LOD Brightness"), &settings.MidLODBrightness, 0.0f, 2.0f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("%s", T(TKEY("mid_lod_brightness_tooltip"),
									  "Raise or lower until middle-distance LOD grass matches the full-detail grass around it. Requires Grass Optimizations with Middle LOD enabled."));
			}
			ImGui::SliderFloat(T(TKEY("far_lod_brightness"), "Far LOD Brightness"), &settings.FarLODBrightness, 0.0f, 2.0f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("%s", T(TKEY("far_lod_brightness_tooltip"),
									  "Raise or lower until the most distant LOD grass matches the middle LOD grass in front of it. Requires Grass Optimizations with Far LOD enabled."));
			}
		}

		ImGui::TreePop();
	}
}

#undef I18N_KEY_PREFIX

void GrassLighting::LoadSettings(json& o_json)
{
	settings = o_json;
	settings.SubsurfaceScatteringAmount = std::clamp(settings.SubsurfaceScatteringAmount, 0.0f, 1.0f);
	settings.SubsurfaceBalance = std::clamp(settings.SubsurfaceBalance, 0.0f, 1.0f);
	settings.CanopyOcclusion = std::clamp(settings.CanopyOcclusion, 0.0f, 1.0f);
	settings.ClumpShading = std::clamp(settings.ClumpShading, 0.0f, 1.0f);
}

void GrassLighting::SaveSettings(json& o_json)
{
	o_json = settings;
}

void GrassLighting::RestoreDefaultSettings()
{
	settings = {};
}
