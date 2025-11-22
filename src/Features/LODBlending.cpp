#include "LODBlending.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LODBlending::Settings,
	LODTerrainBrightness,
	LODObjectBrightness,
	LODObjectSnowBrightness,
	DisableTerrainVertexColors,
	LODTerrainGamma,
	LODObjectGamma,
	LODObjectSnowGamma)

void LODBlending::DrawSettings()
{
	ImGui::SliderFloat("LOD Terrain Brightness", &settings.LODTerrainBrightness, 0.01f, 5.f, "%.2f");
	ImGui::SliderFloat("LOD Object Brightness", &settings.LODObjectBrightness, 0.01f, 5.f, "%.2f");
	ImGui::SliderFloat("LOD Object Snow Brightness", &settings.LODObjectSnowBrightness, 0.01f, 5.f, "%.2f");
	ImGui::SliderFloat("LOD Terrain Gamma", &settings.LODTerrainGamma, 0.1f, 3.f, "%.2f");
	ImGui::SliderFloat("LOD Object Gamma", &settings.LODObjectGamma, 0.1f, 3.f, "%.2f");
	ImGui::SliderFloat("LOD Object Snow Gamma", &settings.LODObjectSnowGamma, 0.1f, 3.f, "%.2f");
	ImGui::Checkbox("Disable Terrain Vertex Colors", (bool*)&settings.DisableTerrainVertexColors);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Disables vertex coloring on nearby terrain. "
			"Best combined with terrain LOD generated in xLODGen with Vertex Color Intensity set to 0. ");
	}
}

void LODBlending::LoadSettings(json& o_json)
{
	settings = o_json;
}

void LODBlending::SaveSettings(json& o_json)
{
	o_json = settings;
}

void LODBlending::RestoreDefaultSettings()
{
	settings = {};
}