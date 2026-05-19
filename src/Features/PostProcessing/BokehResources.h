#pragma once

#include "Buffer.h"

struct BokehResources
{
	static constexpr int NUM_BUILTIN_SHAPES = 6;
	static constexpr int MAX_CUSTOM_SHAPES = 4;
	static constexpr int MAX_SHAPES = NUM_BUILTIN_SHAPES + MAX_CUSTOM_SHAPES;

	std::array<eastl::unique_ptr<Texture2D>, MAX_SHAPES> texBokehShapes = {};
	int numLoadedShapes = NUM_BUILTIN_SHAPES;

	const std::filesystem::path bokehShapesPath = "Data\\Shaders\\PostProcessing\\DoF\\bokehshapes";
	std::array<std::string, NUM_BUILTIN_SHAPES> builtinShapeFiles = {
		"moyheart.png",
		"hex.png",
		"fringy_soft_chr_rb.png",
		"hex_fringy_soft.png",
		"cutestar.png",
		"square.png"
	};
	std::array<std::string, NUM_BUILTIN_SHAPES> builtinShapeNames = {
		"Heart",
		"Hexagon",
		"Fringy Soft",
		"Hex Fringy Soft",
		"Star",
		"Square"
	};

	std::array<std::string, MAX_CUSTOM_SHAPES> customShapePaths = {};
	std::array<std::string, MAX_CUSTOM_SHAPES> customShapeNames = {};

	winrt::com_ptr<ID3D11SamplerState> bokehSampler = nullptr;

	void Setup();
	bool LoadCustomShape(const std::string& filePath, int slotIndex);
	ID3D11ShaderResourceView* GetShapeSRV(int shapeIndex) const;
	int GetTotalShapeCount() const { return numLoadedShapes; }

	const char* GetShapeName(int index) const;

private:
	bool LoadTextureFromFile(const std::filesystem::path& path, int index);
};
