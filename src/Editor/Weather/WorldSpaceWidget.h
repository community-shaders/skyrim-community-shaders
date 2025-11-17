#pragma once

#include "../Widget.h"

class WorldSpaceWidget : public Widget
{
public:
	RE::TESWorldSpace* worldSpace = nullptr;

	WorldSpaceWidget(RE::TESWorldSpace* a_worldspace)
	{
		form = a_worldspace;
		worldSpace = a_worldspace;
	}

	~WorldSpaceWidget();

	virtual void DrawWidget() override;
	virtual void LoadSettings() override;
	virtual void SaveSettings() override;

	struct Settings
	{
		int temp;  // Temp var to resolve macro issue in cpp file as worldspace has no settings at the moment. Can be removed once settings are added.
	};

	Settings settings;
};