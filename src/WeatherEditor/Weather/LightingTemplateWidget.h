#pragma once

#include "../Widget.h"

class LightingTemplateWidget : public Widget
{
public:
	LightingTemplateWidget(RE::BGSLightingTemplate* a_lightingTemplate) :
		lightingTemplate(a_lightingTemplate)
	{
		if (!a_lightingTemplate) {
			logger::error("LightingTemplateWidget created with null pointer");
			return;
		}
		form = a_lightingTemplate;
		LoadFromGameSettings();
		vanillaSettings = settings;
		originalSettings = settings;
	}

	~LightingTemplateWidget() override = default;

	void DrawWidget() override;
	void LoadSettings() override;
	void SaveSettings() override;
	void ApplyChanges() override;
	void RevertChanges() override;
	bool HasUnsavedChanges() const override;

	RE::BGSLightingTemplate* GetLightingTemplate() const { return lightingTemplate; }

	// Public types required by NLOHMANN macros
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

private:
	void LoadFromGameSettings();
	bool DrawBasicSettings();
	bool DrawFogSettings();
	bool DrawDALCSettings();

	RE::BGSLightingTemplate* lightingTemplate = nullptr;

	Settings settings;
	Settings vanillaSettings;
	Settings originalSettings;
};