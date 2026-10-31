#pragma once

#include "../Widget.h"
#include "DALCSettings.h"

/** @brief The INTERIOR_DATA fields a CellLightingWidget edits. */
struct CellLightingSettings
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

	DALCSettings dalc;

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
	bool operator==(const CellLightingSettings&) const = default;
};

/**
 * @brief Widget for editing interior cell lighting properties.
 *
 * Exposes INTERIOR_DATA fields (ambient, directional, fog, DALC) and
 * lighting-template inheritance flags for a single interior cell.
 */
class CellLightingWidget : public SettingsWidget<CellLightingSettings>
{
public:
	/**
	 * @brief Constructs a cell lighting widget for the given cell.
	 * @param a_cell The interior cell whose lighting data will be edited.
	 */
	CellLightingWidget(RE::TESObjectCELL* a_cell) :
		cell(a_cell)
	{
		form = a_cell;
		if (cell) {
			LoadFromGameSettings();
			CaptureVanillaSettings();
		}
	}

	~CellLightingWidget() override = default;

	/** @brief Renders the cell lighting editor UI with Basic, Fog, DALC, and Inheritance tabs. */
	void DrawWidget() override;

	/** @brief Returns the human-readable widget type name for window sizing. */
	const char* GetWidgetTypeName() const override { return "Cell Lighting"; }

	/** @brief Deserializes cell lighting settings from the stored JSON blob. */
	void LoadSettings() override;

	/** @brief Serializes current cell lighting settings to the JSON blob. */
	void SaveSettings() override;

	/** @brief Writes the current settings into the game's cell lighting data. */
	void ApplyChanges() override;

	/** @brief Collects all searchable cell lighting settings for the search dropdown. */
	std::vector<SearchResult> CollectSearchableSettings() const override;

	RE::TESObjectCELL* cell = nullptr;

private:
	void LoadFromGameSettings();
};
