#pragma once

#include "../Widget.h"

class VolumetricLightingWidget : public Widget
{
public:
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

	void DrawWidget() override;
	const char* GetWidgetTypeName() const override { return "Volumetric Lighting"; }
	void LoadSettings() override;
	void SaveSettings() override;
	void ApplyChanges() override;
	void RevertChanges() override;
	bool HasUnsavedChanges() const override;
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
