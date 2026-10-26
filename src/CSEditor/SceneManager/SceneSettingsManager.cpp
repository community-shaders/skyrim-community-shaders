#include "SceneSettingsManager.h"

#include "Feature.h"
#include "Globals.h"
#include "Presets/UnifiedPresetCatalog.h"
#include "SceneSettingsCatalog.generated.h"
#include "SceneSettingsInternal.h"
#include "State.h"
#include "Utils/Climate.h"
#include "Utils/FileSystem.h"
#include "Utils/Format.h"
#include "Utils/Game.h"
#include "Utils/SettingsCatalog.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <functional>
#include <regex>

using namespace SceneSettingsInternal;

namespace
{

	SceneSettingsManager* sceneSettingsManagerSingleton = nullptr;

	/** @brief A scene directory inside a pack; empty without a pack so discovery finds nothing. */
	std::filesystem::path GetPackSceneDir(const std::filesystem::path& packRoot, std::string_view directoryName)
	{
		return packRoot.empty() ? std::filesystem::path{} : packRoot / directoryName;
	}
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
	const auto featureName = std::string(featureShortName);
	// A blend re-applies the same set every tick, and each read-back is a full SaveSettings.
	if (const auto verified = verifiedApplySignatures.find(featureName);
		verified != verifiedApplySignatures.end() && verified->second == signature)
		return;
	pendingApplyVerifications[featureName] = {
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

		auto& failures = verification.transition ? transitionApplyFailures : applyFailures;
		if (retained) {
			failures.erase(featureShortName);
			verifiedApplySignatures[featureShortName] = verification.signature;
			// A restore carries no clamp, and once it is confirmed its baseline has nothing left to retry.
			for (const auto& update : verification.updates) {
				const SettingAddress address{ featureShortName, update.settingPath, update.key };
				if (!update.clampToControlRange && !appliedSettings.contains(address))
					baselineSettings.erase(address);
			}
		} else {
			if (failures[featureShortName].Record(verification.signature, std::chrono::steady_clock::now()))
				logger::warn("[SceneSettings] {} did not retain settings after reporting a successful apply",
					featureShortName);
			featureApplyDocuments.erase(featureShortName);
			verifiedApplySignatures.erase(featureShortName);
			// Record what the feature reports instead of dropping the address: the scene layer still owes it
			// the baseline, and the mismatch against the resolved value drives the retry.
			bool keptAny = false;
			for (size_t index = 0; index < verification.updates.size(); ++index) {
				const auto& update = verification.updates[index];
				const SettingAddress address{ featureShortName, update.settingPath, update.key };
				const bool hasObserved = index < observed.size() && !observed[index].is_null() &&
				                         baselineSettings.contains(address);
				if (hasObserved)
					appliedSettings[address] = observed[index];
				else
					appliedSettings.erase(address);
				keptAny = keptAny || hasObserved;
			}
			// A rejected restore is applied again, so its feature is scene-driven once more.
			if (keptAny)
				appliedFeatureNames.insert(featureShortName);
			else
				PruneAppliedFeatureName(featureShortName);
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

std::filesystem::path SceneSettingsManager::GetActiveScenePackRoot()
{
	return UnifiedPresetCatalog::GetSingleton().GetActivePackRoot();
}

bool SceneSettingsManager::HasScenePayload(const std::filesystem::path& packRoot)
{
	std::error_code ec;
	const std::array sceneDirectories{ GetOverwritesPath(SceneType::InteriorOnly, packRoot),
		GetOverwritesPath(SceneType::TimeOfDay, packRoot), GetWeatherOverwritesDir(packRoot), GetLocationOverwritesDir(packRoot) };
	return std::ranges::any_of(sceneDirectories, [&](const auto& directory) { return std::filesystem::is_directory(directory, ec); });
}

std::filesystem::path SceneSettingsManager::GetOverwritesPath(SceneType type, const std::filesystem::path& packRoot)
{
	// Location overwrites are keyed by form under GetLocationOverwritesDir(), not by scene type name.
	assert(IsEntryListSceneType(type));
	return GetPackSceneDir(packRoot, GetSceneTypeName(type));
}

std::filesystem::path SceneSettingsManager::GetWeatherOverwritesDir(const std::filesystem::path& packRoot)
{
	return GetPackSceneDir(packRoot, "Weather");
}

std::filesystem::path SceneSettingsManager::GetLocationOverwritesDir(const std::filesystem::path& packRoot)
{
	return GetPackSceneDir(packRoot, "Locations");
}

bool SceneSettingsManager::IsReservedPresetName(std::string_view presetName)
{
	return Util::PathHelpers::IsHiddenLibraryEntry(presetName);
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
	/// Shift of each period's start in blend lengths, so blends start as the sky leaves night or day, finish as
	/// it settles into day or night, and centre on the colour peaks. Indexed by TimeOfDayPeriod.
	constexpr std::array<float, SceneSettingsManager::kPeriodCount> kBlendOffsets = { 0.5f, 0.0f, -0.5f, 0.5f, 0.0f, -0.5f };
}

float SceneSettingsManager::GetCurrentGameHour()
{
	// Prefer calendar (ground truth), which the Weather Editor slider writes to.
	// sky->currentGameHour may lag when timeScale is 0 (time paused).
	auto calendar = Util::Climate::GetCalendar();
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
	auto calendar = Util::Climate::GetCalendar();
	if (calendar && calendar->gameHour)
		calendar->gameHour->value = std::clamp(hour, 0.0f, 24.0f);
}

SceneSettingsManager::PeriodStartHours SceneSettingsManager::GetPeriodStartHours()
{
	const auto sky = globals::game::sky;
	if (!sky || !sky->currentClimate)
		return kFallbackPeriodStartHours;

	const auto day = Util::Climate::GetDayHours(*sky->currentClimate);
	const PeriodStartHours starts = { day.nightEnd, day.sunrisePeak, day.dayStart,
		day.dayEnd, day.sunsetPeak, day.nightStart };
	// Every period needs some length, Night included across midnight.
	const bool valid = std::ranges::adjacent_find(starts, std::greater_equal<>{}) == starts.end() &&
	                   starts.back() < starts.front() + 24.0f;
	return valid ? starts : kFallbackPeriodStartHours;
}

float SceneSettingsManager::GetPeriodEndHour(const PeriodStartHours& starts, int index)
{
	assert(index >= 0 && index < kPeriodCount);
	return index + 1 < kPeriodCount ? starts[index + 1] : starts.front() + 24.0f;
}

float SceneSettingsManager::GetPeriodMidHour(TimeOfDayPeriod period) const
{
	const int index = static_cast<int>(period);
	if (index < 0 || index >= kPeriodCount)
		return GetCurrentGameHour();

	// Blends are symmetric about the shifted starts, so this is also the middle of the full-weight hours.
	// Night ends past 24, so its middle lands after midnight.
	const auto starts = GetBlendedPeriods().starts;
	const float mid = (starts[index] + GetPeriodEndHour(starts, index)) * 0.5f;
	return mid >= 24.0f ? mid - 24.0f : mid;
}

SceneSettingsManager::PeriodLookup SceneSettingsManager::FindPeriodForHour(const PeriodStartHours& starts, float hour)
{
	for (int index = 0; index < kPeriodCount; ++index) {
		const float start = starts[index];
		const float end = GetPeriodEndHour(starts, index);
		// Night ends past 24, so pre-dawn hours have to be compared against hour + 24.
		const float periodHour = (end > 24.0f && hour < start) ? hour + 24.0f : hour;
		if (periodHour >= start && periodHour < end)
			return { index, periodHour };
	}
	return {};
}

SceneSettingsManager::BlendedPeriods SceneSettingsManager::GetBlendedPeriods() const
{
	const auto boundaries = GetPeriodStartHours();
	// A shifted period must still fit a whole blend; periods that grow with the blend never limit it.
	float blendHours = GetTransitionControl(TransitionControl::TimeOfDayHours);
	for (int index = 0; index < kPeriodCount; ++index) {
		const float blendLengthsNeeded = 1.0f + kBlendOffsets[index] - kBlendOffsets[(index + 1) % kPeriodCount];
		if (blendLengthsNeeded > 0.0f)
			blendHours = std::min(blendHours, (GetPeriodEndHour(boundaries, index) - boundaries[index]) / blendLengthsNeeded);
	}

	BlendedPeriods blended{ .blendHours = blendHours };
	for (int index = 0; index < kPeriodCount; ++index)
		blended.starts[index] = boundaries[index] + kBlendOffsets[index] * blendHours;
	return blended;
}

std::array<float, SceneSettingsManager::kPeriodCount> SceneSettingsManager::GetTimeOfDayFactors() const
{
	std::array<float, kPeriodCount> factors{};
	const auto blended = GetBlendedPeriods();
	const auto& [starts, blendHours] = blended;
	const auto lookup = FindBlendPeriod(blended);
	if (lookup.index < 0) {
		factors[static_cast<int>(TimeOfDayPeriod::Day)] = 1.0f;
		return factors;
	}

	// A zero-length blend always takes this branch, so the division below never sees it.
	const float hoursToEnd = GetPeriodEndHour(starts, lookup.index) - lookup.hour;
	if (hoursToEnd >= blendHours) {
		factors[lookup.index] = 1.0f;
		return factors;
	}

	// Inside the blend-out zone: cross-fade into the next period. The ease is symmetric, so either side can be eased.
	const float weight = EaseTransition(hoursToEnd / blendHours);
	factors[lookup.index] = weight;
	factors[(lookup.index + 1) % kPeriodCount] = 1.0f - weight;
	return factors;
}

void SceneSettingsManager::SetTransitionControl(TransitionControl control, std::optional<float> value, bool deferSave)
{
	const auto& spec = GetTransitionControlSpec(control);
	if (value) {
		if (!std::isfinite(*value))
			return;
		*value = std::clamp(*value, spec.min, spec.max);
	}
	auto& user = userTransitionControls[static_cast<size_t>(control)];
	if (value.has_value() == user.has_value() && (!value || std::abs(*user - *value) < kBlendEpsilon))
		return;
	// The location duration is saved inside the location section, which only exists once it has loaded.
	if (control == TransitionControl::LocationSeconds) {
		if (!TryEnsureLocationDataLoaded())
			return;
		locationTransitionModified = true;
		locationUserSettingsModified = true;
	}
	user = value;
	RefreshTransitionControls();
	ReapplyAndSaveOrDefer(deferSave);
}

void SceneSettingsManager::RefreshTransitionControls()
{
	for (size_t index = 0; index < kTransitionControlCount; ++index) {
		const auto preset = activePresetMetadata ? activePresetMetadata->transitionControls[index] : std::nullopt;
		transitionControls[index] = userTransitionControls[index].value_or(preset.value_or(kTransitionControlSpecs[index].defaultValue));
	}
}

float SceneSettingsManager::EaseTransition(float linear) const
{
	const float progress = std::clamp(linear, 0.0f, 1.0f);
	const float smoother = progress * progress * progress * (progress * (progress * 6.0f - 15.0f) + 10.0f);
	return std::lerp(progress, smoother, GetTransitionControl(TransitionControl::Ease));
}

SceneSettingsManager::PeriodLookup SceneSettingsManager::FindBlendPeriod(const BlendedPeriods& blended)
{
	// Blends are centred on the shifted starts.
	float hour = GetCurrentGameHour() - blended.blendHours * 0.5f;
	if (hour < 0.0f)
		hour += 24.0f;
	return FindPeriodForHour(blended.starts, hour);
}

SceneSettingsManager::TimeOfDayPeriod SceneSettingsManager::GetCurrentPeriod()
{
	// Read as the blend weights are, so mid-blend the period is the outgoing one and the next is incoming.
	const auto* manager = GetSingleton();
	const auto lookup = manager ? FindBlendPeriod(manager->GetBlendedPeriods()) :
	                              FindPeriodForHour(GetPeriodStartHours(), GetCurrentGameHour());
	return lookup.index < 0 ? TimeOfDayPeriod::Day : static_cast<TimeOfDayPeriod>(lookup.index);
}

// --- Feature Metadata ---

bool SceneSettingsManager::IsFeatureAllowedForType(SceneType type, const std::string& featureShortName)
{
	if (!Feature::FindFeatureByShortName(featureShortName))
		return false;

	const auto allowedByPolicy = [&] {
		switch (type) {
		case SceneType::InteriorOnly:
			return IsInteriorOnlyFeatureAllowed(featureShortName);
		case SceneType::TimeOfDay:
			return IsTimeOfDayFeatureAllowed(featureShortName);
		case SceneType::Location:
			return IsInteriorOnlyFeatureAllowed(featureShortName) || IsTimeOfDayFeatureAllowed(featureShortName);
		default:
			return false;
		}
	};
	return allowedByPolicy() && CatalogHasSceneSettings(featureShortName, type);
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

void SceneSettingsManager::MarkEntriesLoaded()
{
	BumpEntryPresentationRevision();
	activeEntryCacheDirty = true;
	resolverDirty = true;
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
	return !entry.paused;
}

bool SceneSettingsManager::HasDuplicateEntry(SceneType type, const std::string& featureShortName,
	const std::vector<std::string>& settingPath, const std::string& settingKey, EntrySource source, TimeOfDayPeriod period) const
{
	const bool matchPeriod = type == SceneType::TimeOfDay;
	const auto& typeEntries = GetEntries(type);
	return std::any_of(typeEntries.begin(), typeEntries.end(), [&](const auto& entry) {
		return entry.source == source && (!matchPeriod || entry.period == period) &&
		       IsSameSetting(entry, featureShortName, settingPath, settingKey);
	});
}

void SceneSettingsManager::RemoveSetting(SceneType type, size_t index)
{
	if (!IsEntryListSceneType(type))
		return;
	auto& vec = GetEntriesMut(type);
	if (index >= vec.size())
		return;

	const auto entry = vec[index];
	assert(entry.source == EntrySource::User && "a mod's entry is suppressed with a tombstone, never removed");

	logger::info("[SceneSettings] Removed {} entry: {}", GetSceneTypeName(type),
		GetSettingLogName(entry.featureShortName, entry.settingPath, entry.settingKey));

	vec.erase(vec.begin() + static_cast<ptrdiff_t>(index));
	BumpEntryPresentationRevision();
	MarkEntryListUserSettingsModified(type);
	SaveAllUserSettings();
	ReapplyIfActive();
}

void SceneSettingsManager::CommitSceneSettingChanges()
{
	SaveAllUserSettings();
	ReapplyIfActive();
}

void SceneSettingsManager::ReapplyAndSaveOrDefer(bool deferSave)
{
	if (deferSave)
		MarkDeferredSceneChanges();
	else
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

	CommitSceneSettingChanges();
}

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
	if (globals::state) {
		const auto frame = globals::state->frameCount;
		if (lastUpdateFrame == frame)
			return;
		lastUpdateFrame = frame;
	}
	// Nothing retained them since the last Update: the menu closed or left the feature.
	if (!std::exchange(sketchesRetained, false))
		DropSketches();
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
	const SettingAddress address{ featureShortName, settingPath, settingKey };
	const auto applied = appliedSettings.find(address);
	if (applied == appliedSettings.end())
		return false;
	const auto baseline = baselineSettings.find(address);
	return baseline == baselineSettings.end() || !AppliedValuesEqual(applied->second, baseline->second);
}

bool SceneSettingsManager::IsFeatureOverridingBase(const std::string& featureShortName) const
{
	return std::ranges::any_of(appliedSettings, [&](const auto& item) {
		return item.first.featureShortName == featureShortName &&
		       IsActiveSceneSetting(item.first.featureShortName, item.first.settingPath, item.first.settingKey);
	});
}

void SceneSettingsManager::CaptureExternalFeatureChanges(Feature* feature, bool sketch)
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
		if (sketch) {
			RecordBaselineEdit(address, value);
			continue;
		}
		baselineSettings[address] = value;
		appliedSettings[address] = value;
	}
	resolverDirty = true;
	if (!resolverSuspended)
		ResolveAndApply(true);
}

void SceneSettingsManager::RecordBaselineEdit(const SettingIdentity& setting, const json& value)
{
	// Baseline capture must see the edit.
	InvalidateFeatureSnapshot(setting.featureShortName);
	auto baselineIt = baselineSettings.find(setting);
	if (!appliedSettings.contains(setting) || baselineIt == baselineSettings.end())
		return;

	if (!sketchOriginals.empty() && !HasSketches(setting.featureShortName))
		DropSketches();
	assert((sketchOriginals.empty() || sketchOriginals.begin()->first.featureShortName == setting.featureShortName) &&
		   "sketches belong to one feature at a time");
	sketchOriginals.try_emplace(setting, baselineIt->second);
	sketchesRetained = true;
	baselineIt->second = value;
	appliedSettings[setting] = value;
	locationTransitionBatchesDirty = true;
}

bool SceneSettingsManager::IsSketched(const SettingIdentity& setting) const
{
	return sketchOriginals.contains(setting);
}

bool SceneSettingsManager::HasSketches(const std::string& featureShortName) const
{
	return !sketchOriginals.empty() && sketchOriginals.begin()->first.featureShortName == featureShortName;
}

void SceneSettingsManager::RetainSketches(const std::string& featureShortName)
{
	if (HasSketches(featureShortName))
		sketchesRetained = true;
}

void SceneSettingsManager::ReleaseSketches(const std::string& featureShortName)
{
	if (HasSketches(featureShortName))
		DropSketches();
}

void SceneSettingsManager::DropSketches()
{
	if (sketchOriginals.empty())
		return;
	// The sketched value stays as the feature's base; the scene simply resolves over it again.
	sketchOriginals.clear();
	resolverDirty = true;
	locationTransitionBatchesDirty = true;
}

void SceneSettingsManager::HoldSketchedValues(ResolvedSettingMap& resolved)
{
	// A scene that stops supplying an address leaves its sketch as the plain base.
	std::erase_if(sketchOriginals, [&](const auto& item) { return !resolved.contains(item.first); });
	for (const auto& [address, _] : sketchOriginals)
		if (auto baselineIt = baselineSettings.find(address); baselineIt != baselineSettings.end())
			resolved[address] = baselineIt->second;
}

std::optional<SceneSettingsManager::SceneContextId> SceneSettingsManager::FindWinningContext(
	const SettingIdentity& setting) const
{
	const auto period = GetCurrentPeriod();
	const auto nextPeriod = static_cast<TimeOfDayPeriod>((static_cast<int>(period) + 1) % kPeriodCount);
	const auto& weather = blendSnapshot.weather;
	const bool periodBlending = blendSnapshot.timeOfDayFactors[static_cast<size_t>(nextPeriod)] > 0.0f;
	const bool weatherBlending = weather.previousWeatherId != 0;

	// Mid-blend the incoming context is the one to author, unless it supplies nothing and the value
	// is fading out of the outgoing one.
	if (const auto incoming = FindSupplyingContext(setting, periodBlending ? nextPeriod : period, weather.currentWeatherId))
		return incoming;
	if (!periodBlending && !weatherBlending)
		return std::nullopt;
	return FindSupplyingContext(setting, period, weatherBlending ? weather.previousWeatherId : weather.currentWeatherId);
}

std::vector<SceneSettingsManager::WinningContext> SceneSettingsManager::GetWinningContexts(
	const std::string& featureShortName) const
{
	std::vector<WinningContext> winners;
	// Addresses sort by feature first, so this feature's applied values are one contiguous run.
	for (auto it = appliedSettings.lower_bound(SettingAddress{ .featureShortName = featureShortName });
		it != appliedSettings.end() && it->first.featureShortName == featureShortName; ++it) {
		const auto context = FindWinningContext(it->first);
		if (!context)
			continue;
		if (auto winner = std::ranges::find(winners, *context, &WinningContext::context); winner != winners.end())
			++winner->settings;
		else
			winners.push_back({ *context, 1 });
	}

	// Narrowest first, the resolve order read backwards.
	const auto rank = [](const WinningContext& winner) {
		switch (winner.context.type) {
		case SceneContextType::Location:
			return 0;
		case SceneContextType::Weather:
			return 1;
		default:
			return 2;
		}
	};
	std::ranges::stable_sort(winners, {}, rank);
	return winners;
}

std::optional<SceneSettingsManager::SceneContextId> SceneSettingsManager::FindSupplyingContext(
	const SettingIdentity& setting, TimeOfDayPeriod period, RE::FormID weatherId) const
{
	const auto withActiveSet = [&](SceneContextId context) {
		context.period = IsSceneTimeOfDayEnabled(context) ? period : TimeOfDayPeriod::Count;
		return context;
	};

	// Narrowest first, mirroring the resolve order in reverse.
	std::vector<SceneContextId> candidates;
	const auto& locationTargets = GetCurrentLocationTargets();
	for (auto target = locationTargets.rbegin(); target != locationTargets.rend(); ++target)
		candidates.push_back(withActiveSet(
			{ .type = SceneContextType::Location, .locationType = target->type, .locationFormKey = target->formKey }));
	if (Util::IsInterior()) {
		candidates.push_back({ .type = SceneContextType::Interior });
	} else {
		if (weatherId != 0)
			candidates.push_back(withActiveSet({ .type = SceneContextType::Weather, .weatherId = weatherId }));
		candidates.push_back({ .type = SceneContextType::TimeOfDay, .period = period });
	}

	for (const auto& context : candidates) {
		// A tombstone only empties its own context, so the search goes on beneath it.
		switch (GetSettingProvenance(context, setting.featureShortName, setting.settingPath, setting.settingKey).layer) {
		case SettingLayer::User:
		case SettingLayer::Overwrite:
			return context;
		default:
			break;
		}
	}
	return std::nullopt;
}

void SceneSettingsManager::CommitSketches(std::span<const SettingIdentity> settings)
{
	std::vector<std::pair<SceneContextId, const SettingIdentity*>> targets;
	for (const auto& setting : settings) {
		if (!IsSketched(setting))
			continue;
		const auto context = FindWinningContext(setting);
		if (!context)
			continue;
		// Created before any index is read: an insertion renumbers the entries behind it.
		if (!FindContextUserEntry(*context, setting.featureShortName, setting.settingPath, setting.settingKey) &&
			!AddContextSetting(*context, setting.featureShortName, setting.settingPath, setting.settingKey, true))
			continue;
		targets.emplace_back(*context, &setting);
	}
	if (targets.empty())
		return;

	std::map<SceneContextId, std::vector<EntryValueUpdate>> updatesByContext;
	for (const auto& [context, setting] : targets) {
		const auto index = FindContextUserEntry(context, setting->featureShortName, setting->settingPath, setting->settingKey);
		auto originalIt = sketchOriginals.find(*setting);
		auto baselineIt = baselineSettings.find(*setting);
		if (!index || originalIt == sketchOriginals.end() || baselineIt == baselineSettings.end())
			continue;
		updatesByContext[context].push_back({ *index, baselineIt->second });
		baselineIt->second = std::move(originalIt->second);
		sketchOriginals.erase(originalIt);
	}
	InvalidateFeatureSnapshot(targets.front().second->featureShortName);
	for (const auto& [context, updates] : updatesByContext)
		UpdateContextEntryValues(context, updates, true);
	CommitSceneSettingChanges();
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
	return HasActiveSettingsForFeature(featureShortName);
}

void SceneSettingsManager::SuspendSceneLayer()
{
	if (++sceneLayerSuspendDepth > 1)
		return;

	resolverSuspended = true;
	// The caller reads or writes the base, so a feature backing off its restore must still be tried now.
	restoreRetryAfter.clear();
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
		// Older builds kept one file per scene type; nothing reads them now.
		for (const auto type : { SceneType::InteriorOnly, SceneType::TimeOfDay }) {
			std::error_code ec;
			if (const auto legacy = Util::PathHelpers::GetSceneSettingsPath() / (GetSceneTypeName(type) + ".json");
				std::filesystem::exists(legacy, ec))
				logger::warn("[SceneSettings] {} is no longer read; its settings now live in {}",
					Util::PathHelpers::ToUtf8(legacy), Util::PathHelpers::ToUtf8(GetUserSettingsFilePath()));
		}
		DiscoverOverwrites(SceneType::InteriorOnly);
		DiscoverOverwrites(SceneType::TimeOfDay);
		DiscoverPresetMetadata();
		LoadAllUserSettings();
		MarkEntriesLoaded();
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
		MarkEntriesLoaded();
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
	MarkEntriesLoaded();
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
