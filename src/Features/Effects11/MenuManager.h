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
	void RenderSettingsPanel();
	void RenderWeatherControl();
	void RenderDebugControl();

	// Helper UI methods
	void RenderAllSettings();
	float GetTimeOfDayBlendFactor(int timeIndex) const;
};