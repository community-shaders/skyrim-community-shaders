#include "TerrainVariation.h"
#include "../FeatureBuffer.h"
#include "../Globals.h"
#include "../State.h"
#include "../Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	TerrainVariation::Settings,
	enableTilingFix)

void TerrainVariation::DrawSettings()
{
	bool oldEnabled = settings.enableTilingFix;
	ImGui::Checkbox("Enable Terrain Tiling Fix", (bool*)&settings.enableTilingFix);
	if (oldEnabled != (bool)settings.enableTilingFix) {
		// Update the shader settings when the checkbox is toggled
		UpdateShaderSettings();
		logger::info("TerrainVariation setting changed to: {}", settings.enableTilingFix);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Reduces the repeating pattern effect on terrain textures.\n"
			"This technique creates more natural-looking terrain by adding variation to texture sampling.\n"
			"Stochastic texturing is applied only when parallax effects are not active (beyond 2048 units from camera).");
	}
	if (settings.enableTilingFix) {
		ImGui::Separator();

		bool paramsChanged = false;

		if (paramsChanged) {
			UpdateShaderSettings();
			logger::info("TerrainVariation parameters updated");
		}
	}
}

void TerrainVariation::UpdateShaderSettings()
{
	if (!globals::state) {
		return;
	}

	// Mark the vertex descriptor as dirty to trigger an update
	if (globals::game::stateUpdateFlags) {
		globals::game::stateUpdateFlags->set(RE::BSGraphics::DIRTY_VERTEX_DESC);
	}
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

bool TerrainVariation::DrawFailLoadMessage() const
{
	return false;
}

void TerrainVariation::DrawUnloadedUI()
{
	ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "This feature is not installed!");

	ImGui::Spacing();
	ImGui::TextWrapped(
		"Terrain Variation reduces the repeating pattern effect on terrain textures.\n"
		"This technique creates more natural-looking terrain by adding variation to texture sampling.\n"
		"Stochastic texturing is applied only when parallax effects are not active (beyond 2048 units from camera).");

	ImGui::Spacing();
	ImGui::TextWrapped("Key features:");
	ImGui::BulletText("Reduces terrain texture tiling");
	ImGui::BulletText("Automatically activates when parallax effects end");
	ImGui::Spacing();
}