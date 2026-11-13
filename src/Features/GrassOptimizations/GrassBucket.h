#pragma once

#include "Buffer.h"
#include "GrassMeshLibrary.h"

struct GrassBucket;
struct GrassShape;

inline constexpr uint32_t kGrassStride = 32;   // The engine supplies 32-byte instance records.
inline constexpr uint32_t kSourceStride = 16;  // GrassInstance.hlsli defines the packed record layout.
inline constexpr uint32_t kCullGroupSize = 64;
inline constexpr uint32_t kMaxRecordGroups = 1u << 19;

// Draw tiers use the full mesh followed by the middle and far LOD meshes.
inline constexpr uint32_t kTierCount = 1 + (uint32_t)GrassMeshLibrary::LODTier::kCount;
inline constexpr uint32_t kCullRecordStride = 16;
// Each indirect draw stores five DWORDs, with the instance count second.
inline constexpr uint32_t kArgsStride = 5 * sizeof(uint32_t);

// Newly captured slices share this per-frame upload budget.
inline constexpr uint64_t kUploadBudgetBytes = 16ull << 20;
// Compaction shares this per-frame copy budget, but one slice may exceed it to guarantee progress.
inline constexpr uint64_t kDefragBudgetBytes = 8ull << 20;

/** @brief Stores world-space bounds for CPU culling. */
struct SliceBounds
{
	alignas(16) float lo[4]{};
	alignas(16) float hi[4]{};
};
static_assert(sizeof(SliceBounds) == 32);

/** @brief Distinguishes native group lifetimes when the engine reuses a group slot. */
struct GrassGroup
{
	std::atomic<bool> current{ true };
};

/** @brief Stores a captured group's packed instances and placement data. */
struct GrassChunk
{
	GrassBucket* bucket = nullptr;
	GrassShape* owner = nullptr;
	std::shared_ptr<GrassGroup> lifetime;
	// Grass shapes span cells, so removals identify slices by engine group.
	uint32_t engineGroup = UINT32_MAX;

	std::vector<uint8_t> data;
	uint32_t count = 0;
	uint32_t bufferOffset = UINT32_MAX;
	uint32_t firstCullGroup = UINT32_MAX;  // This slice's groups stay contiguous while the bucket's clusters are valid.

	RE::NiPoint3 origin;
	float maxScale = 1.0f;
	float fadeStart = 0.0f;
	SliceBounds bounds;
};

/** @brief Tracks a native shape's registration and owns its captured chunks. */
struct GrassShape
{
	RE::BSMultiStreamInstanceTriShape* native = nullptr;
	GrassMeshLibrary::MeshRecord* mesh = nullptr;
	RE::NiPointer<RE::BSMultiStreamInstanceTriShape> geometry;

	std::atomic<bool> registered{ true };
	std::atomic<uint32_t> pendingChanges{ 0 };
	// The lifecycle mutex protects groups, while the render thread owns chunks.
	std::unordered_map<uint32_t, std::shared_ptr<GrassGroup>> groups;

	GrassBucket* current = nullptr;
	std::unordered_map<uint32_t, std::unique_ptr<GrassChunk>> chunks;
	uint32_t residentInstances = 0;
	uint32_t residentGroups = 0;
	bool admitted = false;
};

/** @brief Matches the HLSL layout for one contiguous group of source instances. */
struct CullGroup
{
	float origin[3];
	float fadeStart;
	uint32_t sourceStart;
	uint32_t count;
};
static_assert(sizeof(CullGroup) == 24);

/** @brief Owns source records retained across views and asynchronous resizes. */
struct GrassSourceBuffers
{
	uint32_t capacity = 0;
	std::unique_ptr<Buffer> instances;
};

/** @brief Owns a shared output page with one view per LOD tier. */
struct GrassCullBuffers
{
	std::unique_ptr<Buffer> records;
	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, kTierCount> compactedSRVs;

	uint32_t capacity = 0;
	uint32_t tiers = 0;
	uint32_t used = 0;

	uint32_t idleFrames = 0;
	uint32_t underutilizedFrames = 0;
};

/** @brief Holds allocation results until the render thread adopts them. */
struct BucketAllocation
{
	uint32_t capacity = 0;
	GrassSourceBuffers source;

	bool ok = false;
	std::atomic<bool> done{ false };
};

/** @brief Owns source data and culling state for one mesh and material combination. */
struct GrassBucket
{
	/** @brief Stores four padded run bounds in SSE lanes. */
	struct alignas(16) SliceRunBatch
	{
		float loX[4]{}, loY[4]{}, loZ[4]{};
		float hiX[4]{}, hiY[4]{}, hiZ[4]{};
		uint32_t firstGroup[4]{};  // Each lane indexes a contiguous range in cullGroups.
		uint32_t groupCount[4]{};

		/** @brief Starts or extends a lane's padded bounds with one slice. */
		void IncludeBounds(uint32_t lane, const SliceBounds& bounds, float pad, bool starts)
		{
			const float lx = bounds.lo[0] - pad, ly = bounds.lo[1] - pad, lz = bounds.lo[2] - pad;
			const float hx = bounds.hi[0] + pad, hy = bounds.hi[1] + pad, hz = bounds.hi[2] + pad;
			loX[lane] = starts ? lx : std::min(loX[lane], lx);
			loY[lane] = starts ? ly : std::min(loY[lane], ly);
			loZ[lane] = starts ? lz : std::min(loZ[lane], lz);
			hiX[lane] = starts ? hx : std::max(hiX[lane], hx);
			hiY[lane] = starts ? hy : std::max(hiY[lane], hy);
			hiZ[lane] = starts ? hz : std::max(hiZ[lane], hz);
		}
	};
	static_assert(sizeof(SliceRunBatch) == 128);
	STATIC_ASSERT_ALIGNAS_16(SliceRunBatch);

	// Each parent bounds this many cell runs, and four parents share an SSE batch.
	inline static constexpr uint32_t kRunsPerCullBlock = 16;
	static_assert(kRunsPerCullBlock % 4 == 0);

	GrassBucket() = default;
	GrassBucket(const GrassBucket&) = delete;
	GrassBucket& operator=(const GrassBucket&) = delete;

	/** @brief Returns whether captured slices are waiting for upload. */
	bool HasPendingSlices() const { return uploadedSlices < slices.size(); }

	/** @brief Clears per-frame culling state to prevent draws from using stale results. */
	void ResetCullState()
	{
		output = nullptr;
		cullSlot = UINT32_MAX;
		sliceTableCount = 0;
		groupCount = 0;
		lodMeshes = {};
	}

	GrassAsset asset;

	GrassSourceBuffers source;
	// The allocation worker prepares source replacements for the render thread to adopt.
	std::shared_ptr<BucketAllocation> allocation;
	std::vector<GrassChunk*> slices;

	uint32_t totalInstances = 0;     // This includes instances awaiting upload.
	uint32_t residentInstances = 0;  // This marks the end of resident records, including removal gaps.
	uint32_t uploadedSlices = 0;     // Uploaded slices form a prefix in increasing source-offset order.
	uint32_t underutilizedFrames = 0;
	bool fragmented = false;

	float boundExtent = 0.0f;
	float maxScale = 1.0f;
	float boundsPadding = 0.0f;
	RE::NiPoint3 coarseMin{};
	RE::NiPoint3 coarseMax{};
	bool coarseValid = false;

	std::unique_ptr<Buffer> groups;
	std::vector<CullGroup> cullGroups;
	uint32_t groupCapacity = 0;
	uint32_t residentGroups = 0;  // Uploaded groups form a prefix of cullGroups.
	// Removed groups keep zero counts until the cached runs are rebuilt.
	uint32_t deadGroups = 0;
	// Removals and compaction mark resident group ranges for upload.
	std::vector<std::pair<uint32_t, uint32_t>> dirtyGroupRanges;

	std::vector<SliceRunBatch> sliceRunBatches;
	uint32_t sliceRunCount = 0;  // This excludes unused lanes in the final batch.
	std::vector<SliceRunBatch> sliceBlockBatches;
	uint32_t clusteredSlices = 0;  // Cached runs cover this slice prefix, and additions extend its tail.
	bool clustersValid = false;
	bool sliceBlocksUseful = false;      // Scattered run order can make parent bounds too broad.
	uint64_t lastRunCell = 0;            // The cell of the newest run, which appended slices may extend.
	uint32_t appendedRuns = 0;           // Runs added in arrival order since the last spatially ordered rebuild.
	bool spatialRebuildPending = false;  // Appended runs are valid but scattered, and the cull reorders one bucket per frame.

	// Keep the resolved LOD selection for all draws in this frame.
	std::array<const GrassMeshLibrary::LODMesh*, kTierCount - 1> lodMeshes{};
	ID3D11ShaderResourceView* complexSRV = nullptr;  // The active texture cache owns this view.
	float distScale = 1.0f;
	float minPixelScale = 1.0f;
	GrassCullBuffers* output = nullptr;
	uint32_t outputOffset = 0;
	uint32_t cullSlot = UINT32_MAX;

	// This window selects visible source runs from the shared slice table.
	uint32_t sliceTableOffset = 0;
	uint32_t sliceTableCount = 0;
	uint32_t groupCount = 0;
	bool optimizedReady = false;
};
