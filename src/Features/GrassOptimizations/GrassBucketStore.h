#pragma once

#include <span>
#include <variant>

#include "Buffer.h"
#include "GrassBucket.h"

/** @brief Owns a registered group snapshot through packing and render-thread admission. */
struct PendingCapture
{
	GrassChunk slice;
	std::vector<uint8_t> sourceData;
	std::shared_ptr<GrassShape> shape;
	GrassAsset asset;
};

/** @brief Owns grass buckets, staged lifecycle events and GPU resources. */
class GrassBucketStore
{
public:
	/** @brief Stores feature settings refreshed once per grass frame. */
	struct FrameParams
	{
		bool enableMeshLOD = false;
		bool enableMidLOD = false;
		bool enableFarLOD = false;
		float fadeStart = 0.0f;  // New slices begin fading at this time.
	};

	/** @brief The native shapes and draw proxies one frame renders, published once per frame by the render thread. */
	struct CullSnapshot
	{
		struct NativeShape
		{
			std::shared_ptr<GrassShape> registration;
			bool direct = false;
			// Render thread only. Setup and draw both ask per pass, and a group the engine adds mid-frame invalidates the answer.
			mutable uint32_t optimizedFrame = UINT32_MAX;
			mutable uint32_t optimizedGroups = 0;
			mutable bool optimized = false;
		};

		/** @brief Returns the published state of a native shape, or null when it has none. */
		const NativeShape* Find(const RE::NiAVObject* shape) const
		{
			const auto it = nativeShapes.find(shape);
			return it != nativeShapes.end() ? &it->second : nullptr;
		}

		ankerl::unordered_dense::map<const RE::NiAVObject*, NativeShape> nativeShapes;
		std::vector<RE::NiPointer<RE::BSMultiStreamInstanceTriShape>> proxies;
	};

	/** @brief Stops preparation and releases engine references while lifecycle locks remain alive. */
	~GrassBucketStore();

	/** @brief Refreshes the settings used for new captures and this frame's LOD selection. */
	void BeginFrame(const FrameParams& params)
	{
		frameParams = params;
	}

	/** @brief Uploads admitted slices without changing this frame's bucket membership. */
	void UploadPending(ID3D11DeviceContext* ctx);

	/** @brief Releases proxies after their queued frame passes finish, then applies completed events. */
	void EndFrame();

	/** @brief Registers a model before admitting its existing instance groups. */
	void RegisterModel(RE::BSMultiStreamInstanceTriShape* shape, const char* modelPath);

	/** @brief Copies a registered engine group into the ordered preparation queue. */
	void CaptureEngineGroup(RE::BSMultiStreamInstanceTriShape* shape, uint32_t engineGroup);

	/** @brief Captures newly attached cache groups without duplicating groups already in preparation. */
	void CaptureQueuedGroups();

	/** @brief Retires an engine group's coverage and stages removal at the next frame boundary. */
	void RemoveEngineGroup(RE::BSMultiStreamInstanceTriShape* shape, uint32_t engineGroup,
		void (*remove)(RE::BSMultiStreamInstanceTriShape*, uint32_t));

	/** @brief Stages a shape's destruction for the render thread. */
	void StageRemoval(RE::BSMultiStreamInstanceTriShape* shape);

	/** @brief Returns the latest published snapshot, valid on any thread until the frame after the next publish. */
	const CullSnapshot* GetCullSnapshot() const { return publishedSnapshot.load(std::memory_order_acquire); }

	/** @brief Publishes the proxy set after frame-end lifecycle changes. */
	void PublishCullSnapshot();

	/** @brief Invalidates direct native submission when a bucket enters or leaves optimized rendering. */
	void SetOptimizedReady(GrassBucket& bucket, bool ready);

	/** @brief Returns the bucket that owns this render proxy. */
	GrassBucket* FindBucket(const RE::BSGeometry* shape);

	/** @brief Retains the current registration while holding the lifecycle lock. */
	std::shared_ptr<GrassShape> FindShape(RE::BSMultiStreamInstanceTriShape* shape);

	/** @brief Returns whether this frame's optimized draw covers every native instance. */
	static bool IsShapeOptimized(const GrassShape& shape);

	/** @brief IsShapeOptimized for a published shape, cached per frame and per engine group count. */
	static bool IsShapeOptimized(const CullSnapshot::NativeShape& native, uint32_t frame);

	/** @brief Returns whether this native group has resident records in a ready bucket. */
	static bool IsGroupOptimized(const GrassShape& shape, uint32_t group);

	/** @brief Resolves LODs and refreshes conservative bounds before CPU culling. */
	void PrepareBucket(GrassBucket& bucket);

	/** @brief Recomputes a bucket's padded union AABB over all of its slices. */
	void UpdateCoarseBounds(GrassBucket& b);

	/** @brief Extends cached runs with uploaded slices and rebuilds invalidated runs. */
	void MergeSlicesIntoRuns(GrassBucket& b);

	/** @brief Uploads changed cull groups and returns false if allocation fails. */
	bool UploadCullGroups(GrassBucket& b, ID3D11DeviceContext* ctx);

	/** @brief Assigns visible buckets reusable output pages without copying previous contents. */
	bool PrepareCullOutputs(std::span<GrassBucket*> visible);

	/** @brief Shared indirect arguments for the current visible-bucket order. */
	std::unique_ptr<Buffer> arguments;

	std::unordered_map<BucketKey, GrassBucket, BucketKeyHash> buckets;
	GrassMeshLibrary meshLibrary;

private:
	/** @brief Interleaves a packed cell's coordinates so nearby cells sort together. */
	static uint64_t MortonCode(uint64_t cell);

	ankerl::unordered_dense::map<const RE::BSGeometry*, GrassBucket*> proxyBuckets;

	struct Removal
	{
		std::shared_ptr<GrassShape> shape;
		uint32_t engineGroup = UINT32_MAX;
		std::shared_ptr<GrassGroup> lifetime;
	};
	using PendingEvent = std::variant<PendingCapture, Removal>;

	class EngineGroupLock
	{
	public:
		EngineGroupLock();
		~EngineGroupLock();
		EngineGroupLock(const EngineGroupLock&) = delete;
		EngineGroupLock& operator=(const EngineGroupLock&) = delete;

	private:
		inline static REL::Relocation<volatile long*> word{ REL::RelocationID(524610, 411250) };
	};

	static void PackInstances(GrassChunk& slice, const void* sourceData);

	static std::optional<PendingCapture> CaptureMetadata(RE::BSMultiStreamInstanceTriShape* shape, uint32_t count, uint32_t stride);

	// The caller holds pendingMutex and EngineGroupLock while copying native group data.
	void CaptureEngineGroup(const std::shared_ptr<GrassShape>& registration, uint32_t engineGroup);

	// The caller holds pendingMutex while updating native group lifetimes.
	void QueueEvent(PendingEvent event);

	void PreparePending();

	void DrainPending();

	void ApplyCapture(PendingCapture& capture, std::unordered_set<GrassBucket*>& affected, std::vector<std::unique_ptr<GrassChunk>>& retired);

	void ApplyRemoval(const Removal& removal, std::unordered_set<GrassBucket*>& affected, std::vector<std::unique_ptr<GrassChunk>>& retired);

	void AppendSlice(GrassBucket& bucket, GrassChunk& chunk);

	/** @brief Removes detached chunk references and repairs the bucket's resident prefixes. */
	void PruneSlices(GrassBucket& bucket);

	void MigrateShapeSlices(GrassBucket& source, GrassBucket& destination, GrassShape& shape);

	void EraseBucket(GrassBucket& bucket);

	void UpdateShapeResidency(const GrassChunk& slice, bool resident);

	// Bound every instance allocation by D3D11's 32-bit buffer byte width.
	inline static constexpr uint32_t kMaxBucketInstances = UINT32_MAX / kSourceStride;
	static uint32_t ReservedInstanceCapacity(uint32_t needed);
	static uint32_t GrownInstanceCapacity(uint32_t current, uint32_t needed);
	static bool CanReduceInstanceCapacity(uint32_t capacity, uint32_t needed);

	void AdoptBucketBuffers(GrassBucket& b, GrassSourceBuffers& replacement, ID3D11DeviceContext* ctx);

	/** @brief Creates buffers of `capacity` on the allocation thread, or adopts a finished allocation that fits the resident data. Returns true when buffers were adopted this call. */
	bool ResizeBucketAsync(GrassBucket& b, uint32_t capacity, ID3D11DeviceContext* ctx);

	void QueueAllocation(std::shared_ptr<BucketAllocation> job);

	void RetireSlices(std::vector<std::unique_ptr<GrassChunk>>& slices);

	static std::unique_ptr<Buffer> CreateRawBuffer(uint64_t bytes, uint32_t bindings, uint32_t misc, const char* name);

	static bool CreateBucketSourceBuffers(GrassSourceBuffers& b, uint32_t capacity);

	static bool CreateBucketCullScratch(GrassCullBuffers& b, uint32_t capacity, uint32_t tiers);

	bool EnsureArgsCapacity(uint32_t buckets);

	/** @brief Slides resident slices down over removal gaps within a per-frame byte budget, keeping their groups pointed at the moved data. Returns true once no gaps remain. */
	bool DefragBucket(GrassBucket& b, uint64_t& budgetBytes, ID3D11DeviceContext* ctx);

	bool MoveInstances(GrassBucket& b, uint32_t oldFirst, uint32_t newFirst, uint32_t count, ID3D11DeviceContext* ctx);

	void AppendNewSlices(GrassBucket& bucket, uint64_t& budgetBytes, ID3D11DeviceContext* ctx);
	void UploadSlices(GrassBucket& bucket, uint32_t from, uint32_t to, uint32_t prefix, ID3D11DeviceContext* ctx);

	FrameParams frameParams;
	uint32_t argsCapacity = 0;
	std::vector<std::unique_ptr<GrassCullBuffers>> outputPages;
	uint64_t outputReserveBytes = 0;
	uint32_t outputReserveFrames = 0;
	std::unordered_set<GrassBucket*> maintenance;
	std::unique_ptr<BS::thread_pool<>> allocator;

	std::vector<PendingEvent> pendingEvents;
	std::vector<PendingEvent> readyEvents;
	std::unordered_map<RE::BSMultiStreamInstanceTriShape*, std::shared_ptr<GrassShape>> shapes;
	std::vector<std::shared_ptr<GrassShape>> activeShapes;

	std::mutex pendingMutex;
	bool preparing = false;
	bool stopping = false;
	bool snapshotDirty = true;

	std::vector<RE::NiPointer<RE::BSMultiStreamInstanceTriShape>> retiredProxies;
	// Like retiredProxies, the previous snapshot outlives one publish for readers still finishing the last frame.
	std::unique_ptr<const CullSnapshot> cullSnapshot;
	std::unique_ptr<const CullSnapshot> retiredSnapshot;
	std::atomic<const CullSnapshot*> publishedSnapshot{ nullptr };

	std::unique_ptr<Buffer> compactionScratch;
	std::unique_ptr<BS::thread_pool<>> preparation;
};
