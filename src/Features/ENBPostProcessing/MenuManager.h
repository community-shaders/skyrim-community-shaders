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

	// Helper UI methods
	void RenderAllSettings();
	std::map<std::string, std::vector<std::string>> GetCategorizedSettings() const;
};