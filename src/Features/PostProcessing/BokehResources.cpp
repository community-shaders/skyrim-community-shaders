#include "BokehResources.h"
#include "Util.h"

#include <DirectXTex.h>

void BokehResources::Setup()
{
	auto device = globals::d3d::device;

	logger::debug("BokehResources: Loading built-in bokeh shapes...");
	for (int i = 0; i < NUM_BUILTIN_SHAPES; i++) {
		auto shapePath = bokehShapesPath / builtinShapeFiles[i];
		LoadTextureFromFile(shapePath, i);
	}
	numLoadedShapes = NUM_BUILTIN_SHAPES;

	// Load any previously saved custom shapes
	for (int i = 0; i < MAX_CUSTOM_SHAPES; i++) {
		if (!customShapePaths[i].empty()) {
			LoadTextureFromFile(std::filesystem::path(customShapePaths[i]), NUM_BUILTIN_SHAPES + i);
			if (texBokehShapes[NUM_BUILTIN_SHAPES + i])
				numLoadedShapes = std::max(numLoadedShapes, NUM_BUILTIN_SHAPES + i + 1);
		}
	}

	logger::debug("BokehResources: Creating sampler...");
	{
		D3D11_SAMPLER_DESC samplerDesc = {
			.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
			.AddressU = D3D11_TEXTURE_ADDRESS_MIRROR,
			.AddressV = D3D11_TEXTURE_ADDRESS_MIRROR,
			.AddressW = D3D11_TEXTURE_ADDRESS_MIRROR,
			.MaxAnisotropy = 1,
			.MinLOD = 0,
			.MaxLOD = D3D11_FLOAT32_MAX
		};
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, bokehSampler.put()));
	}
}

bool BokehResources::LoadTextureFromFile(const std::filesystem::path& path, int index)
{
	if (index < 0 || index >= MAX_SHAPES)
		return false;

	auto device = globals::d3d::device;

	DirectX::ScratchImage image;
	try {
		DX::ThrowIfFailed(DirectX::LoadFromWICFile(path.c_str(), DirectX::WIC_FLAGS_NONE, nullptr, image));
	} catch (std::runtime_error& e) {
		logger::warn("BokehResources: Error loading bokeh shape {}: {}", path.string(), e.what());
		return false;
	}

	ID3D11Resource* pRsrc = nullptr;
	try {
		DX::ThrowIfFailed(CreateTexture(device, image.GetImages(), image.GetImageCount(), image.GetMetadata(), &pRsrc));
	} catch (std::runtime_error& e) {
		logger::warn("BokehResources: Error creating texture for bokeh shape {}: {}", path.string(), e.what());
		return false;
	}

	texBokehShapes[index] = eastl::make_unique<Texture2D>(reinterpret_cast<ID3D11Texture2D*>(pRsrc));

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
		.Format = texBokehShapes[index]->desc.Format,
		.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
		.Texture2D = {
			.MostDetailedMip = 0,
			.MipLevels = 1 }
	};
	texBokehShapes[index]->CreateSRV(srvDesc);
	return true;
}

bool BokehResources::LoadCustomShape(const std::string& filePath, int slotIndex)
{
	if (slotIndex < 0 || slotIndex >= MAX_CUSTOM_SHAPES)
		return false;

	// Validate file extension
	auto ext = std::filesystem::path(filePath).extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
	if (ext != ".png" && ext != ".dds" && ext != ".jpg" && ext != ".jpeg" && ext != ".bmp" && ext != ".tga") {
		logger::warn("BokehResources: Unsupported file format: {}", ext);
		return false;
	}

	// Validate that path doesn't traverse outside expected directories
	auto absPath = std::filesystem::absolute(filePath);
	if (!std::filesystem::exists(absPath)) {
		logger::warn("BokehResources: File does not exist: {}", absPath.string());
		return false;
	}

	int index = NUM_BUILTIN_SHAPES + slotIndex;
	if (LoadTextureFromFile(absPath, index)) {
		customShapePaths[slotIndex] = absPath.string();
		// Derive display name from filename without extension
		customShapeNames[slotIndex] = absPath.stem().string();
		numLoadedShapes = std::max(numLoadedShapes, index + 1);
		return true;
	}
	return false;
}

ID3D11ShaderResourceView* BokehResources::GetShapeSRV(int shapeIndex) const
{
	if (shapeIndex < 0 || shapeIndex >= MAX_SHAPES)
		return nullptr;
	if (!texBokehShapes[shapeIndex])
		return nullptr;
	return texBokehShapes[shapeIndex]->srv.get();
}

const char* BokehResources::GetShapeName(int index) const
{
	if (index < 0 || index >= MAX_SHAPES)
		return "Unknown";
	if (index < NUM_BUILTIN_SHAPES)
		return builtinShapeNames[index].c_str();
	int customIdx = index - NUM_BUILTIN_SHAPES;
	if (customIdx < MAX_CUSTOM_SHAPES && !customShapeNames[customIdx].empty())
		return customShapeNames[customIdx].c_str();
	return "Empty Slot";
}
