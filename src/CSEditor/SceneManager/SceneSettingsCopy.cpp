#include "SceneSettingsManager.h"

#include "Globals.h"
#include "SceneSettingsContextRules.h"
#include "SceneSettingsInternal.h"
#include "SceneSettingsLocationTargets.h"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <tuple>

using namespace SceneSettingsInternal;
using namespace SceneSettingsLocationTargets;
using namespace SceneSettingsContextRules;

namespace
{
	/** @brief Visits the contexts of the saved set a scene resolves: the flat one, or one per period. */
	template <class Visit>
	void ForEachActiveSetContext(SceneSettingsManager::SceneContextId context, bool timeOfDayEnabled, Visit&& visit)
	{
		if (!timeOfDayEnabled) {
			context.period = SceneSettingsManager::TimeOfDayPeriod::Count;
			visit(context);
			return;
		}
		for (const auto period : SceneSettingsManager::kPeriods) {
			context.period = period;
			visit(context);
		}
	}

	/** @brief Suffixes a scene name with its period; the flat set keeps the bare name. */
	std::string AppendPeriodName(std::string name, SceneSettingsManager::TimeOfDayPeriod period)
	{
		if (period == SceneSettingsManager::TimeOfDayPeriod::Count)
			return name;
		return std::format("{} / {}", name, GetCopyPeriodName(period));
	}
}

std::vector<SceneSettingsManager::CopyCandidate> SceneSettingsManager::BuildCopyCandidates(
	const SceneContextId& source, const SceneContextId& destination) const
{
	std::vector<CopyCandidate> candidates;
	if (!IsValidSceneContext(source) || !IsValidSceneContext(destination))
		return candidates;
	if (destination.type == SceneContextType::Location &&
		ResolveLocationTargetChain(destination.locationType, destination.locationFormKey).empty())
		return candidates;
	if (IsSameSceneContext(source, destination))
		return candidates;
	const auto* sourceEntries = GetCopyContextEntries(source);
	if (!sourceEntries)
		return candidates;
	static const std::vector<SettingEntry> empty;
	const auto* destinationEntries = GetCopyContextEntries(destination);
	if (!destinationEntries)
		destinationEntries = &empty;

	const auto effectiveEntries = BuildEffectiveContextEntries(*sourceEntries, source);
	std::map<SettingIdentity, json> destinationUserSettings;
	std::set<SettingIdentity> destinationOverwriteSettings;
	for (const auto& entry : *destinationEntries) {
		if (!EntryBelongsToContext(entry, destination))
			continue;
		if (entry.source == EntrySource::User && !entry.deleted)
			destinationUserSettings.try_emplace(
				SettingIdentity{ entry.featureShortName, entry.settingPath, entry.settingKey }, entry.value);
		else if (entry.source == EntrySource::Overwrite && !entry.paused)
			destinationOverwriteSettings.insert({ entry.featureShortName, entry.settingPath, entry.settingKey });
	}

	const auto destinationRules = GetSceneContextRules(destination);
	for (const auto& [identity, entry] : effectiveEntries) {
		auto* setting = FindAllowedCatalogSetting(
			identity.featureShortName, identity.settingPath, identity.settingKey, destinationRules.requireNumeric);
		auto rejection = CopyRejection::None;
		if (!setting)
			rejection = destinationRules.requireNumeric && FindAllowedCatalogSetting(identity.featureShortName,
			                                                   identity.settingPath, identity.settingKey) ?
			                CopyRejection::NotBlendable :
			                CopyRejection::NotInCatalog;
		else if (!IsSettingAllowedForType(destinationRules.sceneType, identity.featureShortName,
					 identity.settingPath, identity.settingKey))
			rejection = CopyRejection::NotAllowedInLayer;
		else if (!IsSceneSettingValueAllowed(entry->value, *setting, entry->value, destinationRules.requireNumeric))
			rejection = CopyRejection::ValueRejected;
		else if (destinationOverwriteSettings.contains(identity))
			rejection = CopyRejection::BlockedByOverwrite;

		const bool compatible = rejection == CopyRejection::None;
		const auto destinationIt = destinationUserSettings.find(identity);
		const bool conflicts = destinationIt != destinationUserSettings.end();
		candidates.push_back({
			.setting = identity,
			.displayName = entry->displayName.empty() ?
			                   GetSceneSettingDisplayName(identity.featureShortName, identity.settingPath, identity.settingKey) :
			                   entry->displayName,
			.value = entry->value,
			.destinationValue = conflicts ? std::optional{ destinationIt->second } : std::nullopt,
			.rejection = rejection,
			.compatible = compatible,
			.conflicts = compatible && conflicts,
		});
	}

	// An aggregate is all-or-nothing: one unusable component disqualifies the whole control.
	std::map<CopyGroupKey, std::vector<size_t>> candidateGroups;
	for (size_t index = 0; index < candidates.size(); ++index)
		candidateGroups[GetCopyGroupKey(candidates[index].setting)].push_back(index);
	for (const auto& [groupKey, indices] : candidateGroups) {
		if (std::all_of(indices.begin(), indices.end(), [&](size_t index) { return candidates[index].compatible; }))
			continue;
		for (const auto index : indices) {
			// A row with no fault of its own is out purely because a sibling is, which is worth saying.
			if (candidates[index].rejection == CopyRejection::None)
				candidates[index].rejection = CopyRejection::GroupCompanionRejected;
			candidates[index].compatible = false;
			candidates[index].conflicts = false;
		}
	}
	std::sort(candidates.begin(), candidates.end(), [](const auto& lhs, const auto& rhs) {
		return std::tie(lhs.displayName, lhs.setting) < std::tie(rhs.displayName, rhs.setting);
	});
	return candidates;
}

std::vector<SceneSettingsManager::CopyCandidate> SceneSettingsManager::GetCopyCandidates(
	const SceneContextId& source, const SceneContextId& destination) const
{
	return BuildCopyCandidates(source, destination);
}

std::vector<SceneSettingsManager::CopySource> SceneSettingsManager::GetCopySources(
	const SceneContextId& destination) const
{
	if (!IsValidSceneContext(destination))
		return {};
	if (destination.type == SceneContextType::Location &&
		ResolveLocationTargetChain(destination.locationType, destination.locationFormKey).empty())
		return {};
	std::set<SettingIdentity> destinationOverwrites;
	if (const auto* destinationEntries = GetCopyContextEntries(destination))
		for (const auto& entry : *destinationEntries)
			if (entry.source == EntrySource::Overwrite && !entry.paused &&
				EntryBelongsToContext(entry, destination))
				destinationOverwrites.insert({ entry.featureShortName, entry.settingPath, entry.settingKey });

	const auto destinationRules = GetSceneContextRules(destination);
	const auto countCompatible = [&](const EffectiveContextEntries& effectiveEntries) {
		struct GroupCount
		{
			size_t members = 0;
			size_t compatible = 0;
		};
		std::map<CopyGroupKey, GroupCount> groups;
		for (const auto& [identity, entry] : effectiveEntries) {
			auto& group = groups[GetCopyGroupKey(identity)];
			++group.members;
			auto* metadata = FindAllowedCatalogSetting(
				identity.featureShortName, identity.settingPath, identity.settingKey, destinationRules.requireNumeric);
			if (!destinationOverwrites.contains(identity) && metadata &&
				IsSettingAllowedForType(destinationRules.sceneType, identity.featureShortName,
					identity.settingPath, identity.settingKey) &&
				IsSceneSettingValueAllowed(entry->value, *metadata, entry->value, destinationRules.requireNumeric))
				++group.compatible;
		}
		size_t count = 0;
		for (const auto& [groupKey, group] : groups)
			if (group.members == group.compatible)
				count += group.members;
		return count;
	};

	std::vector<CopySource> sources;
	const auto addSource = [&](const SceneContextId& context, const EffectiveContextEntries& effectiveEntries) {
		if (IsSameSceneContext(context, destination))
			return;
		if (const auto settingCount = countCompatible(effectiveEntries); settingCount != 0)
			sources.push_back({ context, GetSceneContextDisplayName(context), settingCount, true,
				IsCurrentSceneContext(context) });
	};
	// The last slot holds the flat set, whose entries carry the Count period.
	const auto addActiveSetSources = [&](const SceneContextId& context, const std::vector<SettingEntry>& sourceEntries,
										 bool timeOfDayEnabled) {
		std::array<EffectiveContextEntries, kPeriodCount + 1> periods;
		for (auto entrySource : { EntrySource::Overwrite, EntrySource::User })
			for (const auto& entry : sourceEntries) {
				const auto periodIndex = static_cast<int>(entry.period);
				if (entry.source == entrySource && !entry.paused && periodIndex >= 0 && periodIndex <= kPeriodCount)
					periods[periodIndex][{ entry.featureShortName, entry.settingPath, entry.settingKey }] = &entry;
			}
		ForEachActiveSetContext(context, timeOfDayEnabled, [&](const SceneContextId& setContext) {
			addSource(setContext, periods[static_cast<int>(setContext.period)]);
		});
	};

	const SceneContextId interiorContext{ .type = SceneContextType::Interior, .period = TimeOfDayPeriod::Count };
	addSource(interiorContext,
		BuildEffectiveContextEntries(GetEntries(SceneType::InteriorOnly), interiorContext));

	addActiveSetSources({ .type = SceneContextType::TimeOfDay }, GetEntries(SceneType::TimeOfDay), true);
	for (const auto& [weatherId, config] : weatherSceneConfigs)
		addActiveSetSources({ .type = SceneContextType::Weather, .weatherId = weatherId }, config.entries,
			config.timeOfDayEnabled);
	for (const auto& [configKey, config] : locationSceneConfigs)
		addActiveSetSources(
			{ .type = SceneContextType::Location, .locationType = config.type, .locationFormKey = config.formKey },
			config.entries, config.timeOfDayEnabled);
	std::sort(sources.begin(), sources.end(), [](const auto& lhs, const auto& rhs) {
		return std::tie(lhs.context.type, lhs.displayName, lhs.context) <
		       std::tie(rhs.context.type, rhs.displayName, rhs.context);
	});
	return sources;
}

std::vector<SceneSettingsManager::CopySource> SceneSettingsManager::GetCopyDestinations(
	const SceneContextId& source) const
{
	if (!IsValidSceneContext(source) || !GetCopyContextEntries(source))
		return {};

	std::vector<CopySource> destinations;
	// Every candidate is validated through BuildCopyCandidates, the same path a real copy takes, so a
	// destination is only offered when it would actually accept something.
	const auto addDestination = [&](const SceneContextId& context, std::string displayName) {
		if (IsSameSceneContext(context, source))
			return;
		const auto candidates = BuildCopyCandidates(source, context);
		const auto compatibleCount = static_cast<size_t>(
			std::count_if(candidates.begin(), candidates.end(), [](const auto& candidate) { return candidate.compatible; }));
		if (compatibleCount != 0)
			destinations.push_back({ context, std::move(displayName), compatibleCount,
				IsSceneContextAuthored(context), IsCurrentSceneContext(context) });
	};

	const SceneContextId interiorContext{ .type = SceneContextType::Interior, .period = TimeOfDayPeriod::Count };
	addDestination(interiorContext, GetSceneContextDisplayName(interiorContext));

	const auto addActiveSetDestinations = [&](const SceneContextId& context) {
		ForEachActiveSetContext(context, IsSceneTimeOfDayEnabled(context), [&](const SceneContextId& setContext) {
			addDestination(setContext, GetSceneContextDisplayName(setContext));
		});
	};
	addActiveSetDestinations({ .type = SceneContextType::TimeOfDay });

	// Weather has no "already authored" cache to lean on the way locations do, so every loaded
	// weather form is a candidate destination, whether or not it holds any settings yet.
	if (auto* dataHandler = RE::TESDataHandler::GetSingleton()) {
		for (auto* weather : dataHandler->GetFormArray<RE::TESWeather>()) {
			if (weather)
				addActiveSetDestinations({ .type = SceneContextType::Weather, .weatherId = weather->GetFormID() });
		}
	}

	// The current chain plus everything already authored covers every location this session can
	// navigate to, the same universe the location browser offers.
	std::vector<LocationTarget> locationTargets = GetCurrentLocationTargets();
	for (auto& target : GetAuthoredLocationTargets())
		if (std::none_of(locationTargets.begin(), locationTargets.end(), [&](const auto& existing) {
				return existing.type == target.type &&
				       NormalizeLocationFormKey(existing.formKey) == NormalizeLocationFormKey(target.formKey);
			}))
			locationTargets.push_back(std::move(target));
	for (const auto& target : locationTargets) {
		const SceneContextId context{ .type = SceneContextType::Location,
			.locationType = target.type,
			.locationFormKey = target.formKey };
		ForEachActiveSetContext(context, IsSceneTimeOfDayEnabled(context), [&](const SceneContextId& setContext) {
			addDestination(setContext, AppendPeriodName(
				std::format("{} / {}", GetCopyLocationTypeName(target.type), target.name), setContext.period));
		});
	}

	std::sort(destinations.begin(), destinations.end(), [](const auto& lhs, const auto& rhs) {
		return std::tie(lhs.context.type, lhs.displayName, lhs.context) <
		       std::tie(rhs.context.type, rhs.displayName, rhs.context);
	});
	return destinations;
}

std::string SceneSettingsManager::GetSceneContextDisplayName(const SceneContextId& context) const
{
	switch (context.type) {
	case SceneContextType::Interior:
		return T("feature.scene_manager.context.interior", "Interior");
	case SceneContextType::TimeOfDay:
		return GetCopyPeriodName(context.period);
	case SceneContextType::Weather:
		return AppendPeriodName(Util::GetFormDisplayName(context.weatherId), context.period);
	case SceneContextType::Location: {
		auto configIt = locationSceneConfigs.find(
			GetLocationConfigKey(context.locationType, context.locationFormKey));
		// The form key is the fallback identity for targets the game never named.
		const std::string* name = &context.locationFormKey;
		if (configIt != locationSceneConfigs.end())
			name = configIt->second.name.empty() ? &configIt->second.formKey : &configIt->second.name;
		return AppendPeriodName(
			std::format("{} / {}", GetCopyLocationTypeName(context.locationType), *name), context.period);
	}
	default:
		return {};
	}
}

SceneSettingsManager::CopyResult SceneSettingsManager::CopySettingsToContext(const SceneContextId& source,
	const SceneContextId& destination, CopyConflictPolicy conflictPolicy, bool deferCommit)
{
	CopyResult result;
	if (!IsValidSceneContext(source) || !IsValidSceneContext(destination) ||
		!IsValidCopyConflictPolicy(conflictPolicy))
		return result;
	if ((source.type == SceneContextType::Weather || destination.type == SceneContextType::Weather) &&
		!TryEnsureWeatherDataLoaded())
		return result;
	if ((source.type == SceneContextType::Location || destination.type == SceneContextType::Location) &&
		!TryEnsureLocationDataLoaded())
		return result;

	const auto candidates = BuildCopyCandidates(source, destination);
	if (candidates.empty())
		return result;
	std::map<CopyGroupKey, std::vector<CopyCandidate>> groups;
	for (const auto& candidate : candidates)
		groups[GetCopyGroupKey(candidate.setting)].push_back(candidate);
	const auto groupIncompatible = [](const std::vector<CopyCandidate>& group) {
		return std::any_of(group.begin(), group.end(), [](const auto& candidate) { return !candidate.compatible; });
	};
	const auto groupConflicts = [](const std::vector<CopyCandidate>& group) {
		return std::any_of(group.begin(), group.end(), [](const auto& candidate) { return candidate.conflicts; });
	};

	for (const auto& [groupKey, group] : groups) {
		if (groupIncompatible(group))
			result.incompatible += group.size();
		else
			result.hadConflicts |= groupConflicts(group);
	}
	if (conflictPolicy == CopyConflictPolicy::Cancel && result.hadConflicts) {
		result.cancelled = true;
		return result;
	}

	std::optional<LocationTarget> destinationLocationTarget;
	if (destination.type == SceneContextType::Location) {
		const auto chain = ResolveLocationTargetChain(destination.locationType, destination.locationFormKey);
		const auto destinationKey = GetLocationConfigKey(destination.locationType, destination.locationFormKey);
		auto targetIt = std::find_if(chain.begin(), chain.end(), [&](const auto& target) {
			return GetLocationConfigKey(target.type, target.formKey) == destinationKey;
		});
		if (targetIt == chain.end())
			return result;
		destinationLocationTarget = *targetIt;
	}

	// The destination list is only created once the copy is known to produce entries.
	std::vector<SettingEntry> emptyDestinationEntries;
	std::vector<SettingEntry>* destinationEntries = nullptr;
	bool destinationNeedsMaterialization = false;
	switch (destination.type) {
	case SceneContextType::Interior:
	case SceneContextType::TimeOfDay:
		destinationEntries = &GetEntriesMut(ContextSceneType(destination.type));
		break;
	case SceneContextType::Weather: {
		auto configIt = weatherSceneConfigs.find(destination.weatherId);
		destinationNeedsMaterialization = configIt == weatherSceneConfigs.end();
		destinationEntries = destinationNeedsMaterialization ? &emptyDestinationEntries : &configIt->second.entries;
		break;
	}
	case SceneContextType::Location: {
		auto configIt = locationSceneConfigs.find(
			GetLocationConfigKey(destination.locationType, destination.locationFormKey));
		destinationNeedsMaterialization = configIt == locationSceneConfigs.end();
		destinationEntries = destinationNeedsMaterialization ? &emptyDestinationEntries : &configIt->second.entries;
		break;
	}
	default:
		return {};
	}
	if (!destinationEntries)
		return result;

	std::map<SettingIdentity, size_t> destinationUserIndices;
	for (size_t index = 0; index < destinationEntries->size(); ++index) {
		const auto& entry = (*destinationEntries)[index];
		if (entry.source == EntrySource::User && EntryBelongsToContext(entry, destination))
			destinationUserIndices[{ entry.featureShortName, entry.settingPath, entry.settingKey }] = index;
	}

	// New entries need a baseline to derive their original value from, so gather them in one pass.
	std::vector<SettingAddress> candidateAddresses;
	for (const auto& [groupKey, group] : groups) {
		if (groupIncompatible(group) ||
			(conflictPolicy == CopyConflictPolicy::SkipExisting && groupConflicts(group)))
			continue;
		for (const auto& candidate : group)
			if (!destinationUserIndices.contains(candidate.setting))
				candidateAddresses.push_back({ candidate.setting.featureShortName,
					candidate.setting.settingPath, candidate.setting.settingKey });
	}
	std::sort(candidateAddresses.begin(), candidateAddresses.end());
	candidateAddresses.erase(std::unique(candidateAddresses.begin(), candidateAddresses.end()),
		candidateAddresses.end());
	EnsureBaselines(candidateAddresses);

	ResolvedSettingMap lowerLayers;
	if (destination.type == SceneContextType::Location) {
		auto resolvedLowerLayers = BuildLocationLowerLayers(
			destination.locationType, destination.locationFormKey, EntrySource::User);
		if (!resolvedLowerLayers)
			return result;
		lowerLayers = std::move(*resolvedLowerLayers);
	}
	const PeriodSettingMap* timeOfDayValues = destination.type == SceneContextType::Weather ?
	                                             &BuildTimeOfDayValueGroups() :
	                                             nullptr;
	// A flat weather entry spans every period, so it restores to the time of day playing now.
	const auto timeOfDayPeriod = static_cast<int>(
		destination.period == TimeOfDayPeriod::Count ? GetCurrentPeriod() : destination.period);

	std::map<SettingIdentity, std::optional<float>> sourceTransitions;
	if (const auto* sourceEntries = GetCopyContextEntries(source))
		for (const auto& [identity, entry] : BuildEffectiveContextEntries(*sourceEntries, source))
			sourceTransitions[identity] = entry->transitionSeconds;

	struct PendingCopy
	{
		const CopyCandidate* candidate = nullptr;
		std::optional<size_t> destinationIndex;
		json originalValue;
		std::optional<float> transitionSeconds;
	};
	std::vector<PendingCopy> pending;
	for (const auto& [groupKey, group] : groups) {
		if (groupIncompatible(group))
			continue;
		if (groupConflicts(group) && conflictPolicy == CopyConflictPolicy::SkipExisting) {
			result.skipped += group.size();
			continue;
		}

		// One duration covers the whole control: the destination's own wins, else the source's.
		std::optional<float> groupTransitionSeconds;
		if (destination.type == SceneContextType::Location) {
			bool selected = false;
			for (const auto& candidate : group) {
				if (auto indexIt = destinationUserIndices.find(candidate.setting);
					indexIt != destinationUserIndices.end()) {
					groupTransitionSeconds = (*destinationEntries)[indexIt->second].transitionSeconds;
					selected = true;
					break;
				}
			}
			if (!selected)
				for (const auto& candidate : group) {
					if (auto transitionIt = sourceTransitions.find(candidate.setting);
						transitionIt != sourceTransitions.end()) {
						groupTransitionSeconds = transitionIt->second;
						break;
					}
				}
		}

		std::vector<PendingCopy> groupPending;
		bool groupValid = true;
		for (const auto& candidate : group) {
			std::optional<size_t> destinationIndex;
			if (auto indexIt = destinationUserIndices.find(candidate.setting);
				indexIt != destinationUserIndices.end())
				destinationIndex = indexIt->second;

			const SettingAddress address{ candidate.setting.featureShortName,
				candidate.setting.settingPath, candidate.setting.settingKey };
			auto baselineIt = baselineSettings.find(address);
			json originalValue;
			if (destinationIndex) {
				originalValue = (*destinationEntries)[*destinationIndex].originalValue;
			} else if (destination.type == SceneContextType::Location) {
				if (auto lowerIt = lowerLayers.find(address); lowerIt != lowerLayers.end())
					originalValue = lowerIt->second;
				else if (baselineIt != baselineSettings.end())
					originalValue = baselineIt->second;
			} else if (baselineIt != baselineSettings.end()) {
				originalValue = baselineIt->second;
				// A weather entry sits on the time-of-day layer, which is what it restores to.
				if (timeOfDayValues && IsNumericValue(originalValue)) {
					if (auto valueIt = timeOfDayValues->find(address); valueIt != timeOfDayValues->end())
						originalValue = valueIt->second[timeOfDayPeriod]
						                    .value_or(baselineIt->second.get<float>());
				}
			}
			if (!destinationIndex && !IsSceneSettingPrimitive(originalValue)) {
				groupValid = false;
				break;
			}
			groupPending.push_back({ &candidate, destinationIndex, std::move(originalValue),
				groupTransitionSeconds });
		}
		if (!groupValid) {
			result.incompatible += group.size();
			continue;
		}
		pending.insert(pending.end(), std::make_move_iterator(groupPending.begin()),
			std::make_move_iterator(groupPending.end()));
	}
	if (pending.empty())
		return result;

	if (destinationNeedsMaterialization) {
		if (destination.type == SceneContextType::Weather) {
			destinationEntries = &GetWeatherConfigMut(destination.weatherId).entries;
		} else if (destination.type == SceneContextType::Location) {
			destinationEntries = &EnsureAuthoredLocationConfig(destination.locationType,
				destination.locationFormKey, destinationLocationTarget->name,
				destinationLocationTarget->cocCode, destinationLocationTarget->editorId)
			                          .entries;
		}
	}

	for (auto& copy : pending) {
		if (copy.destinationIndex) {
			auto& destinationEntry = (*destinationEntries)[*copy.destinationIndex];
			// Any user action on an address, including a copy landing on it, must clear a tombstone rather than leave it suppressing.
			const bool wasDeleted = destinationEntry.deleted;
			destinationEntry.value = copy.candidate->value;
			destinationEntry.deleted = false;
			if (destination.type == SceneContextType::Location) {
				destinationEntry.transitionSeconds = copy.transitionSeconds;
				destinationEntry.retainSerializedTransition = false;
			}
			wasDeleted ? ++result.copied : ++result.overwritten;
			continue;
		}
		destinationEntries->push_back({
			.featureShortName = copy.candidate->setting.featureShortName,
			.settingPath = copy.candidate->setting.settingPath,
			.settingKey = copy.candidate->setting.settingKey,
			.displayName = copy.candidate->displayName,
			.value = copy.candidate->value,
			.originalValue = std::move(copy.originalValue),
			.paused = false,
			.source = EntrySource::User,
			.period = destination.period,
			.transitionSeconds = copy.transitionSeconds,
		});
		++result.copied;
	}
	if (!result.Changed() || deferCommit)
		return result;

	CommitContextUserEntryMutation(destination);
	return result;
}

SceneSettingsManager::CopyResult SceneSettingsManager::CopySettings(const SceneContextId& source,
	const SceneContextId& destination, CopyConflictPolicy conflictPolicy)
{
	return CopySettingsToContext(source, destination, conflictPolicy, false);
}

SceneSettingsManager::CopyResult SceneSettingsManager::CopySettingsBatch(const SceneContextId& source,
	std::span<const SceneContextId> destinations, CopyConflictPolicy conflictPolicy)
{
	CopyResult total;
	for (const auto& destination : destinations) {
		const auto result = CopySettingsToContext(source, destination, conflictPolicy, true);
		total.copied += result.copied;
		total.skipped += result.skipped;
		total.overwritten += result.overwritten;
		total.incompatible += result.incompatible;
		total.hadConflicts |= result.hadConflicts;
		total.cancelled |= result.cancelled;
		if (!result.Changed())
			continue;
		MarkContextUserSettingsModified(destination, true);
		// Later destinations derive originalValue from caches keyed on the scene value revision.
		MarkSceneValuesDirty();
	}
	if (!total.Changed())
		return total;

	BumpEntryPresentationRevision();
	CommitSceneSettingChanges();
	return total;
}

bool SceneSettingsManager::IsSceneContextAuthored(const SceneContextId& context) const
{
	switch (context.type) {
	case SceneContextType::Weather:
		return weatherSceneConfigs.contains(context.weatherId);
	case SceneContextType::Location:
		return locationSceneConfigs.contains(GetLocationConfigKey(context.locationType, context.locationFormKey));
	default:
		return true;
	}
}

bool SceneSettingsManager::IsCurrentSceneContext(const SceneContextId& context) const
{
	switch (context.type) {
	case SceneContextType::Weather: {
		const auto* sky = globals::game::sky;
		return sky && sky->currentWeather && sky->currentWeather->GetFormID() == context.weatherId;
	}
	case SceneContextType::Location:
		return std::ranges::any_of(GetCurrentLocationTargets(), [&](const auto& target) {
			return IsSameSceneContext(context, { .type = SceneContextType::Location,
												   .period = context.period,
												   .locationType = target.type,
												   .locationFormKey = target.formKey });
		});
	default:
		return false;
	}
}
