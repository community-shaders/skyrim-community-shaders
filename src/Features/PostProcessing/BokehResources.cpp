#include "BokehResources.h"
#include "Util.h"

#include <DirectXTex.h>

namespace
{
	constexpr float GOLDEN_RATIO_FRACTION = 0.61803398875f;
	constexpr size_t APERTURE_DIRECTION_BINS = 256;

	float Fract(float value)
	{
		return value - std::floor(value);
	}
}

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
		auto extension = path.extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
		if (extension == ".dds")
			DX::ThrowIfFailed(DirectX::LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image));
		else if (extension == ".tga")
			DX::ThrowIfFailed(DirectX::LoadFromTGAFile(path.c_str(), nullptr, image));
		else
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
	if (!BuildShapeSamples(image, index)) {
		logger::warn("BokehResources: Failed to generate gather samples for bokeh shape {}", path.string());
		texBokehShapes[index].reset();
		return false;
	}
	return true;
}

bool BokehResources::BuildShapeSamples(const DirectX::ScratchImage& image, int index)
{
	if (index < 0 || index >= MAX_SHAPES)
		return false;

	DirectX::ScratchImage converted;
	if (DirectX::IsCompressed(image.GetMetadata().format)) {
		if (FAILED(DirectX::Decompress(
				image.GetImages(),
				image.GetImageCount(),
				image.GetMetadata(),
				DXGI_FORMAT_R32G32B32A32_FLOAT,
				converted))) {
			return false;
		}
	} else if (FAILED(DirectX::Convert(
				   image.GetImages(),
				   image.GetImageCount(),
				   image.GetMetadata(),
				   DXGI_FORMAT_R32G32B32A32_FLOAT,
				   DirectX::TEX_FILTER_DEFAULT,
				   0.0f,
				   converted))) {
		return false;
	}

	const DirectX::Image* source = converted.GetImage(0, 0, 0);
	if (!source || source->width == 0 || source->height == 0)
		return false;

	std::vector<float> cumulativeWeights(source->width * source->height);
	std::array<float, APERTURE_DIRECTION_BINS> directionalRadius = {};
	float totalWeight = 0.0f;
	float coverageSum = 0.0f;
	float maxCoverageRadius = 0.0f;
	for (size_t y = 0; y < source->height; ++y) {
		auto* row = reinterpret_cast<const float*>(source->pixels + y * source->rowPitch);
		for (size_t x = 0; x < source->width; ++x) {
			const float* pixel = row + x * 4;
			const float luma = 0.2126f * pixel[0] + 0.7152f * pixel[1] + 0.0722f * pixel[2];
			// sqrt importance leaves the remaining sqrt coverage to the shader. Their product is the
			// original continuous aperture value, while low-coverage edges still receive samples.
			const float coverage = std::clamp(luma * pixel[3], 0.0f, 1.0f);
			coverageSum += coverage;
			totalWeight += std::sqrt(coverage);
			cumulativeWeights[y * source->width + x] = totalWeight;
			if (coverage > 1e-4f) {
				const float unitX = ((float(x) + 0.5f) / float(source->width)) * 2.0f - 1.0f;
				const float unitY = ((float(y) + 0.5f) / float(source->height)) * 2.0f - 1.0f;
				const float radius = std::sqrt(unitX * unitX + unitY * unitY);
				maxCoverageRadius = std::max(maxCoverageRadius, radius);
				const float angle01 = Fract(std::atan2(unitY, unitX) / (2.0f * std::numbers::pi_v<float>)+1.0f);
				const size_t direction = std::min<size_t>((size_t)(angle01 * APERTURE_DIRECTION_BINS), APERTURE_DIRECTION_BINS - 1);
				directionalRadius[direction] = std::max(directionalRadius[direction], radius);
			}
		}
	}
	if (totalWeight <= 1e-6f)
		return false;

	// Raster masks leave occasional empty angular bins, especially at low resolution. Expand each
	// direction slightly and fall back to the nearest populated ray so normalized distance remains
	// stable around corners and concave custom silhouettes.
	std::array<float, APERTURE_DIRECTION_BINS> filledDirectionalRadius = {};
	for (size_t direction = 0; direction < APERTURE_DIRECTION_BINS; ++direction) {
		float radius = 0.0f;
		for (int offset = -2; offset <= 2; ++offset) {
			const size_t neighbor = (size_t)((int(direction) + int(APERTURE_DIRECTION_BINS) + offset) % int(APERTURE_DIRECTION_BINS));
			radius = std::max(radius, directionalRadius[neighbor]);
		}
		for (size_t offset = 3; radius <= 1e-6f && offset < APERTURE_DIRECTION_BINS / 2; ++offset) {
			radius = std::max(
				directionalRadius[(direction + offset) % APERTURE_DIRECTION_BINS],
				directionalRadius[(direction + APERTURE_DIRECTION_BINS - offset) % APERTURE_DIRECTION_BINS]);
		}
		filledDirectionalRadius[direction] = std::max(radius, 1e-4f);
	}

	std::array<ShapeSample, GATHER_SAMPLE_COUNT> samples{};
	for (int i = 0; i < GATHER_SAMPLE_COUNT; ++i) {
		float x = 0.0f;
		float y = 0.0f;
		// The golden-ratio sequence keeps every prefix well distributed, so the 80 and 120 tap
		// quality modes can share one table without the lower mode collapsing into scan lines.
		const float target = Fract(0.5f + (float)i * GOLDEN_RATIO_FRACTION) * totalWeight;
		auto it = std::lower_bound(cumulativeWeights.begin(), cumulativeWeights.end(), target);
		const size_t linearIndex = std::min<size_t>(std::distance(cumulativeWeights.begin(), it), cumulativeWeights.size() - 1);
		const size_t pixelX = linearIndex % source->width;
		const size_t pixelY = linearIndex / source->width;
		// Decorrelated sub-texel positions keep repeated CDF hits from collapsing to exactly the same
		// gather offset on small masks, while remaining deterministic across runs.
		const float jitterX = Fract(0.5f + (float)i * 0.754877666f);
		const float jitterY = Fract(0.5f + (float)i * 0.569840296f);
		x = ((float(pixelX) + jitterX) / float(source->width)) * 2.0f - 1.0f;
		y = ((float(pixelY) + jitterY) / float(source->height)) * 2.0f - 1.0f;

		const float radius = std::sqrt(x * x + y * y);
		const float angle01 = Fract(std::atan2(y, x) / (2.0f * std::numbers::pi_v<float>)+1.0f);
		const size_t direction = std::min<size_t>((size_t)(angle01 * APERTURE_DIRECTION_BINS), APERTURE_DIRECTION_BINS - 1);
		const float normalizedRadius = std::clamp(radius / std::max(filledDirectionalRadius[direction], radius), 0.0f, 1.0f);
		samples[i] = { x, y, std::clamp((normalizedRadius - 0.2f) * 1.25f, 0.0f, 1.0f), normalizedRadius };
	}

	auto bufferName = std::format("BokehResources::ShapeSamples{}", index);
	shapeSampleBuffers[index] = eastl::make_unique<StructuredBuffer>(
		StructuredBufferDesc<ShapeSample>((uint64_t)GATHER_SAMPLE_COUNT, false, true), GATHER_SAMPLE_COUNT, bufferName.c_str());
	shapeSampleBuffers[index]->CreateSRV();
	shapeSampleBuffers[index]->Update(samples.data(), sizeof(samples));
	const float apertureArea = 4.0f * coverageSum / float(source->width * source->height);
	shapeSampleRadiusScales[index] = std::clamp(
		std::sqrt(std::numbers::pi_v<float> / std::max(apertureArea, 1e-4f)), 0.5f, 4.0f);
	shapeSampleMaxRadii[index] = std::max(maxCoverageRadius * shapeSampleRadiusScales[index], 1.0f);
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

ID3D11ShaderResourceView* BokehResources::GetShapeSampleSRV(int shapeIndex) const
{
	if (shapeIndex < 0 || shapeIndex >= MAX_SHAPES || !shapeSampleBuffers[shapeIndex])
		return nullptr;
	return shapeSampleBuffers[shapeIndex]->SRV();
}

float BokehResources::GetShapeSampleRadiusScale(int shapeIndex) const
{
	if (shapeIndex < 0 || shapeIndex >= MAX_SHAPES || !shapeSampleBuffers[shapeIndex])
		return 1.0f;
	return shapeSampleRadiusScales[shapeIndex];
}

float BokehResources::GetShapeSampleMaxRadius(int shapeIndex) const
{
	if (shapeIndex < 0 || shapeIndex >= MAX_SHAPES || !shapeSampleBuffers[shapeIndex])
		return 1.0f;
	return shapeSampleMaxRadii[shapeIndex];
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
