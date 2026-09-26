#include "SceneSettingsManager.h"

#include "SceneSettingsContextRules.h"
#include "SceneSettingsInternal.h"
#include "SceneSettingsLocationTargets.h"
#include "SceneSettingsOverwrites.h"

#include <algorithm>
#include <cmath>
#include <set>

using namespace SceneSettingsInternal;
using namespace SceneSettingsOverwrites;
using namespace SceneSettingsLocationTargets;
using namespace SceneSettingsContextRules;

// --- Per-Location Scene Settings ---

const SceneSettingsManager::LocationSceneConfig SceneSettingsManager::kEmptyLocationConfig{};

std::string SceneSettingsManager::GetLocationConfigKey(LocationTargetType type, std::string_view formKey)
{
	return std::format("{}:{}", GetLocationTargetTypeName(type), NormalizeLocationFormKey(formKey));
}

const char* SceneSettingsManager::GetLocationSectionName(LocationTargetType type)
{
	switch (type) {
	case LocationTargetType::Worldspace:
		return "worldspaces";
	case LocationTargetType::LocationType:
		return "locationTypes";
	case LocationTargetType::Region:
		return "regions";
	case LocationTargetType::Location:
		return "locations";
	case LocationTargetType::Cell:
		return "cells";
	default:
		return "invalid";
	}
}

const char* SceneSettingsManager::GetLocationTargetTypeName(LocationTargetType type)
{
	switch (type) {
	case LocationTargetType::Worldspace:
		return "Worldspace";
	case LocationTargetType::LocationType:
		return "LocationType";
	case LocationTargetType::Region:
		return "Region";
	case LocationTargetType::Location:
		return "Location";
	case LocationTargetType::Cell:
		return "Cell";
	default:
		return "Invalid";
	}
}

const std::vector<SceneSettingsManager::LocationTarget>& SceneSettingsManager::GetCurrentLocationTargets() const
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	auto* cell = player ? player->GetParentCell() : nullptr;
	if (!player || !cell) {
		cachedTargetLocationId = 0;
		cachedTargetCellId = 0;
		cachedTargetRegionId = 0;
		locationTargetsCached = false;
		cachedLocationTargets.clear();
		return cachedLocationTargets;
	}

	auto* location = player->GetCurrentLocation();
	if (!location)
		location = cell->GetLocation();
	const auto locationId = location ? location->GetFormID() : 0;
	const auto cellId = cell->GetFormID();
	// Regions overlap within a cell, so the winning one can change without the cell changing.
	const auto regionId = GetActiveRegionId(cell);
	if (locationTargetsCached && cachedTargetLocationId == locationId &&
		cachedTargetCellId == cellId && cachedTargetRegionId == regionId)
		return cachedLocationTargets;

	cachedLocationTargets = BuildLocationTargetChain(location, cell);
	cachedTargetRegionId = regionId;
	cachedTargetLocationId = locationId;
	cachedTargetCellId = cellId;
	locationTargetsCached = true;
	return cachedLocationTargets;
}

std::vector<SceneSettingsManager::LocationTarget> SceneSettingsManager::GetAuthoredLocationTargets() const
{
	std::vector<LocationTarget> targets;
	for (const auto& [configKey, config] : locationSceneConfigs) {
		if (!config.userAuthored)
			continue;
		// formId is left unresolved: an authored target may name content that is not loaded, and
		// SpidToFormId warns on every miss. Callers that need the live form resolve it themselves.
		targets.push_back({
			.type = config.type,
			.formKey = config.formKey,
			.name = config.name.empty() ? configKey : config.name,
			.editorId = config.editorId,
			.cocCode = config.cocCode,
		});
	}
	std::ranges::sort(targets, [](const auto& lhs, const auto& rhs) { return lhs.name < rhs.name; });
	return targets;
}

const std::vector<SceneSettingsManager::LocationTarget>& SceneSettingsManager::GetLocationCatalog() const
{
	if (!cachedLocationCatalog)
		cachedLocationCatalog = BuildLocationCatalog();
	return *cachedLocationCatalog;
}

bool SceneSettingsManager::AddLocationTarget(const LocationTarget& target)
{
	if (!TryEnsureLocationDataLoaded() || target.formKey.empty() ||
		IsLocationTargetAuthored(target.type, target.formKey))
		return false;

	EnsureAuthoredLocationConfig(target.type, target.formKey, target.name, target.cocCode, target.editorId);
	PrepareLocationUserSettingsMutation(target.type, target.formKey, false);
	SaveAllUserSettings();
	return true;
}

bool SceneSettingsManager::IsLocationTargetAuthored(LocationTargetType type, std::string_view formKey) const
{
	auto it = locationSceneConfigs.find(GetLocationConfigKey(type, formKey));
	return it != locationSceneConfigs.end() && it->second.userAuthored;
}

void SceneSettingsManager::RemoveLocationTarget(LocationTargetType type, const std::string& formKey)
{
	if (!TryEnsureLocationDataLoaded())
		return;

	auto it = locationSceneConfigs.find(GetLocationConfigKey(type, formKey));
	if (it == locationSceneConfigs.end())
		return;

	const auto removedEntries = std::erase_if(it->second.entries,
		[](const SettingEntry& entry) { return entry.source == EntrySource::User; });
	// Shipped overwrites keep the config alive; without them nothing is left to remember.
	if (it->second.entries.empty())
		locationSceneConfigs.erase(it);
	else
		it->second.userAuthored = false;

	// The loader treats presence in the user document as ownership, so the target has to leave it
	// entirely. Deleting only its entries would resurrect the target on the next launch.
	PrepareLocationUserSettingsMutation(type, formKey, false);
	for (const auto* sectionName : { GetLocationSectionName(type),
			 type == LocationTargetType::LocationType ? kLegacyLocationTypeSectionName : nullptr }) {
		if (!sectionName)
			continue;
		if (auto sectionIt = unresolvedLocationUserSettings.find(sectionName);
			sectionIt != unresolvedLocationUserSettings.end())
			for (const auto& rawFormKey : MatchingRawLocationKeys(*sectionIt, type, formKey))
				sectionIt->erase(rawFormKey);
	}

	if (removedEntries != 0)
		BumpEntryPresentationRevision();
	SaveAllUserSettings();
	ReapplyIfActive();
}

SceneSettingsManager::LocationSceneConfig& SceneSettingsManager::GetLocationConfigMut(
	LocationTargetType type, const std::string& formKey, const std::string& name)
{
	const auto canonicalFormKey = CanonicalizeResolvedLocationFormKey(formKey);
	auto& config = locationSceneConfigs[GetLocationConfigKey(type, canonicalFormKey)];
	config.type = type;
	config.formKey = canonicalFormKey;
	if (!name.empty())
		config.name = name;
	return config;
}

SceneSettingsManager::LocationSceneConfig& SceneSettingsManager::EnsureAuthoredLocationConfig(
	LocationTargetType type, const std::string& formKey, const std::string& name, const std::string& cocCode,
	const std::string& editorId)
{
	auto& config = GetLocationConfigMut(type, formKey, name);
	if (!cocCode.empty())
		config.cocCode = cocCode;
	if (!editorId.empty())
		config.editorId = editorId;
	config.userAuthored = true;
	return config;
}

const SceneSettingsManager::LocationSceneConfig& SceneSettingsManager::GetLocationConfig(
	LocationTargetType type, std::string_view formKey) const
{
	auto it = locationSceneConfigs.find(GetLocationConfigKey(type, formKey));
	return it != locationSceneConfigs.end() ? it->second : kEmptyLocationConfig;
}

std::optional<json> SceneSettingsManager::ResolveLocationLowerValue(LocationTargetType type,
	std::string_view formKey, const SettingAddress& address, EntrySource selectedSource)
{
	auto baseline = GetBaselineValue(address);
	if (!IsSceneSettingPrimitive(baseline))
		return std::nullopt;
	auto lowerLayers = BuildLocationLowerLayers(type, formKey, selectedSource);
	if (!lowerLayers)
		return std::nullopt;
	if (auto valueIt = lowerLayers->find(address); valueIt != lowerLayers->end() &&
		IsSceneSettingPrimitive(valueIt->second))
		return valueIt->second;
	return baseline;
}

std::optional<SceneSettingsManager::ResolvedSettingMap> SceneSettingsManager::BuildLocationLowerLayers(
	LocationTargetType type, std::string_view formKey, std::optional<EntrySource> selectedSource)
{
	ResolvedSettingMap lowerLayers;
	const auto locationTargets = ResolveLocationTargetChain(type, formKey);
	// The target being edited is not necessarily where the player stands, so its own chain decides
	// which layers sit underneath it. An unresolved chain falls back to the general TOD/weather one.
	const bool interior = GetLocationChainInteriorState(locationTargets).value_or(false);
	RefreshBlendSnapshot(interior);
	if (interior) {
		ResolveInteriorSettings(lowerLayers);
	} else {
		const auto& timeOfDayValues = BuildTimeOfDayValueGroups();
		ResolveTimeOfDaySettings(lowerLayers, timeOfDayValues);
		ResolveWeatherSettings(lowerLayers, timeOfDayValues);
	}

	bool targetFound = false;
	PeriodSettingMap periodValues;
	const auto selectedTargetKey = GetLocationConfigKey(type, formKey);
	for (const auto& target : locationTargets) {
		const auto targetKey = GetLocationConfigKey(target.type, target.formKey);
		auto configIt = locationSceneConfigs.find(targetKey);
		if (targetKey == selectedTargetKey) {
			targetFound = true;
			// A user entry sits above the overwrite it shadows, so the overwrite is its lower layer.
			// A capture deliberately passes the overwrite layer here to resolve beneath it instead.
			if (selectedSource == EntrySource::User && configIt != locationSceneConfigs.end()) {
				std::vector<SettingEntry> overwrites;
				std::ranges::copy_if(configIt->second.entries, std::back_inserter(overwrites),
					[](const SettingEntry& entry) { return entry.source == EntrySource::Overwrite; });
				ResolveLocationLink(overwrites, configIt->second.timeOfDayEnabled, lowerLayers, periodValues, nullptr);
			}
			break;
		}
		if (configIt != locationSceneConfigs.end())
			ResolveLocationLink(
				configIt->second.entries, configIt->second.timeOfDayEnabled, lowerLayers, periodValues, nullptr);
	}
	if (!targetFound)
		return std::nullopt;
	BlendLocationPeriodValues(lowerLayers, periodValues);
	return lowerLayers;
}

void SceneSettingsManager::PrepareLocationUserSettingsMutation(LocationTargetType type,
	std::string_view formKey, bool replaceMalformedEntries)
{
	locationUserSettingsModified = true;
	if (!unresolvedLocationUserSettings.is_object())
		unresolvedLocationUserSettings = json::object();
	const auto* sectionName = GetLocationSectionName(type);
	auto& section = unresolvedLocationUserSettings[sectionName];
	if (!section.is_object())
		section = json::object();
	const auto canonicalFormKey = CanonicalizeResolvedLocationFormKey(formKey);
	const auto targetKey = GetLocationConfigKey(type, canonicalFormKey);
	// Pinning the mode keeps a later preset from flipping which set the user's edits land in.
	if (auto configIt = locationSceneConfigs.find(targetKey); configIt != locationSceneConfigs.end())
		configIt->second.userTimeOfDayEnabled = configIt->second.timeOfDayEnabled;
	if (replaceMalformedEntries) {
		for (auto& [rawFormKey, rawConfig] : section.items()) {
			if (!rawConfig.is_object() ||
				GetLocationConfigKey(type, CanonicalizeResolvedLocationFormKey(rawFormKey)) != targetKey)
				continue;
			auto entriesIt = rawConfig.find("entries");
			if (entriesIt != rawConfig.end() && !entriesIt->is_array())
				*entriesIt = json::array();
			if (rawFormKey != canonicalFormKey) {
				rawConfig.erase("type");
				rawConfig.erase("name");
				rawConfig.erase("editorId");
				rawConfig.erase("coc");
			}
		}
	}

	auto& rawConfig = section[canonicalFormKey];
	if (!rawConfig.is_object())
		rawConfig = json::object();
	if (replaceMalformedEntries) {
		auto entriesIt = rawConfig.find("entries");
		if (entriesIt != rawConfig.end() && !entriesIt->is_array())
			*entriesIt = json::array();
	}
}

void SceneSettingsManager::RemoveLocationSetting(LocationTargetType type, const std::string& formKey, size_t index)
{
	auto it = locationSceneConfigs.find(GetLocationConfigKey(type, formKey));
	if (it == locationSceneConfigs.end() || index >= it->second.entries.size())
		return;

	const auto entry = it->second.entries[index];
	const bool userEntry = entry.source == EntrySource::User;
	if (entry.source == EntrySource::Overwrite && !entry.sourceFilename.empty() &&
		!RemoveSettingFromOverwriteFile(GetLocationOverwritePath(formKey, entry), entry.settingPath, entry.settingKey))
		return;
	it->second.entries.erase(it->second.entries.begin() + static_cast<ptrdiff_t>(index));
	BumpEntryPresentationRevision();
	if (userEntry) {
		PrepareLocationUserSettingsMutation(type, formKey, false);
		SaveAllUserSettings();
	}
	ReapplyIfActive();
}

std::vector<std::string> SceneSettingsManager::MatchingRawLocationKeys(const json& section,
	LocationTargetType type, std::string_view formKey)
{
	std::vector<std::string> keys;
	if (!section.is_object())
		return keys;
	const auto targetKey = GetLocationConfigKey(type, formKey);
	for (const auto& [rawFormKey, rawConfig] : section.items())
		if (rawConfig.is_object() &&
			GetLocationConfigKey(type, CanonicalizeResolvedLocationFormKey(rawFormKey)) == targetKey)
			keys.push_back(rawFormKey);
	return keys;
}

void SceneSettingsManager::SetLocationTransitionSeconds(float seconds, bool deferSave)
{
	if (!std::isfinite(seconds) || !TryEnsureLocationDataLoaded())
		return;
	seconds = std::clamp(seconds, 0.0f, kMaxLocationTransitionSeconds);
	if (std::abs(locationTransitionSeconds - seconds) < kBlendEpsilon)
		return;
	locationTransitionSeconds = seconds;
	locationTransitionModified = true;
	locationUserSettingsModified = true;
	if (deferSave)
		MarkDeferredSceneChanges();
	else
		SaveAllUserSettings();
	ReapplyIfActive();
}

std::optional<float> SceneSettingsManager::GetLocationEntryTransitionSeconds(
	LocationTargetType type, std::string_view formKey, size_t index) const
{
	const auto& config = GetLocationConfig(type, formKey);
	return index < config.entries.size() ? config.entries[index].transitionSeconds : std::nullopt;
}

void SceneSettingsManager::SetLocationEntryTransitionSeconds(LocationTargetType type,
	const std::string& formKey, std::span<const size_t> indices, std::optional<float> seconds,
	bool deferSave)
{
	auto it = locationSceneConfigs.find(GetLocationConfigKey(type, formKey));
	if (it == locationSceneConfigs.end() || indices.empty())
		return;
	if (seconds) {
		if (!std::isfinite(*seconds))
			return;
		seconds = std::clamp(*seconds, 0.0f, kMaxLocationTransitionSeconds);
	}

	// A component of an aggregate control cannot transition on its own, so the edit takes its siblings.
	auto& locationEntries = it->second.entries;
	std::set<size_t> expandedIndices;
	for (const auto index : indices) {
		if (index >= locationEntries.size())
			return;
		expandedIndices.insert(index);
		const auto& selectedEntry = locationEntries[index];
		const auto selectedGroup = GetCopyGroupKey({ selectedEntry.featureShortName,
			selectedEntry.settingPath, selectedEntry.settingKey });
		if (std::get<5>(selectedGroup) == SettingControlType::Scalar)
			continue;
		for (size_t candidateIndex = 0; candidateIndex < locationEntries.size(); ++candidateIndex) {
			const auto& candidate = locationEntries[candidateIndex];
			if (candidate.source == selectedEntry.source &&
				GetCopyGroupKey({ candidate.featureShortName, candidate.settingPath,
					candidate.settingKey }) == selectedGroup)
				expandedIndices.insert(candidateIndex);
		}
	}

	// Reject the whole edit up front: a transition only means something on a transitionable user float.
	for (const auto index : expandedIndices) {
		const auto& entry = locationEntries[index];
		if (entry.source != EntrySource::User || !IsNumericValue(entry.value) ||
			!FindAllowedCatalogSetting(entry.featureShortName, entry.settingPath, entry.settingKey, true))
			return;
	}

	bool changed = false;
	for (const auto index : expandedIndices) {
		auto& entry = locationEntries[index];
		if (entry.transitionSeconds == seconds)
			continue;
		entry.transitionSeconds = seconds;
		entry.retainSerializedTransition = false;
		changed = true;
	}
	if (!changed)
		return;

	PrepareLocationUserSettingsMutation(type, formKey, false);
	if (deferSave)
		MarkDeferredSceneChanges();
	else
		SaveAllUserSettings();
	ReapplyIfActive();
}

bool SceneSettingsManager::HasLocationEntry(LocationTargetType type, std::string_view formKey,
	const std::string& featureShortName, const std::vector<std::string>& settingPath,
	const std::string& settingKey, TimeOfDayPeriod period, std::optional<EntrySource> source) const
{
	const auto& config = GetLocationConfig(type, formKey);
	return std::any_of(config.entries.begin(), config.entries.end(), [&](const auto& entry) {
		return (!source || entry.source == *source) && entry.period == period &&
		       IsSameSetting(entry, featureShortName, settingPath, settingKey);
	});
}
