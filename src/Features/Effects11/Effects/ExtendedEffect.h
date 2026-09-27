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
	/**
	 * @brief Draws the parameters of several effects as one annotation-ordered tree.
	 * @param options Search and time-period filters; also reports whether anything changed. May be null.
	 */
	static void RenderMergedUI(std::span<Effect*> effects, UITree::FilterMode filter = UITree::FilterMode::All, UITree::ViewOptions* options = nullptr);

private:
	using WeatherValues = std::unordered_map<std::string, std::string>;
	std::unordered_map<uint32_t, WeatherValues> weatherData;

	/** @brief Dirty ini keys per weather file, each mapped to the weather ID whose values it was edited under.
		Several weatherlist sections may share one FileName, so the source weather is tracked per key. */
	using DirtyWeatherKeys = std::unordered_map<std::string, uint32_t>;
	std::unordered_map<std::string, DirtyWeatherKeys> dirtyWeatherFiles;

	std::unordered_map<std::string, int> bindingCache;

	int ResolveTechniqueBinding(const std::string& variableName);
	static float GetPeriodWeight(const std::string& period);
};

using EffectBase = ExtendedEffect;

#else

using EffectBase = Effect;

#endif
