#pragma once

#include "../Widget.h"

class CellLightingWidget : public Widget
{
public:
	CellLightingWidget(RE::TESObjectCELL* a_cell) :
		cell(a_cell)
	{
		form = a_cell;
		if (cell) {
			LoadFromGameSettings();
			vanillaSettings = settings;
			originalSettings = settings;
		}
	}

	~CellLightingWidget() override = default;

	void DrawWidget() override;
	void LoadSettings() override;
	void SaveSettings() override;
	void ApplyChanges() override;
	void RevertChanges() override;
	bool HasUnsavedChanges() const override;

	RE::TESObjectCELL* cell = nullptr;

private:
	void LoadFromGameSettings();

	struct Settings
	{
		// INTERIOR_DATA properties
		float3 ambient = { 1.0f, 1.0f, 1.0f };
		float3 directional = { 1.0f, 1.0f, 1.0f };
		float3 fogColorNear = { 1.0f, 1.0f, 1.0f };
		float3 fogColorFar = { 1.0f, 1.0f, 1.0f };
		float fogNear = 0.0f;
		float fogFar = 10000.0f;
		float fogPower = 1.0f;
		float fogClamp = 1.0f;
		float directionalFade = 1.0f;
		float clipDist = 10000.0f;
		float lightFadeStart = 3500.0f;
		float lightFadeEnd = 5000.0f;
		uint32_t directionalXY = 0;
		uint32_t directionalZ = 0;

		// Directional ambient lighting colors (DALC equivalent)
		float3 directionalXPlus = { 1.0f, 1.0f, 1.0f };
		float3 directionalXMinus = { 1.0f, 1.0f, 1.0f };
		float3 directionalYPlus = { 1.0f, 1.0f, 1.0f };
		float3 directionalYMinus = { 1.0f, 1.0f, 1.0f };
		float3 directionalZPlus = { 1.0f, 1.0f, 1.0f };
		float3 directionalZMinus = { 1.0f, 1.0f, 1.0f };
		float3 directionalSpecular = { 1.0f, 1.0f, 1.0f };
		float fresnelPower = 1.0f;

		// Inheritance flags
		bool inheritAmbientColor = false;
		bool inheritDirectionalColor = false;
		bool inheritFogColor = false;
		bool inheritFogNear = false;
		bool inheritFogFar = false;
		bool inheritDirectionalRotation = false;
		bool inheritDirectionalFade = false;
		bool inheritClipDistance = false;
		bool inheritFogPower = false;
		bool inheritFogMax = false;
		bool inheritLightFadeDistances = false;
		bool operator==(const Settings&) const = default;
	};

	Settings settings;
	Settings vanillaSettings;
	Settings originalSettings;
};
