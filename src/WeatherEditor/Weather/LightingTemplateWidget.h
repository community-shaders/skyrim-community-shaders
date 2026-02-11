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
	};

	Settings settings;
	Settings vanillaSettings;

	~LightingTemplateWidget();

	virtual void DrawWidget() override;
	virtual void LoadSettings() override;
	virtual void SaveSettings() override;

	void SetLightingTemplateValues();
	void LoadLightingTemplateValues();
	void LoadFromGameSettings();
	void ApplyChanges();
	void RevertChanges();

private:
	void DrawDALCSettings();
	void DrawBasicSettings();
	void DrawFogSettings();
};