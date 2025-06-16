#include "TerrainVariation.h"
#include "../FeatureBuffer.h"
#include "../Globals.h"
#include "../State.h"
#include "../Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	TerrainVariation::Settings,
	enableTilingFix,
	startDistance,
	maxDistance,
	heightCompensationFactor,
	shadowRayDirFactor,
	hashQuality)

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
			"This technique creates more natural-looking terrain by adding variation to texture sampling.");
	}

	if (settings.enableTilingFix) {
		ImGui::Separator();

		bool paramsChanged = false;

		// Add UI controls for distance-based parameters
		paramsChanged |= ImGui::SliderFloat("Start Distance", &settings.startDistance, 0.0f, settings.maxDistance - 1.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Distance from camera where variation begins to blend in.\nCloser than this will have no variation applied.");
		}

		paramsChanged |= ImGui::SliderFloat("Maximum Distance", &settings.maxDistance, settings.startDistance + 1.0f, 5000.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Distance from camera where variation reaches maximum intensity.\n"
				"Generally, a distance of atleast 1000 between values is recommended for a smooth transition.");
		}

		ImGui::SeparatorText("Advanced Options");

		ImGui::Checkbox("Show Advanced Options", &showAdvanced);

		if (showAdvanced) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.0f, 1.0f));
			ImGui::TextWrapped(
				"Warning: Only modify these values if you know what you're doing!\n"
				"You may break the intended look of textures.");
			ImGui::PopStyleColor();

			paramsChanged |= ImGui::SliderFloat("Height Compensation Factor", &settings.heightCompensationFactor, 0.5f, 2.0f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Increasing the number will increase the height of all terrain parallax.\n"
					"Compensation multiplier for terrain parallax when Terrain Variation is enabled.\n"
					"This setting only applies when both Terrain Variation and Extended Materials' terrain parallax are enabled.");
			}

			paramsChanged |= ImGui::SliderFloat("Shadow Ray Direction Factor", &settings.shadowRayDirFactor, 0.5f, 3.0f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Multiplier for shadow ray direction when calculating terrain parallax shadows.\n"
					"Higher values make shadows appear stronger but may cause artifacts.\n"
					"This setting only applies when both Terrain Variation and Extended Materials' terrain parallax are enabled.");  // davo yappage
			}

			const char* hashQualityItems[] = { "Low Quality", "High Quality" };
			paramsChanged |= ImGui::Combo("Hash Quality", &settings.hashQuality, hashQualityItems, IM_ARRAYSIZE(hashQualityItems));
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Low quality reduces most terrain tiling with a cheaper formula.\n"
					"High quality is a more complex formula that completely removes terrain tiling.\n"
					"Choose low quality if you experience performance issues.");
			}
		}

		if (paramsChanged) {
			// Ensure minimum distance between values for numerical stability
			if (settings.maxDistance - settings.startDistance < 1.0f) {
				settings.maxDistance = settings.startDistance + 1.0f;
			}

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

	// Calculate invDistanceRange for shader optimization
	float distanceRange = settings.maxDistance - settings.startDistance;
	if (distanceRange <= 0.0f) {
		// Prevent division by zero - use a sensible default
		distanceRange = 1.0f;
	}
	float invDistanceRange = 1.0f / distanceRange;

	// Update the settings struct with calculated values
	// These will be picked up automatically by the feature buffer
	settings.invDistanceRange = invDistanceRange;

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