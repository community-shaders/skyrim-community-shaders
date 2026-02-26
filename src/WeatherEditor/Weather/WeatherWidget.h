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
		if (!a_weather) {
			logger::error("WeatherWidget created with null pointer");
			return;
		}
		form = a_weather;
		weather = a_weather;
		LoadWeatherValues();
		InitializeInheritFlags();
		vanillaSettings = settings;
		originalSettings = settings;
	}

	struct DirectionalColor
	{
		float3 min;
		float3 max;
		bool operator==(const DirectionalColor&) const = default;
	};

	struct DALC
	{
		DirectionalColor directional[3];
		float3 specular;
		float fresnelPower;
		bool operator==(const DALC& o) const { return std::equal(std::begin(directional), std::end(directional), std::begin(o.directional)) && specular == o.specular && fresnelPower == o.fresnelPower; }
	};

	struct Atmosphere
	{
		float3 colorTimes[ColorTimes::kTotal];
		bool operator==(const Atmosphere& o) const { return std::equal(std::begin(colorTimes), std::end(colorTimes), std::begin(o.colorTimes)); }
	};

	struct Cloud
	{
		int cloudLayerSpeedY;
		int cloudLayerSpeedX;
		float3 color[ColorTimes::kTotal];
		float cloudAlpha[ColorTimes::kTotal];
		bool enabled = true;
		std::string texturePath;
		bool operator==(const Cloud& o) const
		{
			return cloudLayerSpeedY == o.cloudLayerSpeedY &&
			       cloudLayerSpeedX == o.cloudLayerSpeedX &&
			       std::equal(std::begin(color), std::end(color), std::begin(o.color)) &&
			       std::equal(std::begin(cloudAlpha), std::end(cloudAlpha), std::begin(o.cloudAlpha)) &&
			       enabled == o.enabled &&
			       texturePath == o.texturePath;
		}
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

		bool operator==(const ImageSpaceSettings&) const = default;
	};

	struct Settings
	{
		std::string parent = "None";
		// Per-parameter inheritance flags (one per parameter, not per TOD)
		std::map<std::string, bool> inheritFlags;
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

		bool operator==(const Settings& o) const;
	};

	Settings settings;
	Settings originalSettings;
	Settings vanillaSettings;

	// Cloud texture cache (layer index -> SRV)
	std::map<int, ID3D11ShaderResourceView*> cloudTextureCache;

	~WeatherWidget();

	virtual void DrawWidget() override;
	virtual void LoadSettings() override;
	virtual void SaveSettings() override;

	WeatherWidget* GetParent();
	bool HasParent() const;
	void SetWeatherValues();
	void LoadWeatherValues();
	void ApplyChanges() override;
	void RevertChanges() override;
	void Delete() override;
	bool HasUnsavedChanges() const override;

	// New methods for per-feature settings
	void SaveFeatureSettings();
	void LoadFeatureSettings();

	// Navigation state for opening specific features
	std::string pendingFeatureNavigation = "";
	std::string pendingSettingHighlight = "";
	void NavigateToFeatureSetting(const std::string& featureName, const std::string& settingName);

private:
	void InitializeInheritFlags();
	void DrawDALCSettings();
	void DrawWeatherColorSettings();
	void DrawCloudSettings();
	void DrawFogSettings();
	void DrawFeatureSettings();

	// Cloud texture loading
	ID3D11ShaderResourceView* GetCloudTexture(int layerIndex);

	// Search functionality
	struct SearchResult
	{
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
	void DrawProperties(std::string category, std::map<std::string, int> properties);
	void InheritFromParent(const std::string& property);
	void InheritAllFromParent();
};