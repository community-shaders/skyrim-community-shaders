#pragma once

#include "Buffer.h"
#include "GrassMeshLibrary.h"

struct GrassBucket;
struct GrassShapeRegistration;

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
inline constexpr uint64_t kUploadBudgetBytes = 4ull << 20;

/** @brief Stores world-space bounds for CPU culling. */
struct SliceBounds
{
	alignas(16) float lo[4]{};
	alignas(16) float hi[4]{};

	/** @brief Returns inverted bounds that any included box replaces. */
	static SliceBounds Empty() { return { { FLT_MAX, FLT_MAX, FLT_MAX, 0.0f }, { -FLT_MAX, -FLT_MAX, -FLT_MAX, 0.0f } }; }

	/** @brief Extends these bounds to contain another box. */
	void Include(const SliceBounds& other)
	{
		for (int axis = 0; axis < 3; ++axis) {
			lo[axis] = std::min(lo[axis], other.lo[axis]);
			hi[axis] = std::max(hi[axis], other.hi[axis]);
		}
	}
};
static_assert(sizeof(SliceBounds) == 32);

/** @brief Bounds the instance centers and scale variation of one GPU cull group. */
struct GrassGroupBounds
{
	float lo[3]{ FLT_MAX, FLT_MAX, FLT_MAX };
	float hi[3]{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
	float maxScale = 1.0f;
	bool valid = true;
};

/** @brief Distinguishes native group lifetimes when the engine reuses a group slot. */
struct GrassGroupLifetime
{
	std::atomic<bool> current{ true };
};

/** @brief Stores a captured group's packed instances and placement data. */
struct GrassInstanceSlice
{
	GrassBucket* bucket = nullptr;
	GrassShapeRegistration* owner = nullptr;
	std::shared_ptr<GrassGroupLifetime> lifetime;
	// Grass shapes span cells, so removals identify slices by engine group.
	uint32_t engineGroup = UINT32_MAX;

	std::vector<uint8_t> data;
	std::vector<GrassGroupBounds> groupBounds;
	uint32_t count = 0;
	uint32_t bufferOffset = UINT32_MAX;    // Slices keep their instance-pool range from upload until removal.
	uint32_t firstCullGroup = UINT32_MAX;  // This slice's groups stay contiguous while the bucket's clusters are valid.

	RE::NiPoint3 origin;
	float maxScale = 1.0f;
	float fadeStart = 0.0f;  // Set at upload, so grass fades in only once it can draw.
	SliceBounds bounds;
};

/** @brief Tracks a native shape's registration and owns its captured chunks. */
struct GrassShapeRegistration
{
	RE::BSMultiStreamInstanceTriShape* native = nullptr;
	GrassMeshLibrary::RegisteredMesh* mesh = nullptr;
	RE::NiPointer<RE::BSMultiStreamInstanceTriShape> geometry;

	std::atomic<bool> registered{ true };
	// The lifecycle mutex protects groups, while the render thread owns chunks.
	std::unordered_map<uint32_t, std::shared_ptr<GrassGroupLifetime>> groups;

	GrassBucket* currentBucket = nullptr;
	std::unordered_map<uint32_t, std::unique_ptr<GrassInstanceSlice>> chunks;
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

/** @brief Best-fit allocator for element ranges of one shared GPU buffer. */
class GrassRangeAllocator
{
public:
	/** @brief Returns the offset of the smallest free range that fits `count` elements, or UINT32_MAX when none fits. */
	uint32_t Allocate(uint32_t count);
	/** @brief Grows the range at `offset` to `newCount` in place when the elements after it are free. */
	bool TryExtend(uint32_t offset, uint32_t count, uint32_t newCount);
	/** @brief Returns a range to the free list, merging it with free neighbours. */
	void Free(uint32_t offset, uint32_t count);
	/** @brief Adds the elements between the current and new capacity as free space. */
	void Grow(uint32_t newCapacity);

	uint32_t Capacity() const { return capacity; }

private:
	void Insert(uint32_t offset, uint32_t count);
	void Erase(std::map<uint32_t, uint32_t>::iterator range);

	std::map<uint32_t, uint32_t> freeRanges;               // Offset to element count.
	std::set<std::pair<uint32_t, uint32_t>> rangesBySize;  // Element count and offset, so allocation stays logarithmic.
	uint32_t capacity = 0;
};

/** @brief One GPU buffer shared by every bucket, suballocated into per-slice or per-bucket ranges. */
struct GrassPool
{
	std::unique_ptr<Buffer> buffer;
	GrassRangeAllocator ranges;
};

/** @brief The cull output shared by every bucket, with one view per LOD tier. */
struct GrassCullBuffers
{
	std::unique_ptr<Buffer> records;
	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, kTierCount> compactedSRVs;

	uint32_t capacity = 0;  // Records per tier.
	uint32_t underutilizedFrames = 0;
};

/** @brief Holds a larger pool buffer created off the render thread until it is adopted. */
struct GrassPoolGrowth
{
	uint32_t capacity = 0;
	std::unique_ptr<Buffer> buffer;

	std::atomic<bool> done{ false };
};

/** @brief Owns source data and culling state for one mesh and material combination. */
struct GrassBucket
{
	/** @brief Stores four unpadded run bounds in SSE lanes, so padding changes need no rebuild. */
	struct alignas(16) SliceRunBatch
	{
		float loX[4]{}, loY[4]{}, loZ[4]{};
		float hiX[4]{}, hiY[4]{}, hiZ[4]{};
		uint32_t firstGroup[4]{};  // Each lane indexes a contiguous range in cullGroups.
		uint32_t groupCount[4]{};

		/** @brief Starts or extends a lane's bounds with one slice. */
		void IncludeBounds(uint32_t lane, const SliceBounds& bounds, bool starts)
		{
			loX[lane] = starts ? bounds.lo[0] : std::min(loX[lane], bounds.lo[0]);
			loY[lane] = starts ? bounds.lo[1] : std::min(loY[lane], bounds.lo[1]);
			loZ[lane] = starts ? bounds.lo[2] : std::min(loZ[lane], bounds.lo[2]);
			hiX[lane] = starts ? bounds.hi[0] : std::max(hiX[lane], bounds.hi[0]);
			hiY[lane] = starts ? bounds.hi[1] : std::max(hiY[lane], bounds.hi[1]);
			hiZ[lane] = starts ? bounds.hi[2] : std::max(hiZ[lane], bounds.hi[2]);
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
		cullSlot = UINT32_MAX;
		sliceTableCount = 0;
		groupCount = 0;
	}

	GrassDrawResources drawResources;

	std::vector<GrassInstanceSlice*> slices;

	uint32_t totalInstances = 0;     // This includes instances awaiting upload.
	uint32_t residentInstances = 0;  // Instances of uploaded slices.
	uint32_t uploadedSlices = 0;     // Uploaded slices form a prefix of slices.

	float boundExtent = 0.0f;
	float maxScale = 1.0f;
	float boundsPadding = 0.0f;
	// The coarse box covers the live cached runs without padding.
	SliceBounds coarseBounds = SliceBounds::Empty();
	bool coarseDirty = false;

	std::vector<CullGroup> cullGroups;
	std::vector<GrassGroupBounds> cullGroupBounds;
	// Records index groups relative to this bucket's range in the shared group pool.
	uint32_t groupBase = 0;
	uint32_t groupCapacity = 0;
	uint32_t residentGroups = 0;  // Uploaded groups form a prefix of cullGroups.
	// Removed groups keep zero counts until the cached runs are rebuilt.
	uint32_t deadGroups = 0;
	std::vector<std::pair<uint32_t, uint32_t>> dirtyGroupRanges;

	std::vector<SliceRunBatch> sliceRunBatches;
	uint32_t sliceRunCount = 0;  // This excludes unused lanes in the final batch.
	// A run left with no live groups is emptied so the cull skips it.
	std::vector<uint32_t> runLiveGroups;
	std::vector<SliceRunBatch> sliceBlockBatches;
	uint32_t clusteredSlices = 0;  // Cached runs cover this slice prefix, and additions extend its tail.
	bool clustersValid = false;
	bool sliceBlocksUseful = false;  // Scattered run order can make parent bounds too broad.
	uint64_t lastRunCell = 0;
	bool rebuildPending = false;  // Dead groups have reached the budgeted compaction threshold.

	std::array<const GrassMeshLibrary::LODMesh*, kTierCount - 1> lodMeshes{};
	// LOD meshes are refreshed only when LOD settings or loaded LOD meshes change.
	uint32_t preparedLODState = UINT32_MAX;
	ID3D11ShaderResourceView* complexSRV = nullptr;  // The active texture cache owns this view.
	// Later frames skip the texture cache lookup while the texture keeps this view.
	ID3D11ShaderResourceView* resolvedTextureView = nullptr;
	float distScale = 1.0f;
	float minPixelScale = 1.0f;
	uint32_t outputOffset = 0;  // This bucket's records start here in each tier of the shared cull output.
	uint32_t cullSlot = UINT32_MAX;

	// This window selects visible source runs from the shared slice table.
	uint32_t sliceTableOffset = 0;
	uint32_t sliceTableCount = 0;
	uint32_t groupCount = 0;

	// The lowest alpha-test reference of depth-writing draws takes effect from the next cull.
	float alphaTestRef = 0.0f;
	bool alphaTestRefKnown = false;
};
