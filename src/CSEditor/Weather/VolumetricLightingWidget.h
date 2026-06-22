#pragma once

#include "../Widget.h"

/**
 * @brief Widget for editing volumetric lighting (god-ray) settings.
 *
 * Exposes intensity, custom color, density, phase function, and sampling
 * parameters from a BGSVolumetricLighting form.
 */
class VolumetricLightingWidget : public Widget
{
public:
	/**
	 * @brief Constructs a volumetric lighting widget for the given form.
	 * @param a_volumetricLighting The BGSVolumetricLighting form to edit.
	 */
	VolumetricLightingWidget(RE::BGSVolumetricLighting* a_volumetricLighting) :
		volumetricLighting(a_volumetricLighting)
	{
		form = a_volumetricLighting;
		if (volumetricLighting) {
			LoadFromGameSettings();
			vanillaSettings = settings;
			originalSettings = settings;
		}
	}

	~VolumetricLightingWidget() override = default;

	/** @brief Renders the volumetric lighting editor UI with Basic, Density, and Advanced tabs. */
	void DrawWidget() override;

	/** @brief Returns the human-readable widget type name for window sizing. */
	const char* GetWidgetTypeName() const override { return "Volumetric Lighting"; }

	/** @brief Deserializes volumetric lighting settings from the stored JSON blob. */
	void LoadSettings() override;

	/** @brief Serializes current volumetric lighting settings to the JSON blob. */
	void SaveSettings() override;

	/** @brief Writes the current settings into the game's BGSVolumetricLighting form. */
	void ApplyChanges() override;

	/** @brief Reverts settings to vanilla values and re-applies them to the game data. */
	void RevertChanges() override;

	/** @brief Returns true if the current settings differ from the last saved state. */
	bool HasUnsavedChanges() const override;

	/** @brief Collects all searchable volumetric lighting settings for the search dropdown. */
	std::vector<SearchResult> CollectSearchableSettings() const override;

	RE::BGSVolumetricLighting* volumetricLighting = nullptr;

private:
	void LoadFromGameSettings();

	struct Settings
	{
		float intensity = 1.0f;
		float customColorContribution = 0.0f;
		float red = 1.0f;
		float green = 1.0f;
		float blue = 1.0f;
		float densityContribution = 0.5f;
		float densitySize = 1.0f;
		float densityWindSpeed = 0.0f;
		float densityFallingSpeed = 0.0f;
		float phaseFunctionContribution = 0.0f;
		float phaseFunctionScattering = 0.0f;
		float samplingRangeFactor = 1.0f;
		bool operator==(const Settings&) const = default;
	};

	Settings settings;
	Settings vanillaSettings;
	Settings originalSettings;
};
