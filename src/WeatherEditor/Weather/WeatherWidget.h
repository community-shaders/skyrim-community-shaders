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
		// Cache the original vanilla values for restoration
		vanillaSettings = settings;
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

	struct ImageSpaceSettings
	{
		// HDR Settings
		float hdrEyeAdaptSpeed = 0.0f;
		float hdrBloomBlurRadius = 0.0f;
		float hdrBloomThreshold = 0.0f;
		float hdrBloomScale = 0.0f;
		float hdrSunlightScale = 0.0f;
		float hdrSkyScale = 0.0f;

		// Cinematic Settings
		float cinematicSaturation = 0.0f;
		float cinematicBrightness = 0.0f;
		float cinematicContrast = 0.0f;

		// Tint Colors
		float3 tintColor = { 1.0f, 1.0f, 1.0f };
		float tintAmount = 0.0f;

		// Depth of Field
		float dofStrength = 0.0f;
		float dofDistance = 0.0f;
		float dofRange = 0.0f;
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

		// ImageSpace settings for each time of day
		ImageSpaceSettings imageSpaces[ColorTimes::kTotal];

		// Per-feature settings storage
		std::map<std::string, json> featureSettings;
	};

	Settings settings;
	// Cached original vanilla values for restoration
	Settings vanillaSettings;

	~WeatherWidget();

	virtual void DrawWidget() override;
	virtual void LoadSettings() override;
	virtual void SaveSettings() override;

	WeatherWidget* GetParent();
	bool HasParent() const;
	void SetWeatherValues();
	void LoadWeatherValues();
	void ApplyChanges();
	void RevertChanges();

	// New methods for per-feature settings
	void SaveFeatureSettings();
	void LoadFeatureSettings();

	// ImageSpace methods
	void LoadImageSpaceValues();
	void SetImageSpaceValues();

private:
	void DrawDALCSettings();
	void DrawWeatherColorSettings();
	void DrawCloudSettings();
	void DrawFogSettings();
	void DrawFeatureSettings();
	
	// Search functionality
	struct SearchResult {
		std::string displayName;
		std::string tabName;
		std::string settingId;
	};
	std::vector<SearchResult> searchResults;
	std::string activeTabOverride = "";
	std::string highlightedSetting = "";
	float highlightStartTime = 0.0f;
	void UpdateSearchResults();
	void NavigateToSetting(const SearchResult& result);
	bool ShouldHighlight(const std::string& settingId) const;
	void DrawImageSpaceSettings();
	void DrawProperties(std::string category, std::map<std::string, int> properties);
	void InheritFromParent(const std::string& property);
};