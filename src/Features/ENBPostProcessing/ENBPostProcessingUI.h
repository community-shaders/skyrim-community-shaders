#pragma once

#include "EffectManager.h"
#include "WeatherManager.h"

class ENBPostProcessingUI
{
public:
	static ENBPostProcessingUI& GetSingleton();

	// Main UI rendering method
	void RenderImGui();

private:
	// UI section rendering methods
	void RenderEffectsList();
	void RenderSettingsPanel();
	void RenderWeatherControl();

	// Helper UI methods
	void RenderAllSettings();
};