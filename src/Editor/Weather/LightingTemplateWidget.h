#pragma once

#include "../Widget.h"

class LightingTemplateWidget : public Widget
{
public:
	RE::BGSLightingTemplate* lightingTemplate = nullptr;

	LightingTemplateWidget(RE::BGSLightingTemplate* a_lightingTemplate)
	{
		form = a_lightingTemplate;
		lightingTemplate = a_lightingTemplate;
	}

	~LightingTemplateWidget();

	virtual void DrawWidget() override;
	virtual void LoadSettings() override;
	virtual void SaveSettings() override;
};