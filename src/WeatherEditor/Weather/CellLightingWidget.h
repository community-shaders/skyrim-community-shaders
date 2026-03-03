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

	// Public types required by NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT macro
	struct DALC
	{
		float3 xPlus = { 1.0f, 1.0f, 1.0f };
		float3 xMinus = { 1.0f, 1.0f, 1.0f };
		float3 yPlus = { 1.0f, 1.0f, 1.0f };
		float3 yMinus = { 1.0f, 1.0f, 1.0f };
		float3 zPlus = { 1.0f, 1.0f, 1.0f };
		float3 zMinus = { 1.0f, 1.0f, 1.0f };
		float3 specular = { 1.0f, 1.0f, 1.0f };
		float fresnelPower = 1.0f;
		bool operator==(const DALC&) const = default;
	};

	struct Inherit
	{
		bool ambientColor = false;
		bool directionalColor = false;
		bool fogColor = false;
		bool fogNear = false;
		bool fogFar = false;
		bool directionalRotation = false;
		bool directionalFade = false;
		bool clipDistance = false;
		bool fogPower = false;
		bool fogMax = false;
		bool lightFadeDistances = false;
		bool operator==(const Inherit&) const = default;
	};

	struct Settings
	{
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
		DALC dalc;
		Inherit inherit;
		bool operator==(const Settings&) const = default;
	};

private:
	void LoadFromGameSettings();

	RE::TESObjectCELL* cell = nullptr;

	Settings settings;
	Settings vanillaSettings;
	Settings originalSettings;
};
