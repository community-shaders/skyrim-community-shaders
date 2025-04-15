#include "TerrainVariation.h"
#include "../Util.h"
#include "../Globals.h"
#include "../State.h"
#include "../FeatureBuffer.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	TerrainVariation::Settings,
	enabled)

void TerrainVariation::DrawSettings()
{
	bool oldEnabled = settings.enabled;
	ImGui::Checkbox("Enable Terrain Tiling Fix", &settings.enabled);
	if (oldEnabled != settings.enabled) {
		// Update the shader settings when the checkbox is toggled
		UpdateShaderSettings();
		logger::info("TerrainVariation setting changed to: {}", settings.enabled);
	}
	
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Reduces the repeating pattern effect on terrain textures.\n"
			"This technique creates more natural-looking terrain by adding variation to texture sampling.");
	}
}

void TerrainVariation::UpdateShaderSettings()
{
	if (!globals::state) {
		return;
	}
	
	// Update settings in the feature buffer and force a shader state update
	// This approach doesn't directly access the buffer but tells the game to update it
	globals::game::stateUpdateFlags->set(RE::BSGraphics::DIRTY_VERTEX_DESC);
	
	logger::debug("TerrainVariation: Updated shader settings, enabled = {}", settings.enabled);
}

void TerrainVariation::PostPostLoad()
{
	logger::info("TerrainVariation: Feature initialized");
	UpdateShaderSettings();
}

void TerrainVariation::LoadSettings(json& o_json)
{
	settings = o_json;
	UpdateShaderSettings();
}

void TerrainVariation::SaveSettings(json& o_json)
{
	o_json = settings;
}

void TerrainVariation::RestoreDefaultSettings()
{
	settings = {};
	UpdateShaderSettings();
}