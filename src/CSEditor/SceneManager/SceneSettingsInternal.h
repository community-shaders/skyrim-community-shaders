#pragma once

#include "SceneSettingsManager.h"

#include "Feature.h"
#include "SceneSettingsCatalog.generated.h"
#include "SceneSettingsPolicy.h"
#include "Utils/SettingsCatalog.h"

#include <filesystem>
#include <functional>
#include <map>

/// Catalog, JSON and policy helpers shared by the SceneSettingsManager translation units.
namespace SceneSettingsInternal
{
	constexpr auto kOverwriteJsonIndent = 2;
	constexpr auto kMaxSceneOverwriteFileSize = 1024 * 1024;
	constexpr const char* kFeatureKey = "_feature";
	constexpr const char* kMetadataKey = "_metadata";
	constexpr const char* kMetadataDescriptionKey = "description";
	/// Per-setting location transition seconds, mirroring the setting tree under `_metadata`.
	constexpr const char* kMetadataEntryTransitionsKey = "entryTransitions";
	constexpr const char* kMetadataEntryDeletionsKey = "entryDeletions";
	constexpr const char* kStatusKey = "status";
	constexpr const char* kStatusDeleted = "deleted";
	/// Picks a weather or location's saved set; also a mod's overwrite metadata key.
	constexpr const char* kTimeOfDayEnabledKey = "timeOfDayEnabled";
	/// Earlier view-only weather toggle; an opt-in migrates to kTimeOfDayEnabledKey.
	constexpr const char* kLegacyShowTimeOfDayKey = "showTimeOfDay";
	constexpr std::string_view kSceneSettingDisplaySeparator = " / ";
	/// Earlier open-shaders name for the locationTypes section and its "Category" type; read and migrated.
	constexpr const char* kLegacyLocationTypeSectionName = "categories";
	constexpr const char* kLegacyLocationTypeName = "Category";

	using namespace Util::Settings;

	using SceneSettingControlType = SceneSettingsManager::SettingControlType;
	using FeatureSettingsCache = std::map<std::string, json>;

	/** @brief Mixes value into signature, boost::hash_combine style. */
	void CombineHash(size_t& signature, size_t value);

	/** @brief Mixes a primitive's type and value into signature. */
	void HashSceneSettingValue(size_t& signature, const json& value);

	/** @brief Whether a value is a bool, number or string, the only kinds a scene entry holds. */
	bool IsSceneSettingPrimitive(const json& value);

	/** @brief Whether a scene type keeps its entries in a flat list: interior or time of day. */
	bool IsEntryListSceneType(SceneSettingsManager::SceneType type);

	/** @brief The transition control an object carries under key, or nullopt when absent or out of range. */
	std::optional<float> ReadTransitionControl(const json& object, const char* key,
		SceneSettingsManager::TransitionControl control, std::string_view context);

	/** @brief A directory's subdirectories or regular files in stable path order; errors are logged. */
	std::vector<std::filesystem::path> GetSortedDirectoryPaths(
		const std::filesystem::path& directory, bool directories, std::string_view context);

	/** @brief A directory's .json files in stable path order. */
	std::vector<std::filesystem::path> GetSortedJsonFiles(
		const std::filesystem::path& directory, std::string_view context);

	/** @brief Comparison form of a SPID: hex form ID plus lowercased plugin; unparsable keys pass through. */
	std::string NormalizeLocationFormKey(std::string_view formKey);

	/** @brief Re-spells a SPID the way FormIdToSpid does once the data handler can resolve it. */
	std::string CanonicalizeResolvedLocationFormKey(std::string_view formKey);

	/// Location form keys become directory names, so refuse anything that could escape the overwrites root.
	bool IsSafeLocationFormKey(std::string_view formKey);

	/** @brief Reads a string field into value; false only when the field exists but is not a string. */
	bool ReadOptionalStringField(const json& object, std::string_view field, std::string& value,
		std::string_view context);

	/** @brief Parses a JSON object file, refusing files over kMaxSceneOverwriteFileSize. */
	bool ReadBoundedSceneJson(const std::filesystem::path& path, json& data);

	/**
	 * @brief Readies one weather or location's raw user settings in section for an edit and returns them.
	 * @param canonicalKey The target's key as it is written back.
	 * @param namesTarget Whether a raw key, possibly spelled differently, names the same target.
	 * @param replaceMalformedEntries Empty a non-array "entries" in every raw object naming the target.
	 * @param dropAliasMetadata Called on those objects other than canonicalKey's, whose metadata it duplicates.
	 */
	json& PrepareRawUserTarget(json& section, const std::string& canonicalKey,
		const std::function<bool(const std::string&)>& namesTarget, bool replaceMalformedEntries,
		const std::function<void(json&)>& dropAliasMetadata = {});

	/** @brief Whether a value is a float: TOD and weather can only interpolate floats, not toggles or enums. */
	bool IsNumericValue(const json& value);

	/// A hand-written file may spell a float as `1`. The catalog accepts that for a float setting, so widen
	/// it at the parse boundary rather than let IsNumericValue drop the entry or skip its transition.
	void WidenParsedIntegerToFloat(const std::string& featureShortName, const std::vector<std::string>& settingPath,
		const std::string& settingKey, json& value);

	/** @brief Whether a path segment is the "settings" wrapper that policy addresses skip. */
	bool IsSceneSettingPathWrapper(std::string_view token);

	/** @brief Case- and spacing-insensitive form of an address token, so identifiers match their display names. */
	std::string NormalizeSceneSettingAddressToken(std::string_view token);

	/** @brief Whether two address tokens are equal once normalized. */
	bool SceneSettingAddressTokensEqual(std::string_view lhs, std::string_view rhs);

	/** @brief Whether a policy path is a token-wise prefix of an address. */
	bool IsSceneSettingPolicyPrefix(
		const std::vector<std::string>& address, const SceneSettingsPolicy::SettingPolicyPath& prefix);

	/** @brief Whether any policy path is a prefix of the address. */
	bool MatchesSceneSettingPolicy(const std::vector<std::string>& address,
		const std::vector<SceneSettingsPolicy::SettingPolicyPath>& paths);

	/** @brief Policy address of a setting: feature, path without wrappers, then key. */
	std::vector<std::string> GetSceneSettingAddress(const std::string& featureShortName,
		const std::vector<std::string>& settingPath, const std::string& settingKey);

	/** @brief Whether the setting falls under the global scene blacklist. */
	bool IsBlacklistedSceneSetting(const std::string& featureShortName,
		const std::vector<std::string>& settingPath, const std::string& settingKey);

	/** @brief Whether value can replace featureValue: same JSON type, or both numbers. */
	bool IsCompatibleSceneSettingValue(const json& featureValue, const json& value);

	/** @brief Joins parts and an optional leaf with the display separator. */
	std::string JoinDisplayParts(const std::vector<std::string>& parts, std::string_view leaf);

	/// Writes into a caller-owned buffer so repeated lookups can reuse one allocation.
	void ToCatalogPath(const std::vector<std::string>& path, std::string& out);

	/** @brief Localized selector path of a setting with ImGui IDs stripped. */
	std::vector<std::string> GetCatalogSelectorPath(const SceneSettingsCatalog::SettingMetadata& setting);

	/** @brief Display path of a setting minus the parts its selector already shows. */
	std::vector<std::string> GetCatalogContextPath(const SceneSettingsCatalog::SettingMetadata& setting);

	/** @brief The setting's display scale, 1 when unset or invalid. */
	double GetCatalogNumericDisplayScale(const SceneSettingsCatalog::SettingMetadata& setting);

	/** @brief Maps a stored value into the control's display space; false when it has no finite image. */
	bool ConvertCatalogNumericStoredToDisplay(const SceneSettingsCatalog::SettingMetadata& setting,
		double storedValue, double& displayValue);

	/** @brief Inverse of ConvertCatalogNumericStoredToDisplay. */
	bool ConvertCatalogNumericDisplayToStored(const SceneSettingsCatalog::SettingMetadata& setting,
		double displayValue, double& storedValue);

	/** @brief Clamps a value into its control's range, since applying bypasses the ImGui bound.
	 *  @return Whether the value was changed. */
	bool ClampCatalogNumericValue(const SceneSettingsCatalog::SettingMetadata& setting, json& value);

	/// An out-of-range entry re-applies every frame its scene is active, so the report is one-shot.
	void WarnOnceAboutClampedSceneSetting(std::string_view featureShortName,
		const SceneSettingsCatalog::SettingMetadata& setting, const json& authored, const json& clamped);

	/** @brief The "All" component stored alongside a setting's aggregate, or null when it has none. */
	const SceneSettingsCatalog::SettingMetadata* FindStoredAllComponent(
		const SceneSettingsCatalog::SettingMetadata& setting);

	/** @brief How a setting is edited: scalar, color, or numeric vector. */
	SceneSettingControlType GetCatalogControlType(const SceneSettingsCatalog::SettingMetadata& setting);

	/** @brief Localized RGBA or XYZW label of a component index; empty outside 0..3. */
	std::string GetSettingComponentName(SceneSettingControlType type, std::int8_t componentIndex);

	/** @brief The catalog's component label, else "All" or the channel name. */
	std::string GetCatalogComponentDisplayName(
		const SceneSettingsCatalog::SettingMetadata& setting, SceneSettingControlType controlType);

	/** @brief Everything the UI needs to draw a setting's control. */
	SceneSettingsManager::SettingControlInfo MakeSettingControlInfo(
		const SceneSettingsCatalog::SettingMetadata& setting);

	/** @brief Whether a value's JSON type fits the setting's catalog type; floats also accept integers. */
	bool IsCatalogValueCompatible(const SceneSettingsCatalog::SettingMetadata& setting, const json& value);

	/** @brief Whether the entry addresses this exact setting. */
	bool IsSameSetting(const SceneSettingsManager::SettingEntry& entry, const std::string& featureShortName,
		const std::vector<std::string>& settingPath, const std::string& settingKey);

	/** @brief Whether entries hold this setting at this period, from any source or only the given one. */
	bool HasEntryForPeriod(const std::vector<SceneSettingsManager::SettingEntry>& entries, const std::string& featureShortName,
		const std::vector<std::string>& settingPath, const std::string& settingKey, SceneSettingsManager::TimeOfDayPeriod period,
		std::optional<SceneSettingsManager::EntrySource> source);

	/** @brief "path / Feature.key" name for log lines. */
	std::string GetSettingLogName(const std::string& featureShortName,
		const std::vector<std::string>& settingPath, const std::string& settingKey);

	/** @brief The object at path, creating missing levels, or null when an existing level is not an object. */
	json* GetOrCreateObjectAtPath(json& data, const std::vector<std::string>& path);

	/** @brief Erases settingKey under path from pathIndex on, pruning objects it leaves empty. */
	bool RemoveObjectValueAtPath(json& data, const std::vector<std::string>& path,
		size_t pathIndex, const std::string& settingKey);

	/** @brief The object at path, or null when any level is missing or not an object. */
	const json* GetObjectAtPath(const json& data, const std::vector<std::string>& path);

	/** @brief Parses a whole segment as an array index. */
	bool ParseCatalogArrayIndex(std::string_view value, size_t& index);

	/** @brief The node at a catalog path, stepping through objects by key and arrays by index. */
	template <class Json>
	Json* GetCatalogNodeAtPath(Json& data, const std::vector<std::string>& path)
	{
		auto* node = &data;
		for (const auto& segment : path) {
			if (node->is_object()) {
				auto it = node->find(segment);
				if (it == node->end())
					return nullptr;
				node = &*it;
				continue;
			}

			size_t index = 0;
			if (!node->is_array() || !ParseCatalogArrayIndex(segment, index) || index >= node->size())
				return nullptr;
			node = &(*node)[index];
		}
		return node;
	}

	/** @brief The node a setting serializes to, narrowed to its component; null when absent. */
	template <class Json>
	Json* GetCatalogSerializedValue(Json& data, const SceneSettingsCatalog::SettingMetadata& setting)
	{
		auto* parent = GetCatalogNodeAtPath(data, SplitCatalogPath(setting.serializedPath));
		if (!parent)
			return nullptr;

		Json* value = nullptr;
		if (parent->is_object()) {
			auto valueIt = parent->find(setting.serializedKey);
			if (valueIt == parent->end())
				return nullptr;
			value = &*valueIt;
		} else {
			size_t index = 0;
			if (!parent->is_array() || !ParseCatalogArrayIndex(setting.serializedKey, index) ||
				index >= parent->size())
				return nullptr;
			value = &(*parent)[index];
		}

		if (setting.serializedComponent < 0)
			return value;
		const auto component = static_cast<size_t>(setting.serializedComponent);
		if (!value->is_array() || component >= value->size())
			return nullptr;
		return &(*value)[component];
	}

	/** @brief Calls back with the path, key and value of every primitive leaf, skipping metadata keys. */
	void CollectOverwriteEntries(const json& data, const std::vector<std::string>& settingPath,
		const std::function<void(const std::vector<std::string>&, const std::string&, const json&)>& callback);

	/** @brief Whether any policy path starts with the feature. */
	bool PolicyContainsFeature(const std::vector<SceneSettingsPolicy::SettingPolicyPath>& paths,
		std::string_view featureShortName);

	/** @brief Whether the location whitelist admits any of the feature's settings. */
	bool IsInteriorOnlyFeatureAllowed(std::string_view featureShortName);

	/** @brief Whether the time-of-day whitelist admits any of the feature's settings. */
	bool IsTimeOfDayFeatureAllowed(std::string_view featureShortName);

	/// Whitelists are address prefixes, so a feature can be admitted whole or setting by setting.
	bool IsSettingAllowedBySceneTypePolicy(SceneSettingsManager::SceneType type,
		const std::string& featureShortName, const std::vector<std::string>& settingPath,
		const std::string& settingKey);

	/** @brief Uncached policy check: scene-controllable and not blacklisted. */
	bool ComputeCatalogSettingAllowedByPolicy(const SceneSettingsCatalog::SettingMetadata& setting);

	/** @brief ComputeCatalogSettingAllowedByPolicy, looked up from a table built once. */
	bool IsCatalogSettingAllowedByPolicy(const SceneSettingsCatalog::SettingMetadata& setting);

	/// Catalog and policy are compile-time constant, so every setting is decided once; the table is
	/// read-only after its thread-safe static init, as both the render and menu threads read it.
	bool IsCatalogSettingAllowedForSceneType(SceneSettingsManager::SceneType type,
		const SceneSettingsCatalog::SettingMetadata& setting);

	/** @brief The policy-allowed catalog setting at this address, optionally only a transitionable one; null otherwise. */
	const SceneSettingsCatalog::SettingMetadata* FindAllowedCatalogSetting(
		std::string_view featureShortName, const std::vector<std::string>& settingPath,
		std::string_view settingKey, bool requireTransitionable = false);

	/** @brief Saves a feature's settings; runs on the render thread, so a throwing feature degrades to false. */
	bool TrySaveFeatureSettings(Feature& feature, std::string_view context, json& settings);

	/** @brief Reads a setting's current value from the feature. */
	bool GetCatalogSettingValue(
		Feature& feature, const SceneSettingsCatalog::SettingMetadata& setting, json& value);

	/// The catalog is grouped by feature, so a feature's settings are one contiguous range.
	std::span<const SceneSettingsCatalog::SettingMetadata> GetCatalogFeatureSettings(
		std::string_view featureShortName);

	/** @brief Whether the feature has any setting allowed for the scene type. */
	bool CatalogHasSceneSettings(
		std::string_view featureShortName, SceneSettingsManager::SceneType type);

	/** @brief Loaded features with at least one setting allowed for the scene type. */
	std::vector<std::string> GetLoadedCatalogFeatureNames(SceneSettingsManager::SceneType type);

	/** @brief The weather region the sky picked for this cell, or 0 when it has none. */
	RE::FormID GetActiveRegionId(RE::TESObjectCELL* cell);

	/** @brief Full display name of a setting, falling back to its key when the catalog lacks it. */
	std::string GetSceneSettingDisplayName(const std::string& featureShortName,
		const std::vector<std::string>& settingPath, const std::string& settingKey);

	/** @brief GetCatalogSettingValue through an optional per-feature snapshot cache. */
	bool GetFeatureSettingValueForValidation(Feature& feature, const std::string& featureShortName,
		const SceneSettingsCatalog::SettingMetadata& setting,
		FeatureSettingsCache* featureSettingsCache, json& featureValue);

	/** @brief Whether value is legal for the setting: type, range, choices and, with requireNumeric, a transitionable float. */
	bool IsSceneSettingValueAllowed(const json& featureValue,
		const SceneSettingsCatalog::SettingMetadata& setting, const json& value, bool requireNumeric);

	/** @brief Checks an entry against the scene type's whitelist, the blacklist, the catalog and the feature's live value. */
	bool ValidateSceneSettingEntry(std::string_view context, SceneSettingsManager::SceneType type,
		const std::string& featureShortName, const std::vector<std::string>& settingPath,
		const std::string& settingKey, const json& value, bool requireNumeric,
		FeatureSettingsCache* featureSettingsCache = nullptr);

	/** @brief Validates every update, then writes all of them or none.
	 *  @param userEntriesChanged Set when a user entry changed. */
	bool ApplyEntryValueUpdates(std::string_view context, SceneSettingsManager::SceneType type,
		std::vector<SceneSettingsManager::SettingEntry>& entries,
		std::span<const SceneSettingsManager::EntryValueUpdate> updates,
		bool requireNumeric, bool& userEntriesChanged);
}
