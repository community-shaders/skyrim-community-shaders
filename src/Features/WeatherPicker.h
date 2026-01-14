#pragma once

#include "Menu.h"
#include "OverlayFeature.h"

struct WeatherPicker : OverlayFeature
{
	// Virtual overrides in Feature.h order
	std::string GetName() override { return "Weather Picker"; }
	std::string GetShortName() override { return "WeatherPicker"; }

	virtual bool SupportsVR() override { return true; }
	virtual bool IsCore() const override { return true; }
	virtual std::string_view GetCategory() const override { return "Sky"; }
	virtual bool IsInMenu() const override { return true; }  // Show in main menu to provide weather debugging UI

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override;
	virtual void DrawSettings() override;

	virtual void DataLoaded() override;

	// WeatherPicker-specific methods
	void RenderWeatherDetailsWindow(bool* open);

	// Core weather display functions that other features can use
	static void DisplayWeatherInfo(RE::TESWeather* weather, float weatherPct = -1.0f, bool showInteractiveElements = true);
	static void RenderCoreWeatherDetails(bool showInteractiveElements = true);
	static void RenderFeatureWeatherAnalysis();

	// --- Refactor helpers for RenderCoreWeatherDetails ---
	static void RenderWeatherControls(RE::Sky* sky);
	static void RenderWeatherInformationDisplay(RE::Sky* sky, bool showInteractiveElements = true);

	struct WeatherDetailsWindowSettings
	{
		bool Enabled = false;
		bool ShowInOverlay = false;
		ImVec2 Position = ImVec2(50.f, 50.f);
		bool PositionSet = false;
	} WeatherDetailsWindow;

public:
	/**
	 * Gets the appropriate color for a weather type based on its flags.
	 * Uses a priority system: Rain > Snow > Aurora > Aurora Follows Sun > Cloudy > Pleasant > Unclassified > Default
	 * @param weather Pointer to the weather object
	 * @return ImVec4 color appropriate for the weather type
	 */
	static ImVec4 GetWeatherTypeColor(RE::TESWeather* weather);
	/**
	 * Renders a weather name with multiple colors if the weather has multiple flags.
	 * Each flag gets its own color segment in the weather name display.
	 * @param weather Pointer to the weather object
	 * @param weatherName The formatted weather name to display
	 * @return true if the main weather name (base name) was hovered, false otherwise
	 */
	static bool RenderMultiColorWeatherName(RE::TESWeather* weather, const std::string& weatherName);

	/**
	 * Get the color associated with a specific weather flag.
	 * @param flag The weather flag to get the color for
	 * @return ImVec4 color for the flag
	 */
	static ImVec4 GetWeatherFlagColor(RE::TESWeather::WeatherDataFlag flag);

	/**
	 * Get the color associated with a specific weather flag by name.
	 * @param flagName The name of the flag to get the color for
	 * @return ImVec4 color for the flag
	 */
	static ImVec4 GetWeatherFlagColorByName(const std::string& flagName);

private:
	// Wind direction offset to align with game's coordinate system
	static constexpr float WIND_DIRECTION_OFFSET = 30.5f;

	// Weather flag filter bits (for 7 weather types)
	static constexpr uint32_t ALL_WEATHER_FLAGS = 0x7F;  // Bits 0-6 all enabled
	static constexpr uint32_t UNCLASSIFIED_FLAG = 0x40;  // Bit 6 only

	// Static state for weather picker and data
	static inline bool s_weathersLoaded = false;
	static inline std::vector<RE::TESWeather*> s_allWeathers;
	static inline std::vector<RE::TESWeather*> s_filteredWeathers;
	static inline int s_selectedWeatherIdx = -1;
	static inline uint32_t s_weatherFlagFilter = ALL_WEATHER_FLAGS;  // Start with all filters enabled by default (bits 0-6)
	static inline uint32_t s_lastWeatherFlagFilter = UNCLASSIFIED_FLAG;
	static inline bool s_accelerateWeatherChange = true;
	static inline RE::TESWeather* s_cachedLastWeather = nullptr;

	// Static helper for display name extraction
	static std::string GetDisplayName(const RE::TESWeather* weather);

	// Weather comparator for consistent sorting
	struct WeatherNameComparator
	{
		bool operator()(const RE::TESWeather* a, const RE::TESWeather* b) const
		{
			return WeatherPicker::GetDisplayName(a) < WeatherPicker::GetDisplayName(b);
		}
	};

	// --- Refactor helpers for DisplayWeatherInfo ---
	static void DisplayWeatherBasicInfo(RE::TESWeather* weather, float weatherPct);
	static void DisplayPrecipitationInfo(RE::TESWeather* weather);
	static void DisplayLightningInfo(RE::TESWeather* weather, bool showInteractiveElements);
	static void DisplayWindInfo(RE::TESWeather* weather);

	// Helper functions
	static void LoadAllWeathers();
	static void UpdateFilteredWeathers();
	static int FindWeatherIndex(RE::TESWeather* targetWeather);
	static std::vector<std::string> GetWeatherFlagNames(RE::TESWeather* weather);

	// Implement OverlayFeature interface
	void DrawOverlay() override;
	bool IsOverlayVisible() const override;
};
