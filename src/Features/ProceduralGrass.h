#pragma once

#include "ProceduralGrass/GrassCellCache.h"
#include "ProceduralGrass/PGrassCommon.h"
#include "ProceduralGrass/PGrassRenderer.h"

#include <limits>

struct ProceduralGrass : Feature
{
public:
	virtual inline std::string GetName() override { return "Procedural Grass"; }
	virtual std::string GetDisplayName() override { return T("feature.procedural_grass.name", "Procedural Grass"); }
	virtual inline std::string GetShortName() override { return "ProceduralGrass"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kGrass; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.procedural_grass.description", "Generates configurable, dynamically lit grass across nearby terrain."),
			{ T("feature.procedural_grass.key_feature_1", "Dynamic grass rendering with realistic lighting and shading"),
				T("feature.procedural_grass.key_feature_2", "Configurable grass density and distribution"),
				T("feature.procedural_grass.key_feature_3", "Real-time grass animation with wind effects") } };
	};

	// Clump grid sizes in world units; the generator divides by the grid size.
	static constexpr float MinClumpGridSize = 16.0f;
	static constexpr float MaxClumpGridSize = 512.0f;

	/** @brief Fields a grass type can override per landscape-texture variant; Settings holds the base values. */
	struct GrassTypeFields
	{
		float grassHeight = 100.0f;
		float grassWidth = 0.6f;
		float stiffness = 0.24f;
		float tipWeight = 0.54f;
		float mid = 0.73f;
		float rotationalStiffness = 1.0f;
		float ao = 0.10f;                                          // Minimum blade AO
		float specular = 0.04f;                                    // Specular reflectance at normal incidence
		float specularAnisotropy = 0.6f;                           // OpenPBR specular anisotropy, stretched across the blade by its veins
		float sheenStrength = 0.16f;                               // OpenPBR fuzz weight: coverage of the blade hairs and wax bloom
		float sheenRoughness = 0.5f;                               // OpenPBR fuzz roughness: low is fibre-like, high is dusty
		float sheenTint = 0.5f;                                    // OpenPBR fuzz colour: 0 is white, 1 is the blade's hue
		float curvedNormalStrength = 0.4f;                         // Edge tilt of the rolled cross-section as a fraction of 90 degrees
		float2 subsurfaceOpacity = float2(0.5f, 0.30f);            // Opaque fraction of the base substrate, base to tip
		float3 grassSubsurfaceTint = float3(0.50f, 0.54f, 0.38f);  // Scattering albedo at the stabilized blade colour
		float grassTransmissionStrength = 1.5f;                    // Thin-subsurface anisotropy + 1; 0..2 splits scattering into reflection and transmission
		float3 baseMinTipRoughness = float3(0.40f, 0.30f, 0.40f);
		float tipRoughnessStart = 0.75f;
		float clumpAOStrength = 0.5f;
		float3 baseColor = float3(0.115f, 0.135f, 0.080f);
		float3 tipColor = float3(0.340f, 0.380f, 0.240f);
		float grassColorHueVariation = 0.45f;                   // Per-blade hue variation
		float grassColorValueVariation = 0.18f;                 // Per-blade brightness variation
		float grassColorTipDryStrength = 0.20f;                 // Tip dry-tint strength
		float grassColorMottleStrength = 0.15f;                 // Along-blade mottle strength
		float3 grassColorCool = float3(0.83f, 0.89f, 0.80f);    // Cool blade tint
		float3 grassColorWarm = float3(1.02f, 1.00f, 0.84f);    // Warm blade tint
		float3 grassColorTipDry = float3(1.04f, 1.02f, 0.88f);  // Dry tip tint
		float grassClumpColorStrength = 0.6f;
		float grassBounceStrength = 0.35f;
		float3 grassBounceColor = float3(0.55f, 0.42f, 0.24f);
		float grassAmbientDesat = 0.5f;
		float clumpGridSize = 64.0f;        // Average spacing between Voronoi clump centres, in world units; about one tuft
		float clumpDistanceFactor = 0.25f;  // Pull toward the clump centre
		float clumpFacingFactor = 0.25f;    // Positive turns blades away from the clump centre, negative toward it
		float clumpLeanFactor = 0.6f;       // Turns a clump's blades toward one shared direction
		float clumpHeightFactor = 0.5f;     // Share of each blade's height taken from its clump
		float grassMinSlope = 0.0f;
		float grassMaxSlope = 40.0f;    // Degrees. 90 never culls grass.
		float grassSlopeFacing = 0.2f;  // Downhill lean strength
	};

	struct Settings : GrassTypeFields
	{
		bool Enabled = true;
		int32_t Quality = 2;  // QualityDensities index

		float grassBaseAO = 0.35f;

		float grassCanopySkyOcclusion = 1.0f;
		float grassDensityAO = 0.2f;

		// Terrain blend and shadow
		float grassTerrainBlendStrength = 1.0f;
		float grassTerrainBlendHeight = 2.0f;
		float grassTerrainBlendNormal = 0.8f;
		float grassTerrainBlendRough = 0.7f;

		// Wind / animation
		float windAngle = 0.0f;
		float windSpeed = 0.4f;

		// Placement
		float grassMapEdgeNoise = 64.0f;
		float grassViewThicken = 0.4f;  // Edge-on blade widening. 0 disables it.

		// Per-LOD densities
		// Defaults match the High preset (see ApplyDensityPreset): Mid and Low use High's lattice.
		int midGrassDensity = 256;
		int lowGrassDensity = 256;

		struct GrassTypeDef
		{
			float weight = 1.0f;
			bool noGrass = false;
			nlohmann::json overrides = nlohmann::json::object();
		};

		// Keyed by "plugin|0xLOCALID" (LandTextureKey).
		std::unordered_map<std::string, std::vector<GrassTypeDef>> textureTypes;

		// Debug
		bool debugIgnoreGrassMap = false;
		bool debugDisableAllCulls = false;
		bool debugTierView = false;
		bool debugIgnorePreProcessedFlag = true;
		bool debugDisableVanillaGrass = false;
	};

	Settings settings;

	virtual void DrawSettings() override;
	virtual bool HasDebugSection() const override { return true; }

	/** @brief Draws the per-type overrides and landscape-texture mix editor. */
	void DrawGrassTypeEditor();
	/** @brief Draws debug toggles and the latest completed diagnostic readbacks. */
	virtual void DrawDebugSection() override;

	/** @brief Draws the per-field override editor for one grass type variant. */
	bool DrawTypeOverrides(nlohmann::json& ov) const;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void RestoreDefaultSettings() override;

	virtual void PostPostLoad() override;
	virtual void DataLoaded() override;
	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;

	void DeferredRendering() const;

	struct Main_RenderShadowmasks_UpdateCamera
	{
		static void thunk(RE::BSGraphics::State* state, RE::NiCamera* camera, bool flag);
		static inline REL::Relocation<decltype(thunk)> func;
	};

private:
	enum class Quality : uint8_t
	{
		Low = 0,
		Medium = 1,
		High = 2,
		Ultra = 3,
		Count = 4
	};

	const char* QualityNames[static_cast<uint8_t>(Quality::Count)] = { "160", "192", "256", "320" };
	uint32_t QualityDensities[static_cast<uint8_t>(Quality::Count)] = { 160, 192, 256, 320 };

	std::unique_ptr<PGrassRenderer<PGrassCommon::HighTierQuadrantCap, 4>> grassRendererHighLOD;
	std::unique_ptr<PGrassRenderer<PGrassCommon::MidTierQuadrantCap, PGrassCommon::MidPatchBladeCount>> grassRendererMidLOD;
	std::unique_ptr<PGrassRenderer<PGrassCommon::LowTierQuadrantCap, 1>> grassRendererLowLOD;

	std::vector<PGrassCommon::Quadrant> quadrantsHighLOD;
	std::vector<PGrassCommon::Quadrant> quadrantsMidLOD;
	std::vector<PGrassCommon::Quadrant> quadrantsLowLOD;
	std::vector<PGrassCommon::Quadrant> quadrantsPresence;
	uint64_t quadrantsHighVersion = 1;
	uint64_t quadrantsMidVersion = 1;
	uint64_t quadrantsLowVersion = 1;
	uint64_t nearVisibleStamp = (std::numeric_limits<uint64_t>::max)();
	std::array<bool, PGrassCommon::LowTierQuadrantCap> nearCoveredQuadrants{};

	bool vanillaGrassSuppressed = false;  // This feature turned vanilla grass off and owes it back when no longer wanted.

	// Low's outer ring reads LAND data on workers. The other tiers use loaded cell LAND data.
	GrassCellCache grassCellCache;
	bool grassCellCachePolicyDirty = true;
	size_t streamRequestCursor = 0;
	int32_t streamRequestCenterX = (std::numeric_limits<int32_t>::min)();
	int32_t streamRequestCenterY = (std::numeric_limits<int32_t>::min)();
	RE::TESWorldSpace* streamRequestWorldSpace = nullptr;

	ID3D11RasterizerState* noCullRS = nullptr;
	ID3D11DepthStencilState* depthWriteDS = nullptr;
	ID3D11DepthStencilState* depthEqualDS = nullptr;
	ID3D11BlendState* depthOnlyBlend = nullptr;
	ID3D11BlendState* defaultBlend = nullptr;
	ID3D11BlendState* terrainFadeBlend = nullptr;

	// Top-down grass density.
	Texture2D* grassDensityTexture = nullptr;
	Texture2D* distantAmbientLUT = nullptr;
	mutable ID3D11ComputeShader* distantAmbientLUTCS = nullptr;

	// Rendered terrain LOD on a wrapping world-aligned grid: measurements, lift, and the surface height bounding it.
	Texture2D* terrainLiftMeasuredTexture = nullptr;
	Texture2D* terrainLiftTexture = nullptr;
	Texture2D* terrainLiftSurfaceTexture = nullptr;
	ID3D11ComputeShader* terrainLiftCS = nullptr;
	int32_t terrainLiftOriginCell[2] = { 0, 0 };
	bool terrainLiftOriginValid = false;
	uint32_t terrainLiftHeightMapGeneration = 0;

	mutable uint32_t distantAmbientLUTFrame = UINT32_MAX;

	ID3D11PixelShader* depthClipPS = nullptr;

	float depthBlendStrength = -1.0f;
	float depthBlendHeight = -1.0f;
	float depthBaseCutoff = -1.0f;

	static constexpr uint32_t grassDensityDim = 256;
	static constexpr float DensityHalfExtent = 10240.0f;  // World-space half size of the camera-centred density window
	static constexpr uint32_t distantAmbientLUTDim = 32;

	// Gathered density reads this Low-tier world-space grass-id texture without atomics.
	static constexpr uint32_t grassPresenceDim = (2 * PGrassCommon::LowTierQuadrantRadius + 1) * (PGrassCommon::QuadrantGrassPitch - 1) + 1;  // 177
	Texture2D* grassPresenceTexture = nullptr;
	ID3D11ComputeShader* densityGatherCS = nullptr;
	std::vector<uint8_t> grassPresenceStaging;        // grassPresenceDim^2 ids. 0 is bare.
	float2 grassPresenceOrigin = float2(0.0f, 0.0f);  // World-space texture origin
	int32_t grassPresenceOriginQuadX = (std::numeric_limits<int32_t>::min)();
	int32_t grassPresenceOriginQuadY = (std::numeric_limits<int32_t>::min)();
	uint64_t grassPresenceContentHash = (std::numeric_limits<uint64_t>::max)();
	mutable bool grassPresenceUploadDirty = true;

	ID3D11SamplerState* linearClampSampler = nullptr;
	ID3D11SamplerState* shadowSampler = nullptr;

	ConstantBuffer* grassGlobalsCB = nullptr;
	ConstantBuffer* grassTypesArrayCB = nullptr;
	ConstantBuffer* grassGeneratorTypesCB = nullptr;
	PGrassCommon::GrassTypesArray resolvedGrassTypes{};
	PGrassCommon::GrassGeneratorTypesArray resolvedGeneratorTypes{};
	bool grassTypesDirty = true;
	uint64_t resolvedSlopeLimitsHash = 0;  // Slope limits the presence map was built with.
	bool resolvedTypeColorsLinear = false;
	bool resolvedTypeColorsACEScg = false;
	Buffer* vertexIndicesHighBuffer = nullptr;
	Buffer* vertexIndicesHighOuterBuffer = nullptr;
	Buffer* vertexIndicesMidBuffer = nullptr;       // 9-index, five-vertex Mid blade
	Buffer* vertexIndicesMidOuterBuffer = nullptr;  // One triangle per straight single Mid blade
	Buffer* vertexIndicesLowBuffer = nullptr;
	Buffer* vertexIndicesLowOuterBuffer = nullptr;

	/** @brief Cached grass ids and heights for one LAND quadrant. */
	struct QuadrantGrass
	{
		uint64_t cacheVersion = 0;
		std::array<uint8_t, PGrassCommon::QuadrantGrassSamples> ids{};  // filled grass ids

		/** @brief World Z per LAND vertex. All values are QuadrantNoHeight when unavailable. */
		std::array<float, PGrassCommon::QuadrantGrassSamples> heights{};
		float minHeight = PGrassCommon::QuadrantNoHeight;
		float maxHeight = PGrassCommon::QuadrantNoHeight;
	};

	/** @brief Copied grass inputs for all four quadrants of one loaded LAND cell. */
	struct LoadedCellGrass
	{
		RE::TESObjectLAND* land = nullptr;
		RE::TESObjectLAND::LoadedLandData* loadedData = nullptr;
		uint64_t lastSeenFrame = 0;
		std::array<QuadrantGrass, 4> quadrants{};
	};

	static constexpr size_t grassMapCacheCapacity = 256;
	std::unordered_map<uint64_t, LoadedCellGrass> grassMapCache;
	uint64_t grassMapFrame = 0;
	uint64_t nextGrassCacheVersion = 1;
	uint64_t grassMapCacheVersion = 1;

	float2 windDirection = float2(1.0f, 0.0f);
	float2 previousWindDirection = float2(1.0f, 0.0f);
	float previousWindSpeed = 0.4f;
	float previousShaderTimer = 0.0f;
	float2 grassLodOrigin = float2(0.0f, 0.0f);
	bool grassLodOriginInitialized = false;
	float nearQuadrantFrustumPadding = 0.0f;

	// Heap-allocated because the 16-byte-aligned struct would pad the feature object.
	std::unique_ptr<PGrassCommon::GrassGlobals> grassGlobalsStaging = std::make_unique<PGrassCommon::GrassGlobals>();
	float nearHiZRadius = 0.0f;
	float hiZClumpReach = 0.0f;
	float lowFadeInPositionPadding = 1.0f;

	/** @brief Per-stage counters for why quadrants are rejected; debug panel only. */
	struct QuadrantReject
	{
		uint32_t cells;
		uint32_t withExterior;
		uint32_t withLand;
		uint32_t withLoadedData;
		uint32_t withMesh;
		uint32_t preProcessed;
	} quadrantReject{};

	/**
	 * @brief Returns all cached LAND grass ids and heights for a cell, rebuilding stale entries together.
	 * @return The cache entry, stable until eviction.
	 */
	const LoadedCellGrass& GetCellCache(RE::TESObjectLAND* land, int32_t cellX, int32_t cellY);
	void SyncGrassCellCachePolicy();
	void ClearGrassMapCache();

	/** @brief Evicts the oldest inactive LAND cells over the cache limit. */
	void EvictGrassMapCache();

	/** @brief Returns terrain Z from cached LAND data, or nullopt outside loaded cells. */
	std::optional<float> GetLandHeightAt(float worldX, float worldY) const;

	/**
	 * @brief Sets every tier's density from a quality preset so neighbouring tiers match.
	 * Mid draws two of High's four blades per patch and Low draws one, both on High's lattice.
	 */
	void ApplyDensityPreset(int32_t quality);
	/** @brief Pushes the current density settings to every tier renderer. */
	void ApplyTierDensities();

	/** @brief Builds a GPU GrassType from base settings and sparse JSON overrides. */
	PGrassCommon::GrassType ResolveGrassType(const nlohmann::json& typeOverride) const;
	/** @brief Packs resolved per-type fields into the GPU layout, clamping values the shaders rely on. */
	static PGrassCommon::GrassType BuildGrassType(const GrassTypeFields& fields);

	/** @brief Returns the stable settings key for a land texture. */
	static std::string LandTextureKey(const RE::TESLandTexture* tex);

	using TextureSelection = PGrassCommon::GrassTextureSelection;

	// Maps texture variants to global type ids and weighted selections. Rebuilt when variants or weights change.
	std::vector<std::pair<std::string, uint32_t>> typeAllocation;
	std::unordered_map<std::string, TextureSelection> textureSelection;
	std::unordered_map<const RE::TESLandTexture*, const TextureSelection*> textureSelectionByTexture;

	/** @brief Rebuilds type ids and weighted selections from settings.textureTypes. */
	void RebuildTypeAllocation();
	/** @brief Bare, base and allocated texture-variant slots in use. */
	uint32_t ActiveGrassTypeCount() const { return std::min<uint32_t>(static_cast<uint32_t>(typeAllocation.size()) + 2u, PGrassCommon::MaxGrassTypes); }
	void LoadTextureTypes();
	void SaveTextureTypes() const;

	static bool ConsoleFunc_ToggleGrass();

	static std::vector<uint16_t> CreateVertexIndicesArray(uint16_t vertCount);

	static void CopyDepthBuffer(ID3D11DeviceContext* ctx, RE::BSGraphics::Renderer* renderer);
	static void SetViewport(ID3D11DeviceContext* ctx, float2 size);

	/** @brief Creates the batched per-tier blade index buffers. */
	void CreateIndexBuffers();
	/** @brief Creates the samplers, rasterizer, depth-stencil and blend states used by the grass passes. */
	void CreatePipelineStates();

	/** @brief Creates the density, distant-ambient and presence textures. */
	void CreateGrassTextures();
	void CompileSupportShaders();

	void PostDepthRendering();
	/** @brief Player position in quadrant and cell units for one visibility update. */
	struct VisibilityOrigin
	{
		int32_t quadrantX;
		int32_t quadrantY;
		int32_t cellX;
		int32_t cellY;
	};

	// Near tiers cover Low's radius plus a streaming guard; Low's cached-LAND ring spans the same area in cells.
	static constexpr int32_t NearCoverageRadius = PGrassCommon::LowTierQuadrantRadius + PGrassCommon::LowTierStreamGuardQuadrants;
	static constexpr int32_t NearCoverageDiameter = NearCoverageRadius * 2 + 1;
	static constexpr int32_t LowCellRadius = (NearCoverageRadius + 1) / 2;
	// Streamed LAND covers Low's ring plus a guard ring, so cells are ready before Low reaches them.
	static constexpr int32_t StreamCellRadius = LowCellRadius + PGrassCommon::StreamGuardCells;

	/** @brief Rebuilds the per-tier quadrant lists when loaded or streamed LAND changes. */
	void GetVisibleQuadrants();

	/** @brief Hashes everything the near quadrant lists depend on, so unchanged frames skip the rebuild. */
	uint64_t ComputeNearVisibilityStamp(RE::TESWorldSpace* landWorldSpace, const RE::GridCellArray* cells, const VisibilityOrigin& origin);

	/** @brief Rebuilds High, Mid, Low and presence quadrants from the loaded grid and Low's streamed ring. */
	void RebuildNearQuadrants(const RE::GridCellArray* cells, const VisibilityOrigin& origin);

	/** @brief Refreshes the terrain-darkening grass-id window when its origin or content changes. */
	void RebuildGrassPresence(int32_t originQuadX, int32_t originQuadY);
	/** @brief Clears a grass id whose type's slope limits reject the LAND slope at a quadrant sample, as the generator culls its blades. */
	uint8_t LimitGrassIdToSlope(uint8_t id, const PGrassCommon::Quadrant& quadrant, uint32_t column, uint32_t row) const;

	/** @brief Player position in quadrant and cell units; tier lists and streamed LAND follow the loaded grid around the player. */
	static VisibilityOrigin GetVisibilityOrigin();

	/** @brief Requests streamed LAND for Low's ring with bounded work per frame. */
	void RequestStreamedCells(RE::TESWorldSpace* landWorldSpace, const VisibilityOrigin& origin);

	/** @brief Binds the constant buffers, heightmap and sampler every generator dispatch reads. */
	void BindGeneratorState(ID3D11DeviceContext* ctx) const;

	/** @brief Index into nearCoveredQuadrants, or -1 outside the near coverage window. */
	static int32_t NearCoverageIndex(int32_t worldQuadrantX, int32_t worldQuadrantY, const VisibilityOrigin& origin);

	void PostDepthRenderPrep(ID3D11DeviceContext* ctx, RE::BSGraphics::Renderer* renderer);

	/** @brief Rebuilds the per-type render and generator tables and the culling bounds derived from them. */
	void ResolveGrassTypes(bool prelinearizeTypeColors);

	/** @brief Inverts the terrain-blend opacity curve into the High depth pass's base cutoff when blend settings change. */
	void UpdateDepthBaseCutoff();

	/**
	 * @brief Generates one group of tiers. High and Mid run first so their depth can occlude Low generation.
	 * @param nearTiers True for High and Mid; false for Low.
	 */
	void GenerateBlades(ID3D11DeviceContext* ctx, bool nearTiers) const;

	/** @brief Renders High and Mid depth. Low writes depth in its colour pass. */
	void RenderDepth(ID3D11DeviceContext* ctx) const;

	static void UnbindGeneratorResources(ID3D11DeviceContext* ctx);

	void DeferredRenderPrep(ID3D11DeviceContext* ctx, RE::BSGraphics::Renderer* renderer) const;
	void UpdateDistantAmbientLUT(ID3D11DeviceContext* ctx) const;

	/** @brief Refreshes a quarter of the terrain lift map from the copied scene depth. */
	void UpdateTerrainLift(ID3D11DeviceContext* ctx, RE::BSGraphics::Renderer* renderer);
	void RenderGrass(ID3D11DeviceContext* ctx) const;
	/** @brief Merges the depth Low wrote in its colour pass into the depth transparent effects read. */
	void MergeLateDepth(ID3D11DeviceContext* ctx, RE::BSGraphics::Renderer* renderer) const;
};
