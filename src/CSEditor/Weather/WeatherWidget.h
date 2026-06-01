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

		// Record form references
		RE::TESImageSpace* imageSpaceRefs[ColorTimes::kTotal] = {};
		RE::BGSVolumetricLighting* volumetricLightingRefs[ColorTimes::kTotal] = {};
		RE::BGSShaderParticleGeometryData* precipitationData = nullptr;
		RE::BGSReferenceEffect* referenceEffect = nullptr;

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

	void DrawWidget() override;
	const char* GetWidgetTypeName() const override { return "Weather"; }
	void LoadSettings() override;
	void SaveSettings() override;

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
	void DrawFogSlider(const char* id, float& prop, float min, float max, const char* fmt, bool& inheritRef, bool isInherited, bool& changed);
	void DrawFogRow(bool matches, const char* inheritKey, const char* label, const char* dayPropKey, const char* nightPropKey, float min, float max, const char* fmt, bool hasParent, WeatherWidget* parentWidget, bool& changed);
	void DrawFeatureSettings();

	// Cloud texture loading
	ID3D11ShaderResourceView* GetCloudTexture(int layerIndex);

	// Search: supply searchable entries; dropdown + tab navigation + highlight live on base Widget.
	std::vector<SearchResult> CollectSearchableSettings() const override;
	void DrawProperties(std::string category, std::map<std::string, int> properties);
	void InheritFromParent(const std::string& property);
	void InheritAllFromParent();
	void SyncInheritedValuesFromParent();
	void PropagateToChildren();
	bool pendingReinit = false;
};