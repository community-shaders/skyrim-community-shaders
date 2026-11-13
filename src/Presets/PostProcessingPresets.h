#pragma once

#include <filesystem>

/// Post Processing presets are Baseline packs in the unified preset system:
/// Presets/<Pack>/Baseline/PostProcessing.json, applied from the Presets page and written by the
/// CS Editor's Export Preset. This module moves the old standalone presets over and writes the file.
namespace PostProcessingPresets
{
	/// Feature short name the Baseline file is keyed by.
	inline constexpr const char* kFeatureShortName = "PostProcessing";

	/**
	 * @brief Converts each legacy PostProcessing/<Name>.json into a Baseline pack of the same name.
	 *
	 * Runs once per session, before the catalog scans. A leftover default.json is skipped: it is the
	 * stock look, which the sub-features now carry as their own defaults.
	 * A pack folder that already exists is never overwritten; its legacy file is left in place. A
	 * converted source is renamed to <Name>.json.migrated so it is not converted again.
	 */
	void MigrateLegacyPresets();

	/**
	 * @brief Writes the current base Post Processing settings into a pack's Baseline folder.
	 * @param packRoot Pack folder; the Baseline subfolder is created when missing.
	 * @return Whether the file was written and passes the override validator.
	 */
	bool WriteBaseline(const std::filesystem::path& packRoot);
}
