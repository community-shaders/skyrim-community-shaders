#pragma once

#include <string>

struct Feature;

/// Scene Manager section listing the Baseline preset files in use and exporting settings into a pack.
namespace FeatureOverwritesPanel
{
	/** @brief Draws the overwrite table, the export button, and their popups. */
	void Draw();

	/** @brief Opens the export popup on the next DrawExport; a non-null feature is preselected with every setting ticked and the feature list hidden. */
	void BeginExport(Feature* feature = nullptr);

	/**
	 * @brief Opens the export popup on an existing Baseline file, its settings ticked, so it can be edited.
	 * @param packId The pack the file belongs to.
	 */
	void BeginExportInto(Feature* feature, const std::string& packId);

	/** @brief Draws the export popup while open; only the caller that opened it draws it. */
	void DrawExport();

	/** @brief Icon button that opens the export on this feature, told apart from the Baseline preset export by glyph and tooltip. */
	void DrawExportButton(Feature* feature, const char* id);

	/** @brief Whether the feature saves any main settings that an overwrite could carry; cached per feature. */
	bool HasExportableSettings(Feature* feature);
}
