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
	virtual inline std::string_view GetCategory() const override { return "Debug"; }
	virtual inline std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Development tool for editing weather settings and testing IBL.",
			{ "Provides weather editing functionality",
				"Includes save/load settings controls",
				"Real-time weather transition monitoring",
				"Debug feature for development" }
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
	void DrawQuickWeatherSpawner();
};
