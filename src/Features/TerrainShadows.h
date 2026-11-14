#pragma once

#include <cstdint>
#include <filesystem>

#include "Buffer.h"

/** @brief Adds heightmap-based terrain shadow casting that updates dynamically with sun position. */
struct TerrainShadows : public Feature
{
public:
	virtual inline std::string GetName() override { return "Terrain Shadows"; }
	virtual std::string GetDisplayName() override { return T("feature.terrain_shadows.name", "Terrain Shadows"); }
	/** @brief Returns the short identifier name. */
	virtual inline std::string GetShortName() override { return "TerrainShadows"; }
	virtual inline std::string_view GetShaderDefineName() override { return "TERRAIN_SHADOWS"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLandscapeAndTextures; }
	/** @brief Returns a description and list of key features for the UI summary. */
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.terrain_shadows.description", "Adds realistic shadow casting from terrain features using heightmap data to create accurate terrain shadows that enhance depth perception and visual realism."),
			{ T("feature.terrain_shadows.key_feature_1", "Heightmap-based terrain shadow calculation"),
				T("feature.terrain_shadows.key_feature_2", "Dynamic shadow updates based on sun position"),
				T("feature.terrain_shadows.key_feature_3", "Support for custom heightmap files"),
				T("feature.terrain_shadows.key_feature_4", "Real-time shadow preprocessing and computation"),
				T("feature.terrain_shadows.key_feature_5", "Integration with existing shadow systems") } };
	};

	virtual inline bool HasShaderDefine(RE::BSShader::Type) override { return true; }

	struct Settings
	{
		bool EnableTerrainShadow = true;
	} settings;

	bool needPrecompute = false;
	uint shadowUpdateIdx = 0;
	std::uint32_t handledTimeJumpRefreshGeneration = 0;

	struct HeightMapMetadata
	{
		std::wstring dir;
		std::string filename;
		std::string worldspace;
		float3 pos0, pos1;  // left-top-z=0 vs right-bottom-z=1
		float2 zRange;
	};
	std::unordered_map<std::string, HeightMapMetadata> heightmaps;
	HeightMapMetadata* cachedHeightmap;

	struct ShadowUpdateCB
	{
		float2 LightPxDir;   // direction on which light descends, from one pixel to next via dda
		float2 LightDeltaZ;  // per LightUVDir, upper penumbra and lower, should be negative
		uint StartPxCoord;
		float2 PxSize;
		float BlendWeight;
		float2 PosRange;
		float2 ZRange;
	} shadowUpdateCBData;
	static_assert(sizeof(ShadowUpdateCB) % 16 == 0);
	std::unique_ptr<ConstantBuffer> shadowUpdateCB = nullptr;

	struct alignas(16) PerFrame
	{
		uint EnableTerrainShadow;
		float3 Scale;
		float2 ZRange;
		float2 Offset;
		float ZBlur;  // world-space half-width of the shadow transition
		float3 pad0;
	};
	STATIC_ASSERT_ALIGNAS_16(PerFrame);

	/** @brief Builds the per-frame constant buffer data with terrain shadow scale, offset, and z-range. */
	PerFrame GetCommonBufferData();

	winrt::com_ptr<ID3D11ComputeShader> shadowUpdateProgram = nullptr;

	std::unique_ptr<Texture2D> texHeightMap = nullptr;
	std::unique_ptr<Texture2D> texShadowHeight = nullptr;

	/** @brief Checks whether a valid heightmap is loaded for the current worldspace. */
	bool IsHeightMapReady();

	/** @brief Scans for heightmap DDS files and creates constant buffers and compute shaders. */
	virtual void SetupResources() override;

	/**
	 * @brief Parses a heightmap DDS filename to extract worldspace metadata.
	 * @param p The filesystem path to the DDS file.
	 * @param xlodgen_style Whether the filename follows xLODGen naming conventions.
	 */
	void ParseHeightmapPath(std::filesystem::path p, bool xlodgen_style);

	/** @brief Compiles the shadow update compute shader from HLSL source. */
	void CompileComputeShaders();

	/** @brief Draws the ImGui settings panel for Terrain Shadows configuration. */
	virtual void DrawSettings() override;
	virtual bool HasDebugSection() const override { return true; }
	virtual void DrawDebugSection() override;

	/** @brief Loads heightmaps, precomputes shadow textures, and updates shadows in the early prepass. */
	virtual void EarlyPrepass() override;
	/** @brief Installs event-driven time-change hooks. */
	virtual void PostPostLoad() override;
	/** @brief Registers engine time-change and player-cell event handlers. */
	virtual void DataLoaded() override;
	/** @brief Requests celestial synchronization after loading an existing save. */
	virtual void GameLoaded() override;
	/** @brief Loads the heightmap DDS for the current worldspace if not already cached. */
	void LoadHeightmap();
	/** @brief Creates the shadow height texture after a new heightmap is loaded. */
	void Precompute();
	/**
	 * @brief Dispatches the shadow update compute shader using the current sun direction.
	 * @param a_refreshImmediately Whether to update the full map without temporal blending.
	 * @return True when the compute dispatch was submitted.
	 */
	bool UpdateShadow(bool a_refreshImmediately);

	/** @brief Binds the shadow height texture to shader resource slots for reflection rendering. */
	virtual void ReflectionsPrepass() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual inline void RestoreDefaultSettings() override { settings = {}; }
	/** @brief Releases the cached shadow update compute shader and recompiles it. */
	virtual void ClearShaderCache() override;
	virtual bool IsCore() const override { return true; };
};