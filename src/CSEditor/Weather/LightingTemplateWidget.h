#pragma once

#include "../Widget.h"
#include "DALCSettings.h"

/** @brief The BGSLightingTemplate fields a LightingTemplateWidget edits. */
struct LightingTemplateSettings
{
	float3 ambient;
	float3 directional;
	float3 fogColorNear;
	float3 fogColorFar;
	float fogNear;
	float fogFar;
	float directionalXY;
	float directionalZ;
	float directionalFade;
	float clipDist;
	float fogPower;
	float fogClamp;
	float lightFadeStart;
	float lightFadeEnd;
	DALCSettings dalc;
	bool operator==(const LightingTemplateSettings&) const = default;
};

/**
 * @brief Widget for editing interior lighting template properties.
 *
 * Exposes ambient, directional, fog, and DALC settings from a
 * BGSLightingTemplate form for live in-game editing.
 */
class LightingTemplateWidget : public SettingsWidget<LightingTemplateSettings>
{
public:
	RE::BGSLightingTemplate* lightingTemplate = nullptr;

	/**
	 * @brief Constructs a lighting template widget for the given form.
	 * @param a_lightingTemplate The BGSLightingTemplate form to edit. Must not be null.
	 */
	LightingTemplateWidget(RE::BGSLightingTemplate* a_lightingTemplate)
	{
		if (!a_lightingTemplate) {
			logger::error("LightingTemplateWidget created with null pointer");
			return;
		}
		form = a_lightingTemplate;
		lightingTemplate = a_lightingTemplate;
		LoadFromGameSettings();
		CaptureVanillaSettings();
	}

	~LightingTemplateWidget();

	/** @brief Renders the lighting template editor UI with Basic, Fog, and DALC tabs. */
	void DrawWidget() override;

	/** @brief Returns the human-readable widget type name for window sizing. */
	const char* GetWidgetTypeName() const override { return "Lighting"; }

	/** @brief Deserializes lighting template settings from the stored JSON blob. */
	void LoadSettings() override;

	/** @brief Serializes current lighting template settings to the JSON blob. */
	void SaveSettings() override;

	/** @brief Collects all searchable lighting template settings for the search dropdown. */
	std::vector<SearchResult> CollectSearchableSettings() const override;

	/** @brief Writes the current widget settings into the game's BGSLightingTemplate data. */
	void SetLightingTemplateValues();

	/** @brief Reads the game's BGSLightingTemplate data into the widget settings. */
	void LoadLightingTemplateValues();

	/** @brief Initializes widget settings from the current game lighting template data. */
	void LoadFromGameSettings();

	/** @brief Applies the current settings to the game's lighting template form. */
	void ApplyChanges() override;

private:
	void DrawDALCSettings();
	void DrawBasicSettings();
	void DrawFogSettings();
};