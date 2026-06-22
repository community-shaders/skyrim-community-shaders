#pragma once

#include "../Widget.h"

/**
 * @brief Widget for editing interior cell lighting properties.
 *
 * Exposes INTERIOR_DATA fields (ambient, directional, fog, DALC) and
 * lighting-template inheritance flags for a single interior cell.
 */
class CellLightingWidget : public Widget
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
			vanillaSettings = settings;
			originalSettings = settings;
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

	/** @brief Reverts settings to vanilla values and re-applies them to the game data. */
	void RevertChanges() override;

	/** @brief Returns true if the current settings differ from the last saved state. */
	bool HasUnsavedChanges() const override;

	/** @brief Collects all searchable cell lighting settings for the search dropdown. */
	std::vector<SearchResult> CollectSearchableSettings() const override;

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
