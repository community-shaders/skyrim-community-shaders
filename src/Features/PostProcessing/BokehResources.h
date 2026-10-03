#pragma once

#include "Buffer.h"

namespace DirectX
{
	class ScratchImage;
}

struct BokehResources
{
	static constexpr int NUM_BUILTIN_SHAPES = 6;
	static constexpr int MAX_CUSTOM_SHAPES = 4;
	static constexpr int MAX_SHAPES = NUM_BUILTIN_SHAPES + MAX_CUSTOM_SHAPES;
	static constexpr int GATHER_SAMPLE_COUNT = 120;

	struct alignas(16) ShapeSample
	{
		float x;
		float y;
		float radialWeight;
		// Radius in the aperture's canonical unit disc. This deliberately differs from length(xy):
		// polygon corners may extend beyond radius one while still belonging to the outer ring.
		float normalizedDistance;
	};
	static_assert(sizeof(ShapeSample) == 16);

	std::array<eastl::unique_ptr<Texture2D>, MAX_SHAPES> texBokehShapes = {};
	// Importance-sampled positions generated once when each texture is loaded. The shader still
	// samples the source texture for continuous coverage and colour; this table prevents sparse
	// silhouettes from throwing away most of the fixed gather taps.
	std::array<eastl::unique_ptr<StructuredBuffer>, MAX_SHAPES> shapeSampleBuffers = {};
	std::array<float, MAX_SHAPES> shapeSampleRadiusScales = {};
	std::array<float, MAX_SHAPES> shapeSampleMaxRadii = {};
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
	ID3D11ShaderResourceView* GetShapeSampleSRV(int shapeIndex) const;
	float GetShapeSampleRadiusScale(int shapeIndex) const;
	float GetShapeSampleMaxRadius(int shapeIndex) const;
	int GetTotalShapeCount() const { return numLoadedShapes; }

	const char* GetShapeName(int index) const;

private:
	bool LoadTextureFromFile(const std::filesystem::path& path, int index);
	bool BuildShapeSamples(const DirectX::ScratchImage& image, int index);
};
