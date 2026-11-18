#pragma once

#include "PGrassCommon.h"

namespace PGrassRendererQuads
{
	// Packed visible-work layout consumed by PGrassBladeGeneratorCS.
	inline constexpr uint32_t WorkQuadrantMask = 0xFFFu;
	inline constexpr uint32_t WorkLaneShift = 12;
	inline constexpr uint32_t WorkHasLand = 1u << 16;
	inline constexpr uint32_t WorkInsideFrustum = 1u << 17;
	inline constexpr uint32_t WorkOccupiedTile = 1u << 21;
	inline constexpr uint32_t WorkTileShift = 22;
	inline constexpr uint32_t WorkFullGrass = 1u << 30;
	inline constexpr uint32_t OccupancyTilesPerAxis = PGrassCommon::QuadrantCellPitch;
	inline constexpr uint32_t OccupancyTileCount = OccupancyTilesPerAxis * OccupancyTilesPerAxis;
	inline constexpr uint32_t ThreadGroupSize = 64;

	/** Stable quadrant identity hash, generated once on the CPU for all patches in that quadrant. */
	uint32_t QuadrantHash(uint32_t x, uint32_t y);

	enum class QuadrantFrustumState : uint8_t
	{
		Outside,
		Intersecting,
		Inside,
	};

	struct SideFrustum
	{
		std::array<float4, 4> planes{};
	};

	SideFrustum BuildSideFrustum(const float4x4& viewProj);

	/** Conservative XY clip test for a padded quadrant LAND AABB. Near/far clipping intentionally matches the generator and stays disabled. */
	QuadrantFrustumState ClassifyQuadrantFrustum(const PGrassCommon::Quadrant& quadrant, const SideFrustum& frustum, const float4& cameraPosAdjust, float xyPadding);

	/** @brief Per-frame inputs of one tier's blade generation. */
	struct GenerateParams
	{
		uint64_t contentVersion = 0;
		float2 lodOrigin;
		float4 lodFadeIn;   // x: fade-in start, y: inverse range, z: unused, w: fade-out endpoint
		float4 lodFadeOut;  // x: fade-out start, y: inverse range, z: edge keep
		float frustumPadding = 0.0f;
		float fadeInPositionPadding = 0.0f;
		float edgeNoise = 0.0f;
		bool disableCulls = false;

		bool operator==(const GenerateParams&) const = default;
	};
}

template <uint32_t QuadrantCount, uint32_t PatchBladeCount>
class PGrassRenderer
{
public:
	/**
	 * @brief Creates a tier renderer with a lazily sized blade buffer.
	 * @param slopeExtraBlades Extra candidate blade slots per patch for filling sloped ground. Keep this small because it enlarges the
	 *						   blade buffer and the base thread's candidate loop.
	 */
	PGrassRenderer(PGrassCommon::Tier tier, uint32_t grassDensity, Buffer* vertexIndicesBuf, Buffer* outerVertexIndicesBuf,
		uint32_t bladeStrideBytes, uint32_t slopeExtraBlades = 1);

	/** @brief Sets the tier's lattice density. */
	void SetDensity(uint32_t grassDensity);

	void ClearShaderCache();

	/** @brief Builds the visible work list when its inputs change and dispatches blade generation. */
	void GenerateBlades(ID3D11DeviceContext* ctx, const std::vector<PGrassCommon::Quadrant>& quadrants, const PGrassRendererQuads::GenerateParams& params);
	void RenderDepth(ID3D11DeviceContext* ctx, ID3D11PixelShader* depthClipPS = nullptr);
	void RenderGrass(ID3D11DeviceContext* ctx);

	/** @brief Returns the latest completed debug readback without waiting for the GPU. */
	uint32_t ReadBladeCount();

private:
	using ShaderDefines = std::vector<std::pair<const char*, const char*>>;
	using Tier = PGrassCommon::Tier;

	// Hash coordinates are offset so negative cells still produce distinct, stable seeds.
	static constexpr int32_t QuadrantHashCellOffsetX = 61;
	static constexpr int32_t QuadrantHashCellOffsetY = 60;

	const Tier tier;
	uint32_t density = 0;
	uint32_t bladeStrideBytes = sizeof(PGrassCommon::Blade);  // High stores SH, Mid stores the probe root; either may include collision.
	std::string densityString;
	uint32_t slopeExtraBlades = 0;
	std::string patchBladeCountString = std::to_string(PatchBladeCount);
	std::string bladeBatchSizeString;
	uint32_t patchesPerQuadrant = 0;
	uint32_t patchesPerRow = 0;
	uint32_t patchRows = 0;
	uint32_t bladeBufferCapacity = 0;
	bool bladeCapacityClampLogged = false;
	std::string quadrantCountString = std::to_string(QuadrantCount);

	winrt::com_ptr<ID3D11ComputeShader> generatorCS;
	bool generatorCompileAttempted = false;
	winrt::com_ptr<ID3D11ComputeShader> batchArgsCS;

	struct CachedGenerator
	{
		winrt::com_ptr<ID3D11ComputeShader> shader;
		uint64_t lastUse = 0;
	};

	std::unordered_map<std::string, CachedGenerator> generatorVariants;
	uint64_t generatorVariantUse = 0;

	// Indexed by (depth ? 2 : 0) + (outer ? 1 : 0).
	std::array<winrt::com_ptr<ID3D11VertexShader>, 4> vertexShaders;

	// Feature variants for wetness and local-light availability.
	std::array<winrt::com_ptr<ID3D11PixelShader>, 8> pixelShaders;

	std::unique_ptr<StructuredBuffer> bladesSB;

	// Per-slot generator inputs. A slot is restaged and uploaded only when its quadrant, a border neighbour, or a shared
	// input changes, so streaming one cell does not rebuild the whole tier.
	std::unique_ptr<StructuredBuffer> quadrantGrassCellsSB;
	std::vector<uint32_t> quadrantGrassCellsStaging;  // one packed 2x2 LAND-id cell per 16x16 quadrant cell
	std::unique_ptr<StructuredBuffer> quadrantOccupancySB;
	std::vector<uint32_t> quadrantOccupancyStaging;
	std::vector<uint64_t> quadrantOccupancyVersions;
	std::unique_ptr<StructuredBuffer> quadrantHeightSB;
	std::vector<float> quadrantHeightStaging;
	std::unique_ptr<StructuredBuffer> tileHeightBoundsSB;
	std::vector<float2> tileHeightBoundsStaging;
	std::vector<PGrassCommon::Quadrant> stagedQuadrants;
	std::vector<uint8_t> slotDirty;
	std::unordered_map<uint64_t, uint32_t> slotByWorldQuadrant;

	std::unique_ptr<StructuredBuffer> visibleWorkSB;
	std::vector<uint32_t> visibleWorkStaging;
	std::unique_ptr<ConstantBuffer> workRangeCB;

	struct OccupiedTile
	{
		uint16_t tile = 0;
		uint16_t patchCount = 0;
	};

	std::array<float4, PGrassRendererQuads::OccupancyTileCount> tileLocalBounds{};
	std::array<uint32_t, PGrassRendererQuads::OccupancyTileCount> tilePatchCounts{};
	std::vector<OccupiedTile> visibleTilesStaging;

	struct OccupancyCacheEntry
	{
		uint64_t cacheVersion = 0;
		uint32_t density = 0;
		float edgeNoise = -1.0f;
		uint16_t occupiedTileCount = 0;
		std::array<OccupiedTile, PGrassRendererQuads::OccupancyTileCount> occupiedTiles{};
	};

	std::unordered_map<uint64_t, OccupancyCacheEntry> occupancyCache;

	struct VisibleWorkCandidate
	{
		uint32_t quadrantIndex = 0;
		uint32_t flags = 0;
		uint32_t tileOffset = 0;
		uint32_t tileCount = 0;
		const OccupiedTile* cachedTiles = nullptr;
	};

	std::vector<VisibleWorkCandidate> visibleWorkCandidates;

	struct WorkListState
	{
		PGrassRendererQuads::GenerateParams params;
		uint32_t density = 0;
		uint32_t quadrantCount = 0;
		float4x4 viewProj;
		float4 cameraPosAdjust;

		bool operator==(const WorkListState&) const = default;
	};

	WorkListState lastWorkListState{};
	uint64_t cachedRequiredBladeCount = 0;
	uint32_t cachedWorkGX = 0;
	bool hasCachedWorkList = false;

	std::unique_ptr<ConstantBuffer> quadrantsCB;
	PGrassCommon::QuadrantDataArray<QuadrantCount> quadrantDataStaging{};

	// Inputs every slot depends on; a change restages all of them.
	uint64_t lastUploadVersion = 0;
	float lastTileReach = -1.0f;
	size_t lastQuadrantCount = 0;
	bool hasUploadedQuadrants = false;

	std::unique_ptr<Buffer> argsBuffer;
	std::unique_ptr<Buffer> batchArgsBuffer;
	winrt::com_ptr<ID3D11Buffer> argsStaging;
	bool argsReadbackPending = false;
	uint32_t lastBladeCount = 0;
	Buffer* vertexIndicesBuffer = nullptr;
	Buffer* outerVertexIndicesBuffer = nullptr;

	/** @brief Low uses the reduced lighting model. */
	bool UsesSimpleLighting() const { return tier == Tier::Low; }
	bool UsesGrassCollision() const;
	bool UsesBatchedDraws() const { return tier != Tier::High; }
	bool HasDepthPrepass() const { return tier == Tier::High || tier == Tier::Mid; }
	const char* LodDefine() const;
	const char* VertexDefine() const;

	void CreateBuffers();
	/** @brief Draws the inner and outer blade lists when their required shaders are available. */
	void DrawBlades(ID3D11DeviceContext* ctx, bool depth, ID3D11PixelShader* innerPS, ID3D11PixelShader* outerPS);
	/** Grow the blade buffer for this frame's visible candidate work. */
	void EnsureBladeCapacity(uint64_t requiredBladeCount);
	void AppendVertexShaderDefines(ShaderDefines& defines) const;

	ID3D11ComputeShader* GetBladeGeneratorCS();
	void ClearGeneratorCache();
	ID3D11ComputeShader* GetBatchArgsCS();
	/** @brief Appends lit-shader feature defines; simple lighting keeps only the features its reduced model evaluates. */
	void AppendFeatureDefines(ShaderDefines& defines, bool simpleLighting) const;

	/** @brief Returns the depth or colour VS for the inner or outer geometry segment, compiling on first use. */
	ID3D11VertexShader* GetVertexShader(bool depth, bool outer);
	ID3D11PixelShader* GetPS(bool noWetness = false, bool noLocalLights = false, bool innerHigh = false);

	/** @brief Restages and uploads the slots whose quadrant, border neighbours or shared inputs changed. */
	void UploadQuadrantInputs(const std::vector<PGrassCommon::Quadrant>& quadrants, const PGrassRendererQuads::GenerateParams& params, float tileReach);
	/** @brief Stages one slot's constant-buffer entry, heights and tile bounds. */
	void StageQuadrant(uint32_t index, const PGrassCommon::Quadrant& quadrant, float tileReach);
	/** @brief Packs one slot's grass cells, uniting its border samples with every neighbour sharing them. */
	void PackQuadrantGrassCells(uint32_t index, const std::vector<PGrassCommon::Quadrant>& quadrants);
	/** @brief Stages conservative per-tile LAND height bounds that cover jittered and clumped roots. */
	void StageTileHeightBounds(uint32_t index, const PGrassCommon::Quadrant& quadrant, float tileReach);

	/** @brief Returns the cached occupied tiles of a quadrant, recomputing them when its grass map changes. */
	const OccupancyCacheEntry& GetOccupiedTiles(const PGrassCommon::Quadrant& quadrant, uint32_t index, float edgeNoise);
	/** @brief Culls quadrants and tiles on the CPU and uploads the generator's work list. */
	void BuildVisibleWorkList(const std::vector<PGrassCommon::Quadrant>& quadrants, const WorkListState& state);

	/** @brief Binds generator inputs, dispatches the work list, and prepares batched draw arguments. */
	void DispatchGeneration(ID3D11DeviceContext* ctx, ID3D11ComputeShader* bladeGenerator, ID3D11ComputeShader* batchArgsGenerator);
	/** @brief Writes the indirect arguments for an empty frame, or the starting counts generation appends to. */
	void ResetIndirectArgs(ID3D11DeviceContext* ctx, bool generating);

	static std::string BuildDefineList(std::span<const std::pair<const char*, const char*>> defines);

	template <class ShaderT>
	static ShaderT* CompileShader(const wchar_t* path, std::vector<std::pair<const char*, const char*>>& defines, const char* programType);
};
