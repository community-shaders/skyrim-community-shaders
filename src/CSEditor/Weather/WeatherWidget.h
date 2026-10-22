#pragma once

#include "../Widget.h"
#include "DALCSettings.h"

using TESWeather = RE::TESWeather;
using ColorTypes = TESWeather::ColorTypes;
using ColorTimes = TESWeather::ColorTimes;
using FogData = TESWeather::FogData;

struct WeatherAtmosphere
{
	float3 colorTimes[ColorTimes::kTotal];
	bool operator==(const WeatherAtmosphere& o) const { return std::equal(std::begin(colorTimes), std::end(colorTimes), std::begin(o.colorTimes)); }
};

struct WeatherCloud
{
	int cloudLayerSpeedY;
	int cloudLayerSpeedX;
	float3 color[ColorTimes::kTotal];
	float cloudAlpha[ColorTimes::kTotal];
	bool enabled = true;
	std::string texturePath;
	bool operator==(const WeatherCloud& o) const
	{
		return cloudLayerSpeedY == o.cloudLayerSpeedY &&
		       cloudLayerSpeedX == o.cloudLayerSpeedX &&
		       std::equal(std::begin(color), std::end(color), std::begin(o.color)) &&
		       std::equal(std::begin(cloudAlpha), std::end(cloudAlpha), std::begin(o.cloudAlpha)) &&
		       enabled == o.enabled &&
		       texturePath == o.texturePath;
	}
};

/** @brief The TESWeather fields a WeatherWidget edits. */
struct WeatherSettings
{
	std::string parent = "None";
	// Per-parameter inheritance flags (one per parameter, not per TOD)
	std::map<std::string, bool> inheritFlags;
	std::map<std::string, int> weatherProperties;
	std::map<std::string, float3> weatherColors;
	std::map<std::string, float> fogProperties;

	WeatherAtmosphere atmosphereColors[ColorTypes::kTotal];
	DALCSettings dalc[ColorTimes::kTotal];
	WeatherCloud clouds[TESWeather::kTotalLayers];

	// Record form references
	RE::TESImageSpace* imageSpaceRefs[ColorTimes::kTotal] = {};
	RE::BGSVolumetricLighting* volumetricLightingRefs[ColorTimes::kTotal] = {};
	RE::BGSShaderParticleGeometryData* precipitationData = nullptr;
	RE::BGSReferenceEffect* referenceEffect = nullptr;

	bool operator==(const WeatherSettings& o) const;
};

/**
 * @brief Primary widget for editing TESWeather form data.
 *
 * Covers all weather properties: atmosphere colors, DALC directional
 * ambient lighting, clouds (32 layers), fog, per-TOD form references
 * (ImageSpace, volumetric lighting, precipitation, visual effect),
 * and parent-child inheritance.
 */
class WeatherWidget : public SettingsWidget<WeatherSettings>
{
public:
	using DALC = DALCSettings;
	using Atmosphere = WeatherAtmosphere;
	using Cloud = WeatherCloud;

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
		CaptureVanillaSettings();
	}

	// Null entries cache failed loads until the cache is cleared.
	std::map<std::string, winrt::com_ptr<ID3D11ShaderResourceView>> cloudTextureCache;

	/** @brief Renders the full weather editor UI with tabs for properties, DALC, atmosphere, clouds, fog, and records. */
	void DrawWidget() override;

	/** @brief Opens and focuses the window on its Scene Manager tab. */
	void OpenSceneManagerTab();

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

	/** @brief Whether the editor lets weathers inherit and this one has a parent to inherit from. */
	bool InheritsFromParent() const;

	/** @brief Writes the current widget settings into the game's TESWeather data. */
	void SetWeatherValues();

	/** @brief Reads the game's TESWeather data into the widget settings. */
	void LoadWeatherValues();

	/** @brief Applies the current settings to the game's weather form. */
	void ApplyChanges() override;

	/** @brief Reverted form references are only read when the weather reinitializes. */
	void RevertChanges() override
	{
		pendingReinit = true;
		SettingsWidget::RevertChanges();
	}

	/** @brief Deletes the saved settings file and reverts to vanilla. */
	void Delete() override;

	/** @brief Restored form references are only read when the weather reinitializes. */
	void RestoreEdits(const UndoHistory::Snapshot& snapshot) override
	{
		pendingReinit = true;
		SettingsWidget::RestoreEdits(snapshot);
	}

private:
	void NavigateToSearchResult(const SearchResult& result) override;
	void InitializeInheritFlags();
	void DrawDALCSettings();
	void DrawWeatherColorSettings();
	void DrawCloudSettings();
	void DrawFogSettings();
	void DrawFogSlider(const char* id, float& prop, float min, float max, const char* fmt, bool& inheritRef, bool isInherited, bool& changed);
	void DrawFogRow(bool matches, const char* inheritKey, const char* label, const char* dayPropKey, const char* nightPropKey, float min, float max, const char* fmt, bool hasParent, WeatherWidget* parentWidget, bool& changed);

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