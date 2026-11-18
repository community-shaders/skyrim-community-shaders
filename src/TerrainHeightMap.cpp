#include "TerrainHeightMap.h"

#include <DirectXTex.h>
#include <pystring/pystring.h>

#include "Globals.h"
#include "Util.h"

void TerrainHeightMap::ParseHeightmapPath(std::filesystem::path p, bool xlodgen_style)
{
	auto filename = p.filename();
	if (filename.extension() != ".dds")
		return;
	logger::debug("Found dds: {}", filename.string());

	auto splitstr = pystring::split(filename.stem().string(), ".");
	if (splitstr.size() != (xlodgen_style ? 9 : 10)) {
		logger::debug("{} has incorrect number ({}) of fields", filename.string(), splitstr.size());
		return;
	}

	bool middle_check = xlodgen_style ? ((splitstr[1] == "Terrain") && (splitstr[2] == "HeightMap")) : (splitstr[1] == "HeightMap");
	if (middle_check) {
		Metadata metadata;
		try {
			if (xlodgen_style) {
				metadata.worldspace = splitstr[0];
				metadata.pos0.x = std::stoi(splitstr[3]) * 4096.f;
				metadata.pos1.y = std::stoi(splitstr[4]) * 4096.f;
				metadata.pos1.x = (static_cast<float>(std::stoi(splitstr[5])) + 1.f) * 4096.f;
				metadata.pos0.y = (static_cast<float>(std::stoi(splitstr[6])) + 1.f) * 4096.f;
				metadata.pos0.z = -32767 * 8.f;
				metadata.pos1.z = 32767 * 8.f;
				metadata.zRange.x = std::stoi(splitstr[7]) * 8.f;
				metadata.zRange.y = std::stoi(splitstr[8]) * 8.f;
			} else {
				metadata.worldspace = splitstr[0];
				metadata.pos0.x = std::stoi(splitstr[2]) * 4096.f;
				metadata.pos1.y = std::stoi(splitstr[3]) * 4096.f;
				metadata.pos1.x = (static_cast<float>(std::stoi(splitstr[4])) + 1.f) * 4096.f;
				metadata.pos0.y = (static_cast<float>(std::stoi(splitstr[5])) + 1.f) * 4096.f;
				metadata.pos0.z = std::stoi(splitstr[6]) * 8.f;
				metadata.pos1.z = std::stoi(splitstr[7]) * 8.f;
				metadata.zRange.x = std::stoi(splitstr[8]) * 8.f;
				metadata.zRange.y = std::stoi(splitstr[9]) * 8.f;
			}
		} catch (std::exception& e) {
			logger::debug("Failed to parse {}. Error: {}", filename.string(), e.what());
			return;
		}

		if (!(metadata.pos0.x < metadata.pos1.x) || !(metadata.pos1.y < metadata.pos0.y) ||
			metadata.pos0.z == metadata.pos1.z || !(metadata.zRange.x < metadata.zRange.y)) {
			logger::warn("Skipping height map {}: invalid coordinate spans", filename.string());
			return;
		}

		metadata.dir = p.parent_path().wstring();
		metadata.filename = filename.string();

		if (heightmaps.contains(metadata.worldspace))
			logger::warn("{} has more than one height maps!", metadata.worldspace);
		heightmaps[metadata.worldspace] = metadata;

		logger::info("{} loaded.", filename.string());
	} else
		logger::debug("{} has unknown type ({})", filename.string(), splitstr[1]);
}

void TerrainHeightMap::Discover()
{
	if (discovered)
		return;
	discovered = true;

	logger::debug("Listing xLODGen height maps...");
	{
		std::filesystem::path texture_dir{ L"Data\\textures\\Terrain\\" };
		std::error_code ec;
		for (auto const& dir_entry : std::filesystem::directory_iterator{ texture_dir, ec }) {
			auto dir_path = dir_entry.path();
			if (!std::filesystem::is_directory(dir_path, ec))
				continue;

			for (auto const& sub_dir_entry : std::filesystem::directory_iterator{ dir_path, ec })
				ParseHeightmapPath(sub_dir_entry.path(), true);
		}
	}

	logger::debug("Listing height maps...");
	{
		std::filesystem::path texture_dir{ L"Data\\textures\\heightmaps\\" };
		std::error_code ec;
		for (auto const& dir_entry : std::filesystem::directory_iterator{ texture_dir, ec })
			ParseHeightmapPath(dir_entry.path(), false);
	}
}

bool TerrainHeightMap::IsReady() const
{
	if (auto tes = RE::TES::GetSingleton())
		if (auto worldspace = Util::GetLandDataWorldspace(tes->GetRuntimeData2().worldSpace))
			return cachedHeightmap && cachedHeightmap->worldspace == worldspace->GetFormEditorID();
	return false;
}

void TerrainHeightMap::LoadForCurrentWorldspace()
{
	auto tes = globals::game::tes;
	if (!tes)
		return;

	auto worldspace = Util::GetLandDataWorldspace(tes->GetRuntimeData2().worldSpace);

	if (!worldspace)
		return;

	std::string worldspace_name = worldspace->GetFormEditorID();
	// Keep the previous world's map cached when no replacement exists.
	if (!heightmaps.contains(worldspace_name))
		return;

	if (cachedHeightmap && cachedHeightmap->worldspace == worldspace_name)
		return;

	auto device = globals::d3d::device;

	logger::debug("Loading height map...");
	{
		auto& target_heightmap = heightmaps[worldspace_name];

		// Skip a file that already failed instead of reloading it every frame.
		if (target_heightmap.loadFailed)
			return;
		target_heightmap.loadFailed = true;

		DirectX::ScratchImage image;
		try {
			std::filesystem::path path{ target_heightmap.dir };
			path /= target_heightmap.filename;

			DX::ThrowIfFailed(LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image));
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}

		try {
			const auto& metadata = image.GetMetadata();
			UINT formatSupport = 0;
			DX::ThrowIfFailed(device->CheckFormatSupport(metadata.format, &formatSupport));
			constexpr UINT requiredSupport = D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE;
			if (metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D || metadata.arraySize != 1 ||
				(formatSupport & requiredSupport) != requiredSupport) {
				logger::error("Unsupported height map {}: expected a sampleable single-slice 2D texture", target_heightmap.filename);
				return;
			}

			winrt::com_ptr<ID3D11Resource> resource;
			DX::ThrowIfFailed(CreateTexture(device, image.GetImages(), image.GetImageCount(), metadata, resource.put()));
			auto texture = std::make_unique<Texture2D>(resource.as<ID3D11Texture2D>().detach(), "TerrainHeightMap::HeightMap");

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
				.Format = texture->desc.Format,
				.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
				.Texture2D = {
					.MostDetailedMip = 0,
					.MipLevels = 1 }
			};
			texture->CreateSRV(srvDesc);

			texHeightMap = std::move(texture);
			cachedHeightmap = &target_heightmap;
			target_heightmap.loadFailed = false;
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}
	}

	++loadGeneration;
}

float3 TerrainHeightMap::GetScale() const
{
	if (!cachedHeightmap)
		return {};

	const auto invScale = cachedHeightmap->pos1 - cachedHeightmap->pos0;
	return { 1.f / invScale.x, 1.f / invScale.y, 1.f / invScale.z };
}

float2 TerrainHeightMap::GetOffset() const
{
	if (!cachedHeightmap)
		return {};

	// Texel centres lie on terrain vertices anchored at the south-west corner.
	const auto scale = GetScale();
	const float2 halfTexel = texHeightMap ? float2{ 0.5f / texHeightMap->desc.Width, -0.5f / texHeightMap->desc.Height } : float2{};
	return float2{ -cachedHeightmap->pos0.x * scale.x, -cachedHeightmap->pos0.y * scale.y } + halfTexel;
}

float2 TerrainHeightMap::GetPosRange() const
{
	return cachedHeightmap ? float2{ cachedHeightmap->pos0.z, cachedHeightmap->pos1.z } : float2{};
}

float2 TerrainHeightMap::GetZRange() const
{
	return cachedHeightmap ? cachedHeightmap->zRange : float2{};
}
