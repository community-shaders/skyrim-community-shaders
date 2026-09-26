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
	/// Marks a SceneSettings root json as a preset's identity file rather than any other document.
	constexpr const char* kPresetMetadataKey = "presetMetadata";
	constexpr const char* kPresetMetadataNameKey = "name";
	constexpr const char* kPresetMetadataVersionKey = "version";
	constexpr const char* kTimeOfDayTransitionHoursKey = "periodTransitionHours";

	using namespace Util::Settings;

	using SceneSettingControlType = SceneSettingsManager::SettingControlType;
	using FeatureSettingsCache = std::map<std::string, json>;

	void CombineHash(size_t& signature, size_t value);

	void HashSceneSettingValue(size_t& signature, const json& value);

	bool IsSceneSettingPrimitive(const json& value);

	bool IsEntryListSceneType(SceneSettingsManager::SceneType type);

	bool WriteJsonAtomically(const std::filesystem::path& path, const json& data, int indent,
		std::string_view context);

	/** @brief The period transition an object carries, or nullopt when absent or out of range. */
	std::optional<float> ReadTimeOfDayTransitionHours(const json& object, std::string_view context);

	std::vector<std::filesystem::path> GetSortedDirectoryPaths(
		const std::filesystem::path& directory, bool directories, std::string_view context);

	std::vector<std::filesystem::path> GetSortedJsonFiles(
		const std::filesystem::path& directory, std::string_view context);

	std::string NormalizeLocationFormKey(std::string_view formKey);

	std::string CanonicalizeResolvedLocationFormKey(std::string_view formKey);

	/// Location form keys become directory names, so refuse anything that could escape the overwrites root.
	bool IsSafeLocationFormKey(std::string_view formKey);

	bool ReadOptionalStringField(const json& object, std::string_view field, std::string& value,
		std::string_view context);

	bool IsSceneMetadataKey(std::string_view key);

	bool ReadBoundedSceneJson(const std::filesystem::path& path, json& data);

	// TOD/weather can only interpolate float settings, not integer toggles or enum values.
	bool IsNumericValue(const json& value);

	/// A hand-written file may spell a float as `1`. The catalog accepts that for a float setting, so widen
	/// it at the parse boundary rather than let IsNumericValue drop the entry or skip its transition.
	void WidenParsedIntegerToFloat(const std::string& featureShortName, const std::vector<std::string>& settingPath,
		const std::string& settingKey, json& value);

	bool IsSceneSettingPathWrapper(std::string_view token);

	std::string NormalizeSceneSettingAddressToken(std::string_view token);

	bool SceneSettingAddressTokensEqual(std::string_view lhs, std::string_view rhs);

	bool IsSceneSettingPolicyPrefix(
		const std::vector<std::string>& address, const SceneSettingsPolicy::SettingPolicyPath& prefix);

	bool MatchesSceneSettingPolicy(const std::vector<std::string>& address,
		const std::vector<SceneSettingsPolicy::SettingPolicyPath>& paths);

	std::vector<std::string> GetSceneSettingAddress(const std::string& featureShortName,
		const std::vector<std::string>& settingPath, const std::string& settingKey);

	bool IsBlacklistedSceneSetting(const std::string& featureShortName,
		const std::vector<std::string>& settingPath, const std::string& settingKey);

	bool HasSceneOverwriteContent(const json& data);

	bool IsCompatibleSceneSettingValue(const json& featureValue, const json& value);

	std::string JoinDisplayParts(const std::vector<std::string>& parts, std::string_view leaf);

	/// Writes into a caller-owned buffer so repeated lookups can reuse one allocation.
	void ToCatalogPath(const std::vector<std::string>& path, std::string& out);

	std::vector<std::string> GetCatalogSelectorPath(const SceneSettingsCatalog::SettingMetadata& setting);

	bool EqualDisplayText(std::string_view lhs, std::string_view rhs);

	std::vector<std::string> GetCatalogContextPath(const SceneSettingsCatalog::SettingMetadata& setting);

	double GetCatalogNumericDisplayScale(const SceneSettingsCatalog::SettingMetadata& setting);

	bool ConvertCatalogNumericStoredToDisplay(const SceneSettingsCatalog::SettingMetadata& setting,
		double storedValue, double& displayValue);

	bool ConvertCatalogNumericDisplayToStored(const SceneSettingsCatalog::SettingMetadata& setting,
		double displayValue, double& storedValue);

	/// Applying a value bypasses the ImGui call that would have bounded it, so a document authored
	/// elsewhere can push a control past its range. Bounds are in display space and both transforms are
	/// monotonic, so the range converts once rather than every value round-tripping.
	bool ClampCatalogNumericValue(const SceneSettingsCatalog::SettingMetadata& setting, json& value);

	/// An out-of-range entry re-applies every frame its scene is active, so the report is one-shot.
	void WarnOnceAboutClampedSceneSetting(std::string_view featureShortName,
		const SceneSettingsCatalog::SettingMetadata& setting, const json& authored, const json& clamped);

	const SceneSettingsCatalog::SettingMetadata* FindStoredAllComponent(
		const SceneSettingsCatalog::SettingMetadata& setting);

	SceneSettingControlType GetCatalogControlType(const SceneSettingsCatalog::SettingMetadata& setting);

	std::string GetSettingComponentName(SceneSettingControlType type, std::int8_t componentIndex);

	std::string GetCatalogComponentDisplayName(
		const SceneSettingsCatalog::SettingMetadata& setting, SceneSettingControlType controlType);

	SceneSettingsManager::SettingControlInfo MakeSettingControlInfo(
		const SceneSettingsCatalog::SettingMetadata& setting);

	bool IsCatalogValueCompatible(const SceneSettingsCatalog::SettingMetadata& setting, const json& value);

	bool IsSameSetting(const SceneSettingsManager::SettingEntry& entry, const std::string& featureShortName,
		const std::vector<std::string>& settingPath, const std::string& settingKey);

	std::string GetSettingLogName(const std::string& featureShortName,
		const std::vector<std::string>& settingPath, const std::string& settingKey);

	json* GetObjectAtPath(json& data, const std::vector<std::string>& path, bool create);

	bool RemoveObjectValueAtPath(json& data, const std::vector<std::string>& path,
		size_t pathIndex, const std::string& settingKey);

	const json* GetObjectAtPath(const json& data, const std::vector<std::string>& path);

	json* GetObjectAtPath(json& data, const std::vector<std::string>& path);

	bool ParseCatalogArrayIndex(std::string_view value, size_t& index);

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

	void CollectOverwriteEntries(const json& data, const std::vector<std::string>& settingPath,
		const std::function<void(const std::vector<std::string>&, const std::string&, const json&)>& callback);

	bool PolicyContainsFeature(const std::vector<SceneSettingsPolicy::SettingPolicyPath>& paths,
		std::string_view featureShortName);

	bool IsInteriorOnlyFeatureAllowed(std::string_view featureShortName);

	bool IsTimeOfDayFeatureAllowed(std::string_view featureShortName);

	/// Whitelists are address prefixes, so a feature can be admitted whole or setting by setting.
	bool IsSettingAllowedBySceneTypePolicy(SceneSettingsManager::SceneType type,
		const std::string& featureShortName, const std::vector<std::string>& settingPath,
		const std::string& settingKey);

	bool ComputeCatalogSettingAllowedByPolicy(const SceneSettingsCatalog::SettingMetadata& setting);

	bool IsCatalogSettingAllowedByPolicy(const SceneSettingsCatalog::SettingMetadata& setting);

	/// Catalog and policy are both compile-time constant, so decide every setting up front.
	/// Built once (thread-safe static init) and read-only afterwards: read from both the render
	/// thread and the menu thread.
	bool IsCatalogSettingAllowedForSceneType(SceneSettingsManager::SceneType type,
		const SceneSettingsCatalog::SettingMetadata& setting);

	const SceneSettingsCatalog::SettingMetadata* FindAllowedCatalogSetting(
		std::string_view featureShortName, const std::vector<std::string>& settingPath,
		std::string_view settingKey, bool requireTransitionable = false);

	// Runs on the render thread, so a throwing feature must degrade to "skip the override", not crash.
	bool TrySaveFeatureSettings(Feature& feature, std::string_view context, json& settings);

	bool GetCatalogSettingValue(
		Feature& feature, const SceneSettingsCatalog::SettingMetadata& setting, json& value);

	/// The catalog is grouped by feature, so a feature's settings are one contiguous range.
	std::span<const SceneSettingsCatalog::SettingMetadata> GetCatalogFeatureSettings(
		std::string_view featureShortName);

	bool CatalogHasSceneSettings(
		std::string_view featureShortName, SceneSettingsManager::SceneType type);

	std::vector<std::string> GetLoadedCatalogFeatureNames(SceneSettingsManager::SceneType type);

	/** @brief The weather region the sky picked for this cell, or 0 when it has none. */
	RE::FormID GetActiveRegionId(RE::TESObjectCELL* cell);

	std::string GetSceneSettingDisplayName(const std::string& featureShortName,
		const std::vector<std::string>& settingPath, const std::string& settingKey);

	bool GetFeatureSettingValueForValidation(Feature& feature, const std::string& featureShortName,
		const SceneSettingsCatalog::SettingMetadata& setting,
		FeatureSettingsCache* featureSettingsCache, json& featureValue);

	bool IsSceneSettingValueAllowed(const json& featureValue,
		const SceneSettingsCatalog::SettingMetadata& setting, const json& value, bool requireNumeric);

	/** @brief Checks an entry against the scene type's whitelist, the blacklist, the catalog and the feature's live value. */
	bool ValidateSceneSettingEntry(std::string_view context, SceneSettingsManager::SceneType type,
		const std::string& featureShortName, const std::vector<std::string>& settingPath,
		const std::string& settingKey, const json& value, bool requireNumeric,
		FeatureSettingsCache* featureSettingsCache = nullptr);

	bool ApplyEntryValueUpdates(std::string_view context, SceneSettingsManager::SceneType type,
		std::vector<SceneSettingsManager::SettingEntry>& entries,
		std::span<const SceneSettingsManager::EntryValueUpdate> updates,
		bool requireNumeric, bool& userEntriesChanged);
}
