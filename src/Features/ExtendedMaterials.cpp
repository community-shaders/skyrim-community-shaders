#include "ExtendedMaterials.h"

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
	if (ImGui::TreeNodeEx("Complex Material", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Enable Complex Material", (bool*)&settings.EnableComplexMaterial);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Enables support for the Complex Material specification which makes use of the environment mask. "
				"This includes parallax, as well as more realistic metals and specular reflections. "
				"May lead to some warped textures on modded content which have an invalid alpha channel in their environment mask. ");
		}

		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Parallax", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Enable Parallax", (bool*)&settings.EnableParallax);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Enables parallax on standard meshes made for parallax.");
		}

		if (ImGui::Checkbox("Enable Terrain", (bool*)&settings.EnableTerrain)) {
			if (settings.EnableTerrain) {
				DataLoaded();
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Enables terrain parallax using the alpha channel of each landscape texture. "
				"Therefore, all landscape textures must support parallax for this effect to work properly. ");
		}
		ImGui::Checkbox("Enable Terrain Height Blending", (bool*)&settings.EnableHeightBlending);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Enables landscape texture blending based on parallax. ");
		}
		ImGui::Checkbox("Enable Parallax Warping Fix", (bool*)&settings.EnableParallaxWarpingFix);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Enables a fix reducing parallax scale on curved and smooth normal triangles.");
		}

		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Approximate Soft Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Enable Shadows", (bool*)&settings.EnableShadows);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Enables cheap soft shadows when using parallax. "
				"This applies to all directional and point lights. ");
		}
		ImGui::Checkbox("Extend Shadows", (bool*)&settings.ExtendShadows);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Extends parallax shadows beyond the range of parallax. Small performance impact.");
		}

		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TreePop();
	}
}

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