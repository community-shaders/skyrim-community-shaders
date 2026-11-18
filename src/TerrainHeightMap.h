#pragma once

#include "Buffer.h"

#include <filesystem>

/**
 * @brief Shared terrain heightmap for the current land worldspace.
 *
 * Decode normalised samples with lerp(GetPosRange().x, GetPosRange().y, sample).
 * GetZRange() returns the separate authored elevation bounds.
 */
class TerrainHeightMap
{
public:
	struct Metadata
	{
		std::wstring dir;
		std::string filename;
		std::string worldspace;
		float3 pos0, pos1;  // World-space top-left and bottom-right. Z decodes samples 0 and 1.
		float2 zRange;
	};

	/** @brief Scans heightmap directories once for DDS metadata. */
	void Discover();

	/**
	 * @brief Loads the current land worldspace's DDS if it differs from the cached map.
	 * Callers use GetLoadGeneration() to detect reloads.
	 */
	void LoadForCurrentWorldspace();

	/** @brief Cache version, incremented after each successful load. */
	uint32_t GetLoadGeneration() const { return loadGeneration; }

	/** @brief True when the cached map matches the current land worldspace. */
	bool IsReady() const;

	/** @brief True when a DDS was discovered for this worldspace, whether loaded or not. */
	bool Contains(const std::string& worldspace) const { return heightmaps.contains(worldspace); }

	Texture2D* GetTexture() const { return texHeightMap.get(); }
	ID3D11ShaderResourceView* GetSRV() const { return texHeightMap ? texHeightMap->srv.get() : nullptr; }
	const Metadata* GetCached() const { return cachedHeightmap; }

	/** @brief World-to-heightmap scale: xy maps to UV, z to normalised elevation. */
	float3 GetScale() const;
	/** @brief UV offset that aligns heightmap texel centres with terrain vertices. */
	float2 GetOffset() const;
	/** @brief Authored elevation bounds, separate from the texture's normalisation range. */
	float2 GetZRange() const;
	/** @brief World-Z endpoints for decoding normalised texture samples. */
	float2 GetPosRange() const;

private:
	/** @brief Extracts world bounds from an xLODGen or legacy DDS filename. */
	void ParseHeightmapPath(std::filesystem::path p, bool xlodgen_style);

	bool discovered = false;
	uint32_t loadGeneration = 0;
	std::unordered_map<std::string, Metadata> heightmaps;
	Metadata* cachedHeightmap = nullptr;
	std::unique_ptr<Texture2D> texHeightMap = nullptr;
};
