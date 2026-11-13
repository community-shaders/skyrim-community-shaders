#pragma once

#include "Utils/Serialize.h"

#include <array>

class Widget;

/** @brief One DALC axis: the colour lit from its positive (max) and negative (min) side. */
struct DirectionalColor
{
	float3 min;
	float3 max;
	bool operator==(const DirectionalColor&) const = default;
};

/** @brief Editable copy of a BGSDirectionalAmbientLightingColors, shared by the weather, lighting template and cell widgets. */
struct DALCSettings
{
	DirectionalColor directional[3];
	float3 specular;
	float fresnelPower = 1.0f;
	bool operator==(const DALCSettings&) const = default;

	/** @brief Copies the game's colours into these settings. */
	void Read(const RE::BGSDirectionalAmbientLightingColors& source);

	/** @brief Writes these settings into the game's colours. */
	void Write(RE::BGSDirectionalAmbientLightingColors& target) const;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(DirectionalColor, max, min)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(DALCSettings, specular, fresnelPower, directional)

/** @brief Search ids and labels of the controls DrawDALCControls draws, in drawing order. */
namespace DALCSetting
{
	inline constexpr const char* kSpecular = "Specular";
	inline constexpr const char* kFresnelPower = "Fresnel Power";
	inline constexpr const char* kXPlus = "X+ (Right)";
	inline constexpr const char* kXMinus = "X- (Left)";
	inline constexpr const char* kYPlus = "Y+ (Front)";
	inline constexpr const char* kYMinus = "Y- (Back)";
	inline constexpr const char* kZPlus = "Z+ (Up)";
	inline constexpr const char* kZMinus = "Z- (Down)";
	inline constexpr std::array kAll{ kSpecular, kFresnelPower, kXPlus, kXMinus, kYPlus, kYMinus, kZPlus, kZMinus };
}

/**
 * @brief Draws the DALC controls matching the widget's search: specular and fresnel, then the six axis colours.
 * @param inheritedTooltip When set, the controls are greyed as inherited and this tooltip says why.
 * @return Whether any value changed.
 */
bool DrawDALCControls(Widget& widget, DALCSettings& dalc, const char* inheritedTooltip = nullptr);
