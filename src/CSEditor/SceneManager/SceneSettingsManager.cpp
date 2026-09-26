#include "SceneSettingsManager.h"

#include "Feature.h"
#include "Globals.h"
#include "SceneSettingsCatalog.generated.h"
#include "SceneSettingsInternal.h"
#include "SceneSettingsOverwrites.h"
#include "State.h"
#include "Utils/FileSystem.h"
#include "Utils/Format.h"
#include "Utils/SettingsCatalog.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <functional>
#include <regex>

using namespace SceneSettingsInternal;
using namespace SceneSettingsOverwrites;

namespace
{

	SceneSettingsManager* sceneSettingsManagerSingleton = nullptr;

	/// RAII CPU pass for the in-game Profiling UI; ends the pass on every early-return path.
	struct ProfilerPassScope
	{
		explicit ProfilerPassScope(const std::string& name)
		{
			if (globals::profiler)
				globals::profiler->BeginPass(name);
		}
		~ProfilerPassScope()
		{
			if (globals::profiler)
				globals::profiler->EndPass();
		}
	};
}

SceneSettingsManager::SceneSettingsManager()
{
	assert(!sceneSettingsManagerSingleton);
	sceneSettingsManagerSingleton = this;
}

SceneSettingsManager::~SceneSettingsManager()
{
	if (sceneSettingsManagerSingleton == this)
		sceneSettingsManagerSingleton = nullptr;
}

SceneSettingsManager* SceneSettingsManager::GetSingleton()
{
	return sceneSettingsManagerSingleton;
}

size_t SceneSettingsManager::GetCatalogUpdateSignature(std::string_view featureShortName,
	std::span<const CatalogSceneSettingUpdate> updates)
{
	size_t signature = std::hash<std::string_view>{}(featureShortName);
	for (const auto& update : updates) {
		for (const auto& segment : update.settingPath)
			CombineHash(signature, std::hash<std::string_view>{}(segment));
		CombineHash(signature, std::hash<std::string_view>{}(update.key));
	}
	return signature;
}

bool SceneSettingsManager::ApplyCatalogSceneSettings(
	Feature& feature, const std::vector<CatalogSceneSettingUpdate>& updates)
{
	if (updates.empty())
		return true;

	const auto featureShortName = feature.GetShortName();
	auto documentIt = featureApplyDocuments.find(featureShortName);
	if (documentIt == featureApplyDocuments.end()) {
		json settingsDocument;
		if (!TrySaveFeatureSettings(feature, "snapshot settings", settingsDocument))
			return false;
		documentIt = featureApplyDocuments.emplace(featureShortName, std::move(settingsDocument)).first;
	}
	auto& settingsDocument = documentIt->second;

	// Resolved up front: the document outlives this call, so a rejected update must not have touched it.
	std::vector<const SceneSettingsCatalog::SettingMetadata*> catalogSettings;
	std::vector<json*> targetValues;
	catalogSettings.reserve(updates.size());
	targetValues.reserve(updates.size());
	for (const auto& update : updates) {
		auto* setting = FindAllowedCatalogSetting(featureShortName, update.settingPath, update.key);
		auto* currentValue = setting ? GetCatalogSerializedValue(settingsDocument, *setting) : nullptr;
		if (!currentValue || !IsSceneSettingPrimitive(*currentValue) ||
			!IsSceneSettingPrimitive(update.value) ||
			!IsCompatibleSceneSettingValue(*currentValue, update.value))
			return false;
		catalogSettings.push_back(setting);
		targetValues.push_back(currentValue);
	}

	std::vector<json> originalValues;
	originalValues.reserve(updates.size());
	for (size_t index = 0; index < updates.size(); ++index) {
		auto& targetValue = *targetValues[index];
		originalValues.push_back(std::move(targetValue));
		targetValue = updates[index].value;
		if (updates[index].clampToControlRange &&
			ClampCatalogNumericValue(*catalogSettings[index], targetValue))
			WarnOnceAboutClampedSceneSetting(
				featureShortName, *catalogSettings[index], updates[index].value, targetValue);
	}

	try {
		feature.LoadSettings(settingsDocument);
		return true;
	} catch (const std::exception& e) {
		logger::warn("[SceneSettings] Failed to apply settings for {}: {}", featureShortName, e.what());
	} catch (...) {
		logger::warn("[SceneSettings] Failed to apply settings for {}", featureShortName);
	}

	try {
		for (size_t index = 0; index < updates.size(); ++index)
			*targetValues[index] = std::move(originalValues[index]);
		feature.LoadSettings(settingsDocument);
	} catch (...) {
		featureApplyDocuments.erase(featureShortName);
		logger::error("[SceneSettings] Failed to restore {} after an apply error", featureShortName);
	}
	return false;
}

void SceneSettingsManager::ScheduleApplyVerification(std::string_view featureShortName,
	const std::vector<CatalogSceneSettingUpdate>& updates, size_t signature, bool transition)
{
	pendingApplyVerifications[std::string(featureShortName)] = {
		.appliedFrame = lastUpdateFrame,
		.updates = updates,
		.signature = signature,
		.transition = transition,
	};
}

void SceneSettingsManager::VerifyPendingApplies()
{
	for (auto verificationIt = pendingApplyVerifications.begin();
		verificationIt != pendingApplyVerifications.end();) {
		const auto& [featureShortName, verification] = *verificationIt;
		// Give the feature the frame it was handed the values in before reading them back.
		if (verification.appliedFrame == lastUpdateFrame) {
			++verificationIt;
			continue;
		}

		auto* feature = Feature::FindFeatureByShortName(featureShortName);
		std::vector<json> observed;
		const bool retained = feature &&
		                      FeatureRetainedUpdates(*feature, featureShortName, verification.updates, &observed);

		if (!retained) {
			logger::warn("[SceneSettings] {} did not retain settings after reporting a successful apply",
				featureShortName);
			featureApplyDocuments.erase(featureShortName);
			// Record what the feature actually reports rather than dropping the address: the scene
			// layer did touch it, so it still owes the baseline back, and the mismatch against the
			// resolved value is what drives the retry. A restore keeps dropping it, as it always did.
			for (size_t index = 0; index < verification.updates.size(); ++index) {
				const auto& update = verification.updates[index];
				const SettingAddress address{ featureShortName, update.settingPath, update.key };
				const bool hasObserved = update.clampToControlRange && index < observed.size() &&
				                         !observed[index].is_null();
				if (hasObserved)
					appliedSettings[address] = observed[index];
				else
					appliedSettings.erase(address);
			}
			PruneAppliedFeatureName(featureShortName);
			auto& failure = (verification.transition ? transitionApplyFailures : applyFailures)[featureShortName];
			failure.signature = verification.signature;
			failure.retryAfter = std::chrono::steady_clock::now() + kApplyRetryDelay;
			failure.warningLogged = true;
			resolverDirty = true;
		}
		verificationIt = pendingApplyVerifications.erase(verificationIt);
	}
}

// --- Path Resolution ---

std::string SceneSettingsManager::GetSceneTypeName(SceneType type)
{
	switch (type) {
	case SceneType::InteriorOnly:
		return "InteriorOnly";
	case SceneType::TimeOfDay:
		return "TimeOfDay";
	case SceneType::Location:
		return "Location";
	default:
		return "Unknown";
	}
}

std::filesystem::path SceneSettingsManager::GetUserSettingsFilePath()
{
	return Util::PathHelpers::GetSceneSettingsPath() / "SceneManager.json";
}

std::filesystem::path SceneSettingsManager::GetOverwritesPath(SceneType type)
{
	// Location overwrites are keyed by form under GetLocationOverwritesDir(), not by scene type name.
	assert(IsEntryListSceneType(type));
	return Util::PathHelpers::GetSceneSettingsPath() / GetSceneTypeName(type);
}

std::filesystem::path SceneSettingsManager::GetWeatherOverwritesDir()
{
	return Util::PathHelpers::GetSceneSettingsPath() / "Weather";
}

std::filesystem::path SceneSettingsManager::GetLocationOverwritesDir()
{
	return Util::PathHelpers::GetSceneSettingsPath() / "Locations";
}

std::filesystem::path SceneSettingsManager::GetPresetMetadataPath(const std::string& presetName)
{
	return Util::PathHelpers::GetSceneSettingsPath() / (presetName + ".json");
}

bool SceneSettingsManager::IsReservedPresetName(std::string_view presetName)
{
	// Windows paths are case-insensitive, so "scenemanager" would still replace the user document.
	return _stricmp(std::string(presetName).c_str(), GetUserSettingsFilePath().stem().string().c_str()) == 0;
}

bool SceneSettingsManager::IsValidPresetVersion(std::string_view version)
{
	static const std::regex pattern(R"(^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$)");
	return std::regex_match(version.begin(), version.end(), pattern);
}

// --- Time of Day Period Helpers ---

const char* SceneSettingsManager::GetPeriodName(TimeOfDayPeriod period)
{
	int idx = static_cast<int>(period);
	return (idx >= 0 && idx < kPeriodCount) ? kPeriodNames[idx] : "Unknown";
}

SceneSettingsManager::TimeOfDayPeriod SceneSettingsManager::GetPeriodFromName(const std::string& name)
{
	for (auto period : kPeriods) {
		if (name == GetPeriodName(period))
			return period;
	}
	return TimeOfDayPeriod::Count;
}

namespace
{
	/// The editor can run before globals are cached, so the singleton is the fallback.
	RE::Calendar* GetCalendar()
	{
		return globals::game::calendar ? globals::game::calendar : RE::Calendar::GetSingleton();
	}
}

float SceneSettingsManager::GetCurrentGameHour()
{
	// Prefer calendar (ground truth), which the Weather Editor slider writes to.
	// sky->currentGameHour may lag when timeScale is 0 (time paused).
	auto calendar = GetCalendar();
	float hour = 12.0f;
	if (calendar && calendar->gameHour)
		hour = calendar->gameHour->value;
	else if (auto sky = globals::game::sky)
		hour = sky->currentGameHour;
	if (!std::isfinite(hour))
		hour = 12.0f;

	// Normalize into [0, 24) so midnight is 0 and never 24.
	hour = std::clamp(hour, 0.0f, 24.0f);
	if (hour >= 24.0f)
		hour = 0.0f;
	return hour;
}

void SceneSettingsManager::SetGameHour(float hour)
{
	if (!std::isfinite(hour))
		return;
	auto calendar = GetCalendar();
	if (calendar && calendar->gameHour)
		calendar->gameHour->value = std::clamp(hour, 0.0f, 24.0f);
}

float SceneSettingsManager::GetPeriodMidHour(TimeOfDayPeriod period)
{
	const int index = static_cast<int>(period);
	if (index < 0 || index >= kPeriodCount)
		return GetCurrentGameHour();

	// Night ends past 24, so its middle lands after midnight.
	const float mid = (kPeriodHours[index][0] + kPeriodHours[index][1]) * 0.5f;
	return mid >= 24.0f ? mid - 24.0f : mid;
}

SceneSettingsManager::PeriodLookup SceneSettingsManager::FindPeriodForHour(float hour)
{
	for (int index = 0; index < kPeriodCount; ++index) {
		const float start = kPeriodHours[index][0];
		const float end = kPeriodHours[index][1];
		// Night ends past 24, so pre-dawn hours have to be compared against hour + 24.
		const float periodHour = (end > 24.0f && hour < start) ? hour + 24.0f : hour;
		if (periodHour >= start && periodHour < end)
			return { index, periodHour };
	}
	return {};
}

static_assert(std::ranges::all_of(SceneSettingsManager::kPeriodHours, [](const auto& hours) {
	return hours[1] - hours[0] >= SceneSettingsManager::kMaxTimeOfDayTransitionHours;
}));

std::array<float, SceneSettingsManager::kPeriodCount> SceneSettingsManager::GetTimeOfDayFactors() const
{
	std::array<float, kPeriodCount> factors{};
	const auto lookup = FindPeriodForHour(GetCurrentGameHour());
	if (lookup.index < 0) {
		factors[static_cast<int>(TimeOfDayPeriod::Day)] = 1.0f;
		return factors;
	}

	// A zero-length blend always takes this branch, so the division below never sees it.
	const float hoursToEnd = kPeriodHours[lookup.index][1] - lookup.hour;
	if (hoursToEnd >= timeOfDayTransitionHours) {
		factors[lookup.index] = 1.0f;
		return factors;
	}

	// Inside the blend-out zone: cross-fade into the next period.
	const float weight = hoursToEnd / timeOfDayTransitionHours;
	factors[lookup.index] = weight;
	factors[(lookup.index + 1) % kPeriodCount] = 1.0f - weight;
	return factors;
}

void SceneSettingsManager::SetTimeOfDayTransitionHours(std::optional<float> hours, bool deferSave)
{
	if (hours) {
		if (!std::isfinite(*hours))
			return;
		*hours = std::clamp(*hours, 0.0f, kMaxTimeOfDayTransitionHours);
	}
	if (hours.has_value() == userTimeOfDayTransitionHours.has_value() &&
		(!hours || std::abs(*userTimeOfDayTransitionHours - *hours) < kBlendEpsilon))
		return;
	userTimeOfDayTransitionHours = hours;
	RefreshTimeOfDayTransitionHours();
	if (deferSave)
		MarkDeferredSceneChanges();
	else
		SaveAllUserSettings();
	ReapplyIfActive();
}

void SceneSettingsManager::RefreshTimeOfDayTransitionHours()
{
	timeOfDayTransitionHours = kDefaultTimeOfDayTransitionHours;
	for (const auto& preset : presetMetadata)
		timeOfDayTransitionHours = preset.transitionHours.value_or(timeOfDayTransitionHours);
	timeOfDayTransitionHours = userTimeOfDayTransitionHours.value_or(timeOfDayTransitionHours);
}

SceneSettingsManager::TimeOfDayPeriod SceneSettingsManager::GetCurrentPeriod()
{
	const auto lookup = FindPeriodForHour(GetCurrentGameHour());
	return lookup.index < 0 ? TimeOfDayPeriod::Day : static_cast<TimeOfDayPeriod>(lookup.index);
}

// --- Feature Metadata ---

bool SceneSettingsManager::IsFeatureAllowedForType(SceneType type, const std::string& featureShortName)
{
	if (!Feature::FindFeatureByShortName(featureShortName))
		return false;

	switch (type) {
	case SceneType::InteriorOnly:
		return IsInteriorOnlyFeatureAllowed(featureShortName) &&
		       CatalogHasSceneSettings(featureShortName, type);
	case SceneType::TimeOfDay:
		return IsTimeOfDayFeatureAllowed(featureShortName) &&
		       CatalogHasSceneSettings(featureShortName, type);
	case SceneType::Location:
		return (IsInteriorOnlyFeatureAllowed(featureShortName) ||
		           IsTimeOfDayFeatureAllowed(featureShortName)) &&
		       CatalogHasSceneSettings(featureShortName, type);
	default:
		return false;
	}
}

bool SceneSettingsManager::IsSettingAllowedForType(SceneType type,
	const std::string& featureShortName, const std::vector<std::string>& settingPath,
	const std::string& settingKey, bool requireTransitionable)
{
	auto* setting = FindAllowedCatalogSetting(featureShortName, settingPath, settingKey, requireTransitionable);
	return Feature::FindFeatureByShortName(featureShortName) && setting &&
	       IsCatalogSettingAllowedForSceneType(type, *setting);
}

bool SceneSettingsManager::IsSceneSettingAllowed(
	std::string_view featureShortName, std::string_view settingPath, std::string_view settingKey)
{
	auto* setting = SceneSettingsCatalog::FindSetting(featureShortName, settingPath, settingKey);
	return setting && IsCatalogSettingAllowedByPolicy(*setting);
}

std::vector<std::string> SceneSettingsManager::GetExteriorRelevantFeatureNames()
{
	return GetLoadedCatalogFeatureNames(SceneType::TimeOfDay);
}

std::vector<std::string> SceneSettingsManager::GetLocationRelevantFeatureNames()
{
	return GetLoadedCatalogFeatureNames(SceneType::Location);
}

std::string SceneSettingsManager::GetFeatureDisplayName(const std::string& featureShortName)
{
	auto* feature = Feature::FindFeatureByShortName(featureShortName);
	return feature ? feature->GetDisplayName() : featureShortName;
}

bool SceneSettingsManager::GetSettingControlInfo(const SettingEntry& entry, SettingControlInfo& info)
{
	auto* setting = FindAllowedCatalogSetting(
		entry.featureShortName, entry.settingPath, entry.settingKey);
	if (!setting)
		return false;
	info = MakeSettingControlInfo(*setting);
	return true;
}

std::string SceneSettingsManager::GetSettingDisplayName(const std::string& settingKey)
{
	return StripImGuiId(Util::PrettifyIdentifier(settingKey));
}

json SceneSettingsManager::GetFeatureSettingValue(const std::string& featureShortName,
	const std::vector<std::string>& settingPath, const std::string& settingKey)
{
	auto* setting = FindAllowedCatalogSetting(featureShortName, settingPath, settingKey);
	if (!setting)
		return {};
	auto* feature = Feature::FindFeatureByShortName(featureShortName);
	if (!feature)
		return {};

	SceneLayerGuard guard;
	json value;
	if (GetCatalogSettingValue(*feature, *setting, value))
		return value;
	return {};
}

// --- Generic Entry Management ---

std::vector<SceneSettingsManager::SettingEntry>& SceneSettingsManager::GetEntriesMut(SceneType type)
{
	assert(IsEntryListSceneType(type));
	return entries[type];
}

void SceneSettingsManager::BumpEntryPresentationRevision()
{
	++entryPresentationRevision;
	MarkSceneValuesDirty();
}

void SceneSettingsManager::MarkSceneValuesDirty()
{
	++sceneValueRevision;
	locationOverridesDirty = true;
}

const std::vector<SceneSettingsManager::SettingEntry>& SceneSettingsManager::GetEntries(SceneType type) const
{
	static const std::vector<SettingEntry> empty;
	if (!IsEntryListSceneType(type))
		return empty;
	auto it = entries.find(type);
	return (it != entries.end()) ? it->second : empty;
}

void SceneSettingsManager::MarkEntryListUserSettingsModified(SceneType type)
{
	assert(IsEntryListSceneType(type));
	if (type == SceneType::InteriorOnly)
		interiorUserSettingsModified = true;
	else
		timeOfDayUserSettingsModified = true;
}

bool SceneSettingsManager::IsEntryActive(const SettingEntry& entry) const
{
	return !entry.paused && !IsFeaturePaused(entry.featureShortName);
}

bool SceneSettingsManager::HasEntryFromSource(SceneType type, const std::string& featureShortName,
	const std::vector<std::string>& settingPath, const std::string& settingKey, EntrySource source) const
{
	for (const auto& entry : GetEntries(type)) {
		if (entry.source == source && IsSameSetting(entry, featureShortName, settingPath, settingKey))
			return true;
	}
	return false;
}

bool SceneSettingsManager::HasEntryForPeriod(const std::string& featureShortName,
	const std::vector<std::string>& settingPath, const std::string& settingKey,
	TimeOfDayPeriod period, EntrySource source) const
{
	for (const auto& entry : GetEntries(SceneType::TimeOfDay)) {
		if (entry.source == source && entry.period == period &&
			IsSameSetting(entry, featureShortName, settingPath, settingKey))
			return true;
	}
	return false;
}

bool SceneSettingsManager::HasDuplicateEntry(SceneType type, const std::string& featureShortName,
	const std::vector<std::string>& settingPath, const std::string& settingKey, EntrySource source, TimeOfDayPeriod period) const
{
	if (!IsEntryListSceneType(type))
		return false;
	if (type == SceneType::TimeOfDay)
		return HasEntryForPeriod(featureShortName, settingPath, settingKey, period, source);
	return HasEntryFromSource(type, featureShortName, settingPath, settingKey, source);
}

void SceneSettingsManager::RemoveSetting(SceneType type, size_t index)
{
	if (!IsEntryListSceneType(type))
		return;
	auto& vec = GetEntriesMut(type);
	if (index >= vec.size())
		return;

	const auto entry = vec[index];
	if (entry.source == EntrySource::Overwrite && !entry.sourceFilename.empty() &&
		!RemoveSettingFromOverwriteFile(GetSceneOverwritePath(type, entry), entry.settingPath, entry.settingKey))
		return;

	logger::info("[SceneSettings] Removed {} entry: {} (source={})", GetSceneTypeName(type),
		GetSettingLogName(entry.featureShortName, entry.settingPath, entry.settingKey),
		entry.source == EntrySource::Overwrite ? "overwrite" : "user");

	vec.erase(vec.begin() + static_cast<ptrdiff_t>(index));
	BumpEntryPresentationRevision();
	if (entry.source == EntrySource::User) {
		MarkEntryListUserSettingsModified(type);
		SaveAllUserSettings();
	}
	ReapplyIfActive();
}

void SceneSettingsManager::CommitSceneSettingChanges()
{
	SaveAllUserSettings();
	ReapplyIfActive();
}

void SceneSettingsManager::MarkDeferredSceneChanges()
{
	deferredSceneChangesPending = true;
	deferredSceneChangesDeadline = std::chrono::steady_clock::now() + kDeferredSaveDelay;
}

void SceneSettingsManager::HoldDeferredSceneChanges()
{
	// Only ever pushes an existing deadline out, so grabbing a control without moving it cannot
	// schedule a save of its own.
	if (deferredSceneChangesPending)
		deferredSceneChangesDeadline = std::chrono::steady_clock::now() + kDeferredSaveDelay;
}

void SceneSettingsManager::FlushDeferredSceneChanges()
{
	if (!deferredSceneChangesPending || std::chrono::steady_clock::now() < deferredSceneChangesDeadline)
		return;

	SaveAllUserSettings();
	ReapplyIfActive();
}

// --- Event Handler ---

RE::BSEventNotifyControl SceneSettingsManager::MenuOpenCloseEventHandler::ProcessEvent(
	const RE::MenuOpenCloseEvent* a_event,
	RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	if (a_event && a_event->menuName == RE::LoadingMenu::MENU_NAME && !a_event->opening) {
		// Defer the transition to next frame - cell data isn't available yet.
		// The sink outlives the manager, so a menu close during shutdown must find no singleton.
		if (auto* manager = GetSingleton())
			manager->queuedLoadingTransition.store(true, std::memory_order_relaxed);
	}

	return RE::BSEventNotifyControl::kContinue;
}

// --- Scene Application ---

void SceneSettingsManager::Update()
{
	ProfilerPassScope profilerPass("SceneSettingsManager::Update");
	if (globals::state) {
		const auto frame = globals::state->frameCount;
		if (lastUpdateFrame == frame)
			return;
		lastUpdateFrame = frame;
	}
	VerifyPendingApplies();
	FlushDeferredSceneChanges();

	if (queuedLoadingTransition.exchange(false, std::memory_order_relaxed))
		OnLoadingTransition();
	else
		ResolveAndApply();
}

void SceneSettingsManager::OnLoadingTransition()
{
	resolverDirty = true;
	// The cell can take several frames to arrive, so the committed context stays stale until a
	// resolve actually succeeds. The sky is mid-transition on arrival, so its cache is stale too.
	suppressLocationTransitionUntilContextResolved = true;
	cachedPreviousWeatherId = 0;
	ResolveAndApply(true, false);
}

void SceneSettingsManager::ReapplyIfActive()
{
	activeEntryCacheDirty = true;
	resolverDirty = true;
	MarkSceneValuesDirty();
	if (!resolverSuspended)
		ResolveAndApply(true);
}

bool SceneSettingsManager::HasActiveSettingsForFeature(const std::string& featureShortName) const
{
	return appliedFeatureNames.contains(featureShortName);
}

bool SceneSettingsManager::HasAnySceneEntriesForFeature(const std::string& featureShortName) const
{
	if (configuredFeatureNamesRevision != entryPresentationRevision) {
		configuredFeatureNamesCache.clear();
		const auto collect = [&](const auto& sourceEntries) {
			for (const auto& entry : sourceEntries)
				configuredFeatureNamesCache.insert(entry.featureShortName);
		};
		for (const auto& [_, sourceEntries] : entries)
			collect(sourceEntries);
		for (const auto& [_, config] : weatherSceneConfigs)
			collect(config.entries);
		for (const auto& [_, config] : locationSceneConfigs)
			collect(config.entries);
		configuredFeatureNamesRevision = entryPresentationRevision;
	}
	return configuredFeatureNamesCache.contains(featureShortName);
}

bool SceneSettingsManager::IsActiveSceneSetting(std::string_view featureShortName,
	std::string_view settingPath, std::string_view settingKey) const
{
	return IsActiveSceneSetting(std::string(featureShortName), SplitCatalogPath(settingPath), std::string(settingKey));
}

bool SceneSettingsManager::IsActiveSceneSetting(const std::string& featureShortName,
	const std::vector<std::string>& settingPath, const std::string& settingKey) const
{
	return appliedSettings.contains({ featureShortName, settingPath, settingKey });
}

void SceneSettingsManager::CaptureExternalFeatureChanges(Feature* feature)
{
	if (!feature)
		return;
	if (appliedSettings.empty()) {
		InvalidateFeatureSnapshot(feature->GetShortName());
		return;
	}

	json featureSettings;
	if (!TrySaveFeatureSettings(*feature, "inspect external changes", featureSettings))
		return;

	std::vector<std::pair<SettingAddress, json>> changedSettings;
	const auto featureShortName = feature->GetShortName();
	InvalidateFeatureSnapshot(featureShortName);
	for (const auto& [address, appliedValue] : appliedSettings) {
		if (address.featureShortName != featureShortName)
			continue;
		auto* setting = FindAllowedCatalogSetting(
			address.featureShortName, address.settingPath, address.settingKey);
		if (!setting)
			continue;
		const auto* value = GetCatalogSerializedValue(featureSettings, *setting);
		// Narrowing is not a user edit: treating it as one would overwrite the captured baseline.
		if (!value || !IsSceneSettingPrimitive(*value) ||
			!IsCompatibleSceneSettingValue(appliedValue, *value) || AppliedValuesEqual(appliedValue, *value))
			continue;
		changedSettings.emplace_back(address, *value);
	}

	if (changedSettings.empty())
		return;
	for (const auto& [address, value] : changedSettings) {
		baselineSettings[address] = value;
		appliedSettings[address] = value;
	}
	resolverDirty = true;
	if (!resolverSuspended)
		ResolveAndApply(true);
}

SceneSettingsManager::SceneLayerGuard::SceneLayerGuard() :
	manager(GetSingleton())
{
	if (manager)
		manager->SuspendSceneLayer();
}

SceneSettingsManager::SceneLayerGuard::~SceneLayerGuard()
{
	// Resuming re-applies through arbitrary features, and a destructor is noexcept.
	try {
		if (manager)
			manager->ResumeSceneLayer();
	} catch (const std::exception& e) {
		logger::error("[SceneSettings] Failed to resume the scene layer: {}", e.what());
	} catch (...) {
		logger::error("[SceneSettings] Failed to resume the scene layer");
	}
}

bool SceneSettingsManager::IsFeatureSceneControlled(const std::string& featureShortName) const
{
	return HasActiveSettingsForFeature(featureShortName) && !IsFeaturePaused(featureShortName);
}

bool SceneSettingsManager::IsFeaturePaused(const std::string& featureShortName) const
{
	auto it = featurePauseStates.find(featureShortName);
	return it != featurePauseStates.end() && it->second;
}

void SceneSettingsManager::SetFeaturePaused(const std::string& featureShortName, bool paused)
{
	featurePauseStates[featureShortName] = paused;
	ReapplyIfActive();
}

void SceneSettingsManager::SuspendSceneLayer()
{
	if (++sceneLayerSuspendDepth > 1)
		return;

	resolverSuspended = true;
	RestoreAppliedSettings();
}

void SceneSettingsManager::ResumeSceneLayer()
{
	if (sceneLayerSuspendDepth <= 0) {
		logger::warn("[SceneSettings] ResumeSceneLayer called without a matching suspend");
		sceneLayerSuspendDepth = 0;
		return;
	}
	if (--sceneLayerSuspendDepth > 0)
		return;

	// The layer was off while suspended, so anything the caller read or wrote is the new base.
	InvalidateFeatureSnapshot();
	resolverSuspended = false;
	resolverDirty = true;
	ResolveAndApply(true);
}

void SceneSettingsManager::LoadAll()
{
	if (!dataLoaded) {
		dataLoaded = true;
		DiscoverOverwrites(SceneType::InteriorOnly);
		DiscoverOverwrites(SceneType::TimeOfDay);
		DiscoverPresetMetadata();
		LoadAllUserSettings();
		BumpEntryPresentationRevision();
		activeEntryCacheDirty = true;
		resolverDirty = true;
	}
	TryEnsureLocationDataLoaded();
}

void SceneSettingsManager::ReloadOverwrites()
{
	// Nothing has been discovered yet, so the first load will read the current files anyway.
	if (!dataLoaded)
		return;

	const auto dropOverwrites = [](std::vector<SettingEntry>& sourceEntries) {
		std::erase_if(sourceEntries,
			[](const SettingEntry& entry) { return entry.source == EntrySource::Overwrite; });
	};
	const auto dropSceneOverwrites = [&](PeriodicSceneConfig& config) {
		dropOverwrites(config.entries);
		config.overwriteTimeOfDayEnabled.reset();
	};
	for (auto& [type, sourceEntries] : entries)
		dropOverwrites(sourceEntries);
	for (auto& [weatherId, config] : weatherSceneConfigs)
		dropSceneOverwrites(config);
	for (auto& [configKey, config] : locationSceneConfigs)
		dropSceneOverwrites(config);

	DiscoverOverwrites(SceneType::InteriorOnly);
	DiscoverOverwrites(SceneType::TimeOfDay);
	DiscoverPresetMetadata();
	// Discovery for a layer that never loaded would run without its user settings, so it waits.
	if (weatherDataLoaded)
		DiscoverWeatherOverwrites();
	if (locationDataLoaded)
		DiscoverLocationOverwrites();
	RefreshTimeOfDayModes();

	BumpEntryPresentationRevision();
	ReapplyIfActive();
}

void SceneSettingsManager::OnDataLoaded()
{
	gameDataReady = true;
	if (dataLoaded)
		TryEnsureLocationDataLoaded();
}

bool SceneSettingsManager::TryEnsureLocationDataLoaded()
{
	if (locationDataLoaded)
		return true;
	if (!gameDataReady || !RE::TESDataHandler::GetSingleton())
		return false;
	if (!userSettingsDocumentLoaded)
		LoadAllUserSettings();

	try {
		DiscoverLocationOverwrites();
		if (userSettingsDocumentLoaded && userSettingsDocumentWritable && preservedUserSettingsRoot.is_object())
			LoadLocationUserSettings(preservedUserSettingsRoot);
		RefreshTimeOfDayModes();
		locationDataLoaded = true;
		locationTargetsCached = false;
		BumpEntryPresentationRevision();
		activeEntryCacheDirty = true;
		resolverDirty = true;
		return true;
	} catch (const std::exception& e) {
		logger::error("[SceneSettings] Failed to load location settings: {}", e.what());
		return false;
	}
}

bool SceneSettingsManager::TryEnsureWeatherDataLoaded()
{
	if (weatherDataLoaded)
		return true;
	if (!globals::game::sky || !RE::TESDataHandler::GetSingleton())
		return false;
	if (!userSettingsDocumentLoaded)
		LoadAllUserSettings();

	weatherDataLoaded = true;
	LoadWeatherData();
	BumpEntryPresentationRevision();
	activeEntryCacheDirty = true;
	resolverDirty = true;
	return true;
}

void SceneSettingsManager::LoadWeatherData()
{
	DiscoverWeatherOverwrites();
	LoadWeatherUserSettings();
	RefreshTimeOfDayModes();
}

float SceneSettingsManager::GetTimeOfDayPeriodFallbackFloat(float baseValue, const std::string& featureShortName,
	const std::vector<std::string>& settingPath, const std::string& settingKey, int periodIndex) const
{
	const json* value = nullptr;
	EntrySource source = EntrySource::Overwrite;
	const auto period = static_cast<TimeOfDayPeriod>(periodIndex);

	for (const auto& entry : GetEntries(SceneType::TimeOfDay)) {
		// A tombstone supplies nothing; skipping it here lets a weather capture fall through to base.
		if (!IsEntryActive(entry) || entry.period != period || entry.deleted ||
			!IsSameSetting(entry, featureShortName, settingPath, settingKey))
			continue;
		if (!value || (entry.source == EntrySource::User && source != EntrySource::User)) {
			value = &entry.value;
			source = entry.source;
		}
	}

	if (!value)
		return baseValue;
	if (!IsNumericValue(*value)) {
		logger::warn("[SceneSettings] Time of day fallback value for '{}' is not a float",
			GetSettingLogName(featureShortName, settingPath, settingKey));
		return baseValue;
	}

	const float result = value->get<float>();
	return std::isfinite(result) ? result : baseValue;
}
