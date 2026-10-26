#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using json = nlohmann::json;

#include "Feature.h"
#include "Globals.h"
#include "Presets/UnifiedPresetCatalog.h"
#include "Utils/Form.h"

/// Manages interior, time-of-day, weather, and location-specific setting overrides.
/// Applies catalog-backed settings in priority order and avoids redundant feature updates.
class SceneSettingsManager
{
public:
	/** @brief The process-wide instance. */
	static SceneSettingsManager* GetSingleton();

	/** @brief Scene layer an entry list or catalog whitelist belongs to. */
	enum class SceneType
	{
		InteriorOnly,
		TimeOfDay,
		Location
	};

	/** @brief Time-of-day period; Count doubles as the flat (non-periodic) marker. */
	enum class TimeOfDayPeriod
	{
		Dawn = 0,
		Sunrise,
		Day,
		Sunset,
		Dusk,
		Night,
		Count
	};

	/// Number of time-of-day periods (avoids repeated static_cast).
	static constexpr int kPeriodCount = static_cast<int>(TimeOfDayPeriod::Count);

	/// Display names for each period - must match TimeOfDayPeriod order.
	static constexpr std::array<const char*, kPeriodCount> kPeriodNames = {
		"Dawn", "Sunrise", "Day", "Sunset", "Dusk", "Night"
	};

	/// Every real period in order, excluding the Count sentinel.
	static constexpr std::array<TimeOfDayPeriod, kPeriodCount> kPeriods = {
		TimeOfDayPeriod::Dawn, TimeOfDayPeriod::Sunrise, TimeOfDayPeriod::Day,
		TimeOfDayPeriod::Sunset, TimeOfDayPeriod::Dusk, TimeOfDayPeriod::Night
	};

	/// Start hour of each period in period order; each runs to the next start and Night wraps past midnight.
	using PeriodStartHours = std::array<float, kPeriodCount>;

	/// Vanilla SkyrimClimate's starts, used without a climate or when its timing leaves a period empty.
	static constexpr PeriodStartHours kFallbackPeriodStartHours = { 5.0f, 7.5f, 10.0f, 16.0f, 18.5f, 21.0f };

	/// Default blend zone in hours.
	static constexpr float kDefaultTimeOfDayTransitionHours = 1.0f;
	/// Upper bound for the setting; the applied blend is also capped so every period fits it.
	static constexpr float kMaxTimeOfDayTransitionHours = 2.0f;
	/// Default duration used by location float transitions.
	static constexpr float kDefaultLocationTransitionSeconds = 5.0f;
	/// Largest accepted typed location transition duration.
	static constexpr float kMaxLocationTransitionSeconds = 300.0f;
	/// Weather transition length relative to each weather's own, applied through fWeatherTransMin/Max.
	static constexpr float kDefaultWeatherTransitionScale = 1.0f;
	static constexpr float kMinWeatherTransitionScale = 0.1f;
	static constexpr float kMaxWeatherTransitionScale = 10.0f;
	/// Weight of smootherstep over a linear blend; 0 is linear.
	static constexpr float kDefaultTransitionEase = 1.0f;

	/// Global transition controls a preset ships and the user can override.
	enum class TransitionControl
	{
		TimeOfDayHours,
		LocationSeconds,
		WeatherScale,
		Ease,
		Count
	};
	static constexpr size_t kTransitionControlCount = static_cast<size_t>(TransitionControl::Count);
	using TransitionControlValues = std::array<std::optional<float>, kTransitionControlCount>;

	/// Manifest key, accepted range and default of one transition control.
	struct TransitionControlSpec
	{
		const char* key;
		float min;
		float max;
		float defaultValue;
	};
	static constexpr std::array<TransitionControlSpec, kTransitionControlCount> kTransitionControlSpecs = { {
		{ "periodTransitionHours", 0.0f, kMaxTimeOfDayTransitionHours, kDefaultTimeOfDayTransitionHours },
		{ "locationTransitionSeconds", 0.0f, kMaxLocationTransitionSeconds, kDefaultLocationTransitionSeconds },
		{ "weatherTransitionScale", kMinWeatherTransitionScale, kMaxWeatherTransitionScale, kDefaultWeatherTransitionScale },
		{ "transitionEase", 0.0f, 1.0f, kDefaultTransitionEase },
	} };
	static constexpr const TransitionControlSpec& GetTransitionControlSpec(TransitionControl control)
	{
		return kTransitionControlSpecs[static_cast<size_t>(control)];
	}

	/// Listens for LoadingMenu close to detect cell transitions, deferring reset work until then.
	class MenuOpenCloseEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		virtual RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;

		/** @brief Subscribes once; false when the UI event source is unavailable. */
		static bool Register()
		{
			static bool registered = false;
			if (registered)
				return true;

			static MenuOpenCloseEventHandler singleton;
			auto ui = globals::game::ui;
			if (!ui) {
				logger::error("[SceneSettings] UI event source not found");
				return false;
			}
			auto eventSource = ui->GetEventSource<RE::MenuOpenCloseEvent>();
			if (!eventSource) {
				logger::error("[SceneSettings] MenuOpenCloseEvent source not found");
				return false;
			}
			eventSource->AddEventSink(&singleton);
			registered = true;
			logger::info("[SceneSettings] Registered MenuOpenCloseEventHandler");
			return true;
		}
	};

	/** @brief Who authored an entry. */
	enum class EntrySource
	{
		User,      // User-added via UI
		Overwrite  // Loaded from overwrite file
	};

	/// Layer fresh captures resolve beneath: pinning over a mod value keeps what would apply without
	/// the mod. Lower user layers still stack.
	static constexpr EntrySource kCaptureSourceLayer = EntrySource::Overwrite;

	/** @brief One scene override of a single feature setting, user-authored or from a mod's file. */
	struct SettingEntry
	{
		std::string featureShortName;              // Feature's GetShortName()
		std::vector<std::string> settingPath;      // Feature-owned subfeature/object path
		std::string settingKey;                    // Feature-owned scene setting key
		std::string displayName;                   // Cached UI label
		json value;                                // Override value (bool, float, int, etc.)
		json originalValue;                        // Value at time of creation, for revert
		json serializedTemplate = json::object();  // Preserved forward-compatible fields
		bool paused = false;                       // Temporarily disabled
		/// Removes this context's mod value at the address instead of supplying one, so the layers beneath
		/// show through. An explicit state, not an empty `value`: the resolve, copy and export paths all
		/// read `value` unguarded.
		bool deleted = false;
		EntrySource source = EntrySource::User;
		std::string sourceFilename;                       // For overwrites: the filename it came from
		std::filesystem::path sourcePath;                 // For overwrites: exact file path
		TimeOfDayPeriod period = TimeOfDayPeriod::Count;  // Which period this entry belongs to; Count for flat entries
		std::optional<float> transitionSeconds;           // Location float transition override
		// A transition this build cannot honor is dropped from the entry but kept in the template,
		// so a document authored by another implementation round-trips with its field intact.
		bool retainSerializedTransition = false;
	};

	/** @brief Removes the user's entries, leaving the overwrites. @return How many were removed. */
	static size_t EraseUserEntries(std::vector<SettingEntry>& entries)
	{
		return std::erase_if(entries, [](const SettingEntry& entry) { return entry.source == EntrySource::User; });
	}

	/// One indexed value in an atomic scene-setting update.
	struct EntryValueUpdate
	{
		size_t index;
		json value;
	};

	// --- Generic Entry Management (scene-type agnostic) ---

	/// Monotonic revision for entry structure and pause state used by presentation caches.
	std::uint64_t GetEntryPresentationRevision() const { return entryPresentationRevision; }

	/// A value derived from the entry lists by a walk too costly to repeat per frame.
	template <typename T>
	struct RevisionCache
	{
		/** @brief The cached value, rebuilt through build once the entries it reflects have moved on.
		 *  @param force Rebuilds regardless, for a caller whose value depends on more than the entries. */
		template <typename Build>
		const T& Get(std::uint64_t currentRevision, Build&& build, bool force = false)
		{
			if (force || !valid || revision != currentRevision) {
				value = build();
				revision = currentRevision;
				valid = true;
			}
			return value;
		}

		std::uint64_t revision = 0;
		bool valid = false;
		T value{};
	};

	/// Mods supplying overwrite entries anywhere, in discovery order, deduplicated. Loads weather and
	/// location data first if not already loaded; returns whatever it has if either fails to load.
	std::vector<std::string> GetOverwriteModNames();

	/// Every scene file the pack of this name currently owns, across every scene directory in Presets/<Name>/.
	static std::vector<std::filesystem::path> FindPresetFiles(const std::string& packId);

	using PresetType = UnifiedPresetCatalog::PresetType;

	/** @brief Manifest + artwork inputs for ExportPreset. The inherited artwork paths are the pack's existing ones;
	 *  sources are absolute paths to copy into Presets/<Name>/; empty source with clear* = drop that field on re-export. */
	struct PresetExportInfo : PackManifest
	{
		PresetExportInfo() { version = kDefaultVersion; }

		PresetType type = PresetType::CS;
		std::filesystem::path logoSource;
		std::filesystem::path coverSource;
		std::vector<std::filesystem::path> screenshotSources;
		bool clearLogo = false;
		bool clearCover = false;
		bool clearScreenshots = false;
		std::map<std::string, json> baselines;
		std::set<std::string> replaceBaselines;
		std::map<std::string, bool> disableAtBoot;
	};

	/** @brief Bakes every context's winning values into Presets/<Name>/, replacing its scene files and merging its manifest and artwork.
	 *  When type is E11, also saves and copies the active ENB layout into Presets/<Name>/effects11/.
	 *  @return Whether every file was written. */
	bool ExportPreset(const PresetExportInfo& info);

	/// A unified pack manifest as the Scene Manager reads it.
	struct PresetMetadata : PackManifest
	{
		std::filesystem::path path;
		TransitionControlValues transitionControls;  // Transition controls the preset ships, if any
	};

	/// Whether a preset version is a semantic MAJOR.MINOR.PATCH triple.
	static bool IsValidPresetVersion(std::string_view version);

	/// Manifest of the pack supplying the overwrite layer, if one is active.
	const std::optional<PresetMetadata>& GetActivePresetMetadata() const { return activePresetMetadata; }

	/** @brief Reads a pack manifest; the name falls back to the folder name. Nullopt when the pack has no manifest. */
	static std::optional<PresetMetadata> ReadPresetMetadata(const std::filesystem::path& packRoot);

	/// Whether a pack of this name would be hidden from preset scans.
	static bool IsReservedPresetName(std::string_view presetName);

	// --- Scene Application ---

	/// Called every frame from State::Draw().
	void Update();

	/// Called by MenuOpenCloseEventHandler once a loading screen closes.
	void OnLoadingTransition();

	/// Check if any scene settings are active for a given feature
	bool HasActiveSettingsForFeature(const std::string& featureShortName) const;
	/// Whether a feature has any entry authored anywhere, in effect here or not.
	bool HasAnySceneEntriesForFeature(const std::string& featureShortName) const;
	/** @brief Whether the scene layer currently applies a value at this address that differs from the base. */
	bool IsActiveSceneSetting(std::string_view featureShortName,
		std::string_view settingPath, std::string_view settingKey) const;
	bool IsActiveSceneSetting(const std::string& featureShortName,
		const std::vector<std::string>& settingPath, const std::string& settingKey) const;
	/** @brief Folds a feature's external edits at applied addresses into its baselines, then re-resolves. */
	void CaptureExternalFeatureChanges(Feature* feature, bool sketch = false);

	/// Whether the scene layer is currently driving this feature, so a settings UI without sketch
	/// support must not offer its base settings: the next resolve would revert the edit.
	bool IsFeatureSceneControlled(const std::string& featureShortName) const;
	/// Whether any applied value of the feature differs from its base, i.e. a layer is visibly overriding it.
	bool IsFeatureOverridingBase(const std::string& featureShortName) const;

	/// RAII suspend of the scene layer. Anything reading or writing a feature's *base* settings must
	/// hold one, otherwise it captures an overridden value as if it were the user's choice.
	class SceneLayerGuard
	{
	public:
		SceneLayerGuard();
		~SceneLayerGuard();

		SceneLayerGuard(const SceneLayerGuard&) = delete;
		SceneLayerGuard& operator=(const SceneLayerGuard&) = delete;

	private:
		SceneSettingsManager* manager;
	};

	// --- Persistence ---

	/// Commits a user edit: runs the CS editor's automatic save, which writes SceneManager.json.
	void SaveAllUserSettings();

	/// Writes all user data (interior, TOD, weather, location) to unified SceneManager.json, unless it
	/// matches what was last written. Only SaveManager calls this; everything else goes through
	/// SaveAllUserSettings.
	void WriteUserSettings();

	/** @brief Replaces one scene type's overwrite entries with those found in the active pack. */
	void DiscoverOverwrites(SceneType type);

	/// Re-reads every overwrite file from disk, replacing the mod layer. User entries are untouched.
	/// Picks up files an export just wrote, and drops entries whose file is gone.
	void ReloadOverwrites();

	/// Discover weather-specific overwrite files from Weather/{SPID}/ folders.
	void DiscoverWeatherOverwrites();

	/// Load non-weather scene types (overwrites + user settings). Called early from Setup().
	void LoadAll();

	// --- Path Resolution ---

	/** @brief Persisted name of a scene type, also its overwrite directory name. */
	static std::string GetSceneTypeName(SceneType type);
	/** @brief Path of the unified SceneManager.json user document. */
	static std::filesystem::path GetUserSettingsFilePath();

	/** @brief Folder of the unified pack whose scene files form the overwrite layer; empty when no pack is active. */
	static std::filesystem::path GetActiveScenePackRoot();

	/** @brief Whether a pack folder holds any scene directory. */
	static bool HasScenePayload(const std::filesystem::path& packRoot);

	/** @brief A pack's overwrite directory for an interior or time-of-day scene type. */
	static std::filesystem::path GetOverwritesPath(SceneType type, const std::filesystem::path& packRoot = GetActiveScenePackRoot());

	// --- Time of Day Helpers (public for UI) ---

	/** @brief Persisted name of a period, also its overwrite subdirectory. */
	static const char* GetPeriodName(TimeOfDayPeriod period);
	/** @brief Inverse of GetPeriodName; Count when the name matches no period. */
	static TimeOfDayPeriod GetPeriodFromName(const std::string& name);
	/** @brief Game hour in [0, 24), read from the calendar first since the sky lags while time is paused. */
	static float GetCurrentGameHour();

	/// Writes the game hour every reader, including GetCurrentGameHour, resolves against.
	static void SetGameHour(float hour);

	/** @brief Period starts from the active climate's sky colour timing, see Util::Climate::DayHours. */
	static PeriodStartHours GetPeriodStartHours();

	/// Middle of a period's full-weight hours, wrapped into [0, 24): Night runs past midnight.
	float GetPeriodMidHour(TimeOfDayPeriod period) const;

	/// Per-period blend weights for the current game hour. Weights sum to 1.
	std::array<float, kPeriodCount> GetTimeOfDayFactors() const;

	/** @brief The control's value: the user's, else the active preset's, else the default. */
	float GetTransitionControl(TransitionControl control) const { return transitionControls[static_cast<size_t>(control)]; }
	/** @brief The user's own value, or nullopt while the preset or default supplies it. */
	std::optional<float> GetUserTransitionControl(TransitionControl control) const
	{
		return userTransitionControls[static_cast<size_t>(control)];
	}
	/** @brief Set and persist the user's value, clamped to the control's range; nullopt hands it back to the preset. */
	void SetTransitionControl(TransitionControl control, std::optional<float> value, bool deferSave = false);
	/** @brief Linear progress in 0..1 eased toward smootherstep by the Ease control. */
	float EaseTransition(float linear) const;

	/// Returns the period the time-of-day blend is in: mid-blend, the outgoing one.
	static TimeOfDayPeriod GetCurrentPeriod();

	// --- Feature Metadata ---

	/// Get loaded feature short names with transitionable settings.
	static std::vector<std::string> GetExteriorRelevantFeatureNames();
	/// Superset of the exterior set: the location layer also accepts interior-only settings.
	static std::vector<std::string> GetLocationRelevantFeatureNames();

	/// Check whether the feature exposes settings supported by the scene type.
	static bool IsFeatureAllowedForType(SceneType type, const std::string& featureShortName);

	/// Check whether a single catalog setting is scene-controllable for the scene type.
	static bool IsSettingAllowedForType(SceneType type, const std::string& featureShortName,
		const std::vector<std::string>& settingPath, const std::string& settingKey,
		bool requireTransitionable = false);

	/// Check the shared catalog and settings blacklist policy.
	static bool IsSceneSettingAllowed(
		std::string_view featureShortName, std::string_view settingPath, std::string_view settingKey);

	/// Get the localized display name for a feature.
	static std::string GetFeatureDisplayName(const std::string& featureShortName);

	/// Logical Scene Manager editor represented by one or more persisted values.
	enum class SettingControlType : std::uint8_t
	{
		Scalar,
		Numeric,
		Color,
	};

	/// Logical-control metadata for one stored scene-setting entry.
	struct SettingControlInfo
	{
		std::vector<std::string> settingPath;
		std::string settingKey;
		std::string displayName;
		std::string componentDisplayName;
		std::vector<std::string> displayPath;
		SettingControlType controlType = SettingControlType::Scalar;
		std::int8_t componentIndex = -1;
		std::int8_t componentStart = -1;
		std::uint8_t componentCount = 0;
		bool aggregateAll = false;
	};

	/// Get the logical ImGui control represented by a scalar scene setting.
	static bool GetSettingControlInfo(const SettingEntry& entry, SettingControlInfo& info);

	/// Get a UI-friendly display label for a setting key.
	static std::string GetSettingDisplayName(const std::string& settingKey);

	// --- Per-Weather Scene Settings ---

	/// Entries split into two saved sets: flat (period Count) and per-period. The time-of-day mode
	/// selects which set resolves; the other is kept so toggling back restores it.
	struct PeriodicSceneConfig
	{
		std::vector<SettingEntry> entries;
		bool timeOfDayEnabled = false;
		std::optional<bool> userTimeOfDayEnabled;
		std::optional<bool> overwriteTimeOfDayEnabled;

		/** @brief The user's choice, else a shipped preset's, else per-period when every entry is. */
		void RefreshTimeOfDayMode()
		{
			timeOfDayEnabled = userTimeOfDayEnabled.value_or(overwriteTimeOfDayEnabled.value_or(
				!entries.empty() && std::ranges::all_of(entries, [](const SettingEntry& entry) {
					return entry.period != TimeOfDayPeriod::Count;
				})));
		}

		/** @brief Whether entries stored under this period belong to the active set. */
		bool IsPeriodActive(TimeOfDayPeriod period) const
		{
			return timeOfDayEnabled ? period != TimeOfDayPeriod::Count : period == TimeOfDayPeriod::Count;
		}

		/** @brief Drops the user's entries and time-of-day choice, before the user document is read again. */
		void ClearUserLayer()
		{
			EraseUserEntries(entries);
			userTimeOfDayEnabled.reset();
		}
	};

	using WeatherSceneConfig = PeriodicSceneConfig;

	/** @brief Whether a weather holds any numeric entry. */
	bool HasWeatherConfig(RE::FormID weatherId);

	/** @brief A pack's weather directory, holding one folder per weather SPID. */
	static std::filesystem::path GetWeatherOverwritesDir(const std::filesystem::path& packRoot = GetActiveScenePackRoot());

	// --- Per-Location Scene Settings ---

	/// Broadest to narrowest, in chain order. A location type (a LocType keyword) covers every location
	/// that carries it; a region bounds its locations to one stretch of a worldspace.
	enum class LocationTargetType
	{
		Worldspace,
		LocationType,
		Region,
		Location,
		Cell
	};
	static constexpr std::array kLocationTargetTypes{ LocationTargetType::Worldspace,
		LocationTargetType::LocationType, LocationTargetType::Region, LocationTargetType::Location,
		LocationTargetType::Cell };

	/// Persisted "type" discriminator, also used as the overwrite metadata targetType.
	static const char* GetLocationTargetTypeName(LocationTargetType type);

	/** @brief One link of a location chain, identified by type and form key. */
	struct LocationTarget
	{
		LocationTargetType type = LocationTargetType::Location;
		std::string formKey;
		std::string name;
		/// Several links of a chain can share a full name (Winterhold the hold and the town), so the
		/// editor ID is what tells them apart.
		std::string editorId;
		std::string cocCode;
		RE::FormID formId = 0;
	};

	/** @brief A location target's entries plus the identity needed to persist and display it. */
	struct LocationSceneConfig : PeriodicSceneConfig
	{
		LocationTargetType type = LocationTargetType::Location;
		std::string formKey;
		std::string name;
		/// Captured when the target is taken on, so same-named targets stay distinguishable in the
		/// editor even when the plugin that defines them is not installed.
		std::string editorId;
		std::string cocCode;
		/// The user put this target on their list. Keeps a target that has no settings yet persistable,
		/// and separates it from a config that only exists because a mod shipped an overwrite for it.
		bool userAuthored = false;
	};

	/// The player's location chain, outermost first. Cached until the location or cell FormID moves.
	const std::vector<LocationTarget>& GetCurrentLocationTargets() const;

	/// Targets the user has taken on, for the editor's location list.
	std::vector<LocationTarget> GetAuthoredLocationTargets() const;

	/** @brief Every target the game defines, for the editor's picker. Built once: forms are fixed after data load. */
	const std::vector<LocationTarget>& GetLocationCatalog() const;

	/// Put a target on the user's list so it can be authored before it has any settings.
	bool AddLocationTarget(const LocationTarget& target);
	/** @brief Whether the target is on the user's list. */
	bool IsLocationTargetAuthored(LocationTargetType type, std::string_view formKey) const;

	/// Drop a target from the user's list, discarding the settings they authored for it.
	void RemoveLocationTarget(LocationTargetType type, const std::string& formKey);

	/// Every target kind shares one directory; each target's type comes from its form, not its path.
	static std::filesystem::path GetLocationOverwritesDir(const std::filesystem::path& packRoot = GetActiveScenePackRoot());

	/// Return an entry-specific location transition duration, or null for the global duration.
	std::optional<float> GetLocationEntryTransitionSeconds(
		LocationTargetType type, std::string_view formKey, size_t index) const;
	/// Set one or more location entries to the same transition duration as one atomic edit.
	void SetLocationEntryTransitionSeconds(LocationTargetType type, const std::string& formKey,
		std::span<const size_t> indices, std::optional<float> seconds, bool deferSave = false);

	// --- Generic Scene Copy ---

	/// Identifies one physical persisted setting.
	struct SettingIdentity
	{
		std::string featureShortName;
		std::vector<std::string> settingPath;
		std::string settingKey;

		auto operator<=>(const SettingIdentity&) const = default;
	};

	/// Kind of scene context participating in a copy or authoring operation.
	enum class SceneContextType : std::uint8_t
	{
		Interior,
		TimeOfDay,
		Weather,
		Location,
	};

	/// Stable identity for a time period, weather period, or location target.
	struct SceneContextId
	{
		SceneContextType type = SceneContextType::TimeOfDay;
		TimeOfDayPeriod period = TimeOfDayPeriod::Count;
		RE::FormID weatherId = 0;
		LocationTargetType locationType = LocationTargetType::Location;
		std::string locationFormKey;

		auto operator<=>(const SceneContextId&) const = default;
	};

	/// Whether a context can store entries per time-of-day period. Interior cannot.
	static bool IsPeriodicContext(SceneContextType type);

	/** @brief Whether a weather or location resolves its per-period set rather than its flat one.
	 *  Always true for TimeOfDay and false for Interior. */
	bool IsSceneTimeOfDayEnabled(const SceneContextId& context) const;
	/** @brief Switches which saved set a weather or location resolves, persisting the choice. */
	void SetSceneTimeOfDayEnabled(const SceneContextId& context, bool enabled);

	/// How an existing destination user setting is handled. Cancel has no caller yet: the conflict
	/// modal only offers skip and overwrite.
	enum class CopyConflictPolicy : std::uint8_t
	{
		SkipExisting,
		OverwriteExisting,
		Cancel,
	};

	/// Why a candidate cannot be copied, so a preview can explain itself. None when it can.
	enum class CopyRejection : std::uint8_t
	{
		None,
		NotInCatalog,            ///< No catalog entry the destination layer permits.
		NotBlendable,            ///< Exists, but cannot blend, so a time-of-day or weather layer cannot hold it.
		NotAllowedInLayer,       ///< Feature not loaded, or the destination layer forbids the setting.
		ValueRejected,           ///< The source value is not a legal value at the destination.
		BlockedByOverwrite,      ///< An unpaused mod-authored overwrite holds the destination address.
		GroupCompanionRejected,  ///< This row is fine; a sibling in the same control is not.
	};

	/// One source context with settings compatible with a destination.
	struct CopySource
	{
		SceneContextId context;
		std::string displayName;
		size_t settingCount = 0;
		bool authored = false;  ///< Holds entries, or is on the user's location list.
		bool current = false;   ///< The live weather, or a link of the player's location chain.
	};

	/// One physical setting available to copy.
	struct CopyCandidate
	{
		SettingIdentity setting;
		std::string displayName;
		json value;
		/// The user value this would replace, so a preview can render the transition.
		std::optional<json> destinationValue;
		CopyRejection rejection = CopyRejection::None;
		bool compatible = false;
		bool conflicts = false;
	};

	/// Aggregate result of one transactional copy.
	struct CopyResult
	{
		size_t copied = 0;
		size_t skipped = 0;
		size_t overwritten = 0;
		size_t incompatible = 0;
		bool hadConflicts = false;
		bool cancelled = false;

		/// Return whether the operation changed the destination.
		bool Changed() const { return copied != 0 || overwritten != 0; }
	};

	/// Return non-empty contexts that contain compatible data for a destination.
	std::vector<CopySource> GetCopySources(const SceneContextId& destination) const;
	/// Return every context a source can copy compatible data into, including pages with no
	/// settings yet: every weather and known location, not just the ones already authored.
	std::vector<CopySource> GetCopyDestinations(const SceneContextId& source) const;
	/// Inspect the settings and conflicts in a proposed copy without mutating state.
	std::vector<CopyCandidate> GetCopyCandidates(const SceneContextId& source,
		const SceneContextId& destination) const;
	/// Copy settings as one validated mutation and one save/reapply operation.
	CopyResult CopySettings(const SceneContextId& source, const SceneContextId& destination,
		CopyConflictPolicy conflictPolicy);
	/// Copy one source into several destinations as one mutation: one revision bump, one save, one reapply.
	CopyResult CopySettingsBatch(const SceneContextId& source, std::span<const SceneContextId> destinations,
		CopyConflictPolicy conflictPolicy);
	/// Name one context the way the copy source list spells it.
	std::string GetSceneContextDisplayName(const SceneContextId& context) const;

	// --- Context-Keyed Entry Access (Scene Manager authoring UI) ---

	/// Split a catalog setting path into the segment vector every entry API takes.
	static std::vector<std::string> SplitSettingPath(std::string_view catalogPath);

	/// Index of the user entry for one setting in one context, or nullopt when none exists.
	std::optional<size_t> FindContextUserEntry(const SceneContextId& context,
		const std::string& featureShortName, const std::vector<std::string>& settingPath,
		const std::string& settingKey) const;

	/// Add a user entry capturing the feature's current base value. Returns its index.
	std::optional<size_t> AddContextSetting(const SceneContextId& context,
		const std::string& featureShortName, const std::vector<std::string>& settingPath,
		const std::string& settingKey, bool deferSave = false);

	/// Validate and update a group of context entries before applying any of them.
	void UpdateContextEntryValues(const SceneContextId& context,
		std::span<const EntryValueUpdate> updates, bool deferSave = false);
	/// Keeps a deferred save out of reach for another delay, so a drag held still does not write the
	/// file mid-gesture. Nothing pending means nothing to hold.
	void HoldDeferredSceneChanges();
	/// Remove one entry from a context.
	void RemoveContextSetting(const SceneContextId& context, size_t index);

	/** @brief Removes the mod's value at an address in one context by persisting a user tombstone; the
	 *  layers beneath show through. Never touches a mod's file; an existing user entry becomes the tombstone.
	 *  @return Whether a tombstone now covers the address. */
	bool TombstoneContextSetting(const SceneContextId& context, const std::string& featureShortName,
		const std::vector<std::string>& settingPath, const std::string& settingKey);

	/// Clears a tombstone at an address, restoring whatever the lower layers supply.
	void ClearContextTombstone(const SceneContextId& context, const std::string& featureShortName,
		const std::vector<std::string>& settingPath, const std::string& settingKey);
	/// Toggle the paused state of one context entry.
	void TogglePauseContextEntry(const SceneContextId& context, size_t index);
	/// Revert one context entry's value to its originalValue.
	void RevertContextEntryToDefault(const SceneContextId& context, size_t index);

	/// Entries stored for one context, unfiltered by period. Empty when the context holds none.
	std::span<const SettingEntry> GetContextEntries(const SceneContextId& context) const;

	/** @brief The feature's own value at an address the scene layer is applied over, or null when the
	 *  layer leaves it alone and the live member already holds it. */
	const json* FindAppliedBaseline(const SettingIdentity& setting) const;

	/// Which layer supplies the winning value at an address in a context.
	enum class SettingLayer : std::uint8_t
	{
		None,       // nothing supplies it: the feature's base applies
		Overwrite,  // a discovered overwrite file wins
		User,       // a user entry wins
		Deleted,    // a user tombstone removes this context's mod value
	};

	/** @brief Where the winning value at an address comes from. */
	struct SettingProvenance
	{
		SettingLayer layer = SettingLayer::None;
	};

	/** @brief Names the layer driving one address, for the gutter's colour and the export modal. */
	SettingProvenance GetSettingProvenance(const SceneContextId& context,
		const std::string& featureShortName, const std::vector<std::string>& settingPath,
		const std::string& settingKey) const;

	// --- Baseline sketches (main menu) ---

	/** @brief Records a main-menu edit of a feature's base value. Where a scene applies, the edit
	 *  becomes a sketch the resolver holds in place until the sketches are dropped. Another
	 *  feature's sketches are dropped first. */
	void RecordBaselineEdit(const SettingIdentity& setting, const json& value);
	/** @brief Whether a main-menu edit is currently held as a sketch at this address. */
	bool IsSketched(const SettingIdentity& setting) const;
	/** @brief Whether the held sketches belong to this feature. */
	bool HasSketches(const std::string& featureShortName) const;
	/// Keeps this feature's sketches alive through the next Update; without a retain from a drawn body they drop.
	void RetainSketches(const std::string& featureShortName);
	/** @brief Drops this feature's sketches now, leaving their values as its base and the scene resolving over them. */
	void ReleaseSketches(const std::string& featureShortName);

	/** @brief The context supplying an address's winning value: narrowest location link, else weather, else
	 *  period or interior. Mid-blend the incoming side wins when it supplies one; null when no scene does. */
	std::optional<SceneContextId> FindWinningContext(const SettingIdentity& setting) const;

	/// A scene context supplying some of a feature's applied values, and how many of them.
	struct WinningContext
	{
		SceneContextId context;
		size_t settings = 0;
	};

	/** @brief The contexts supplying a feature's applied values right now, narrowest first, so the base
	 *  editor can name the layers overriding it. Empty while no scene applies to the feature. */
	std::vector<WinningContext> GetWinningContexts(const std::string& featureShortName) const;

	/** @brief Writes sketched values into the context winning each address and restores the base
	 *  each held before it was sketched. */
	void CommitSketches(std::span<const SettingIdentity> settings);

	/// Readback comparison, tolerant of a feature storing a double-valued setting in a float.
	static bool AppliedValuesEqual(const json& lhs, const json& rhs);

	/// Mod name an overwrite entry came from: the filename stem up to the last underscore.
	static std::string GetOverwriteModName(const SettingEntry& entry);

	/// What a context's user entries amount to, for the page-wide actions that act on all of them.
	struct ContextEntrySummary
	{
		size_t total = 0;
		size_t paused = 0;

		/** @brief Whether every entry is paused; false for an empty context. */
		bool AllPaused() const { return total != 0 && paused == total; }
	};

	/// Count the user entries belonging to one context, and how many of them are paused.
	ContextEntrySummary GetContextUserEntrySummary(const SceneContextId& context) const;
	/// Pause or resume every user entry in a context as one save.
	void SetContextEntriesPaused(const SceneContextId& context, bool paused);
	/// Remove every user entry in a context as one save. Mod-authored overwrites are left alone,
	/// as are raw entries this session could not resolve to a loaded feature.
	void ClearContextEntries(const SceneContextId& context);

	/// Whether any user entry exists in any context, tombstones included.
	bool HasAnyUserEntries() const;

	/** @brief Drops every user entry in every context, tombstones included.
	 *  Suppressed mod values come back. Unresolved raw entries are left in the document. */
	void ClearAllUserEntries();

	/// Enables location discovery once Skyrim form data is guaranteed to be available.
	void OnDataLoaded();

	/// Current and outgoing weather with the sky's blend factor. Ids are 0 when no weather is active.
	struct WeatherBlend
	{
		RE::FormID currentWeatherId = 0;
		RE::FormID previousWeatherId = 0;
		float lerp = 0.0f;
	};

	// --- Debug Inspection ---

	/// One stored entry, flattened for the debug UI.
	struct DebugEntry
	{
		std::string feature;
		std::string path;
		std::string key;
		std::string value;
		std::string period;                      // Empty when the entry is not per-period
		std::optional<float> transitionSeconds;  // Only set when the entry overrides the global duration
		bool overwrite = false;
		bool paused = false;
		bool active = false;
		bool resolvable = false;
	};

	/// A group of entries the resolver treats as one layer (scene type, weather, or location).
	struct DebugLayer
	{
		std::string name;
		std::string detail;
		bool matchesCurrentScene = false;
		std::vector<DebugEntry> entries;
	};

	/// One address the resolver currently drives, with the inputs that produced its value.
	struct DebugResolvedSetting
	{
		std::string feature;
		std::string path;
		std::string key;
		std::string baseline;
		std::string applied;
		std::array<std::optional<float>, kPeriodCount> timeOfDayValues{};
		std::array<std::optional<float>, kPeriodCount> currentWeatherValues{};
		std::array<std::optional<float>, kPeriodCount> previousWeatherValues{};
	};

	/// One location float transition mid-flight, sampled for the debug UI.
	struct DebugLocationTransition
	{
		std::string feature;
		std::string path;
		std::string key;
		float startValue = 0.0f;
		float targetValue = 0.0f;
		float currentValue = 0.0f;
		float progress = 0.0f;
		float duration = 0.0f;
		bool restoreAtEnd = false;
	};

	/// Everything the resolver has in flight, sampled for the debug UI.
	struct DebugSnapshot
	{
		// Live scene context
		bool playerReady = false;
		bool menuOpen = false;
		bool interior = false;
		RE::FormID cellId = 0;
		std::string cellName;
		std::string cellEditorId;
		RE::FormID locationId = 0;
		std::string locationName;
		std::vector<LocationTarget> locationTargets;
		float gameHour = 0.0f;
		TimeOfDayPeriod period = TimeOfDayPeriod::Count;
		std::array<float, kPeriodCount> timeOfDayFactors{};
		WeatherBlend weather;
		std::string currentWeatherName;
		std::string previousWeatherName;

		// Load and resolver state
		bool dataLoaded = false;
		bool weatherDataLoaded = false;
		bool locationDataLoaded = false;
		bool gameDataReady = false;
		bool resolverSuspended = false;
		bool resolverDirty = false;
		bool activeEntryCacheDirty = false;
		bool hasActiveSceneEntries = false;
		bool deferredSceneChangesPending = false;
		int sceneLayerSuspendDepth = 0;

		// Inputs of the most recent resolve, and the factors it blended with
		bool lastInterior = false;
		RE::FormID lastCellId = 0;
		RE::FormID lastLocationId = 0;
		float lastHour = -1.0f;
		WeatherBlend lastWeather;
		std::array<float, kPeriodCount> blendFactors{};

		// Location float transitions
		float transitionTime = 0.0f;  // Pause-aware clock the transitions ease against
		float lastTransitionTick = -1.0f;
		float globalTransitionSeconds = 0.0f;
		bool transitionBatchesDirty = false;
		size_t transitionBatchCount = 0;
		std::vector<DebugLocationTransition> locationTransitions;
		std::vector<std::string> transitionApplyFailures;

		std::vector<DebugLayer> sceneLayers;
		std::vector<DebugLayer> weatherLayers;
		std::vector<DebugLayer> locationLayers;
		std::vector<DebugResolvedSetting> resolvedSettings;
		std::vector<std::string> applyFailures;
		std::vector<std::string> restoreFailures;
	};

	/// Sample the full resolver state. Built on demand, only for the debug UI.
	DebugSnapshot GetDebugSnapshot() const;

	/// The user's scene settings at one moment, for undo. Two snapshots are equal when they would save
	/// the same SceneManager.json.
	struct UserSettingsSnapshot
	{
		std::map<SceneType, std::vector<SettingEntry>> entries;
		std::map<RE::FormID, WeatherSceneConfig> weatherSceneConfigs;
		std::map<std::string, LocationSceneConfig> locationSceneConfigs;
		TransitionControlValues userTransitionControls;
		json document;

		bool operator==(const UserSettingsSnapshot& other) const { return document == other.document; }
	};

	/** @brief Copies the user's scene settings, for undo. */
	UserSettingsSnapshot CaptureUserSettings();

	/** @brief Puts back scene settings CaptureUserSettings copied, then saves and reapplies them. */
	void RestoreUserSettings(const UserSettingsSnapshot& snapshot);

	/** @brief Whether an edit is waiting on the deferred save, or a failed save is being retried. */
	bool HasUnsavedChanges() const { return deferredSceneChangesPending; }

protected:
	SceneSettingsManager();
	~SceneSettingsManager();

private:
	SceneSettingsManager(const SceneSettingsManager&) = delete;
	SceneSettingsManager& operator=(const SceneSettingsManager&) = delete;

	// --- Per scene-type storage ---
	std::map<SceneType, std::vector<SettingEntry>> entries;
	std::map<SceneType, std::vector<json>> unresolvedUserEntries;
	std::uint64_t entryPresentationRevision = 0;
	json preservedUserSettingsRoot = json::object();
	bool userSettingsDocumentLoaded = false;
	bool userSettingsDocumentWritable = true;
	bool userSettingsWriteBlockedWarning = false;
	bool interiorUserSettingsModified = false;
	bool timeOfDayUserSettingsModified = false;
	bool weatherUserSettingsModified = false;
	bool locationUserSettingsModified = false;
	bool locationTransitionModified = false;
	std::optional<PresetMetadata> activePresetMetadata;
	TransitionControlValues userTransitionControls;
	std::array<float, kTransitionControlCount> transitionControls = GetDefaultTransitionControls();
	bool dataLoaded = false;
	bool deferredSceneChangesPending = false;
	std::chrono::steady_clock::time_point deferredSceneChangesDeadline{};
	int deferredSaveFailures = 0;
	static constexpr auto kDeferredSaveDelay = std::chrono::milliseconds(250);
	static constexpr auto kDeferredSaveRetryDelay = std::chrono::seconds(2);
	/// A permanently locked file (MO2 VFS, antivirus, read-only install) must not log every retry forever.
	static constexpr int kMaxDeferredSaveRetries = 5;

	std::atomic<bool> queuedLoadingTransition = false;

	/// Float epsilon - changes smaller than this skip the LoadSettings call.
	static constexpr float kBlendEpsilon = 1e-3f;

	/// Minimum game-hour delta before re-running the blend. At the default
	/// timescale (20x), this equals about 0.18 real seconds.
	static constexpr float kHourUpdateThreshold = 1e-3f;

	/// Each transition tick costs a LoadSettings per feature, and a smoothstep is indistinguishable at
	/// 30 Hz, so the tick is decoupled from the frame rate.
	static constexpr float kLocationTransitionTickInterval = 1.0f / 30.0f;

	int sceneLayerSuspendDepth = 0;

	// --- Per-Weather Scene storage ---
	std::map<RE::FormID, WeatherSceneConfig> weatherSceneConfigs;
	json unresolvedWeatherUserSettings = json::object();
	bool weatherDataLoaded = false;

	// --- Per-Location Scene storage ---
	std::map<std::string, LocationSceneConfig> locationSceneConfigs;
	static const LocationSceneConfig kEmptyLocationConfig;
	json unresolvedLocationUserSettings = json::object();
	bool locationDataLoaded = false;
	bool gameDataReady = false;

	using SettingAddress = SettingIdentity;
	/** @brief One value pushed into a feature through its catalog-backed settings document. */
	struct CatalogSceneSettingUpdate
	{
		std::vector<std::string> settingPath;
		std::string key;
		json value;
		/// A restore carries the feature's own baseline, which is not the scene layer's to bound.
		bool clampToControlRange = true;
	};

	using ResolvedSettingMap = std::map<SettingAddress, json>;
	ResolvedSettingMap baselineSettings;
	ResolvedSettingMap appliedSettings;
	/// Sketched addresses, each with the base it held before its first sketch. One feature at a time.
	ResolvedSettingMap sketchOriginals;
	bool sketchesRetained = false;
	/// Drops every sketch so the next resolve re-applies the scene over the sketched base.
	void DropSketches();
	/// Resolves each sketched address to its base, so the scene leaves the sketch in place.
	void HoldSketchedValues(ResolvedSettingMap& resolved);
	/// Reused across resolves so the per-frame path does not reallocate the map.
	ResolvedSettingMap resolvedSettingsScratch;
	std::set<std::string> restoreFailureWarnings;
	std::map<std::string, std::chrono::steady_clock::time_point> restoreRetryAfter;
	/** @brief Retry backoff for a feature whose last apply of a given update set failed. */
	struct ApplyFailureState
	{
		size_t signature = 0;
		std::chrono::steady_clock::time_point retryAfter{};
		bool warningLogged = false;

		/** @brief Backs off retries for this signature; true only on the first failure, when the caller should warn. */
		bool Record(size_t failedSignature, std::chrono::steady_clock::time_point now)
		{
			signature = failedSignature;
			retryAfter = now + kApplyRetryDelay;
			return !std::exchange(warningLogged, true);
		}
	};
	std::map<std::string, ApplyFailureState> applyFailures;
	std::map<std::string, ApplyFailureState> transitionApplyFailures;
	static constexpr auto kApplyRetryDelay = std::chrono::seconds(2);
	/// An apply a feature accepted, to be read back next frame. A feature that silently clamps or
	/// discards what it was handed reports success, so the scene layer would otherwise believe it.
	struct PendingApplyVerification
	{
		std::uint32_t appliedFrame = 0;
		std::vector<CatalogSceneSettingUpdate> updates;
		size_t signature = 0;
		bool transition = false;
	};
	std::map<std::string, PendingApplyVerification> pendingApplyVerifications;
	/// Signature each feature last kept, verified once: later values of that update set are trusted.
	std::map<std::string, size_t> verifiedApplySignatures;
	bool resolverDirty = true;
	bool resolverSuspended = false;
	bool activeEntryCacheDirty = true;
	bool hasActiveSceneEntries = false;
	std::uint32_t lastUpdateFrame = std::numeric_limits<std::uint32_t>::max();
	bool lastResolvedInterior = false;
	RE::FormID lastResolvedLocationId = 0;
	RE::FormID lastResolvedCellId = 0;
	RE::FormID lastResolvedWorldspaceId = 0;
	RE::FormID lastResolvedRegionId = 0;
	float lastResolvedHour = -1.0f;
	RE::FormID lastResolvedCurrentWeatherId = 0;
	RE::FormID lastResolvedPreviousWeatherId = 0;
	float lastResolvedWeatherLerp = -1.0f;
	/// Set whenever the committed context goes stale (a load, or no active entries), cleared only by
	/// a resolve that commits again. Until then a cell change cannot be read as the player walking.
	bool suppressLocationTransitionUntilContextResolved = false;
	mutable RE::FormID cachedPreviousWeatherId = 0;
	mutable RE::FormID cachedTargetLocationId = 0;
	mutable RE::FormID cachedTargetCellId = 0;
	mutable RE::FormID cachedTargetRegionId = 0;
	mutable bool locationTargetsCached = false;
	mutable std::vector<LocationTarget> cachedLocationTargets;
	mutable std::optional<std::vector<LocationTarget>> cachedLocationCatalog;

	/// One float easing from its pre-location value to the location override, or back.
	struct LocationTransition
	{
		float startValue = 0.0f;
		float targetValue = 0.0f;
		float startTime = 0.0f;
		float duration = 0.0f;
		bool restoreAtEnd = false;
	};
	/// A feature's in-flight transitions, pushed as one LoadSettings call per tick.
	struct LocationTransitionBatch
	{
		std::vector<SettingAddress> addresses;
		/// Points into activeLocationTransitions, which must stay node-based for these to survive
		/// the erases the batch loop performs while iterating.
		std::vector<LocationTransition*> transitions;
		std::vector<CatalogSceneSettingUpdate> updates;
		size_t signature = 0;
	};
	std::map<SettingAddress, LocationTransition> activeLocationTransitions;
	std::map<std::string, LocationTransitionBatch> locationTransitionBatches;
	bool locationTransitionBatchesDirty = true;
	float lastLocationTransitionTick = -1.0f;
	std::chrono::steady_clock::time_point lastBlendResolveTime{};
	ResolvedSettingMap lastLocationOverrideValues;
	std::map<SettingAddress, float> lastLocationTransitionDurations;
	std::map<SettingAddress, float> pendingLocationTransitionDurations;
	ResolvedSettingMap cachedLocationOverrides;
	bool cachedLocationOverridesValid = false;
	bool locationOverridesDirty = true;

	/// Feature settings as they were before the scene layer, so baselines cost one SaveSettings each.
	std::map<std::string, json> featureBaseSnapshots;
	/// The document each apply mutates in place, so a per-frame transition costs no SaveSettings
	/// and no full copy of the feature's settings.
	std::map<std::string, json> featureApplyDocuments;
	std::set<std::string> appliedFeatureNames;
	mutable std::set<std::string> configuredFeatureNamesCache;
	mutable std::uint64_t configuredFeatureNamesRevision = std::numeric_limits<std::uint64_t>::max();

	/// Period containing a game hour, with that hour normalized into the period's range.
	struct PeriodLookup
	{
		int index = -1;
		float hour = 0.0f;
	};
	/** @brief Period containing an hour; index -1 when none does. */
	static PeriodLookup FindPeriodForHour(const PeriodStartHours& starts, float hour);
	/** @brief End hour of a period: the next start, with Night's pushed past midnight. */
	static float GetPeriodEndHour(const PeriodStartHours& starts, int index);

	/// Period starts shifted so each blend sits inside the sky's transition, with blends centred on them.
	struct BlendedPeriods
	{
		PeriodStartHours starts{};
		float blendHours = 0.0f;
	};
	/** @brief Starts shifted by the applied blend length, which is capped so every shifted period fits it. */
	BlendedPeriods GetBlendedPeriods() const;
	/** @brief The shifted period the current hour is in, the one whose blend out the weights describe. */
	static PeriodLookup FindBlendPeriod(const BlendedPeriods& blended);

	/// FindWinningContext's search against one period and weather (0 for none).
	std::optional<SceneContextId> FindSupplyingContext(const SettingIdentity& setting, TimeOfDayPeriod period,
		RE::FormID weatherId) const;

	// --- Per-Weather helpers ---
	/// Load weather overwrites/user settings once game data is available for SPID resolution.
	bool TryEnsureWeatherDataLoaded();
	/** @brief Loads location overwrites and user settings once game data is ready; false until then. */
	bool TryEnsureLocationDataLoaded();
	/** @brief The SceneManager.json document the user's settings amount to, over the preserved one.
	 *  @param weatherLoaded, locationLoaded Whether those sections' data is loaded, and so can be written. */
	json BuildUserSettingsDocument(bool weatherLoaded, bool locationLoaded);
	/** @brief Discovers weather overwrites and loads the weather user settings. */
	void LoadWeatherData();
	/** @brief The weather's config, created empty when it has none. */
	WeatherSceneConfig& GetWeatherConfigMut(RE::FormID weatherId);
	/** @brief Removes one user entry. */
	void RemoveWeatherSetting(RE::FormID weatherId, size_t index);
	/** @brief Removes one user entry of a weather or location config.
	 *  @param prepareUserEdit Readies the config's raw user settings before the removal is saved. */
	void RemovePeriodicEntry(PeriodicSceneConfig& config, size_t index, const std::function<void()>& prepareUserEdit);
	/** @brief Whether the weather holds an entry for this setting and period, optionally of one source. */
	bool HasWeatherEntryForPeriod(RE::FormID weatherId, const std::string& featureShortName,
		const std::vector<std::string>& settingPath, const std::string& settingKey, TimeOfDayPeriod period,
		std::optional<EntrySource> source = std::nullopt);
	/** @brief The outgoing weather while blending, cached because the sky can drop it; 0 once the blend completes. */
	RE::FormID GetEffectivePreviousWeatherId(const RE::Sky* sky, float weatherLerp) const;
	/** @brief Samples the sky's current and outgoing weather and their blend factor. */
	WeatherBlend GetWeatherBlend() const;
	/** @brief The time-of-day layer's value for one period, user over overwrite, else the base value. */
	float GetTimeOfDayPeriodFallbackFloat(float baseValue, const std::string& featureShortName,
		const std::vector<std::string>& settingPath, const std::string& settingKey, int periodIndex) const;

	/// Live sky/time state sampled once per resolve so every setting blends against identical factors.
	struct BlendSnapshot
	{
		WeatherBlend weather;
		std::array<float, kPeriodCount> timeOfDayFactors{};
	};
	/// Resamples the blend inputs. Call once at the top of any pass that runs the resolvers.
	void RefreshBlendSnapshot(bool interior);
	BlendSnapshot blendSnapshot;

	/// Active per-period values for one address, indexed by period.
	using PeriodValues = std::array<std::optional<float>, kPeriodCount>;
	using PeriodSettingMap = std::map<SettingAddress, PeriodValues>;
	/** @brief Period values valid while revision matches sceneValueRevision. */
	struct CachedPeriodSettingMap
	{
		std::uint64_t revision = std::numeric_limits<std::uint64_t>::max();
		PeriodSettingMap values;
	};
	/// Bumped whenever an entry's value, pause state or membership changes.
	std::uint64_t sceneValueRevision = 0;
	mutable CachedPeriodSettingMap timeOfDayValueGroups;
	mutable std::map<RE::FormID, CachedPeriodSettingMap> weatherValueGroups;
	/// Per-period location values, rebuilt with cachedLocationOverrides but re-blended every resolve.
	PeriodSettingMap cachedLocationPeriodValues;
	/** @brief Groups one saved set's floats by address, dropping anything unresolvable.
	 *  @param timeOfDayEnabled Selects the per-period set; otherwise each flat entry fills every period.
	 *  @param type The scene type whose whitelist the entries are judged by. */
	void CollectPeriodValueGroups(const std::vector<SettingEntry>& sourceEntries, bool timeOfDayEnabled,
		SceneType type, PeriodSettingMap& values) const;
	/** @brief Time-of-day values grouped by address, cached per scene value revision. */
	const PeriodSettingMap& BuildTimeOfDayValueGroups() const;
	/** @brief One weather's values grouped by address, cached per scene value revision. */
	const PeriodSettingMap& BuildWeatherValueGroups(RE::FormID weatherId) const;

	// --- Central runtime resolver ---
	/** @brief Resolves the current scene and pushes it to the features.
	 *  @param allowLocationTransitions Cleared across a loading screen so the new values land before the player arrives. */
	void ResolveAndApply(bool force = false, bool allowLocationTransitions = true);
	/** @brief Whether any active set holds a resolvable entry, recomputed only after the entry lists change. */
	bool HasActiveSceneEntriesCached();
	/** @brief Layers every scene into the reused scratch map.
	 *  @param interior Passed down from the caller's resolve, which already sampled it. */
	ResolvedSettingMap& BuildResolvedSettings(bool collectLocationTransitionDurations, bool interior);
	/** @brief Pushes changed values to their features and restores addresses the scene no longer drives. */
	void ApplyResolvedSettings(const ResolvedSettingMap& resolved, bool forceRetry);
	/** @brief Returns every applied address to its baseline and drops in-flight transitions. */
	void RestoreAppliedSettings();
	/** @brief Overlays the interior layer. */
	void ResolveInteriorSettings(ResolvedSettingMap& resolved) const;
	/** @brief Blends the per-period time-of-day values at the snapshot's factors. */
	void ResolveTimeOfDaySettings(ResolvedSettingMap& resolved, const PeriodSettingMap& values) const;
	/** @brief Blends current and outgoing weather over the time-of-day values. */
	void ResolveWeatherSettings(ResolvedSettingMap& resolved, const PeriodSettingMap& timeOfDayValues) const;
	/** @brief Walks the chain into flat overrides and per-period values, narrowest link winning. */
	void ResolveLocationSettings(ResolvedSettingMap& resolved, PeriodSettingMap& periodValues,
		const std::vector<LocationTarget>& locationTargets, bool collectTransitionDurations);
	/** @brief Layers one chain link's active set over the links broader than it. */
	void ResolveLocationLink(const std::vector<SettingEntry>& linkEntries, bool timeOfDayEnabled,
		ResolvedSettingMap& resolved, PeriodSettingMap& periodValues,
		std::map<SettingAddress, float>* transitionDurations) const;
	/** @brief Blends per-period location values against whatever the lower layers resolved. */
	void BlendLocationPeriodValues(ResolvedSettingMap& resolved, const PeriodSettingMap& periodValues) const;
	/** @brief Records the entry's transition duration, or the global one, for its address. */
	void RecordLocationTransitionDuration(const SettingEntry& entry, const SettingAddress& address,
		std::map<SettingAddress, float>& transitionDurations) const;
	/** @brief Writes one source's resolvable entries over resolved; a tombstone erases its address, so the
	 *  layers beneath show through. */
	void OverlayEntries(ResolvedSettingMap& resolved, const std::vector<SettingEntry>& sourceEntries,
		SceneType type, EntrySource source,
		std::map<SettingAddress, float>* transitionDurations = nullptr) const;
	/// Overlay both sources, shipped overwrites first so user entries win.
	void OverlayAllEntries(ResolvedSettingMap& resolved, const std::vector<SettingEntry>& sourceEntries,
		SceneType type, std::map<SettingAddress, float>* transitionDurations = nullptr) const;
	/** @brief The time-of-day value a weather entry stacks on; a flat entry uses the current period. */
	std::optional<float> ResolveWeatherLowerValue(RE::FormID weatherId, const SettingAddress& address,
		TimeOfDayPeriod period, EntrySource selectedSource);
	/** @brief The feature's own value at an address, null when the catalog does not allow it. */
	json GetBaselineValue(const SettingAddress& address);
	/// Feature settings with the live scene layer folded back out, cached until invalidated.
	const json* GetFeatureBaseSnapshot(const std::string& featureShortName);
	/** @brief Captures a baseline for each address that has none yet. */
	void EnsureBaselines(std::span<const SettingAddress> addresses);
	/** @brief Drops a feature's cached base snapshot; an empty name drops every feature's. */
	void InvalidateFeatureSnapshot(std::string_view featureShortName = {});
	/// Forget a feature once its last applied setting is gone, so it stops counting as scene-driven.
	void PruneAppliedFeatureName(const std::string& featureShortName);
	/** @brief The value a location entry stacks on: its chain's lower layers, else the baseline. */
	std::optional<json> ResolveLocationLowerValue(LocationTargetType type, std::string_view formKey,
		const SettingAddress& address, EntrySource selectedSource);
	/// Resolve everything a location target sits on top of, or null when the target is unreachable.
	std::optional<ResolvedSettingMap> BuildLocationLowerLayers(LocationTargetType type,
		std::string_view formKey, std::optional<EntrySource> selectedSource = std::nullopt);

	// --- Generic Scene Copy ---
	/// The entry that wins for each setting in a context, user over overwrite.
	using EffectiveContextEntries = std::map<SettingIdentity, const SettingEntry*>;
	/** @brief Whether the context's fields are consistent with its type. */
	static bool IsValidSceneContext(const SceneContextId& context);
	/** @brief Context equality with location form keys normalized. */
	static bool IsSameSceneContext(const SceneContextId& lhs, const SceneContextId& rhs);
	/** @brief The winning entry per setting in one context; exports retain tombstones. */
	static EffectiveContextEntries BuildEffectiveContextEntries(
		const std::vector<SettingEntry>& contextEntries, const SceneContextId& context, bool includeDeleted = false);
	/** @brief The entry list backing a context, null when it has none. */
	const std::vector<SettingEntry>* GetCopyContextEntries(const SceneContextId& context) const;
	/** @brief Mutable GetCopyContextEntries, loading weather or location data first. */
	std::vector<SettingEntry>* GetContextEntriesMut(const SceneContextId& context);
	/// As GetContextEntriesMut, but a weather or location context that has no config yet gets one.
	std::vector<SettingEntry>* EnsureContextEntriesMut(const SceneContextId& context);
	/** @brief Marks the user document section a context is stored in as needing a rewrite.
	 *  @param replaceMalformedEntries Repairs a raw "entries" value that is not an array, which only
	 *         a mutation about to append to it needs. */
	void MarkContextUserSettingsModified(const SceneContextId& context, bool replaceMalformedEntries);
	/** @brief One presentation bump, one mark and one save for a mutation spanning a whole context.
	 *  @param deferSave Holds the save and the resolve until the edit settles. */
	void CommitContextUserEntryMutation(const SceneContextId& context, bool deferSave = false,
		bool replaceMalformedEntries = true);
	/// The value a fresh entry pins: the layers beneath the context, or the feature's own value where
	/// the context stacks on nothing.
	std::optional<json> CaptureContextValue(const SceneContextId& context, const SettingAddress& address);
	/// The value an entry reverts to: what it was created with, or, where the entry stacks on lower
	/// layers, whatever those supply now.
	std::optional<json> ResolveContextEntryDefault(const SceneContextId& context, const SettingEntry& entry);
	/** @brief The source's winning entries, each checked for compatibility and conflict with the destination. */
	std::vector<CopyCandidate> BuildCopyCandidates(const SceneContextId& source,
		const SceneContextId& destination) const;
	/// Deferring the commit lets a fan-out over the periods land as one save.
	CopyResult CopySettingsToContext(const SceneContextId& source, const SceneContextId& destination,
		CopyConflictPolicy conflictPolicy, bool deferCommit);
	/// Whether a context holds entries or is on the user's location list.
	bool IsSceneContextAuthored(const SceneContextId& context) const;
	/// Whether a context is the live weather or a link of the player's location chain.
	bool IsCurrentSceneContext(const SceneContextId& context) const;
	/// Exact: an eased value moves by far less than any tolerance would forgive, and a skipped apply
	/// would accumulate that difference into a visible staircase.
	static bool ResolvedValuesEqual(const json& lhs, const json& rhs);
	/** @brief Reads a feature back to confirm it kept every value, which a clean LoadSettings does not prove.
	 *  @param observed Receives the reported value per update, null where none; empty when unreadable.
	 *  @return Whether every update survived the round trip. */
	static bool FeatureRetainedUpdates(Feature& feature, std::string_view featureShortName,
		const std::vector<CatalogSceneSettingUpdate>& updates, std::vector<json>* observed = nullptr);
	/** @brief Hash of the feature and the addresses an update set touches, not their values. */
	static size_t GetCatalogUpdateSignature(std::string_view featureShortName,
		std::span<const CatalogSceneSettingUpdate> updates);
	/** @brief Writes the updates into the feature's cached settings document and reloads it, rolling back on a throw. */
	bool ApplyCatalogSceneSettings(
		Feature& feature, const std::vector<CatalogSceneSettingUpdate>& updates);
	/** @brief Queues a read-back of an accepted apply.
	 *  @param transition Selects which failure map a rejected apply backs off in. */
	void ScheduleApplyVerification(std::string_view featureShortName,
		const std::vector<CatalogSceneSettingUpdate>& updates, size_t signature, bool transition);
	/// Drops any queued apply the feature did not actually keep, and backs that feature off.
	void VerifyPendingApplies();

	// --- Location float transitions ---
	/// Seconds on the game clock, so transitions freeze with the game rather than the wall clock.
	float GetPauseAwareTime() const;
	/// Eased position of a transition at the given time.
	float EaseLocationTransition(const LocationTransition& transition, float now) const;
	/** @brief Whether the transition has run its full duration. */
	static bool IsLocationTransitionFinished(const LocationTransition& transition, float now);
	/** @brief Starts a transition for each location-driven address whose value or membership changed. */
	void StartLocationTransitions(const ResolvedSettingMap& resolved, float now, bool animateChanges);
	/// Re-reads each in-flight transition's endpoint so one crossing a time-of-day or weather
	/// boundary eases toward where the scene is now rather than where it was when the transition began.
	void RefreshLocationTransitionEndpoints(const ResolvedSettingMap& resolved);
	/** @brief Applies one throttled tick of eased values. @return Whether any feature was applied. */
	bool AdvanceLocationTransitions(float now);
	/// Drop transitions the main apply already landed on, restoring the ones that eased back out.
	void RetireFinishedLocationTransitions(float now);
	/** @brief Regroups in-flight transitions into per-feature apply batches, skipping sketched addresses. */
	void RebuildLocationTransitionBatches();
	/** @brief Drops every transition and location cache so the next resolve starts fresh. */
	void ClearLocationTransitions();

	// --- Per-Location helpers ---
	/** @brief The target's config, or a shared empty one when it has none. */
	const LocationSceneConfig& GetLocationConfig(LocationTargetType type, std::string_view formKey) const;
	/** @brief The target's config, or null when it has none. */
	const LocationSceneConfig* FindLocationConfig(LocationTargetType type, std::string_view formKey) const;
	LocationSceneConfig* FindLocationConfig(LocationTargetType type, std::string_view formKey);
	/** @brief Removes one user entry. */
	void RemoveLocationSetting(LocationTargetType type, const std::string& formKey, size_t index);
	/** @brief Whether the target holds an entry for this setting and period, optionally of one source. */
	bool HasLocationEntry(LocationTargetType type, std::string_view formKey,
		const std::string& featureShortName, const std::vector<std::string>& settingPath,
		const std::string& settingKey, TimeOfDayPeriod period, std::optional<EntrySource> source = std::nullopt) const;
	/** @brief Map key of a target's config, as "Type:normalizedFormKey". */
	static std::string GetLocationConfigKey(LocationTargetType type, std::string_view formKey);
	/// User-document section holding the targets of this type.
	static const char* GetLocationSectionName(LocationTargetType type);
	/** @brief The target's config under its canonical form key, created when missing. */
	LocationSceneConfig& GetLocationConfigMut(LocationTargetType type, const std::string& formKey,
		const std::string& name = {});
	/// Upserts a target's identity and claims it for the user, shared by adding a target and its first setting.
	LocationSceneConfig& EnsureAuthoredLocationConfig(LocationTargetType type, const std::string& formKey,
		const std::string& name, const std::string& cocCode, const std::string& editorId = {});
	/// Raw user-document keys that resolve to one target: a form can be spelled several ways in the file.
	static std::vector<std::string> MatchingRawLocationKeys(const json& section, LocationTargetType type,
		std::string_view formKey);
	/** @brief Loads the active pack's location overwrites, one folder per target. */
	void DiscoverLocationOverwrites();
	/** @brief Loads one target folder, taking the target type from the form its name resolves to. */
	void DiscoverLocationOverwritesForTarget(const std::filesystem::path& targetDir);
	/** @brief Drops every user location entry and reloads them from the document's "location" section. */
	void LoadLocationUserSettings(const json& data);
	/** @brief Marks a target's user section modified and pins its time-of-day mode before an edit. */
	void PrepareLocationUserSettingsMutation(LocationTargetType type, std::string_view formKey,
		bool replaceMalformedEntries);

	// --- Helpers ---
	/** @brief The entry list of an interior or time-of-day scene type, empty for any other. */
	const std::vector<SettingEntry>& GetEntries(SceneType type) const;
	std::vector<SettingEntry>& GetEntriesMut(SceneType type);
	/** @brief Removes one user entry. */
	void RemoveSetting(SceneType type, size_t index);
	/** @brief Saves the user document and re-resolves. */
	void CommitSceneSettingChanges();
	/** @brief Re-resolves now; with deferSave the save waits for the deferred-change window. */
	void ReapplyAndSaveOrDefer(bool deferSave);
	/** @brief Invalidates the UI's entry views; also dirties the scene value caches. */
	void BumpEntryPresentationRevision();
	/** @brief A layer's entries were (re)loaded: rebuild the active-entry cache and re-resolve. */
	void MarkEntriesLoaded();
	/// Entry values changed: drop the per-period caches and re-resolve the location layer.
	void MarkSceneValuesDirty();
	/** @brief Whether the entry is not paused. */
	bool IsEntryActive(const SettingEntry& entry) const;
	/// Active, catalog-permitted and, for TimeOfDay or a per-period entry, transitionable float entry.
	bool IsResolvableEntry(const SettingEntry& entry, SceneType type) const;
	static SettingAddress GetEntryAddress(const SettingEntry& entry);
	/** @brief Whether a same-source entry exists for this setting; the period only counts for TimeOfDay. */
	bool HasDuplicateEntry(SceneType type, const std::string& featureShortName,
		const std::vector<std::string>& settingPath, const std::string& settingKey,
		EntrySource source, TimeOfDayPeriod period = TimeOfDayPeriod::Count) const;

	/** @brief Dirties every resolve cache and re-resolves now unless the layer is suspended. */
	void ReapplyIfActive();
	/** @brief Marks the interior or time-of-day user section as needing a rewrite. */
	void MarkEntryListUserSettingsModified(SceneType type);
	/** @brief Marks a weather's user section modified and pins its time-of-day mode before an edit. */
	void PrepareWeatherUserSettingsMutation(RE::FormID weatherId, bool replaceMalformedEntries);
	/** @brief Schedules a commit once edits have been quiet for kDeferredSaveDelay. */
	void MarkDeferredSceneChanges();
	/** @brief Commits the deferred changes once their deadline has passed. */
	void FlushDeferredSceneChanges();
	/** @brief Restores every applied setting and halts resolving; nests, so pair each with a resume. */
	void SuspendSceneLayer();
	/** @brief Ends one suspend; the outermost drops the cached feature snapshots and re-resolves. */
	void ResumeSceneLayer();

	// --- Overwrite discovery helper ---
	/** @brief Loads every overwrite file in a directory into one scene type, tagged with a period. */
	void DiscoverOverwritesInDir(SceneType type, const std::filesystem::path& dir,
		TimeOfDayPeriod period = TimeOfDayPeriod::Count);

	/// Re-reads the active pack's manifest.
	void DiscoverPresetMetadata();

	/** @brief Re-derives every transition control: the user's value, else the active pack's, else the default. */
	void RefreshTransitionControls();

	/** @brief Every transition control at its default, for initialization. */
	static constexpr std::array<float, kTransitionControlCount> GetDefaultTransitionControls()
	{
		std::array<float, kTransitionControlCount> values{};
		for (size_t index = 0; index < kTransitionControlCount; ++index)
			values[index] = kTransitionControlSpecs[index].defaultValue;
		return values;
	}

	/// Discover overwrite files for a single weather SPID folder.
	void DiscoverWeatherOverwritesForSpid(RE::FormID weatherId, const std::filesystem::path& weatherDir);

	/// Load non-weather user settings from unified SceneManager.json.
	void LoadAllUserSettings();

	/// Load weather user settings from SceneManager.json. Requires TESDataHandler.
	void LoadWeatherUserSettings();

	/** @brief Re-derives which saved set each weather and location resolves once its entries have loaded. */
	void RefreshTimeOfDayModes();
};
