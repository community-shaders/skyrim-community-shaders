#include "TerrainVariation.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "Menu/Fonts.h"
#include "../Util.h"

#define I18N_KEY_PREFIX "feature.terrain_variation."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	TerrainVariation::Settings,
	enableLODTerrainTilingFix)

void TerrainVariation::DrawSettings()
{
	{
		MenuFonts::FontRoleGuard bodyGuard(Menu::FontRole::Body);
		ImGui::TextWrapped("%s", T(TKEY("always_enabled_note"),
			"Terrain Variation is always enabled when installed. To turn it off, use Disable at Boot."));
	}

	ImGui::Spacing();

	bool lodTilingFix = settings.enableLODTerrainTilingFix != 0;
	if (ImGui::Checkbox(T(TKEY("apply_to_lod_terrain"), "Apply to LOD Terrain"), &lodTilingFix)) {
		settings.enableLODTerrainTilingFix = lodTilingFix ? 1u : 0u;
		logger::info("TerrainVariation LOD setting changed to: {}", settings.enableLODTerrainTilingFix != 0);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("apply_to_lod_terrain_tooltip"),
							  "Applies the tiling fix to LOD terrain objects.\nThis helps reduce the visible tiling effect on distant terrain."));
	}
}

#undef I18N_KEY_PREFIX

void TerrainVariation::PostPostLoad()
{
	logger::info("TerrainVariation: Feature initialized");
}

void TerrainVariation::LoadSettings(json& o_json)
{
	settings = o_json;
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
