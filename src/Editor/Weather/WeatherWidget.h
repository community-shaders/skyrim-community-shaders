#pragma once

#include "../Widget.h"

using TESWeather = RE::TESWeather;
using ColorTypes = TESWeather::ColorTypes;
using ColorTimes = TESWeather::ColorTimes;
using FogData = TESWeather::FogData;

class WeatherWidget : public Widget
{
public:
	WeatherWidget* parent = nullptr;
	TESWeather* weather = nullptr;

	WeatherWidget(TESWeather* a_weather)
	{
		form = a_weather;
		weather = a_weather;
		LoadWeatherValues();
	}

	struct DirectionalColor
	{
		float3 min;
		float3 max;
	};

	struct DALC
	{
		DirectionalColor directional[3];
		float3 specular;
		float fresnelPower;
	};

	struct Atmosphere
	{
		float3 colorTimes[ColorTimes::kTotal];
	};

	struct Cloud
	{
		int cloudLayerSpeedY;
		int cloudLayerSpeedX;
		float3 color[ColorTimes::kTotal];
		float cloudAlpha[ColorTimes::kTotal];
	};

	struct Settings
	{
		std::string parent = "None";
		std::map<std::string, bool> inheritance;
		std::map<std::string, int> weatherProperties;
		std::map<std::string, float3> weatherColors;
		std::map<std::string, float> fogProperties;

		Atmosphere atmosphereColors[ColorTypes::kTotal];
		DALC dalc[ColorTimes::kTotal];
		Cloud clouds[TESWeather::kTotalLayers];
	};

	Settings settings;

	~WeatherWidget();

	virtual void DrawWidget() override;
	virtual void LoadSettings() override;
	virtual void SaveSettings() override;

	WeatherWidget* GetParent();
	bool HasParent() const;
	void SetWeatherValues();
	void LoadWeatherValues();

private:
	void DrawDALCSettings();
	void DrawWeatherColorSettings();
	void DrawCloudSettings();
	void DrawProperties(std::string category, std::map<std::string, int> properties);
	void InheritFromParent(const std::string& property);
};