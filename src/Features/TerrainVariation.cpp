#include "TerrainVariation.h"
#include "../FeatureBuffer.h"
#include "../Globals.h"
#include "../State.h"
#include "../Util.h"
#include "ShaderCache.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	TerrainVariation::Settings,
	enableTilingFix,
	enableLODTerrainTilingFix)

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

	ImGui::Separator();

	bool oldLODEnabled = settings.enableLODTerrainTilingFix;
	ImGui::Checkbox("Apply to LOD Terrain", (bool*)&settings.enableLODTerrainTilingFix);
	if (oldLODEnabled != (bool)settings.enableLODTerrainTilingFix) {
		UpdateShaderSettings();
		logger::info("TerrainVariation LOD setting changed to: {}", settings.enableLODTerrainTilingFix);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Applies the tiling fix to LOD terrain objects.\n"
			"This helps reduce the visible tiling effect on distant terrain.");
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

bool TerrainVariation::RefineShaderDefineForDescriptor(RE::BSShader::Type shaderType, uint32_t descriptor)
{
	if (shaderType != RE::BSShader::Type::Lighting) {
		return true;
	}
	const auto technique = static_cast<SIE::ShaderCache::LightingShaderTechniques>(0x3F & (descriptor >> 24));
	switch (technique) {
	case SIE::ShaderCache::LightingShaderTechniques::MTLand:
	case SIE::ShaderCache::LightingShaderTechniques::MTLandLODBlend:
	case SIE::ShaderCache::LightingShaderTechniques::LODLand:
		return true;
	default:
		return false;
	}
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
}

bool TerrainVariation::DrawFailLoadMessage() const
{
	return false;
}