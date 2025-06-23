#pragma once

#include "Feature.h"

struct WeatherPicker : Feature
{
	static WeatherPicker* GetSingleton()
	{
		static WeatherPicker singleton;
		return &singleton;
	}

	// Virtual overrides in Feature.h order
	std::string GetName() override { return "Weather Picker"; }
	std::string GetShortName() override { return "WeatherPicker"; }

	virtual bool SupportsVR() override { return true; }
	virtual bool IsCore() const override { return true; }
	virtual std::string_view GetCategory() const override { return "Debug"; }
	virtual bool IsInMenu() const override { return true; }  // Show in main menu to provide weather debugging UI

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override;
	virtual void DrawSettings() override;

	virtual void DataLoaded() override;

	// WeatherPicker-specific methods
	void RenderWeatherDetailsWindow(bool* open);

	// Core weather display functions that other features can use
	static void DisplayWeatherInfo(RE::TESWeather* weather, float weatherPct = -1.0f, bool showInteractiveElements = true);
	static void RenderCoreWeatherDetails(bool isPopupWindow = false);
	static void RenderFeatureWeatherAnalysis();

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

	// Weather comparator for consistent sorting
	struct WeatherNameComparator
	{
		bool operator()(const RE::TESWeather* a, const RE::TESWeather* b) const
		{
			auto getDisplayName = [](const RE::TESWeather* weather) -> std::string {
				const char* name = weather->GetName();
				if (name && strlen(name) > 0) {
					return std::string(name);
				}
				const char* editorID = weather->GetFormEditorID();
				if (editorID && strlen(editorID) > 0) {
					return std::string(editorID);
				}
				return std::to_string(weather->GetFormID());
			};
			return getDisplayName(a) < getDisplayName(b);
		}
	};

	// Helper functions
	static void LoadAllWeathers();
	static void UpdateFilteredWeathers();
	static int FindWeatherIndex(RE::TESWeather* targetWeather);
	static std::vector<std::string> GetWeatherFlagNames(RE::TESWeather* weather);
};
