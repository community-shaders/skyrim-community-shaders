#include "SceneSettingsManager.h"

#include "Feature.h"
#include "Globals.h"
#include "SceneSettingsInternal.h"
#include "State.h"
#include "Utils/Game.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>

using namespace SceneSettingsInternal;

void SceneSettingsManager::ResolveAndApply(bool force, bool allowLocationTransitions)
{
	if (resolverSuspended || sceneLayerSuspendDepth > 0)
		return;
	if (!locationDataLoaded)
		TryEnsureLocationDataLoaded();
	if (!weatherDataLoaded)
		TryEnsureWeatherDataLoaded();
	if (!HasActiveSceneEntriesCached()) {
		applyFailures.clear();
		// The committed context freezes while nothing is active, so the resolve that follows the
		// next entry must not mistake the stale cell for the player having walked.
		suppressLocationTransitionUntilContextResolved = true;
		if (!appliedSettings.empty())
			RestoreAppliedSettings();
		else
			ClearLocationTransitions();
		resolverDirty = !appliedSettings.empty();
		return;
	}

	if (globals::state && globals::state->IsMainOrLoadingMenuOpen()) {
		RestoreAppliedSettings();
		resolverDirty = true;
		return;
	}

	auto* player = RE::PlayerCharacter::GetSingleton();
	auto* cell = player ? player->GetParentCell() : nullptr;
	if (!player || !cell) {
		RestoreAppliedSettings();
		resolverDirty = true;
		return;
	}

	const bool interior = Util::IsInterior();
	const auto hour = GetCurrentGameHour();
	auto* location = player->GetCurrentLocation();
	if (!location)
		location = cell->GetLocation();
	const auto* worldspace = player->GetWorldspace();
	const auto locationId = location ? location->GetFormID() : 0;
	const auto cellId = cell->GetFormID();
	const auto worldspaceId = worldspace ? worldspace->GetFormID() : 0;
	const auto regionId = GetActiveRegionId(cell);
	const bool cellChanged = cellId != lastResolvedCellId;
	// Regions overlap within a cell, so region-scoped settings can go stale without the cell changing.
	const bool locationContextChanged = locationId != lastResolvedLocationId || cellChanged ||
	                                    regionId != lastResolvedRegionId || worldspaceId != lastResolvedWorldspaceId;
	// Only walking between exterior cells of one worldspace eases; anything behind a loading screen
	// has to be in place by the time the player sees it.
	const bool walkedBetweenWorldspaceCells = allowLocationTransitions && cellChanged &&
	                                          !suppressLocationTransitionUntilContextResolved &&
	                                          !interior && !lastResolvedInterior &&
	                                          lastResolvedCellId != 0 &&
	                                          worldspaceId != 0 && worldspaceId == lastResolvedWorldspaceId;

	if (!interior)
		TryEnsureWeatherDataLoaded();
	RefreshBlendSnapshot(interior);
	const auto& weather = blendSnapshot.weather;

	const bool contextChanged = interior != lastResolvedInterior ||
	                            locationContextChanged ||
	                            weather.currentWeatherId != lastResolvedCurrentWeatherId ||
	                            weather.previousWeatherId != lastResolvedPreviousWeatherId ||
	                            std::abs(weather.lerp - lastResolvedWeatherLerp) >= kBlendEpsilon ||
	                            // Indoors resolves nothing from the hour, so time alone cannot change it.
	                            (!interior && (lastResolvedHour < 0.0f ||
	                                              std::abs(hour - lastResolvedHour) >= kHourUpdateThreshold));
	const auto now = std::chrono::steady_clock::now();
	const bool applyRetryDue = std::any_of(applyFailures.begin(), applyFailures.end(),
		[&](const auto& item) { return now >= item.second.retryAfter; });
	const auto transitionTime = GetPauseAwareTime();
	if (!force && !resolverDirty && !contextChanged && !applyRetryDue) {
		AdvanceLocationTransitions(transitionTime);
		return;
	}

	resolverDirty = false;
	const bool reconcileLocationTransitions = locationContextChanged || locationOverridesDirty;
	auto& resolved = BuildResolvedSettings(reconcileLocationTransitions, interior);
	RefreshLocationTransitionEndpoints(resolved);
	if (reconcileLocationTransitions) {
		// Editing a value in place snaps to it, as does arriving from a loading screen.
		StartLocationTransitions(resolved, transitionTime, walkedBetweenWorldspaceCells);
		locationOverridesDirty = false;
	}
	for (const auto& [address, transition] : activeLocationTransitions)
		resolved[address] = EaseLocationTransition(transition, transitionTime);
	ApplyResolvedSettings(resolved, force);
	RetireFinishedLocationTransitions(transitionTime);

	// Committed, so the context is trustworthy again and the next cell change is a real walk.
	suppressLocationTransitionUntilContextResolved = false;
	lastResolvedInterior = interior;
	lastResolvedLocationId = locationId;
	lastResolvedCellId = cellId;
	lastResolvedWorldspaceId = worldspaceId;
	lastResolvedRegionId = regionId;
	lastResolvedHour = hour;
	lastResolvedCurrentWeatherId = weather.currentWeatherId;
	lastResolvedPreviousWeatherId = weather.previousWeatherId;
	lastResolvedWeatherLerp = weather.lerp;
}

float SceneSettingsManager::GetPauseAwareTime() const
{
	return globals::state ? globals::state->timer : 0.0f;
}

float SceneSettingsManager::EaseLocationTransition(const LocationTransition& transition, float now)
{
	const auto linear = transition.duration > 0.0f ?
	                        std::clamp((now - transition.startTime) / transition.duration, 0.0f, 1.0f) :
	                        1.0f;
	const auto smooth = linear * linear * (3.0f - 2.0f * linear);
	return transition.startValue + (transition.targetValue - transition.startValue) * smooth;
}

bool SceneSettingsManager::IsLocationTransitionFinished(const LocationTransition& transition, float now)
{
	return transition.duration <= 0.0f || now - transition.startTime >= transition.duration;
}

void SceneSettingsManager::StartLocationTransitions(
	const ResolvedSettingMap& resolved, float now, bool animateChanges)
{
	ResolvedSettingMap nextOverrideValues;
	for (const auto& [address, _] : pendingLocationTransitionDurations) {
		if (auto resolvedIt = resolved.find(address);
			resolvedIt != resolved.end() && IsNumericValue(resolvedIt->second))
			nextOverrideValues.emplace(address, resolvedIt->second);
	}

	std::set<SettingAddress> candidateAddresses;
	for (const auto* values : { &lastLocationOverrideValues, &nextOverrideValues })
		for (const auto& [address, _] : *values)
			candidateAddresses.insert(address);
	for (const auto& [address, _] : activeLocationTransitions)
		candidateAddresses.insert(address);

	for (const auto& address : candidateAddresses) {
		auto previousIt = lastLocationOverrideValues.find(address);
		auto nextIt = nextOverrideValues.find(address);
		const bool membershipChanged = (previousIt == lastLocationOverrideValues.end()) !=
		                               (nextIt == nextOverrideValues.end());
		const bool valueChanged = previousIt != lastLocationOverrideValues.end() &&
		                          nextIt != nextOverrideValues.end() &&
		                          !ResolvedValuesEqual(previousIt->second, nextIt->second);
		auto previousDurationIt = lastLocationTransitionDurations.find(address);
		auto nextDurationIt = pendingLocationTransitionDurations.find(address);
		const float duration = nextDurationIt != pendingLocationTransitionDurations.end() ?
		                           nextDurationIt->second :
		                           locationTransitionSeconds;
		const bool durationChanged = previousDurationIt != lastLocationTransitionDurations.end() &&
		                             nextDurationIt != pendingLocationTransitionDurations.end() &&
		                             std::abs(previousDurationIt->second - nextDurationIt->second) >= kBlendEpsilon;
		if (!membershipChanged && !valueChanged &&
			!(durationChanged && activeLocationTransitions.contains(address)))
			continue;
		if (!animateChanges) {
			if (activeLocationTransitions.erase(address) != 0)
				locationTransitionBatchesDirty = true;
			continue;
		}

		auto baselineIt = baselineSettings.find(address);
		if (baselineIt == baselineSettings.end() || !IsNumericValue(baselineIt->second))
			continue;

		// Retarget from wherever the value currently sits so a reversal never snaps.
		float startValue = baselineIt->second.get<float>();
		if (auto activeIt = activeLocationTransitions.find(address); activeIt != activeLocationTransitions.end())
			startValue = EaseLocationTransition(activeIt->second, now);
		else if (auto appliedIt = appliedSettings.find(address);
			appliedIt != appliedSettings.end() && IsNumericValue(appliedIt->second))
			startValue = appliedIt->second.get<float>();

		const auto resolvedIt = resolved.find(address);
		const bool restoreAtEnd = nextIt == nextOverrideValues.end() && resolvedIt == resolved.end();
		const auto& targetJson = nextIt != nextOverrideValues.end() ? nextIt->second :
		                         resolvedIt != resolved.end()      ? resolvedIt->second :
		                                                             baselineIt->second;
		if (!IsNumericValue(targetJson))
			continue;
		const auto targetValue = targetJson.get<float>();
		if (!std::isfinite(startValue) || !std::isfinite(targetValue))
			continue;
		if (duration <= 0.0f || std::abs(targetValue - startValue) < kBlendEpsilon) {
			if (activeLocationTransitions.erase(address) != 0)
				locationTransitionBatchesDirty = true;
			continue;
		}
		activeLocationTransitions.insert_or_assign(address, LocationTransition{
															   .startValue = startValue,
															   .targetValue = targetValue,
															   .startTime = now,
															   .duration = duration,
															   .restoreAtEnd = restoreAtEnd,
														   });
		locationTransitionBatchesDirty = true;
	}
	lastLocationOverrideValues = std::move(nextOverrideValues);
	lastLocationTransitionDurations = pendingLocationTransitionDurations;
}

void SceneSettingsManager::RefreshLocationTransitionEndpoints(const ResolvedSettingMap& resolved)
{
	for (auto& [address, transition] : activeLocationTransitions) {
		// A restoring transition eases back to a captured baseline, which never drifts.
		if (transition.restoreAtEnd)
			continue;
		const auto resolvedIt = resolved.find(address);
		if (resolvedIt == resolved.end() || !IsNumericValue(resolvedIt->second))
			continue;
		// Only the endpoint moves: the eased output stays a continuous function of it, so retargeting
		// mid-flight cannot snap, and the transition still lands exactly on the live value.
		if (const auto targetValue = resolvedIt->second.get<float>(); std::isfinite(targetValue))
			transition.targetValue = targetValue;
	}
}

bool SceneSettingsManager::AdvanceLocationTransitions(float now)
{
	if (activeLocationTransitions.empty())
		return false;
	// A negative delta means the timer restarted, so it must not latch the tick off forever.
	const auto sinceLastTick = now - lastLocationTransitionTick;
	if (sinceLastTick >= 0.0f && sinceLastTick < kLocationTransitionTickInterval)
		return false;
	lastLocationTransitionTick = now;
	if (locationTransitionBatchesDirty)
		RebuildLocationTransitionBatches();

	bool appliedAny = false;
	const auto retryNow = std::chrono::steady_clock::now();
	for (auto& [featureShortName, batch] : locationTransitionBatches) {
		// A batch nobody can see move is a save/load round trip for nothing. The final tick always
		// applies, so the target value still lands exactly.
		bool valuesMoved = false;
		for (size_t index = 0; index < batch.transitions.size(); ++index) {
			const auto eased = EaseLocationTransition(*batch.transitions[index], now);
			valuesMoved = valuesMoved || IsLocationTransitionFinished(*batch.transitions[index], now) ||
			              std::abs(eased - batch.updates[index].value.get<float>()) >= kBlendEpsilon;
			batch.updates[index].value = eased;
		}
		if (!valuesMoved)
			continue;

		auto failureIt = transitionApplyFailures.find(featureShortName);
		if (failureIt != transitionApplyFailures.end() && failureIt->second.signature != batch.signature) {
			transitionApplyFailures.erase(failureIt);
			failureIt = transitionApplyFailures.end();
		}
		if (failureIt != transitionApplyFailures.end() && retryNow < failureIt->second.retryAfter)
			continue;

		// Warn once per distinct batch, then back off, so a stuck feature cannot spam the log.
		const auto recordFailure = [&](std::string_view message) {
			auto& failure = transitionApplyFailures[featureShortName];
			failure.signature = batch.signature;
			if (!failure.warningLogged) {
				logger::warn("[SceneSettings] {}", message);
				failure.warningLogged = true;
			}
			failure.retryAfter = retryNow + kApplyRetryDelay;
		};

		auto* feature = Feature::FindFeatureByShortName(featureShortName);
		if (!feature) {
			recordFailure(std::format(
				"Cannot apply location transition, feature {} is not loaded", featureShortName));
			continue;
		}
		if (!ApplyCatalogSceneSettings(*feature, batch.updates)) {
			recordFailure(std::format("Failed to apply location transition for {}", featureShortName));
			continue;
		}
		transitionApplyFailures.erase(featureShortName);
		ScheduleApplyVerification(featureShortName, batch.updates, batch.signature, true);
		appliedAny = true;

		bool restoredSetting = false;
		bool retainedSetting = false;
		for (size_t index = 0; index < batch.transitions.size(); ++index) {
			const auto& address = batch.addresses[index];
			const auto& transition = *batch.transitions[index];
			const bool finished = IsLocationTransitionFinished(transition, now);
			if (finished && transition.restoreAtEnd) {
				appliedSettings.erase(address);
				baselineSettings.erase(address);
				restoredSetting = true;
			} else {
				appliedSettings[address] = batch.updates[index].value;
				retainedSetting = true;
			}
			if (finished) {
				activeLocationTransitions.erase(address);
				locationTransitionBatchesDirty = true;
			}
		}
		if (retainedSetting)
			appliedFeatureNames.insert(featureShortName);
		else if (restoredSetting)
			PruneAppliedFeatureName(featureShortName);
	}
	if (activeLocationTransitions.empty()) {
		locationTransitionBatches.clear();
		transitionApplyFailures.clear();
		locationTransitionBatchesDirty = false;
	}
	return appliedAny;
}

void SceneSettingsManager::RetireFinishedLocationTransitions(float now)
{
	std::set<std::string> restoredFeatures;
	for (auto transitionIt = activeLocationTransitions.begin(); transitionIt != activeLocationTransitions.end();) {
		const auto& [address, transition] = *transitionIt;
		auto appliedIt = appliedSettings.find(address);
		if (!IsLocationTransitionFinished(transition, now) || appliedIt == appliedSettings.end() ||
			!IsNumericValue(appliedIt->second) ||
			std::abs(appliedIt->second.get<float>() - transition.targetValue) >= kBlendEpsilon) {
			++transitionIt;
			continue;
		}
		if (transition.restoreAtEnd) {
			appliedSettings.erase(address);
			baselineSettings.erase(address);
			restoredFeatures.insert(address.featureShortName);
		}
		transitionIt = activeLocationTransitions.erase(transitionIt);
		locationTransitionBatchesDirty = true;
	}
	for (const auto& featureShortName : restoredFeatures)
		PruneAppliedFeatureName(featureShortName);
}

void SceneSettingsManager::RebuildLocationTransitionBatches()
{
	locationTransitionBatches.clear();
	for (auto& [address, transition] : activeLocationTransitions) {
		auto& batch = locationTransitionBatches[address.featureShortName];
		if (batch.addresses.empty())
			batch.signature = std::hash<std::string_view>{}(address.featureShortName);
		batch.addresses.push_back(address);
		batch.transitions.push_back(&transition);
		batch.updates.push_back({ address.settingPath, address.settingKey, transition.startValue,
			!transition.restoreAtEnd });
		for (const auto& segment : address.settingPath)
			CombineHash(batch.signature, std::hash<std::string_view>{}(segment));
		CombineHash(batch.signature, std::hash<std::string_view>{}(address.settingKey));
		for (const auto value : { transition.startValue, transition.targetValue, transition.duration })
			HashSceneSettingValue(batch.signature, value);
		CombineHash(batch.signature, static_cast<size_t>(transition.restoreAtEnd));
	}
	std::erase_if(transitionApplyFailures,
		[&](const auto& item) { return !locationTransitionBatches.contains(item.first); });
	locationTransitionBatchesDirty = false;
}

void SceneSettingsManager::ClearLocationTransitions()
{
	activeLocationTransitions.clear();
	locationTransitionBatches.clear();
	transitionApplyFailures.clear();
	lastLocationOverrideValues.clear();
	lastLocationTransitionDurations.clear();
	pendingLocationTransitionDurations.clear();
	cachedLocationOverrides.clear();
	cachedLocationPeriodValues.clear();
	cachedLocationOverridesValid = false;
	locationTransitionBatchesDirty = false;
	lastLocationTransitionTick = -1.0f;
	locationOverridesDirty = true;
}

void SceneSettingsManager::RefreshBlendSnapshot(bool interior)
{
	// Indoors there is no weather to blend, and sampling one would churn the resolver's change check.
	blendSnapshot.weather = interior ? WeatherBlend{} : GetWeatherBlend();
	blendSnapshot.timeOfDayFactors = GetTimeOfDayFactors();
}

SceneSettingsManager::WeatherBlend SceneSettingsManager::GetWeatherBlend() const
{
	WeatherBlend blend;
	auto* sky = globals::game::sky;
	if (!sky)
		return blend;
	blend.currentWeatherId = sky->currentWeather ? sky->currentWeather->GetFormID() : 0;
	blend.lerp = std::isfinite(sky->currentWeatherPct) ? std::clamp(sky->currentWeatherPct, 0.0f, 1.0f) : 0.0f;
	blend.previousWeatherId = GetEffectivePreviousWeatherId(sky, blend.lerp);
	return blend;
}

SceneSettingsManager::SettingAddress SceneSettingsManager::GetEntryAddress(const SettingEntry& entry)
{
	return { entry.featureShortName, entry.settingPath, entry.settingKey };
}

bool SceneSettingsManager::IsResolvableEntry(const SettingEntry& entry, SceneType type) const
{
	// A per-period entry blends across periods, so it has to be a transitionable float whatever its layer.
	const bool floatsOnly = type == SceneType::TimeOfDay || entry.period != TimeOfDayPeriod::Count;
	// A tombstone is resolvable on purpose: suppressing an address means restoring its baseline,
	// and this gate is what gets that baseline collected.
	return IsEntryActive(entry) &&
	       IsSettingAllowedForType(type, entry.featureShortName, entry.settingPath, entry.settingKey, floatsOnly) &&
	       (!floatsOnly || IsNumericValue(entry.value));
}

bool SceneSettingsManager::HasActiveSceneEntriesCached()
{
	if (!activeEntryCacheDirty)
		return hasActiveSceneEntries;

	const auto hasResolvable = [&](const std::vector<SettingEntry>& sourceEntries, SceneType type) {
		return std::any_of(sourceEntries.begin(), sourceEntries.end(),
			[&](const SettingEntry& entry) { return IsResolvableEntry(entry, type); });
	};
	const auto hasResolvableInActiveSet = [&](const PeriodicSceneConfig& config, SceneType flatType) {
		return std::any_of(config.entries.begin(), config.entries.end(), [&](const SettingEntry& entry) {
			return config.IsPeriodActive(entry.period) && IsResolvableEntry(entry, flatType);
		});
	};

	hasActiveSceneEntries =
		std::any_of(entries.begin(), entries.end(),
			[&](const auto& item) { return hasResolvable(item.second, item.first); }) ||
		std::any_of(weatherSceneConfigs.begin(), weatherSceneConfigs.end(),
			[&](const auto& item) { return hasResolvableInActiveSet(item.second, SceneType::TimeOfDay); }) ||
		std::any_of(locationSceneConfigs.begin(), locationSceneConfigs.end(),
			[&](const auto& item) { return hasResolvableInActiveSet(item.second, SceneType::Location); });

	activeEntryCacheDirty = false;
	return hasActiveSceneEntries;
}

SceneSettingsManager::ResolvedSettingMap& SceneSettingsManager::BuildResolvedSettings(
	bool collectLocationTransitionDurations, bool interior)
{
	// Reuse the map's nodes across frames; null marks a slot the current resolve did not fill.
	auto& resolved = resolvedSettingsScratch;
	for (auto& [_, value] : resolved)
		value = nullptr;

	std::vector<SettingAddress> requiredBaselines;
	const auto collectBaselines = [&](const std::vector<SettingEntry>& sourceEntries, SceneType type) {
		for (const auto& entry : sourceEntries)
			if (IsResolvableEntry(entry, type))
				if (auto address = GetEntryAddress(entry); !baselineSettings.contains(address))
					requiredBaselines.push_back(std::move(address));
	};
	const auto collectGroupedBaselines = [&](const PeriodSettingMap& values) {
		for (const auto& [address, _] : values)
			if (!baselineSettings.contains(address))
				requiredBaselines.push_back(address);
	};

	const PeriodSettingMap* timeOfDayValues = nullptr;
	const auto& weather = blendSnapshot.weather;
	if (interior) {
		collectBaselines(GetEntries(SceneType::InteriorOnly), SceneType::InteriorOnly);
	} else {
		timeOfDayValues = &BuildTimeOfDayValueGroups();
		collectGroupedBaselines(*timeOfDayValues);
		for (auto weatherId : { weather.currentWeatherId, weather.previousWeatherId })
			if (weatherId != 0)
				collectGroupedBaselines(BuildWeatherValueGroups(weatherId));
	}

	const auto& locationTargets = GetCurrentLocationTargets();
	const bool rebuildLocationOverrides = collectLocationTransitionDurations || !cachedLocationOverridesValid;
	if (rebuildLocationOverrides) {
		for (const auto& target : locationTargets) {
			auto it = locationSceneConfigs.find(GetLocationConfigKey(target.type, target.formKey));
			if (it == locationSceneConfigs.end())
				continue;
			for (const auto& entry : it->second.entries)
				if (it->second.IsPeriodActive(entry.period) &&
					IsResolvableEntry(entry, SceneType::Location))
					if (auto address = GetEntryAddress(entry); !baselineSettings.contains(address))
						requiredBaselines.push_back(std::move(address));
		}
	}
	std::sort(requiredBaselines.begin(), requiredBaselines.end());
	requiredBaselines.erase(
		std::unique(requiredBaselines.begin(), requiredBaselines.end()), requiredBaselines.end());
	EnsureBaselines(requiredBaselines);

	if (interior) {
		ResolveInteriorSettings(resolved);
	} else {
		ResolveTimeOfDaySettings(resolved, *timeOfDayValues);
		ResolveWeatherSettings(resolved, *timeOfDayValues);
	}

	if (rebuildLocationOverrides) {
		pendingLocationTransitionDurations.clear();
		cachedLocationOverrides.clear();
		cachedLocationPeriodValues.clear();
		ResolveLocationSettings(cachedLocationOverrides, cachedLocationPeriodValues, locationTargets, true);
		cachedLocationOverridesValid = true;
	}
	BlendLocationPeriodValues(resolved, cachedLocationPeriodValues);
	for (const auto& [address, value] : cachedLocationOverrides)
		resolved[address] = value;
	std::erase_if(resolved, [](const auto& item) { return item.second.is_null(); });
	return resolved;
}

void SceneSettingsManager::ApplyResolvedSettings(const ResolvedSettingMap& resolved, bool forceRetry)
{
	struct PendingUpdate
	{
		const SettingAddress* address = nullptr;
		const json* value = nullptr;
		bool restore = false;
	};

	std::map<std::string, std::vector<PendingUpdate>> pendingByFeature;
	for (const auto& [address, _] : appliedSettings) {
		if (resolved.contains(address))
			continue;
		auto baselineIt = baselineSettings.find(address);
		if (baselineIt != baselineSettings.end())
			pendingByFeature[address.featureShortName].push_back({ &address, &baselineIt->second, true });
	}

	for (const auto& [address, value] : resolved) {
		auto appliedIt = appliedSettings.find(address);
		if (appliedIt != appliedSettings.end() && ResolvedValuesEqual(appliedIt->second, value))
			continue;
		pendingByFeature[address.featureShortName].push_back({ &address, &value, false });
	}
	std::erase_if(applyFailures, [&](const auto& item) { return !pendingByFeature.contains(item.first); });

	for (const auto& [featureShortName, pending] : pendingByFeature) {
		std::vector<CatalogSceneSettingUpdate> updates;
		updates.reserve(pending.size());
		for (const auto& update : pending)
			updates.push_back({ update.address->settingPath, update.address->settingKey, *update.value,
				!update.restore });

		// Memoized: the backoff check, the failure record and the verification all want the same hash.
		std::optional<size_t> signature;
		const auto getSignature = [&]() {
			if (!signature) {
				signature = GetCatalogUpdateSignature(featureShortName, updates);
				for (const auto& update : pending)
					CombineHash(*signature, static_cast<size_t>(update.restore));
			}
			return *signature;
		};
		// Warn once per distinct failure signature, then back off, so a stuck feature cannot spam the log.
		const auto recordApplyFailure = [&](std::chrono::steady_clock::time_point now, std::string_view message) {
			auto& failure = applyFailures[featureShortName];
			failure.signature = getSignature();
			if (!failure.warningLogged) {
				logger::warn("[SceneSettings] {}", message);
				failure.warningLogged = true;
			}
			failure.retryAfter = now + kApplyRetryDelay;
		};

		auto failureIt = applyFailures.find(featureShortName);
		if (failureIt != applyFailures.end() && failureIt->second.signature != getSignature()) {
			applyFailures.erase(failureIt);
			failureIt = applyFailures.end();
		}
		const auto now = std::chrono::steady_clock::now();
		if (!forceRetry && failureIt != applyFailures.end() && now < failureIt->second.retryAfter)
			continue;

		auto* feature = Feature::FindFeatureByShortName(featureShortName);
		if (!feature) {
			recordApplyFailure(now,
				std::format("Cannot apply resolved settings, feature {} is not loaded", featureShortName));
			continue;
		}
		if (!ApplyCatalogSceneSettings(*feature, updates)) {
			recordApplyFailure(now, std::format("Failed to apply resolved settings for {}", featureShortName));
			continue;
		}
		applyFailures.erase(featureShortName);
		ScheduleApplyVerification(featureShortName, updates, getSignature(), false);
		restoreFailureWarnings.erase(featureShortName);

		bool restoredSetting = false;
		bool appliedSetting = false;
		for (const auto& update : pending) {
			if (update.restore) {
				// Erase the applied entry last: the address points at its key.
				baselineSettings.erase(*update.address);
				appliedSettings.erase(*update.address);
				restoredSetting = true;
			} else {
				appliedSettings[*update.address] = *update.value;
				appliedSetting = true;
			}
		}
		if (appliedSetting)
			appliedFeatureNames.insert(featureShortName);
		else if (restoredSetting)
			PruneAppliedFeatureName(featureShortName);
	}
}

void SceneSettingsManager::RestoreAppliedSettings()
{
	ClearLocationTransitions();

	struct PendingRestore
	{
		SettingAddress address;
		CatalogSceneSettingUpdate update;
	};

	std::map<std::string, std::vector<PendingRestore>> updatesByFeature;
	for (const auto& [address, _] : appliedSettings) {
		auto baselineIt = baselineSettings.find(address);
		if (baselineIt != baselineSettings.end())
			updatesByFeature[address.featureShortName].push_back({
				address, { address.settingPath, address.settingKey, baselineIt->second, false } });
	}

	for (const auto& [featureShortName, pending] : updatesByFeature) {
		const auto now = std::chrono::steady_clock::now();
		if (auto retryIt = restoreRetryAfter.find(featureShortName);
			retryIt != restoreRetryAfter.end() && now < retryIt->second)
			continue;
		auto* feature = Feature::FindFeatureByShortName(featureShortName);
		if (!feature) {
			if (restoreFailureWarnings.insert(featureShortName).second)
				logger::warn("[SceneSettings] Cannot restore {}, feature is not loaded", featureShortName);
			restoreRetryAfter[featureShortName] = now + kApplyRetryDelay;
			continue;
		}

		std::vector<CatalogSceneSettingUpdate> updates;
		updates.reserve(pending.size());
		for (const auto& item : pending)
			updates.push_back(item.update);
		if (!ApplyCatalogSceneSettings(*feature, updates)) {
			if (restoreFailureWarnings.insert(featureShortName).second)
				logger::warn("[SceneSettings] Failed to restore base settings for {}", featureShortName);
			restoreRetryAfter[featureShortName] = now + kApplyRetryDelay;
			continue;
		}
		// Verify before dropping the baseline: erasing it on an unverified restore strands the
		// user's original value with nothing left to retry from.
		if (!FeatureRetainedUpdates(*feature, featureShortName, updates)) {
			if (restoreFailureWarnings.insert(featureShortName).second)
				logger::warn("[SceneSettings] {} did not retain restored base settings", featureShortName);
			featureApplyDocuments.erase(featureShortName);
			restoreRetryAfter[featureShortName] = now + kApplyRetryDelay;
			continue;
		}
		restoreFailureWarnings.erase(featureShortName);
		restoreRetryAfter.erase(featureShortName);
		// The restore deliberately undoes whatever the last apply put there.
		pendingApplyVerifications.erase(featureShortName);

		for (const auto& item : pending) {
			appliedSettings.erase(item.address);
			baselineSettings.erase(item.address);
		}
		appliedFeatureNames.erase(featureShortName);
		featureApplyDocuments.erase(featureShortName);
	}

	if (appliedSettings.empty()) {
		baselineSettings.clear();
		appliedFeatureNames.clear();
		featureApplyDocuments.clear();
		restoreFailureWarnings.clear();
		restoreRetryAfter.clear();
	} else {
		resolverDirty = true;
	}
}

void SceneSettingsManager::ResolveInteriorSettings(ResolvedSettingMap& resolved) const
{
	OverlayAllEntries(resolved, GetEntries(SceneType::InteriorOnly), SceneType::InteriorOnly);
}

void SceneSettingsManager::CollectPeriodValueGroups(
	const std::vector<SettingEntry>& sourceEntries, bool timeOfDayEnabled, SceneType type,
	PeriodSettingMap& values) const
{
	// Shipped overwrites are the layer's defaults; the user's own entry for the same address wins.
	for (auto source : { EntrySource::Overwrite, EntrySource::User }) {
		for (const auto& entry : sourceEntries) {
			const bool flat = entry.period == TimeOfDayPeriod::Count;
			const auto periodIndex = static_cast<int>(entry.period);
			if (entry.source != source || flat == timeOfDayEnabled || periodIndex < 0 ||
				periodIndex > kPeriodCount || !IsEntryActive(entry))
				continue;
			// A flat entry stands for every period at once.
			const int firstPeriod = flat ? 0 : periodIndex;
			const int lastPeriod = flat ? kPeriodCount : periodIndex + 1;
			// A tombstone clears its periods rather than filling them, so the layer beneath shows through.
			if (entry.deleted) {
				if (auto valuesIt = values.find(GetEntryAddress(entry)); valuesIt != values.end())
					for (int slot = firstPeriod; slot < lastPeriod; ++slot)
						valuesIt->second[slot].reset();
				continue;
			}
			if (!IsResolvableEntry(entry, type))
				continue;
			const auto value = entry.value.get<float>();
			if (!std::isfinite(value))
				continue;
			auto& periodValues = values[GetEntryAddress(entry)];
			for (int slot = firstPeriod; slot < lastPeriod; ++slot)
				periodValues[slot] = value;
		}
	}
}

const SceneSettingsManager::PeriodSettingMap& SceneSettingsManager::BuildTimeOfDayValueGroups() const
{
	if (timeOfDayValueGroups.revision == sceneValueRevision)
		return timeOfDayValueGroups.values;
	timeOfDayValueGroups.values.clear();
	CollectPeriodValueGroups(GetEntries(SceneType::TimeOfDay), true, SceneType::TimeOfDay, timeOfDayValueGroups.values);
	timeOfDayValueGroups.revision = sceneValueRevision;
	return timeOfDayValueGroups.values;
}

const SceneSettingsManager::PeriodSettingMap& SceneSettingsManager::BuildWeatherValueGroups(
	RE::FormID weatherId) const
{
	auto& cached = weatherValueGroups[weatherId];
	if (cached.revision == sceneValueRevision)
		return cached.values;
	cached.values.clear();
	if (auto configIt = weatherSceneConfigs.find(weatherId); configIt != weatherSceneConfigs.end())
		CollectPeriodValueGroups(configIt->second.entries, configIt->second.timeOfDayEnabled, SceneType::TimeOfDay,
			cached.values);
	cached.revision = sceneValueRevision;
	return cached.values;
}

void SceneSettingsManager::ResolveTimeOfDaySettings(
	ResolvedSettingMap& resolved, const PeriodSettingMap& values) const
{
	const auto& factors = blendSnapshot.timeOfDayFactors;
	for (const auto& [address, periodValues] : values) {
		auto baselineIt = baselineSettings.find(address);
		if (baselineIt == baselineSettings.end() || !IsNumericValue(baselineIt->second))
			continue;
		const auto baseline = baselineIt->second.get<float>();
		float result = 0.0f;
		for (int periodIndex = 0; periodIndex < kPeriodCount; ++periodIndex)
			result += factors[periodIndex] * periodValues[periodIndex].value_or(baseline);
		resolved[address] = result;
	}
}

void SceneSettingsManager::ResolveWeatherSettings(
	ResolvedSettingMap& resolved, const PeriodSettingMap& timeOfDayValues) const
{
	const auto& weather = blendSnapshot.weather;
	if (weather.currentWeatherId == 0)
		return;

	const auto& factors = blendSnapshot.timeOfDayFactors;
	const auto& currentValues = BuildWeatherValueGroups(weather.currentWeatherId);
	const auto& previousValues = BuildWeatherValueGroups(weather.previousWeatherId);

	const auto resolveWeather = [&](const SettingAddress& address, const PeriodSettingMap& weatherValues,
								   float baseline) -> std::optional<float> {
		auto weatherIt = weatherValues.find(address);
		if (weatherIt == weatherValues.end())
			return std::nullopt;
		auto timeOfDayIt = timeOfDayValues.find(address);
		float result = 0.0f;
		for (int periodIndex = 0; periodIndex < kPeriodCount; ++periodIndex) {
			const auto lower = timeOfDayIt != timeOfDayValues.end() ?
			                       timeOfDayIt->second[periodIndex].value_or(baseline) :
			                       baseline;
			result += factors[periodIndex] * weatherIt->second[periodIndex].value_or(lower);
		}
		return result;
	};

	const auto blendAddress = [&](const SettingAddress& address) {
		auto baselineIt = baselineSettings.find(address);
		if (baselineIt == baselineSettings.end() || !IsNumericValue(baselineIt->second))
			return;
		const auto baseline = baselineIt->second.get<float>();
		// Where the time-of-day layer left this address is what an unconfigured weather blends from.
		float lowerValue = baseline;
		if (auto resolvedIt = resolved.find(address);
			resolvedIt != resolved.end() && IsNumericValue(resolvedIt->second))
			lowerValue = resolvedIt->second.get<float>();

		const auto currentValue = resolveWeather(address, currentValues, baseline);
		const auto previousValue = resolveWeather(address, previousValues, baseline);
		if (!currentValue && !previousValue)
			return;
		const auto from = previousValue.value_or(lowerValue);
		const auto to = currentValue.value_or(lowerValue);
		resolved[address] = from + (to - from) * weather.lerp;
	};

	for (const auto& [address, _] : currentValues)
		blendAddress(address);
	for (const auto& [address, _] : previousValues)
		if (!currentValues.contains(address))
			blendAddress(address);
}

void SceneSettingsManager::ResolveLocationSettings(ResolvedSettingMap& resolved, PeriodSettingMap& periodValues,
	const std::vector<LocationTarget>& locationTargets, bool collectTransitionDurations)
{
	auto* transitionDurations = collectTransitionDurations ? &pendingLocationTransitionDurations : nullptr;
	for (const auto& target : locationTargets) {
		auto it = locationSceneConfigs.find(GetLocationConfigKey(target.type, target.formKey));
		if (it != locationSceneConfigs.end())
			ResolveLocationLink(it->second.entries, it->second.timeOfDayEnabled, resolved, periodValues,
				transitionDurations);
	}
}

void SceneSettingsManager::ResolveLocationLink(const std::vector<SettingEntry>& linkEntries, bool timeOfDayEnabled,
	ResolvedSettingMap& resolved, PeriodSettingMap& periodValues,
	std::map<SettingAddress, float>* transitionDurations) const
{
	if (!timeOfDayEnabled) {
		OverlayAllEntries(resolved, linkEntries, SceneType::Location, transitionDurations);
		// A narrower flat link replaces any blend a broader per-period link started.
		std::erase_if(periodValues, [&](const auto& item) { return resolved.contains(item.first); });
		return;
	}
	for (const auto& entry : linkEntries) {
		if (entry.period == TimeOfDayPeriod::Count || !IsResolvableEntry(entry, SceneType::Location))
			continue;
		auto address = GetEntryAddress(entry);
		// A broader flat value is what this link's unset periods keep; a flat tombstone keeps the baseline.
		if (auto flatIt = resolved.find(address); flatIt != resolved.end()) {
			auto baselineIt = baselineSettings.find(address);
			const auto* seed = IsNumericValue(flatIt->second)    ? &flatIt->second :
			                   baselineIt != baselineSettings.end() ? &baselineIt->second :
			                                                          nullptr;
			if (seed && IsNumericValue(*seed))
				periodValues[address].fill(seed->get<float>());
			resolved.erase(flatIt);
		}
		if (transitionDurations && !entry.deleted)
			RecordLocationTransitionDuration(entry, address, *transitionDurations);
	}
	CollectPeriodValueGroups(linkEntries, true, SceneType::Location, periodValues);
}

void SceneSettingsManager::BlendLocationPeriodValues(ResolvedSettingMap& resolved,
	const PeriodSettingMap& periodValues) const
{
	const auto& factors = blendSnapshot.timeOfDayFactors;
	for (const auto& [address, values] : periodValues) {
		auto baselineIt = baselineSettings.find(address);
		if (baselineIt == baselineSettings.end() || !IsNumericValue(baselineIt->second))
			continue;
		// Unset periods fall through to whatever time of day and weather resolved beneath.
		float lowerValue = baselineIt->second.get<float>();
		if (auto resolvedIt = resolved.find(address);
			resolvedIt != resolved.end() && IsNumericValue(resolvedIt->second))
			lowerValue = resolvedIt->second.get<float>();
		float result = 0.0f;
		for (int periodIndex = 0; periodIndex < kPeriodCount; ++periodIndex)
			result += factors[periodIndex] * values[periodIndex].value_or(lowerValue);
		resolved[address] = result;
	}
}

void SceneSettingsManager::RecordLocationTransitionDuration(const SettingEntry& entry,
	const SettingAddress& address, std::map<SettingAddress, float>& transitionDurations) const
{
	if (entry.deleted) {
		transitionDurations.erase(address);
		return;
	}
	if (!IsNumericValue(entry.value))
		return;
	const auto duration = entry.transitionSeconds.value_or(locationTransitionSeconds);
	transitionDurations[address] = std::clamp(std::isfinite(duration) ? duration : locationTransitionSeconds,
		0.0f, kMaxLocationTransitionSeconds);
}

void SceneSettingsManager::OverlayEntries(ResolvedSettingMap& resolved, const std::vector<SettingEntry>& sourceEntries,
	SceneType type, EntrySource source, std::map<SettingAddress, float>* transitionDurations) const
{
	for (const auto& entry : sourceEntries) {
		// Per-period entries blend through the period maps instead.
		if (entry.source != source || entry.period != TimeOfDayPeriod::Count || !IsEntryActive(entry) ||
			!IsSettingAllowedForType(type, entry.featureShortName, entry.settingPath, entry.settingKey))
			continue;
		auto address = GetEntryAddress(entry);
		if (!baselineSettings.contains(address))
			continue;
		if (transitionDurations)
			RecordLocationTransitionDuration(entry, address, *transitionDurations);
		// A tombstone supplies nothing. Nulling the slot is how this map already marks an address the
		// resolve did not fill, so BuildResolvedSettings prunes it and the baseline is restored.
		resolved[std::move(address)] = entry.deleted ? json(nullptr) : entry.value;
	}
}

void SceneSettingsManager::OverlayAllEntries(ResolvedSettingMap& resolved,
	const std::vector<SettingEntry>& sourceEntries, SceneType type,
	std::map<SettingAddress, float>* transitionDurations) const
{
	// Shipped overwrites are the layer's defaults; the user's own entry for the same address wins.
	OverlayEntries(resolved, sourceEntries, type, EntrySource::Overwrite, transitionDurations);
	OverlayEntries(resolved, sourceEntries, type, EntrySource::User, transitionDurations);
}

const json* SceneSettingsManager::GetFeatureBaseSnapshot(const std::string& featureShortName)
{
	if (auto snapshotIt = featureBaseSnapshots.find(featureShortName); snapshotIt != featureBaseSnapshots.end())
		return &snapshotIt->second;

	auto* feature = Feature::FindFeatureByShortName(featureShortName);
	json snapshot;
	if (!feature || !TrySaveFeatureSettings(*feature, "snapshot settings", snapshot))
		return nullptr;

	// SaveSettings reports the live scene layer, so fold the applied addresses back to their baselines.
	for (auto appliedIt = appliedSettings.lower_bound({ featureShortName, {}, {} });
		appliedIt != appliedSettings.end() && appliedIt->first.featureShortName == featureShortName;
		++appliedIt) {
		const auto& address = appliedIt->first;
		auto baselineIt = baselineSettings.find(address);
		if (baselineIt == baselineSettings.end())
			continue;
		auto* setting = FindAllowedCatalogSetting(
			address.featureShortName, address.settingPath, address.settingKey);
		auto* value = setting ? GetCatalogSerializedValue(snapshot, *setting) : nullptr;
		if (value && IsCompatibleSceneSettingValue(*value, baselineIt->second))
			*value = baselineIt->second;
	}

	return &featureBaseSnapshots.emplace(featureShortName, std::move(snapshot)).first->second;
}

void SceneSettingsManager::EnsureBaselines(std::span<const SettingAddress> addresses)
{
	std::map<std::string, std::vector<const SettingAddress*>> missingByFeature;
	for (const auto& address : addresses)
		if (!baselineSettings.contains(address))
			missingByFeature[address.featureShortName].push_back(&address);

	for (const auto& [featureShortName, missing] : missingByFeature) {
		const auto* snapshot = GetFeatureBaseSnapshot(featureShortName);
		if (!snapshot)
			continue;
		for (const auto* address : missing) {
			auto* setting = FindAllowedCatalogSetting(
				address->featureShortName, address->settingPath, address->settingKey);
			const auto* value = setting ? GetCatalogSerializedValue(*snapshot, *setting) : nullptr;
			if (value && IsSceneSettingPrimitive(*value))
				baselineSettings.try_emplace(*address, *value);
		}
	}
}

void SceneSettingsManager::InvalidateFeatureSnapshot(std::string_view featureShortName)
{
	cachedLocationOverridesValid = false;
	locationOverridesDirty = true;
	if (featureShortName.empty()) {
		featureBaseSnapshots.clear();
		featureApplyDocuments.clear();
		pendingApplyVerifications.clear();
		std::erase_if(baselineSettings,
			[&](const auto& item) { return !appliedSettings.contains(item.first); });
		return;
	}
	const auto featureName = std::string(featureShortName);
	featureBaseSnapshots.erase(featureName);
	featureApplyDocuments.erase(featureName);
	pendingApplyVerifications.erase(featureName);
	std::erase_if(baselineSettings, [&](const auto& item) {
		return item.first.featureShortName == featureName && !appliedSettings.contains(item.first);
	});
}

void SceneSettingsManager::PruneAppliedFeatureName(const std::string& featureShortName)
{
	if (std::none_of(appliedSettings.begin(), appliedSettings.end(),
			[&](const auto& item) { return item.first.featureShortName == featureShortName; })) {
		appliedFeatureNames.erase(featureShortName);
		// The base settings are editable again from here, so the next apply has to re-snapshot them:
		// replaying this document would revert anything changed while the layer was off the feature.
		featureApplyDocuments.erase(featureShortName);
	}
}

json SceneSettingsManager::GetBaselineValue(const SettingAddress& address)
{
	if (!FindAllowedCatalogSetting(address.featureShortName, address.settingPath, address.settingKey))
		return {};
	EnsureBaselines(std::span{ &address, 1 });
	if (auto it = baselineSettings.find(address); it != baselineSettings.end())
		return it->second;
	return {};
}

const json* SceneSettingsManager::FindAppliedBaseline(const SettingIdentity& setting) const
{
	const SettingAddress address{ setting.featureShortName, setting.settingPath, setting.settingKey };
	if (!appliedSettings.contains(address))
		return nullptr;
	const auto found = baselineSettings.find(address);
	return found != baselineSettings.end() ? &found->second : nullptr;
}

bool SceneSettingsManager::ResolvedValuesEqual(const json& lhs, const json& rhs)
{
	return lhs == rhs;
}

bool SceneSettingsManager::AppliedValuesEqual(const json& lhs, const json& rhs)
{
	if (lhs == rhs)
		return true;
	if (!lhs.is_number() || !rhs.is_number())
		return false;
	return static_cast<float>(lhs.get<double>()) == static_cast<float>(rhs.get<double>());
}

bool SceneSettingsManager::FeatureRetainedUpdates(Feature& feature, std::string_view featureShortName,
	const std::vector<CatalogSceneSettingUpdate>& updates, std::vector<json>* observed)
{
	json actualSettings;
	if (!TrySaveFeatureSettings(feature, "verify applied settings", actualSettings))
		return false;

	bool retained = true;
	if (observed)
		observed->reserve(updates.size());
	for (const auto& update : updates) {
		auto* setting = FindAllowedCatalogSetting(featureShortName, update.settingPath, update.key);
		const auto* actual = setting ? GetCatalogSerializedValue(actualSettings, *setting) : nullptr;
		retained = retained && actual && AppliedValuesEqual(*actual, update.value);
		if (observed)
			observed->push_back(actual ? *actual : json{});
	}
	return retained;
}
