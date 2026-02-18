#pragma once

#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

#include "Globals.h"

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

	/// Hour boundaries for each period [start, end).  Night wraps around midnight (21–28 i.e. 21–4).
	static constexpr float kPeriodHours[6][2] = {
		{ 4.0f, 6.0f },    // Dawn
		{ 6.0f, 8.0f },    // Sunrise
		{ 8.0f, 17.0f },   // Day
		{ 17.0f, 19.0f },  // Sunset
		{ 19.0f, 21.0f },  // Dusk
		{ 21.0f, 28.0f }   // Night (wraps past midnight)
	};

	/// Transition blend zone in hours at each period boundary.
	static constexpr float kTransitionHours = 0.5f;

	/// Number of time-of-day periods (avoids repeated static_cast).
	static constexpr int kPeriodCount = static_cast<int>(TimeOfDayPeriod::Count);

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
		bool paused = false;           // Temporarily disabled
		EntrySource source = EntrySource::User;
		std::string sourceFilename;                   // For overwrites: the filename it came from
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

	void SaveUserSettings(SceneType type);
	void LoadUserSettings(SceneType type);
	void DiscoverOverwrites(SceneType type);

	/// Convenience: load all scene types
	void LoadAll();

	// --- Path Resolution ---

	static std::string GetSceneTypeName(SceneType type);
	static std::filesystem::path GetSettingsFilePath(SceneType type);
	static std::filesystem::path GetOverwritesPath(SceneType type);

	// --- Time of Day Helpers (public for UI) ---

	static const char* GetPeriodName(TimeOfDayPeriod period);
	static TimeOfDayPeriod GetPeriodFromName(const std::string& name);
	static float GetCurrentGameHour();
	void GetTimeOfDayFactors(float outFactors[static_cast<int>(TimeOfDayPeriod::Count)]);
	TimeOfDayPeriod GetDominantPeriod();

	// --- Feature Metadata ---

	/// Get loaded feature short names filtered to only interior-relevant features
	static std::vector<std::string> GetInteriorRelevantFeatureNames();

	/// Get loaded feature short names filtered to exterior/TOD-relevant features
	static std::vector<std::string> GetExteriorRelevantFeatureNames();

	/// Get setting keys for a feature by JSON round-tripping its current settings
	static std::vector<std::string> GetFeatureSettingKeys(const std::string& featureShortName);

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

	// --- Helpers ---
	std::vector<SettingEntry>& GetEntriesMut(SceneType type);
	bool IsEntryActive(const SettingEntry& entry) const;
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

	// --- Overwrite discovery helper ---
	void DiscoverOverwritesInDir(SceneType type, const std::filesystem::path& dir,
		TimeOfDayPeriod period = TimeOfDayPeriod::Count);
};
