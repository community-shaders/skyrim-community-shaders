#pragma once

#include "EffectManager.h"
#include "WeatherManager.h"
#include <string>

class ENBPostProcessingUI
{
public:
	static ENBPostProcessingUI& GetSingleton();

	// Main UI rendering method
	void RenderImGui();

private:
	ENBPostProcessingUI() = default;
	~ENBPostProcessingUI() = default;
	ENBPostProcessingUI(const ENBPostProcessingUI&) = delete;
	ENBPostProcessingUI& operator=(const ENBPostProcessingUI&) = delete;

	// UI section rendering methods
	void RenderEffectsList();
	void RenderSettingsPanel();
	void RenderWeatherControl();
	void RenderBloomSettings();
	void RenderLensSettings();

	// Helper UI methods
	void RenderAllSettings();
};