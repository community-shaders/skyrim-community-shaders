#pragma once

#include "EffectManager.h"
#include "WeatherManager.h"

class MenuManager
{
public:
	static MenuManager& GetSingleton();

	// Main UI rendering method
	void RenderImGui();

private:
	// UI section rendering methods
	void RenderEffectsList();
	void RenderSettingsPanel();
	void RenderWeatherControl();
	void RenderDebugControl();

	// Helper UI methods
	void RenderAllSettings();
	std::vector<int> GetActiveTimeOfDayIndices() const;
	float GetTimeOfDayBlendFactor(int timeIndex) const;
};