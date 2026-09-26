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

	struct ExportSetting
	{
		std::string path;
		std::string label;
	};

	/** @brief Lists persisted JSON leaves using catalogue labels when available; arrays remain whole. */
	std::vector<ExportSetting> GetExportSettings(std::string_view featureShortName, const nlohmann::json& settings);

	/** @brief Rebuilds the subtree of values named by the given JSON pointers, dropping empty branches. */
	nlohmann::json SelectSettingPaths(const nlohmann::json& values, const std::vector<std::string>& paths);

	/**
	 * @brief Collects keys of incoming that known has no counterpart for, as dotted paths, recursing into groups.
	 * @param incoming Partial settings object to check; `_`-prefixed metadata keys are ignored.
	 * @param known Canonical settings blob, as written by the feature's SaveSettings.
	 * @param prefix Dotted path of incoming within the whole blob.
	 * @param unknownKeys Receives the unrecognized paths.
	 */
	void CollectUnknownSettingKeys(const nlohmann::json& incoming, const nlohmann::json& known,
		const std::string& prefix, std::vector<std::string>& unknownKeys);
}
