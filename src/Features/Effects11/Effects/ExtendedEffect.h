#pragma once

#include "Effect.h"
#include "../UITree.h"

#include <span>

#ifdef ENABLE_ENB_EXTENDER

class ExtendedEffect : public Effect
{
public:
	void LoadWeatherData();
	void ApplyWeatherBlending(float blendFactor, uint32_t currentWeatherID, uint32_t lastWeatherID);
	void SyncWeatherVarFromUI(size_t index, uint32_t weatherID);
	void ApplyTimeOfDayInterpolation();
	void SaveWeatherOverrides() override;

	void Unload() override;
	bool IsTechniqueEnabled(TechniqueInfo& info) override;

	// Rendering
	void RenderImGui() override;
	static void RenderMergedUI(std::span<Effect*> effects, UITree::FilterMode filter = UITree::FilterMode::All);

private:
	using WeatherValues = std::unordered_map<std::string, std::string>;
	std::unordered_map<uint32_t, WeatherValues> weatherData;

	/** @brief A weather-separated variable whose per-weather values are parsed once, not per frame. */
	struct WeatherVarSlot
	{
		size_t index = 0;  ///< Index into uiVariables
		std::string iniKey;
		int components = 1;
		bool perComponent = false;  ///< Vector stored as KeyX/KeyY/... keys
	};
	/** @brief Parsed weather value; component c is used only when bit c of definedMask is set. */
	struct ParsedWeatherValue
	{
		float values[4] = {};
		uint8_t definedMask = 0;
	};
	std::vector<WeatherVarSlot> weatherVarSlots;
	std::vector<int> weatherSlotOfVariable;                                           ///< uiVariables index -> slot, or -1
	std::unordered_map<uint32_t, std::vector<ParsedWeatherValue>> parsedWeatherData;  ///< One entry per slot for each weatherData ID
	ID3DX11Effect* weatherCacheEffect = nullptr;
	size_t weatherCacheVariableCount = 0;

	/** @brief Rebuilds the weather caches when the effect was recompiled since the last build. */
	void EnsureWeatherCaches();
	void RebuildWeatherCaches();
	/** @brief Parses one slot's value from a weather file; unparsable components are left undefined. */
	static void ParseWeatherValue(const WeatherValues& values, const WeatherVarSlot& slot, ParsedWeatherValue& out);

	/** @brief Time-of-day variable in a group, with its index into the period weight table (-1 if unknown). */
	struct TimeOfDayEntry
	{
		size_t index = 0;
		int period = -1;
	};
	/** @brief Per-period variables that blend into one base effect variable. */
	struct TimeOfDayGroup
	{
		ID3DX11EffectVariable* baseVariable = nullptr;
		int components = 1;
		bool exteriorWeather = false;
		std::vector<TimeOfDayEntry> entries;
	};
	std::vector<TimeOfDayGroup> timeOfDayGroups;
	ID3DX11Effect* timeOfDayCacheEffect = nullptr;
	size_t timeOfDayCacheVariableCount = 0;

	/** @brief Rebuilds the time-of-day groups when the effect was recompiled since the last build. */
	void EnsureTimeOfDayGroups();
	void RebuildTimeOfDayGroups();

	/** @brief Dirty ini keys per weather file, each mapped to the weather ID whose values it was edited under.
		Several weatherlist sections may share one FileName, so the source weather is tracked per key. */
	using DirtyWeatherKeys = std::unordered_map<std::string, uint32_t>;
	std::unordered_map<std::string, DirtyWeatherKeys> dirtyWeatherFiles;

	std::unordered_map<std::string, int> bindingCache;

	int ResolveTechniqueBinding(const std::string& variableName);
	/** @brief Index of a time period name in the period weight table, or -1 if unknown. */
	static int GetPeriodIndex(const std::string& period);
};

using EffectBase = ExtendedEffect;

#else

using EffectBase = Effect;

#endif
