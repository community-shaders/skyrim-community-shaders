#pragma once

struct Feature;

/// Scene Manager section listing applied mod-provided feature overwrites and exporting settings as new ones.
namespace FeatureOverwritesPanel
{
	/** @brief Draws the overwrite table, the export button, and their popups. */
	void Draw();

	/** @brief Opens the export popup; a non-null feature is preselected and the feature picker hidden. */
	void BeginExport(Feature* feature = nullptr);

	/** @brief Draws the export popup while open; must run under the same ImGui ID stack as BeginExport. */
	void DrawExport();

	/** @brief Whether the feature saves any main settings that an overwrite could carry; cached per feature. */
	bool HasExportableSettings(Feature* feature);
}
