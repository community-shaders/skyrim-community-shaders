#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

#include "Globals.h"
#include "Utils/Form.h"

using json = nlohmann::json;

struct Feature;

/// Manages scene-specific setting overrides (Interior Only, TimeOfDay).
/// Applies overrides via Feature::SaveSettings/LoadSettings JSON round-trips with
/// epsilon-cached blending to minimise redundant updates during time-of-day transitions.
/// Event-driven: cell transitions detected via MenuOpenCloseEvent, mutations applied immediately.
class SceneSettingsManager
{
public:
	static SceneSettingsManager* GetSingleton()
	{
		static SceneSettingsManager singleton;
		return &singleton;
	}

	// --- Scene Types ---

	enum class SceneType
	{
		InteriorOnly,
		TimeOfDay
	};

	// --- Time of Day Periods ---

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

	/// Display names for each period — must match TimeOfDayPeriod order.
	static constexpr std::array<const char*, kPeriodCount> kPeriodNames = {
		"Dawn", "Sunrise", "Day", "Sunset", "Dusk", "Night"
	};

	/// Hour boundaries for each period [start, end).  Night wraps around midnight (21–28 i.e. 21–4).
	static constexpr float kPeriodHours[kPeriodCount][2] = {
		{ 4.0f, 6.0f },    // Dawn
		{ 6.0f, 8.0f },    // Sunrise
		{ 8.0f, 17.0f },   // Day
		{ 17.0f, 19.0f },  // Sunset
		{ 19.0f, 21.0f },  // Dusk
		{ 21.0f, 28.0f }   // Night (wraps past midnight)
	};

	/// Transition blend zone in hours at each period boundary.
	static constexpr float kTransitionHours = 0.5f;

	// --- Event Handler ---

	/// Listens for LoadingMenu close to detect cell transitions.
	/// Same pattern as Skylighting::MenuOpenCloseEventHandler.
	class MenuOpenCloseEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		virtual RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;

		static bool Register()
		{
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
			logger::info("[SceneSettings] Registered MenuOpenCloseEventHandler");
			return true;
		}
	};

	// --- Setting Entry ---

	enum class EntrySource
	{
		User,      // User-added via UI
		Overwrite  // Loaded from overwrite file
	};

	struct SettingEntry
	{
		std::string featureShortName;  // Feature's GetShortName()
		std::string settingKey;        // JSON key within the feature's settings
		json value;                    // Override value (bool, float, int, etc.)
		json originalValue;            // Value at time of creation, for revert
		bool paused = false;           // Temporarily disabled
		EntrySource source = EntrySource::User;
		std::string sourceFilename;                       // For overwrites: the filename it came from
		TimeOfDayPeriod period = TimeOfDayPeriod::Count;  // Which period this entry belongs to (TimeOfDay only)
	};

	// --- Generic Entry Management (scene-type agnostic) ---

	const std::vector<SettingEntry>& GetEntries(SceneType type) const;
	bool HasEntryFromSource(SceneType type, const std::string& featureShortName, const std::string& settingKey, EntrySource source) const;
	bool HasActiveOverwrite(SceneType type, const std::string& featureShortName, const std::string& settingKey) const;

	/// Add a setting.  For TimeOfDay entries, specify the target period.
	void AddSetting(SceneType type, const std::string& featureShortName, const std::string& settingKey, const json& value,
		TimeOfDayPeriod period = TimeOfDayPeriod::Count);
	void RemoveSetting(SceneType type, size_t index);
	void TogglePauseEntry(SceneType type, size_t index);
	void UpdateEntryValue(SceneType type, size_t index, const json& newValue, bool deferSave = false);

	/// Revert an entry's value to its originalValue (captured at creation).
	void RevertEntryToDefault(SceneType type, size_t index);

	/// Check if an entry already exists for a specific period (TimeOfDay)
	bool HasEntryForPeriod(const std::string& featureShortName, const std::string& settingKey,
		TimeOfDayPeriod period, EntrySource source) const;

	void SetAllOverwritesPaused(SceneType type, bool paused);
	bool AreAllOverwritesPaused(SceneType type) const;
	bool HasOverwriteEntries(SceneType type) const;
	void DeleteAllOverwrites(SceneType type);

	void SetAllUserPaused(SceneType type, bool paused);
	bool AreAllUserPaused(SceneType type) const;
	void DeleteAllUserSettings(SceneType type);

	// --- Scene Application ---

	/// Called every frame from State::Update().
	void Update();

	/// Called by MenuOpenCloseEventHandler when a cell transition is detected.
	void OnCellTransition();

	/// Check if a specific feature+setting is currently being overridden by any active scene setting
	bool IsSettingControlled(const std::string& featureShortName, const std::string& settingKey) const;

	/// Check if any scene settings are active for a given feature
	bool HasActiveSettingsForFeature(const std::string& featureShortName) const;

	/// Per-feature pause: temporarily disable all scene-specific settings for a feature
	bool IsFeaturePaused(const std::string& featureShortName) const;
	void SetFeaturePaused(const std::string& featureShortName, bool paused);

	// --- Persistence ---

	/// Save all user data (interior, TOD, weather) to unified SceneManager.json.
	void SaveAllUserSettings();

	void DiscoverOverwrites(SceneType type);

	/// Discover weather-specific overwrite files from Weather/{SPID}/ folders.
	void DiscoverWeatherOverwrites();

	/// Load non-weather scene types (overwrites + user settings). Called early from Setup().
	void LoadAll();

	/// Load weather overwrites and weather user settings. Requires TESDataHandler (call after kDataLoaded).
	void LoadWeatherData();

	// --- Path Resolution ---

	static std::string GetSceneTypeName(SceneType type);
	static std::filesystem::path GetUserSettingsFilePath();
	static std::filesystem::path GetOverwritesPath(SceneType type);

	// --- Time of Day Helpers (public for UI) ---

	static const char* GetPeriodName(TimeOfDayPeriod period);
	static TimeOfDayPeriod GetPeriodFromName(const std::string& name);
	static float GetCurrentGameHour();
	void GetTimeOfDayFactors(float outFactors[static_cast<int>(TimeOfDayPeriod::Count)]);
	TimeOfDayPeriod GetDominantPeriod();

	/// Returns the period whose hour range contains the current game hour.
	static TimeOfDayPeriod GetCurrentPeriod();

	// --- Feature Metadata ---

	/// Get loaded feature short names filtered to only interior-relevant features
	static std::vector<std::string> GetInteriorRelevantFeatureNames();

	/// Get loaded feature short names filtered to exterior/TOD-relevant features
	static std::vector<std::string> GetExteriorRelevantFeatureNames();

	/// Check if a feature is allowed for the given scene type (whitelist check)
	static bool IsFeatureAllowedForType(SceneType type, const std::string& featureShortName);

	/// Get the display name for a feature (e.g. "Screen Space GI" from "ScreenSpaceGI")
	static std::string GetFeatureDisplayName(const std::string& featureShortName);

	/// Get setting keys for a feature by JSON round-tripping its current settings
	static std::vector<std::string> GetFeatureSettingKeys(const std::string& featureShortName);

	/// Get only float setting keys that can be smoothly transitioned in Time of Day
	static std::vector<std::string> GetTransitionableSettingKeys(const std::string& featureShortName);

	/// Get current value of a specific setting from a feature
	static json GetFeatureSettingValue(const std::string& featureShortName, const std::string& settingKey);

	/// Detect the JSON type of a setting value for UI rendering
	enum class SettingType
	{
		Boolean,
		Integer,
		Float,
		String,
		Unknown
	};
	static SettingType DetectSettingType(const json& value);

	// --- Per-Weather Scene Settings ---

	/// Per-weather configuration: all entries are per-period (TOD).
	/// The UI flat/TOD toggle is a view-only preference, not a data mode.
	struct WeatherSceneConfig
	{
		std::vector<SettingEntry> entries;
	};

	const WeatherSceneConfig& GetWeatherConfig(RE::FormID weatherId) const;
	bool HasWeatherConfig(RE::FormID weatherId) const;

	/// Add a weather setting.  Requires a valid period (all entries are per-period).
	void AddWeatherSetting(RE::FormID weatherId, const std::string& featureShortName,
		const std::string& settingKey, const json& value, TimeOfDayPeriod period);
	void RemoveWeatherSetting(RE::FormID weatherId, size_t index);
	void TogglePauseWeatherEntry(RE::FormID weatherId, size_t index);
	void UpdateWeatherEntryValue(RE::FormID weatherId, size_t index, const json& newValue, bool deferSave = false);
	void RevertWeatherEntryToDefault(RE::FormID weatherId, size_t index);
	void DeleteAllWeatherSettings(RE::FormID weatherId);

	bool HasWeatherEntryForPeriod(RE::FormID weatherId, const std::string& featureShortName,
		const std::string& settingKey, TimeOfDayPeriod period) const;

	/// Weather UI preference: show TOD table vs flat view (view-only, data is always per-period).
	bool IsWeatherShowTimeOfDay(RE::FormID weatherId) const;
	void SetWeatherShowTimeOfDay(RE::FormID weatherId, bool show);

	static std::filesystem::path GetWeatherOverwritesDir();

private:
	SceneSettingsManager() = default;
	~SceneSettingsManager() = default;
	SceneSettingsManager(const SceneSettingsManager&) = delete;
	SceneSettingsManager& operator=(const SceneSettingsManager&) = delete;

	// --- Per scene-type storage ---
	std::map<SceneType, std::vector<SettingEntry>> entries;
	std::map<SceneType, bool> allOverwritesPausedMap;
	std::map<SceneType, bool> allUserPausedMap;

	// --- Interior state tracking ---
	bool queuedCellTransition = false;
	bool isCurrentlyApplied = false;

	// Stored exterior settings per-feature (only the overridden keys)
	std::map<std::string, json> savedExteriorSettings;

	// --- Time of Day state ---
	bool isTimeOfDayActive = false;
	TimeOfDayPeriod lastDominantPeriod = TimeOfDayPeriod::Count;

	/// Baseline settings saved before TOD activation, for reverting on deactivate.
	std::map<std::string, json> savedTimeOfDayBaseline;

	/// Cache of last-applied blended float values per feature+key.
	/// Used with epsilon comparison to skip redundant LoadSettings calls.
	std::map<std::string, std::map<std::string, float>> lastAppliedTODFloats;

	/// Cache of last-applied non-float values per feature+key.
	std::map<std::string, std::map<std::string, json>> lastAppliedTODOther;

	/// Float epsilon — changes smaller than this skip the LoadSettings call.
	static constexpr float kBlendEpsilon = 1e-3f;

	/// Cached game hour from last blend update.  Used to skip redundant
	/// per-frame map rebuilds when the game hour hasn't moved enough.
	float lastBlendedHour = -1.0f;

	/// Minimum game-hour delta before re-running the blend.  At default
	/// timescale (20×) this equals ~0.36 real seconds — imperceptible yet
	/// saves 98%+ of per-frame map construction work.
	static constexpr float kHourUpdateThreshold = 1e-3f;

	// --- Pause states ---
	std::map<std::string, bool> featurePauseStates;

	static constexpr size_t MAX_OVERWRITE_FILE_SIZE = 1024 * 1024;

	// --- Per-Weather Scene storage ---
	std::map<RE::FormID, WeatherSceneConfig> weatherSceneConfigs;
	static const WeatherSceneConfig kEmptyWeatherConfig;

	/// UI preference per weather: show TOD table vs flat view (keyed by FormID for fast access).
	std::map<RE::FormID, bool> weatherShowTimeOfDay_;

	/// Baseline settings saved before weather scene activation, for reverting.
	std::map<std::string, json> savedWeatherBaseline;

	/// Cache of last-applied weather blend values per feature+key.
	std::map<std::string, std::map<std::string, float>> lastAppliedWeatherFloats;

	/// Last weather FormIDs used for blending — detect weather changes.
	RE::FormID lastCurrentWeatherId = 0;
	RE::FormID lastLastWeatherId = 0;
	float lastWeatherLerp = -1.0f;
	bool isWeatherSceneActive = false;

	// --- Per-Weather helpers ---
	WeatherSceneConfig& GetWeatherConfigMut(RE::FormID weatherId);
	void UpdateWeatherScene();
	void ActivateWeatherScene();
	void DeactivateWeatherScene();
	void SaveWeatherBaseline();
	void RevertWeatherBaseline();

	/// Compute a single float override for a feature+key across two transitioning weathers.
	/// Returns true if an override was computed, with the result in outValue.
	bool ComputeWeatherBlendedFloat(const std::string& shortName, const std::string& key,
		RE::FormID currentId, RE::FormID lastId, float weatherLerp,
		float gameHour, float& outValue);

	// --- Helpers ---
	std::vector<SettingEntry>& GetEntriesMut(SceneType type);
	bool IsEntryActive(const SettingEntry& entry) const;
	bool HasActiveEntries(SceneType type) const;
	bool HasDuplicateEntry(SceneType type, const std::string& featureShortName, const std::string& settingKey,
		EntrySource source, TimeOfDayPeriod period = TimeOfDayPeriod::Count) const;

	void ReapplyIfActive();
	void ApplySettings(SceneType type);
	void SavePartialBaseline(SceneType type, std::map<std::string, json>& outBaseline);
	void RevertFromBaseline(std::map<std::string, json>& baseline);
	void RevertToExteriorSettings();
	void SaveExteriorSettings(SceneType type);
	static void ApplySettingToFeature(const SettingEntry& entry);

	// --- Time of Day lifecycle ---
	void UpdateTimeOfDay();
	void ActivateTimeOfDay();
	void DeactivateTimeOfDay();
	void SaveTimeOfDayBaseline();
	void RevertTimeOfDayBaseline();
	void ApplyTimeOfDayBlended();

	// --- Time of Day blending helpers ---

	/// Lightweight ref to a TOD period entry, used during blending
	/// to avoid copying JSON values from the entry storage.
	struct PeriodRef
	{
		int periodIdx;
		const json* value;
	};

	/// Look up the saved baseline value for a feature+key pair.
	/// @return Pointer to the baseline JSON, or nullptr if not found.
	const json* FindTODBaseline(const std::string& shortName, const std::string& key) const;

	/// Compute a weighted blend of float values across active TOD periods.
	/// Uncovered periods fall back to @p baseVal so the sum is always complete.
	float BlendFloatForPeriods(float baseVal, const std::vector<PeriodRef>& periodRefs,
		const float* factors, const std::string& shortName, const std::string& key) const;

	/// Select the non-float value from the dominant period with type validation.
	/// Falls back to @p baseline if no matching period or on type mismatch.
	json SnapNonFloatToDominant(const json& baseline, const std::vector<PeriodRef>& periodRefs,
		TimeOfDayPeriod dominant, const std::string& shortName, const std::string& key) const;

	// --- Overwrite discovery helper ---
	void DiscoverOverwritesInDir(SceneType type, const std::filesystem::path& dir,
		TimeOfDayPeriod period = TimeOfDayPeriod::Count);

	/// Discover overwrite files for a single weather SPID folder.
	void DiscoverWeatherOverwritesForSpid(RE::FormID weatherId, const std::filesystem::path& weatherDir);

	/// Load non-weather user settings from unified SceneManager.json.
	void LoadAllUserSettings();

	/// Load weather user settings from SceneManager.json. Requires TESDataHandler.
	void LoadWeatherUserSettings();
};
