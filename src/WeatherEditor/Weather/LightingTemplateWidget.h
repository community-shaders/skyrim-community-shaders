#pragma once

#include "../Widget.h"

class LightingTemplateWidget : public Widget
{
public:
	RE::BGSLightingTemplate* lightingTemplate = nullptr;

	LightingTemplateWidget(RE::BGSLightingTemplate* a_lightingTemplate)
	{
		if (!a_lightingTemplate) {
			logger::error("LightingTemplateWidget created with null pointer");
			return;
		}
		form = a_lightingTemplate;
		lightingTemplate = a_lightingTemplate;
		LoadFromGameSettings();
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
		bool operator==(const DALC&) const = default;
	};

	struct Settings
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
		DALC dalc;
		bool operator==(const Settings&) const = default;
	};

	Settings settings;
	Settings vanillaSettings;
	Settings originalSettings;

	~LightingTemplateWidget();

	void DrawWidget() override;
	const char* GetWidgetTypeName() const override { return "Lighting"; }
	void LoadSettings() override;
	void SaveSettings() override;
	bool HasUnsavedChanges() const override;
	std::vector<SearchResult> CollectSearchableSettings() const override;

	void SetLightingTemplateValues();
	void LoadLightingTemplateValues();
	void LoadFromGameSettings();
	void ApplyChanges() override;
	void RevertChanges() override;

private:
	void DrawDALCSettings();
	void DrawBasicSettings();
	void DrawFogSettings();
};