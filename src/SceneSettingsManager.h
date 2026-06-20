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

/**
 * @brief Manages scene-specific setting overrides (Interior Only, TimeOfDay, WeatherSpecific).
 *
 * Zero coupling to individual features -- operates via JSON round-trip through
 * Feature::SaveSettings/LoadSettings.
 * Event-driven: cell transitions detected via MenuOpenCloseEvent; settings mutations are applied
 * on the next frame once cell data is available (deferred by SceneSettingsManager::Update()).
 */
class SceneSettingsManager
{
public:
	/** @brief Gets the singleton instance. */
	static SceneSettingsManager* GetSingleton()
	{
		static SceneSettingsManager singleton;
		return &singleton;
	}

	// --- Scene Types ---

	enum class SceneType
	{
		InteriorOnly
		// Future: TimeOfDay, WeatherSpecific
	};

	// --- Event Handler ---

	/**
	 * @brief Listens for LoadingMenu close to detect cell transitions.
	 *
	 * Same pattern as Skylighting::MenuOpenCloseEventHandler.
	 */
	class MenuOpenCloseEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		/** @brief Handles menu open/close events, queuing cell transitions on loading screen close. */
		virtual RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;

		/** @brief Registers this handler with the UI event source. */
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
		std::string sourceFilename;  // For overwrites: the filename it came from
	};

	// --- Generic Entry Management (scene-type agnostic) ---

	/** @brief Gets the read-only entry list for the given scene type. */
	const std::vector<SettingEntry>& GetEntries(SceneType type) const;

	/** @brief Checks whether an entry with the given source already exists for a feature+setting pair. */
	bool HasEntryFromSource(SceneType type, const std::string& featureShortName, const std::string& settingKey, EntrySource source) const;

	/** @brief Checks whether an active (non-paused) overwrite entry exists for a feature+setting pair. */
	bool HasActiveOverwrite(SceneType type, const std::string& featureShortName, const std::string& settingKey) const;

	/**
	 * @brief Adds a user setting override, persists it, and reapplies if scene is active.
	 * @param type Scene type to add the setting to.
	 * @param featureShortName Target feature's short name.
	 * @param settingKey JSON key within the feature's settings.
	 * @param value Override value.
	 */
	void AddSetting(SceneType type, const std::string& featureShortName, const std::string& settingKey, const json& value);

	/** @brief Removes an entry by index, deleting its overwrite file if applicable. */
	void RemoveSetting(SceneType type, size_t index);

	/** @brief Toggles the paused state of an entry by index. */
	void TogglePauseEntry(SceneType type, size_t index);

	/**
	 * @brief Updates an entry's override value.
	 * @param deferSave If true, skips persisting to disk (for batched UI edits like sliders).
	 */
	void UpdateEntryValue(SceneType type, size_t index, const json& newValue, bool deferSave = false);

	/** @brief Pauses or unpauses all overwrite-sourced entries for a scene type. */
	void SetAllOverwritesPaused(SceneType type, bool paused);

	/** @brief Checks whether all overwrites are currently paused for a scene type. */
	bool AreAllOverwritesPaused(SceneType type) const;

	/** @brief Checks whether any overwrite-sourced entries exist for a scene type. */
	bool HasOverwriteEntries(SceneType type) const;

	/** @brief Deletes all overwrite entries and their backing files for a scene type. */
	void DeleteAllOverwrites(SceneType type);

	/** @brief Pauses or unpauses all user-sourced entries for a scene type. */
	void SetAllUserPaused(SceneType type, bool paused);

	/** @brief Checks whether all user entries are currently paused for a scene type. */
	bool AreAllUserPaused(SceneType type) const;

	/** @brief Deletes all user-sourced entries for a scene type and persists the change. */
	void DeleteAllUserSettings(SceneType type);

	// --- Scene Application ---

	/**
	 * @brief Called each frame from State::Draw() to process deferred cell transitions.
	 *
	 * Cell data is not yet available when the LoadingMenu close event fires,
	 * so the actual transition check is deferred to the next rendered frame.
	 */
	void Update();

	/** @brief Processes a deferred cell transition, applying or reverting interior overrides. */
	void OnCellTransition();

	/** @brief Checks if a specific feature+setting is currently being overridden by any active scene setting. */
	bool IsSettingControlled(const std::string& featureShortName, const std::string& settingKey) const;

	/** @brief Checks if any scene settings are active for a given feature. */
	bool HasActiveSettingsForFeature(const std::string& featureShortName) const;

	/** @brief Checks whether all scene-specific settings are temporarily disabled for a feature. */
	bool IsFeaturePaused(const std::string& featureShortName) const;

	/** @brief Temporarily disables or re-enables all scene-specific settings for a feature. */
	void SetFeaturePaused(const std::string& featureShortName, bool paused);

	// --- Persistence ---

	/** @brief Persists all user-sourced entries for a scene type to disk as JSON. */
	void SaveUserSettings(SceneType type);

	/** @brief Loads user-sourced entries from the JSON file for a scene type. */
	void LoadUserSettings(SceneType type);

	/** @brief Scans the overwrites directory for JSON overwrite files and loads them. */
	void DiscoverOverwrites(SceneType type);

	/** @brief Loads overwrites and user settings for all scene types. */
	void LoadAll();

	// --- Path Resolution ---

	/** @brief Returns the human-readable name for a scene type (e.g. "InteriorOnly"). */
	static std::string GetSceneTypeName(SceneType type);

	/** @brief Returns the JSON file path for user settings of a scene type. */
	static std::filesystem::path GetSettingsFilePath(SceneType type);

	/** @brief Returns the directory path where overwrite files are discovered for a scene type. */
	static std::filesystem::path GetOverwritesPath(SceneType type);

	// --- Feature Metadata ---

	/** @brief Gets loaded feature short names filtered to only interior-relevant features. */
	static std::vector<std::string> GetInteriorRelevantFeatureNames();

	/** @brief Gets setting keys for a feature via JSON round-trip through SaveSettings. */
	static std::vector<std::string> GetFeatureSettingKeys(const std::string& featureShortName);

	/** @brief Gets the current value of a specific setting from a feature via JSON round-trip. */
	static json GetFeatureSettingValue(const std::string& featureShortName, const std::string& settingKey);

	/** @brief Classifies JSON value types for scene settings UI rendering. */
	enum class SettingType
	{
		Boolean,
		Integer,
		Float,
		String,
		Unknown
	};

	/** @brief Detects the JSON type of a setting value for UI rendering. */
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
	bool isCurrentlyApplied = false;
	bool queuedCellTransition = false;

	// Stored exterior settings per-feature (only the overridden keys)
	std::map<std::string, json> savedExteriorSettings;

	// --- Pause states ---
	std::map<std::string, bool> featurePauseStates;

	static constexpr size_t MAX_OVERWRITE_FILE_SIZE = 1024 * 1024;

	// --- Helpers ---
	std::vector<SettingEntry>& GetEntriesMut(SceneType type);
	bool IsEntryActive(const SettingEntry& entry) const;

	void ReapplyIfActive();
	void ApplySettings(SceneType type);
	void RevertToExteriorSettings();
	void SaveExteriorSettings(SceneType type);
	static void ApplySettingToFeature(const SettingEntry& entry);
};
