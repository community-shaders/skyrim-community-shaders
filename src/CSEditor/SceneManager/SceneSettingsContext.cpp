#include "SceneSettingsManager.h"

#include "SceneSettingsContextRules.h"
#include "SceneSettingsInternal.h"
#include "SceneSettingsLocationTargets.h"
#include "Utils/SettingsCatalog.h"

#include <algorithm>
#include <cassert>
#include <filesystem>

using namespace SceneSettingsInternal;
using namespace SceneSettingsLocationTargets;
using namespace SceneSettingsContextRules;

bool SceneSettingsManager::IsPeriodicContext(SceneContextType type)
{
	return type != SceneContextType::Interior;
}

bool SceneSettingsManager::IsValidSceneContext(const SceneContextId& context)
{
	const auto periodIndex = static_cast<int>(context.period);
	const bool realPeriod = periodIndex >= 0 && periodIndex < kPeriodCount;
	// Weather and locations keep a flat set (Count) beside their per-period one.
	const bool periodOrFlat = realPeriod || context.period == TimeOfDayPeriod::Count;
	switch (context.type) {
	case SceneContextType::Interior:
		return context.period == TimeOfDayPeriod::Count && context.weatherId == 0 &&
		       context.locationFormKey.empty() && context.locationType == LocationTargetType::Location;
	case SceneContextType::TimeOfDay:
		return realPeriod && context.weatherId == 0 &&
		       context.locationFormKey.empty() && context.locationType == LocationTargetType::Location;
	case SceneContextType::Weather:
		return context.weatherId != 0 && periodOrFlat &&
		       context.locationFormKey.empty() && context.locationType == LocationTargetType::Location;
	case SceneContextType::Location:
		return periodOrFlat && context.weatherId == 0 &&
		       IsValidLocationTargetType(context.locationType) && !context.locationFormKey.empty();
	default:
		return false;
	}
}

bool SceneSettingsManager::IsSameSceneContext(const SceneContextId& lhs, const SceneContextId& rhs)
{
	if (lhs.type != rhs.type)
		return false;
	switch (lhs.type) {
	case SceneContextType::Interior:
		return true;
	case SceneContextType::TimeOfDay:
		return lhs.period == rhs.period;
	case SceneContextType::Weather:
		return lhs.weatherId == rhs.weatherId && lhs.period == rhs.period;
	case SceneContextType::Location:
		return lhs.locationType == rhs.locationType && lhs.period == rhs.period &&
		       NormalizeLocationFormKey(lhs.locationFormKey) == NormalizeLocationFormKey(rhs.locationFormKey);
	default:
		return false;
	}
}

SceneSettingsManager::EffectiveContextEntries SceneSettingsManager::BuildEffectiveContextEntries(
	const std::vector<SettingEntry>& contextEntries, const SceneContextId& context)
{
	EffectiveContextEntries effectiveEntries;
	// User last: a user entry shadows the overwrite it was authored over.
	for (auto entrySource : { EntrySource::Overwrite, EntrySource::User })
		for (const auto& entry : contextEntries)
			if (entry.source == entrySource && !entry.paused && EntryBelongsToContext(entry, context))
				effectiveEntries[{ entry.featureShortName, entry.settingPath, entry.settingKey }] = &entry;
	// A tombstone shadows the overwrite beneath it and then supplies nothing, so the address leaves the
	// set entirely: neither copy nor export may offer a value the user deleted.
	std::erase_if(effectiveEntries, [](const auto& item) { return item.second->deleted; });
	return effectiveEntries;
}

std::string SceneSettingsManager::GetOverwriteModName(const SettingEntry& entry)
{
	const auto stem = std::filesystem::path(entry.sourceFilename).stem().string();
	const auto lastUnderscore = stem.rfind('_');
	return lastUnderscore == std::string::npos ? stem : stem.substr(0, lastUnderscore);
}

SceneSettingsManager::SettingProvenance SceneSettingsManager::GetSettingProvenance(
	const SceneContextId& context, const std::string& featureShortName,
	const std::vector<std::string>& settingPath, const std::string& settingKey) const
{
	SettingProvenance provenance;
	if (!IsValidSceneContext(context))
		return provenance;

	for (const auto& entry : GetContextEntries(context)) {
		if (!IsEntryActive(entry) || !IsSameSetting(entry, featureShortName, settingPath, settingKey) ||
			!EntryBelongsToContext(entry, context))
			continue;
		if (entry.source == EntrySource::Overwrite) {
			// Discovery order is the entry order, and the last file discovered wins.
			provenance.layer = SettingLayer::Overwrite;
			continue;
		}
		// A user entry outranks every overwrite, so it settles the layer outright.
		provenance.layer = entry.deleted ? SettingLayer::Deleted : SettingLayer::User;
	}
	return provenance;
}

const std::vector<SceneSettingsManager::SettingEntry>* SceneSettingsManager::GetCopyContextEntries(
	const SceneContextId& context) const
{
	if (!IsValidSceneContext(context))
		return nullptr;
	switch (context.type) {
	case SceneContextType::Interior:
		return &GetEntries(SceneType::InteriorOnly);
	case SceneContextType::TimeOfDay:
		return &GetEntries(SceneType::TimeOfDay);
	case SceneContextType::Weather:
		if (auto configIt = weatherSceneConfigs.find(context.weatherId); configIt != weatherSceneConfigs.end())
			return &configIt->second.entries;
		return nullptr;
	case SceneContextType::Location:
		if (auto configIt = locationSceneConfigs.find(
				GetLocationConfigKey(context.locationType, context.locationFormKey));
			configIt != locationSceneConfigs.end())
			return &configIt->second.entries;
		return nullptr;
	default:
		return nullptr;
	}
}

std::vector<std::string> SceneSettingsManager::SplitSettingPath(std::string_view catalogPath)
{
	return SplitCatalogPath(catalogPath);
}

std::span<const SceneSettingsManager::SettingEntry> SceneSettingsManager::GetContextEntries(
	const SceneContextId& context) const
{
	const auto* contextEntries = GetCopyContextEntries(context);
	return contextEntries ? std::span{ *contextEntries } : std::span<const SettingEntry>{};
}

std::vector<SceneSettingsManager::SettingEntry>* SceneSettingsManager::GetContextEntriesMut(
	const SceneContextId& context)
{
	if (!IsValidSceneContext(context))
		return nullptr;
	switch (context.type) {
	case SceneContextType::Interior:
	case SceneContextType::TimeOfDay:
		return &GetEntriesMut(ContextSceneType(context.type));
	case SceneContextType::Weather:
		if (!TryEnsureWeatherDataLoaded())
			return nullptr;
		if (auto configIt = weatherSceneConfigs.find(context.weatherId); configIt != weatherSceneConfigs.end())
			return &configIt->second.entries;
		return nullptr;
	case SceneContextType::Location:
		if (!TryEnsureLocationDataLoaded())
			return nullptr;
		if (auto configIt = locationSceneConfigs.find(
				GetLocationConfigKey(context.locationType, context.locationFormKey));
			configIt != locationSceneConfigs.end())
			return &configIt->second.entries;
		return nullptr;
	default:
		return nullptr;
	}
}

std::vector<SceneSettingsManager::SettingEntry>* SceneSettingsManager::EnsureContextEntriesMut(
	const SceneContextId& context)
{
	if (!IsValidSceneContext(context))
		return nullptr;
	switch (context.type) {
	case SceneContextType::Weather:
		if (!TryEnsureWeatherDataLoaded())
			return nullptr;
		return &GetWeatherConfigMut(context.weatherId).entries;
	case SceneContextType::Location: {
		if (!TryEnsureLocationDataLoaded())
			return nullptr;
		// Adding the first setting authors the target, so the page survives a reload with no settings.
		const auto& existing = GetLocationConfig(context.locationType, context.locationFormKey);
		const auto name = existing.name;
		const auto cocCode = existing.cocCode;
		auto& config = EnsureAuthoredLocationConfig(context.locationType, context.locationFormKey,
			name, cocCode);
		return &config.entries;
	}
	default:
		return GetContextEntriesMut(context);
	}
}

void SceneSettingsManager::MarkContextUserSettingsModified(const SceneContextId& context,
	bool replaceMalformedEntries)
{
	switch (context.type) {
	case SceneContextType::Interior:
	case SceneContextType::TimeOfDay:
		MarkEntryListUserSettingsModified(ContextSceneType(context.type));
		break;
	case SceneContextType::Weather:
		PrepareWeatherUserSettingsMutation(context.weatherId, replaceMalformedEntries);
		break;
	case SceneContextType::Location:
		PrepareLocationUserSettingsMutation(context.locationType, context.locationFormKey,
			replaceMalformedEntries);
		break;
	default:
		break;
	}
}

void SceneSettingsManager::CommitContextUserEntryMutation(const SceneContextId& context, bool deferSave,
	bool replaceMalformedEntries)
{
	BumpEntryPresentationRevision();
	MarkContextUserSettingsModified(context, replaceMalformedEntries);
	// A deferred edit holds the resolve with the save, so a drag does not run ResolveAndApply
	// mid-DrawSettings for every feature holding an active scene override.
	if (deferSave)
		MarkDeferredSceneChanges();
	else
		CommitSceneSettingChanges();
}

std::optional<json> SceneSettingsManager::CaptureContextValue(const SceneContextId& context,
	const SettingAddress& address)
{
	switch (context.type) {
	case SceneContextType::Weather:
		// Weather and location stack on lower layers, so a capture pins what applies without the mods.
		if (const auto lower = ResolveWeatherLowerValue(context.weatherId, address, context.period,
				kCaptureSourceLayer))
			return json(*lower);
		return std::nullopt;
	case SceneContextType::Location:
		return ResolveLocationLowerValue(context.locationType, context.locationFormKey, address,
			kCaptureSourceLayer);
	default: {
		// The entry lists sit directly on the feature's own value.
		auto value = GetFeatureSettingValue(address.featureShortName, address.settingPath,
			address.settingKey);
		return value.is_null() ? std::nullopt : std::optional{ std::move(value) };
	}
	}
}

std::optional<json> SceneSettingsManager::ResolveContextEntryDefault(const SceneContextId& context,
	const SettingEntry& entry)
{
	switch (context.type) {
	case SceneContextType::Weather:
		// Re-resolved rather than remembered: the layers under the entry can have moved since.
		if (const auto lower = ResolveWeatherLowerValue(context.weatherId, GetEntryAddress(entry),
				entry.period, entry.source))
			return json(*lower);
		return std::nullopt;
	case SceneContextType::Location:
		return ResolveLocationLowerValue(context.locationType, context.locationFormKey,
			GetEntryAddress(entry), entry.source);
	default:
		return entry.originalValue.is_null() ? std::nullopt : std::optional{ entry.originalValue };
	}
}

SceneSettingsManager::ContextEntrySummary SceneSettingsManager::GetContextUserEntrySummary(
	const SceneContextId& context) const
{
	ContextEntrySummary summary;
	if (!IsValidSceneContext(context))
		return summary;
	for (const auto& entry : GetContextEntries(context)) {
		if (entry.source != EntrySource::User || !EntryBelongsToContext(entry, context))
			continue;
		++summary.total;
		summary.paused += entry.paused ? 1 : 0;
	}
	return summary;
}

void SceneSettingsManager::SetContextEntriesPaused(const SceneContextId& context, bool paused)
{
	auto* contextEntries = GetContextEntriesMut(context);
	if (!contextEntries)
		return;

	bool changed = false;
	for (auto& entry : *contextEntries) {
		if (entry.source != EntrySource::User || entry.paused == paused ||
			!EntryBelongsToContext(entry, context))
			continue;
		entry.paused = paused;
		changed = true;
	}
	if (changed)
		CommitContextUserEntryMutation(context);
}

void SceneSettingsManager::ClearContextEntries(const SceneContextId& context)
{
	auto* contextEntries = GetContextEntriesMut(context);
	if (!contextEntries)
		return;

	// Unresolved raw entries are left in the document: they belong to features this session cannot judge.
	const auto removed = std::erase_if(*contextEntries, [&](const SettingEntry& entry) {
		return entry.source == EntrySource::User && EntryBelongsToContext(entry, context);
	});
	if (removed != 0)
		CommitContextUserEntryMutation(context);
}

bool SceneSettingsManager::HasAnyUserEntries() const
{
	const auto hasUser = [](const std::vector<SettingEntry>& sourceEntries) {
		return std::any_of(sourceEntries.begin(), sourceEntries.end(),
			[](const SettingEntry& entry) { return entry.source == EntrySource::User; });
	};
	return std::any_of(entries.begin(), entries.end(),
			   [&](const auto& item) { return hasUser(item.second); }) ||
	       std::any_of(weatherSceneConfigs.begin(), weatherSceneConfigs.end(),
			   [&](const auto& item) { return hasUser(item.second.entries); }) ||
	       std::any_of(locationSceneConfigs.begin(), locationSceneConfigs.end(),
			   [&](const auto& item) { return hasUser(item.second.entries); });
}

void SceneSettingsManager::ClearAllUserEntries()
{
	// Unresolved raw entries stay in the document: they belong to features this session cannot judge.
	const auto clear = [](std::vector<SettingEntry>& sourceEntries) {
		return std::erase_if(sourceEntries,
				   [](const SettingEntry& entry) { return entry.source == EntrySource::User; }) != 0;
	};

	bool changed = false;
	for (auto& [type, sourceEntries] : entries)
		if (IsEntryListSceneType(type) && clear(sourceEntries)) {
			MarkEntryListUserSettingsModified(type);
			changed = true;
		}
	for (auto& [weatherId, config] : weatherSceneConfigs)
		if (clear(config.entries)) {
			PrepareWeatherUserSettingsMutation(weatherId, false);
			changed = true;
		}
	for (auto& [configKey, config] : locationSceneConfigs)
		if (clear(config.entries)) {
			locationUserSettingsModified = true;
			changed = true;
		}

	if (!changed)
		return;
	BumpEntryPresentationRevision();
	SaveAllUserSettings();
	ReapplyIfActive();
}

std::optional<size_t> SceneSettingsManager::FindContextUserEntry(const SceneContextId& context,
	const std::string& featureShortName, const std::vector<std::string>& settingPath,
	const std::string& settingKey) const
{
	if (!IsValidSceneContext(context))
		return std::nullopt;
	const auto contextEntries = GetContextEntries(context);
	for (size_t index = 0; index < contextEntries.size(); ++index) {
		const auto& entry = contextEntries[index];
		if (entry.source == EntrySource::User && entry.featureShortName == featureShortName &&
			entry.settingPath == settingPath && entry.settingKey == settingKey &&
			entry.period == context.period)
			return index;
	}
	return std::nullopt;
}

std::optional<size_t> SceneSettingsManager::AddContextSetting(const SceneContextId& context,
	const std::string& featureShortName, const std::vector<std::string>& settingPath,
	const std::string& settingKey, bool deferSave)
{
	if (!IsValidSceneContext(context))
		return std::nullopt;

	const auto rules = GetSceneContextRules(context);
	if (!IsSettingAllowedForType(rules.sceneType, featureShortName, settingPath, settingKey, rules.requireNumeric) ||
		FindContextUserEntry(context, featureShortName, settingPath, settingKey))
		return std::nullopt;

	auto value = CaptureContextValue(context, { featureShortName, settingPath, settingKey });
	if (!value || !ValidateSceneSettingEntry(rules.label, rules.sceneType, featureShortName, settingPath,
					  settingKey, *value, rules.requireNumeric))
		return std::nullopt;

	auto* contextEntries = EnsureContextEntriesMut(context);
	if (!contextEntries)
		return std::nullopt;

	SettingEntry entry;
	entry.featureShortName = featureShortName;
	entry.settingPath = settingPath;
	entry.settingKey = settingKey;
	entry.displayName = GetSceneSettingDisplayName(featureShortName, settingPath, settingKey);
	entry.originalValue = *value;
	entry.value = std::move(*value);
	entry.source = EntrySource::User;
	entry.period = context.period;
	contextEntries->push_back(std::move(entry));

	CommitContextUserEntryMutation(context, deferSave);
	return FindContextUserEntry(context, featureShortName, settingPath, settingKey);
}

void SceneSettingsManager::UpdateContextEntryValues(const SceneContextId& context,
	std::span<const EntryValueUpdate> updates, bool deferSave)
{
	auto* contextEntries = GetContextEntriesMut(context);
	if (!contextEntries)
		return;

	const auto rules = GetSceneContextRules(context);
	bool userEntriesChanged = false;
	if (!ApplyEntryValueUpdates(rules.label, rules.sceneType, *contextEntries, updates, rules.requireNumeric,
			userEntriesChanged))
		return;
	if (userEntriesChanged)
		MarkContextUserSettingsModified(context, false);

	// Only values moved, so the presentation caches still hold. The value caches must still go, and
	// the next Update resolves them so a drag previews live without resolving mid-DrawSettings.
	if (deferSave) {
		resolverDirty = true;
		MarkSceneValuesDirty();
		MarkDeferredSceneChanges();
		return;
	}
	if (userEntriesChanged)
		SaveAllUserSettings();
	ReapplyIfActive();
}

void SceneSettingsManager::RemoveContextSetting(const SceneContextId& context, size_t index)
{
	if (!IsValidSceneContext(context))
		return;

	// A mod's file is never edited from here. Suppressing a mod value goes through
	// TombstoneContextSetting; the implementations below would strip the key from the mod's own file.
	const auto contextEntries = GetContextEntries(context);
	if (index >= contextEntries.size())
		return;
	if (contextEntries[index].source != EntrySource::User) {
		assert(false && "RemoveContextSetting called with an overwrite entry");
		logger::warn("[SceneSettings] Refusing to remove overwrite entry {} through the context façade",
			GetSettingLogName(contextEntries[index].featureShortName,
				contextEntries[index].settingPath, contextEntries[index].settingKey));
		return;
	}

	switch (context.type) {
	case SceneContextType::Interior:
	case SceneContextType::TimeOfDay:
		RemoveSetting(ContextSceneType(context.type), index);
		break;
	case SceneContextType::Weather:
		RemoveWeatherSetting(context.weatherId, index);
		break;
	case SceneContextType::Location:
		RemoveLocationSetting(context.locationType, context.locationFormKey, index);
		break;
	default:
		break;
	}
}

bool SceneSettingsManager::TombstoneContextSetting(const SceneContextId& context,
	const std::string& featureShortName, const std::vector<std::string>& settingPath,
	const std::string& settingKey)
{
	auto* contextEntries = GetContextEntriesMut(context);
	if (!contextEntries)
		return false;

	// A tombstone must not land at an address where an ordinary user entry would be rejected.
	const auto rules = GetSceneContextRules(context);
	if (!IsSettingAllowedForType(rules.sceneType, featureShortName, settingPath, settingKey, rules.requireNumeric))
		return false;

	// An existing user entry becomes the tombstone: two entries at one address would race.
	if (const auto existing = FindContextUserEntry(context, featureShortName, settingPath, settingKey)) {
		auto& entry = (*contextEntries)[*existing];
		entry.deleted = true;
		entry.paused = false;
		CommitContextUserEntryMutation(context);
		return true;
	}

	if (GetSettingProvenance(context, featureShortName, settingPath, settingKey).layer == SettingLayer::None)
		return false;
	// The base is kept for the round trip and for revert; resolution ignores it while deleted. A null
	// one would not survive a reload, so the address stays as it is rather than losing the tombstone.
	auto value = GetFeatureSettingValue(featureShortName, settingPath, settingKey);
	if (value.is_null())
		return false;

	SettingEntry entry;
	entry.featureShortName = featureShortName;
	entry.settingPath = settingPath;
	entry.settingKey = settingKey;
	entry.displayName = GetSceneSettingDisplayName(featureShortName, settingPath, settingKey);
	entry.originalValue = value;
	entry.value = std::move(value);
	entry.source = EntrySource::User;
	entry.deleted = true;
	entry.period = context.period;
	contextEntries->push_back(std::move(entry));
	CommitContextUserEntryMutation(context);
	return true;
}

void SceneSettingsManager::ClearContextTombstone(const SceneContextId& context,
	const std::string& featureShortName, const std::vector<std::string>& settingPath,
	const std::string& settingKey)
{
	auto* contextEntries = GetContextEntriesMut(context);
	if (!contextEntries)
		return;
	const auto existing = FindContextUserEntry(context, featureShortName, settingPath, settingKey);
	if (!existing || *existing >= contextEntries->size() || !(*contextEntries)[*existing].deleted)
		return;
	contextEntries->erase(contextEntries->begin() + static_cast<ptrdiff_t>(*existing));
	CommitContextUserEntryMutation(context);
}

void SceneSettingsManager::TogglePauseContextEntry(const SceneContextId& context, size_t index)
{
	auto* contextEntries = GetContextEntriesMut(context);
	if (!contextEntries || index >= contextEntries->size())
		return;

	// Only the user layer is ours to change. Pausing a mod's entry would hold it back for this session
	// and come back on the next discovery, so it is refused here as it already is in bulk.
	auto& entry = (*contextEntries)[index];
	if (entry.source != EntrySource::User)
		return;

	entry.paused = !entry.paused;
	CommitContextUserEntryMutation(context);
}

void SceneSettingsManager::RevertContextEntryToDefault(const SceneContextId& context, size_t index)
{
	auto* contextEntries = GetContextEntriesMut(context);
	if (!contextEntries || index >= contextEntries->size())
		return;

	// Only the user layer is ours to change. Rewriting a mod's entry would diverge it from its backing
	// file and lose the edit on the next discovery, so it is refused here as it already is in bulk.
	auto& entry = (*contextEntries)[index];
	if (entry.source != EntrySource::User)
		return;

	const auto rules = GetSceneContextRules(context);
	const auto defaultValue = ResolveContextEntryDefault(context, entry);
	if (!defaultValue || !ValidateSceneSettingEntry(rules.label, rules.sceneType, entry.featureShortName,
							  entry.settingPath, entry.settingKey, *defaultValue, rules.requireNumeric))
		return;

	entry.value = *defaultValue;
	entry.originalValue = entry.value;
	// Only the value moved, so the presentation caches hold: no revision bump, unlike a mutation.
	MarkContextUserSettingsModified(context, false);
	CommitSceneSettingChanges();
}
