#pragma once

#include "Feature.h"

// Example feature demonstrating the weather variable registration pattern
class ExampleWeatherFeature : public Feature
{
public:
	// Feature settings structure
	struct Settings
	{
		float intensity = 1.0f;
		float3 color = { 1.0f, 0.5f, 0.2f };
		float4 tintColor = { 1.0f, 1.0f, 1.0f, 1.0f };
		bool enabled = true;
	} settings;

	// Feature interface
	std::string GetName() override { return "Example Weather Feature"; }
	std::string GetShortName() override { return "ExampleWeather"; }

	// Register weather-controllable variables
	// This is the ONLY weather-related method you need to implement!
	void RegisterWeatherVariables() override;

	// Standard feature methods
	void DrawSettings() override;
	void LoadSettings(json& o_json) override;
	void SaveSettings(json& o_json) override;
};
