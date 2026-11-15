#pragma once

#include "Buffer.h"

#include <filesystem>

/**
 * @brief Worldspace-keyed terrain heightmap, shared by every feature that needs terrain elevation.
 *
 * Discovers heightmap DDS files once, then loads and caches the one matching the worldspace the
 * player is in. Texel values are normalised [0,1] and decode to world Z with
 * lerp(GetPosRange().x, GetPosRange().y, texel); @ref GetZRange is the separate authored min/max.
 */
class TerrainHeightMap
{
public:
	struct Metadata
	{
		std::wstring dir;
		std::string filename;
		std::string worldspace;
		float3 pos0, pos1;  // left-top-z=0 vs right-bottom-z=1
		float2 zRange;
	};

	/** @brief Scans the heightmap folders for DDS files. Only the first call does work. */
	void Discover();

	/**
	 * @brief Loads the DDS for the current worldspace unless it is already cached.
	 * Several features call this; each compares @ref GetLoadGeneration to detect a change it has not handled.
	 */
	void LoadForCurrentWorldspace();

	/** @brief Increments whenever a different heightmap is loaded. */
	uint32_t GetLoadGeneration() const { return loadGeneration; }

	/** @brief Whether a heightmap is loaded and belongs to the worldspace the player is in. */
	bool IsReady() const;

	/** @brief Whether a heightmap was discovered for the given worldspace, loaded or not. */
	bool Contains(const std::string& worldspace) const { return heightmaps.contains(worldspace); }

	Texture2D* GetTexture() const { return texHeightMap.get(); }
	ID3D11ShaderResourceView* GetSRV() const { return texHeightMap ? texHeightMap->srv.get() : nullptr; }
	const Metadata* GetCached() const { return cachedHeightmap; }

	/** @brief World space to normalised heightmap scale; xy maps to UV, z to elevation. */
	float3 GetScale() const;
	/** @brief World space to heightmap UV offset, aligning texel centres with terrain vertices; pairs with the xy of @ref GetScale. */
	float2 GetOffset() const;
	/** @brief The world-space Z range the texel values were authored against. */
	float2 GetZRange() const;
	/** @brief {pos0.z, pos1.z}; lerp between these with a texel to get world-space Z. */
	float2 GetPosRange() const;

private:
	/**
	 * @brief Parses a heightmap DDS filename to extract worldspace metadata.
	 * @param p The filesystem path to the DDS file.
	 * @param xlodgen_style Whether the filename follows xLODGen naming conventions.
	 */
	void ParseHeightmapPath(std::filesystem::path p, bool xlodgen_style);

	bool discovered = false;
	uint32_t loadGeneration = 0;
	std::unordered_map<std::string, Metadata> heightmaps;
	Metadata* cachedHeightmap = nullptr;
	std::unique_ptr<Texture2D> texHeightMap = nullptr;
};
