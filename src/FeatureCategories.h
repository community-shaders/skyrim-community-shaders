#pragma once
#include <array>
#include <string_view>

/** @brief Canonical category labels used to group features in the menu UI. */
namespace FeatureCategories
{
	inline constexpr std::string_view kCharacters = "Characters";
	inline constexpr std::string_view kDisplay = "Display";
	inline constexpr std::string_view kGrass = "Grass";
	inline constexpr std::string_view kLandscapeAndTextures = "Landscape & Textures";
	inline constexpr std::string_view kLighting = "Lighting";
	inline constexpr std::string_view kMaterials = "Materials";
	inline constexpr std::string_view kOther = "Other";
	inline constexpr std::string_view kPostProcessing = "Post-Processing";
	inline constexpr std::string_view kSky = "Sky";
	inline constexpr std::string_view kUtility = "Utility";
	inline constexpr std::string_view kWater = "Water";

	/** @brief Order in which categories are listed in feature menus. */
	inline constexpr std::array kMenuOrder = { kDisplay, kUtility, kCharacters, kGrass, kLighting, kMaterials,
		kPostProcessing, kSky, kLandscapeAndTextures, kWater, kOther };
}
