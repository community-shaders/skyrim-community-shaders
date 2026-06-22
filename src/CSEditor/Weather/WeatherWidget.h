#pragma once

#include "../Widget.h"

using TESWeather = RE::TESWeather;
using ColorTypes = TESWeather::ColorTypes;
using ColorTimes = TESWeather::ColorTimes;
using FogData = TESWeather::FogData;

/**
 * @brief Primary widget for editing TESWeather form data.
 *
 * Covers all weather properties: atmosphere colors, DALC directional
 * ambient lighting, clouds (32 layers), fog, per-TOD form references
 * (ImageSpace, volumetric lighting, precipitation, visual effect),
 * per-feature override settings, and parent-child inheritance.
 */
class WeatherWidget : public Widget
{
public:
	WeatherWidget* parent = nullptr;
	TESWeather* weather = nullptr;

	/**
	 * @brief Constructs a weather widget for the given weather form.
	 * @param a_weather The TESWeather form to edit. Must not be null.
	 */
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

	/** @brief Renders the full weather editor UI with tabs for properties, DALC, atmosphere, clouds, fog, records, and features. */
	void DrawWidget() override;

	/** @brief Returns the human-readable widget type name for window sizing. */
	const char* GetWidgetTypeName() const override { return "Weather"; }

	/** @brief Deserializes weather settings from the stored JSON blob. */
	void LoadSettings() override;

	/** @brief Serializes current weather settings to the JSON blob. */
	void SaveSettings() override;

	/** @brief Returns the parent weather widget if one is set, or null. */
	WeatherWidget* GetParent();

	/** @brief Returns true if this weather has a parent weather for inheritance. */
	bool HasParent() const;

	/** @brief Writes the current widget settings into the game's TESWeather data. */
	void SetWeatherValues();

	/** @brief Reads the game's TESWeather data into the widget settings. */
	void LoadWeatherValues();

	/** @brief Applies the current settings to the game's weather form. */
	void ApplyChanges() override;

	/** @brief Reverts settings to vanilla values and re-applies them to the game data. */
	void RevertChanges() override;

	/** @brief Deletes the saved settings file and reverts to vanilla. */
	void Delete() override;

	/** @brief Returns true if the current settings differ from the last saved state. */
	bool HasUnsavedChanges() const override;

	/** @brief Serializes per-feature override settings into the main JSON blob. */
	void SaveFeatureSettings();

	/** @brief Deserializes per-feature override settings from the main JSON blob. */
	void LoadFeatureSettings();

	// Navigation state for opening specific features
	std::string pendingFeatureNavigation = "";
	std::string pendingSettingHighlight = "";

	/**
	 * @brief Queues navigation to a specific feature setting tab and highlights the target setting.
	 * @param featureName The feature name to navigate to.
	 * @param settingName The setting name to highlight after navigation.
	 */
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