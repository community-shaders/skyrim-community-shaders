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
	char currentParentBuffer[256] = "None";

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

	struct WeatherColor
	{
		float3 colorTimes[ColorTimes::kTotal];
	};

	struct Cloud
	{
		int8_t cloudLayerSpeedY;
		int8_t cloudLayerSpeedX;
		float3 color[ColorTimes::kTotal];
		float cloudAlpha[ColorTimes::kTotal];
	};

	struct Settings
	{
		std::string currentParentBuffer;

		int windSpeed;  // underlying type uint8_t

		//// underlying type int8_t
		int transDelta;
		int sunGlare;
		int sunDamage;
		int precipitationBeginFadeIn;
		int precipitationEndFadeOut;
		int thunderLightningBeginFadeIn;
		int thunderLightningEndFadeOut;
		int thunderLightningFrequency;
		int visualEffectBegin;
		int visualEffectEnd;
		int windDirection;
		int windDirectionRange;

		float3 lightningColor;

		WeatherColor weatherColors[ColorTypes::kTotal];
		DALC dalc[ColorTimes::kTotal];
		FogData fog;
		Cloud clouds[TESWeather::kTotalLayers];

		bool inheritWindSettings;
		bool inheritSunSettings;
		bool inheritPrecipitationSettings;
		bool inheritLightningSettings;
		bool inheritVisualEffectSettings;
		bool inheritTransDeltaSettings;
		bool inheritDALCSettings;
		bool inheritWeatherColorSettings;
		bool inheritCloudSettings;
		bool inheritFogSettings;
	};

	Settings settings;

	~WeatherWidget();

	virtual void DrawWidget() override;
	virtual void LoadSettings() override;
	virtual void SaveSettings() override;

	WeatherWidget* GetParent();
	bool HasParent();
	void SetWeatherValues();
	void LoadWeatherValues();

private:
	void DrawWindSettings();
	void DrawDALCSettings();
	void DrawWeatherColorSettings();
	void DrawCloudSettings();
	void DrawLightningSettings();
	void DrawFogSettings();
	void DrawVisualEffectSettings();
	void DrawSunSettings();
	void DrawPrecipitationSettings();
	void DrawSeparator();
};
