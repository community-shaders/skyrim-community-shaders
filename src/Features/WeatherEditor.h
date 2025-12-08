#pragma once

#include "Buffer.h"
#include "Feature.h"
#include "State.h"

struct WeatherEditor : Feature
{
public:
	static WeatherEditor* GetSingleton()
	{
		static WeatherEditor singleton;
		return &singleton;
	}

	virtual inline std::string GetName() override { return "Weather Editor"; }
	virtual inline std::string GetShortName() override { return "WeatherEditor"; }
	virtual inline std::string_view GetShaderDefineName() override { return "WEATHER"; }
	virtual inline std::string_view GetCategory() const override { return "Sky & Weather"; }
	virtual inline std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Development tool for editing weather, testing weather transitions, and managing weather-related feature settings.",
			{ "Provides weather editing functionality",
				"Includes dynamic saving and loading of imagespace, weather and lighting template settings.",
				"Real-time editing and previewing of effects" }
		};
	}

	Texture2D* diffuseIBLTexture = nullptr;
	ID3D11ComputeShader* diffuseIBLCS = nullptr;

	void Bind();

	virtual void DrawSettings() override;
	virtual void Prepass() override;
	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;

	ID3D11ComputeShader* GetDiffuseIBLCS();
	void LerpWeather(RE::TESWeather*, RE::TESWeather*, float);

private:
	void DrawWeatherStatusPanel();
};
