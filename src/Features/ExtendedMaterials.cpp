#include "ExtendedMaterials.h"
#include "../I18n/I18n.h"

#define I18N_KEY_PREFIX "feature.extended_materials."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ExtendedMaterials::Settings,
	EnableComplexMaterial,
	EnableParallax,
	EnableTerrain,
	EnableHeightBlending,
	EnableShadows,
	ExtendShadows,
	EnableParallaxWarpingFix)

void ExtendedMaterials::DataLoaded()
{
	if (&settings.EnableTerrain) {
		if (auto bLandSpecular = globals::game::iniSettingCollection->GetSetting("bLandSpecular:Landscape"); bLandSpecular) {
			if (!bLandSpecular->data.b) {
				logger::info("[CPM] Changing bLandSpecular from {} to {} to support Terrain Parallax", bLandSpecular->data.b, true);
				bLandSpecular->data.b = true;
			}
		}
	}
}

void ExtendedMaterials::DrawSettings()
{
	if (ImGui::TreeNodeEx(T(TKEY("complex_material"), "Complex Material"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox(T(TKEY("enable_complex_material"), "Enable Complex Material"), (bool*)&settings.EnableComplexMaterial);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("enable_complex_material_tooltip"),
								  "Enables support for the Complex Material specification which makes use of the environment mask. "
								  "This includes parallax, as well as more realistic metals and specular reflections. "
								  "May lead to some warped textures on modded content which have an invalid alpha channel in their environment mask. "));
		}

		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx(T(TKEY("parallax"), "Parallax"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox(T(TKEY("enable_parallax"), "Enable Parallax"), (bool*)&settings.EnableParallax);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("enable_parallax_tooltip"), "Enables parallax on standard meshes made for parallax."));
		}

		if (ImGui::Checkbox(T(TKEY("enable_legacy_terrain"), "Enable Legacy Terrain"), (bool*)&settings.EnableTerrain)) {
			if (settings.EnableTerrain) {
				DataLoaded();
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("enable_legacy_terrain_tooltip"),
								  "Enables terrain parallax using the alpha channel of each landscape texture. "
								  "Therefore, all landscape textures must support parallax for this effect to work properly. "));
		}
		ImGui::Checkbox(T(TKEY("enable_height_blending"), "Enable Terrain Height Blending"), (bool*)&settings.EnableHeightBlending);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("enable_height_blending_tooltip"), "Enables landscape texture blending based on parallax. "));
		}
		ImGui::Checkbox(T(TKEY("enable_parallax_warping_fix"), "Enable Parallax Warping Fix"), (bool*)&settings.EnableParallaxWarpingFix);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("enable_parallax_warping_fix_tooltip"), "Enables a fix reducing parallax scale on curved and smooth normal triangles."));
		}

		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx(T(TKEY("soft_shadows"), "Approximate Soft Shadows"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox(T(TKEY("enable_shadows"), "Enable Shadows"), (bool*)&settings.EnableShadows);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("enable_shadows_tooltip"),
								  "Enables cheap soft shadows when using parallax. "
								  "This applies to all directional and point lights. "));
		}
		ImGui::Checkbox(T(TKEY("extend_shadows"), "Extend Shadows"), (bool*)&settings.ExtendShadows);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("extend_shadows_tooltip"),
								  "Extends parallax shadows beyond the range of parallax. Small performance impact."));
		}

		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TreePop();
	}
}

#undef I18N_KEY_PREFIX

void ExtendedMaterials::LoadSettings(json& o_json)
{
	settings = o_json;
}

void ExtendedMaterials::SaveSettings(json& o_json)
{
	o_json = settings;
}

void ExtendedMaterials::RestoreDefaultSettings()
{
	settings = {};
}

bool ExtendedMaterials::HasShaderDefine(RE::BSShader::Type shaderType)
{
	switch (shaderType) {
	case RE::BSShader::Type::Lighting:
		return true;
	default:
		return false;
	}
}
