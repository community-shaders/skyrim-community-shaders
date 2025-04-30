#include "TerrainVariation.h"
#include "../FeatureBuffer.h"
#include "../Globals.h"
#include "../State.h"
#include "../Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	TerrainVariation::Settings,
	enabled,
	startDistance,
	maxDistance)

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

	if (settings.enabled) {
		ImGui::Separator();
		
		bool paramsChanged = false;
		
		// Add UI controls for distance-based parameters
		paramsChanged |= ImGui::SliderFloat("Start Distance", &settings.startDistance, 0.0f, settings.maxDistance - 1.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Distance from camera where variation begins to blend in.\nCloser than this will have no variation applied.");
		}
		
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Distance from camera where variation reaches maximum intensity.");
		}
		
		if (paramsChanged) {
			UpdateShaderSettings();
			logger::info("TerrainVariation distance parameters updated");
		}
	}
}

void TerrainVariation::UpdateShaderSettings()
{
	if (!globals::state) {
		return;
	}

	globals::game::stateUpdateFlags->set(RE::BSGraphics::DIRTY_VERTEX_DESC);

	// The settings are automatically passed to shaders through the FeatureBuffer system
	// from the settings object, which is used in GetFeatureBufferData() in FeatureBuffer.cpp

	logger::debug("TerrainVariation: Updated shader settings, enabled = {}, startDistance = {}, maxDistance = {}, maxIntensity = {}", 
		settings.enabled, settings.startDistance, settings.maxDistance, settings.maxIntensity);
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