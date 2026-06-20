#pragma once
#include "ABTestAggregator.h"
#include "Utils/FileSystem.h"
#include <nlohmann/json.hpp>
#include <vector>

/**
 * @brief Manages A/B testing of feature configurations for performance comparison.
 *
 * Automatically swaps between two configuration snapshots (USER and TEST) at a
 * configurable interval, collecting per-frame draw call timing data through the
 * ABTestAggregator. Both snapshots are held in memory to avoid disk I/O during swaps.
 */
class ABTestingManager
{
public:
	/** @brief Returns the global singleton instance. */
	static ABTestingManager* GetSingleton();

	/**
	 * @brief Sets the swap interval for A/B testing.
	 * @param interval Number of seconds between variant swaps.
	 */
	void SetTestInterval(uint32_t interval);

	/** @brief Returns the current swap interval in seconds. */
	uint32_t GetTestInterval() const { return testInterval; }

	/** @brief Returns true if A/B testing is currently running. */
	bool IsEnabled() const { return abTestingEnabled; }

	/** @brief Returns true if the TEST (variant B) configuration is currently active. */
	bool IsUsingTestConfig() const { return usingTestConfig; }

	/**
	 * @brief Starts A/B testing.
	 *
	 * Snapshots the current settings as variant B (TEST), loads and snapshots
	 * the saved user settings as variant A (USER), then begins the test cycle
	 * starting with variant B.
	 */
	void Enable();

	/**
	 * @brief Stops A/B testing and restores the TEST configuration.
	 *
	 * Loads the variant B (TEST) snapshot back into the active state so the
	 * user returns to the settings they had when testing was enabled.
	 */
	void Disable();

	/**
	 * @brief Per-frame update that handles timed variant swapping.
	 *
	 * Checks elapsed time since the last swap and, when the interval expires,
	 * toggles between USER and TEST configuration snapshots (in-memory, no disk I/O).
	 */
	void Update();

	/** @brief Draws the ImGui A/B test interval slider in the settings panel. */
	void DrawSettingsUI();

	/** @brief Draws the in-game overlay showing the active variant and remaining swap time. */
	void DrawOverlayUI();

	/** @brief Returns a mutable reference to the underlying test data aggregator. */
	ABTestAggregator& GetAggregator() { return aggregator; }

	/** @brief Returns true if both USER and TEST configuration snapshots are available. */
	bool HasCachedSnapshots() const { return hasTestSnapshot && hasUserSnapshot; }

	/** @brief Returns the cached USER (variant A) configuration snapshot. */
	const nlohmann::json& GetUserSnapshot() const { return userConfigSnapshot; }

	/** @brief Returns the cached TEST (variant B) configuration snapshot. */
	const nlohmann::json& GetTestSnapshot() const { return testConfigSnapshot; }

	/**
	 * @brief Returns a human-readable list of setting differences between USER and TEST.
	 * @return Vector of formatted strings in "path: oldValue -> newValue" form.
	 */
	std::vector<std::string> GetConfigDifferencesForDisplay() const;

	/**
	 * @brief Returns structured diff entries between the USER and TEST snapshots.
	 * @param epsilon Floating-point comparison tolerance for detecting changes.
	 * @return Vector of SettingsDiffEntry describing each changed setting.
	 */
	std::vector<SettingsDiffEntry> GetConfigDiffEntries(float epsilon = 0.0001f) const;

	/** @brief Discards both cached configuration snapshots. Called when overlay results are cleared. */
	void ClearCachedSnapshots();

private:
	uint32_t testInterval = 0;
	bool abTestingEnabled = false;
	bool usingTestConfig = false;
	LARGE_INTEGER timingFrequency = { 0 };
	LARGE_INTEGER lastTestSwitch = { 0 };
	ABTestAggregator aggregator;

	// In-memory storage for both variants to avoid disk I/O during swapping
	nlohmann::json testConfigSnapshot;
	nlohmann::json userConfigSnapshot;
	bool hasTestSnapshot = false;
	bool hasUserSnapshot = false;

	// Track what changed between USER and TEST configs
	std::vector<std::string> GetConfigDifferences() const;
};