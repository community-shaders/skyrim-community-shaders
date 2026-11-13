#pragma once

#include "SceneSettingsCatalog.generated.h"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Util::Settings
{
	/** @brief Removes an ImGui identity suffix from visible text. */
	std::string StripImGuiId(std::string_view label);
	/** @brief Decodes escaped catalogue path segments. */
	std::vector<std::string> SplitCatalogPath(std::string_view path);
	/** @brief Identifies structural wrappers omitted from setting labels. */
	bool IsStructuralDisplayPart(std::string_view part);
	/** @brief Formats a catalogue display segment. */
	std::string NormalizeDisplayPart(std::string part);
	/** @brief Returns the translated display hierarchy shared by settings selectors. */
	std::vector<std::string> GetCatalogDisplayPath(const SceneSettingsCatalog::SettingMetadata& setting);
	/** @brief Returns the translated setting label shared by settings selectors. */
	std::string GetCatalogLeafDisplayName(const SceneSettingsCatalog::SettingMetadata& setting);

	/** @brief Whether a settings key is file metadata (`_`-prefixed) rather than a setting. */
	inline bool IsMetadataKey(std::string_view key) { return key.starts_with('_'); }

	/** @brief Removes the top-level metadata keys from a settings object. */
	void EraseMetadataKeys(nlohmann::json& settings);

	/** @brief Writes overlay onto target, merging objects key by key; any other value replaces, so a setting
	 *  whose shape changed (unlike json::update) never throws. */
	void MergeSettings(nlohmann::json& target, const nlohmann::json& overlay);

	/** @brief One exportable setting: its JSON pointer path and display label. */
	struct ExportSetting
	{
		std::string path;
		std::string label;
	};

	/** @brief Lists persisted JSON leaves using catalogue labels when available; arrays remain whole. */
	std::vector<ExportSetting> GetExportSettings(std::string_view featureShortName, const nlohmann::json& settings);

	/** @brief Rebuilds the subtree of values named by the given JSON pointers, dropping empty branches. */
	nlohmann::json SelectSettingPaths(const nlohmann::json& values, const std::vector<std::string>& paths);

	/** @brief Whether value can stand in for a known setting: same JSON type, a number or boolean for a number,
	 *  or anything for a null known value. */
	bool IsCompatibleSettingValue(const nlohmann::json& known, const nlohmann::json& value);

	/**
	 * @brief Collects keys of incoming that known has no counterpart for, or holds with another type, as dotted
	 * paths, recursing into groups. Either one would make the feature's LoadSettings throw or drop it.
	 * @param incoming Partial settings object to check; `_`-prefixed metadata keys are ignored.
	 * @param known Canonical settings blob, as written by the feature's SaveSettings.
	 * @param prefix Dotted path of incoming within the whole blob.
	 * @param unknownKeys Receives the rejected paths; a type mismatch is marked as such.
	 */
	void CollectUnknownSettingKeys(const nlohmann::json& incoming, const nlohmann::json& known,
		const std::string& prefix, std::vector<std::string>& unknownKeys);

	/**
	 * @brief Erases the values of incoming that known holds with another type, recursing into groups. Keys
	 * known lacks are kept, so settings from another version survive.
	 * @param erasedKeys Receives the erased values' dotted paths.
	 */
	void EraseMismatchedSettings(nlohmann::json& incoming, const nlohmann::json& known,
		const std::string& prefix, std::vector<std::string>& erasedKeys);

	/** @brief A value as setting lists show it: strings unquoted, booleans as On / Off, floats without trailing zeros. */
	std::string FormatValue(const nlohmann::json& value);
}
