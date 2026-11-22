#pragma once

#include <span>
#include <variant>

#include "Buffer.h"
#include "GrassBucket.h"

/** @brief Retains a captured group and its resources until the render thread admits it. */
struct PendingCapture
{
	GrassInstanceSlice slice;
	std::vector<uint8_t> sourceData;
	std::shared_ptr<GrassShapeRegistration> shape;
	GrassDrawResources drawResources;
};

/** @brief Owns grass buckets, staged lifecycle events and GPU resources. */
class GrassBucketStore
{
public:
	/** @brief Settings used for this frame's uploads and LOD selection. */
	struct FrameParams
	{
		bool enableMeshLOD = false;
		bool enableMidLOD = false;
		bool enableFarLOD = false;
		float fadeStart = 0.0f;  // Slices uploaded this frame begin fading in at this time.
	};

	/** @brief Proxy groups published by the render thread for drawing. */
	struct CullSnapshot
	{
		/** @brief Buckets with matching engine pass state, drawn through one proxy. */
		struct ProxyGroup
		{
			RE::NiPointer<RE::BSMultiStreamInstanceTriShape> proxy;
			std::vector<GrassBucket*> buckets;
		};

		/** @brief Returns the bucket group this proxy draws, or null for any other geometry. */
		const ProxyGroup* FindGroup(const RE::BSGeometry* geometry) const
		{
			for (const auto& group : proxyGroups) {
				if (group.proxy.get() == geometry)
					return &group;
			}
			return nullptr;
		}

		// Bucket membership changes at frame end, before a replacement snapshot is published.
		std::vector<ProxyGroup> proxyGroups;
	};

	/** @brief Joins workers and releases engine references before the lifecycle mutex is destroyed. */
	~GrassBucketStore();

	/** @brief Updates this frame's upload and LOD settings. */
	void BeginFrame(const FrameParams& params)
	{
		frameParams = params;
	}

	/** @brief Uploads admitted slices without changing this frame's bucket membership. */
	void UploadPending(ID3D11DeviceContext* ctx);

	/** @brief Applies completed lifecycle events and publishes the next frame's proxies. */
	void EndFrame();

	/** @brief Registers a model and queues captures of its existing instance groups. */
	void RegisterModel(RE::BSMultiStreamInstanceTriShape* shape, const char* modelPath);

	/** @brief Copies an engine group's instance data for background packing. */
	void CaptureEngineGroup(RE::BSMultiStreamInstanceTriShape* shape, uint32_t engineGroup);

	/** @brief Captures attached cache groups that have not already been queued. */
	void CaptureQueuedGroups();

	/** @brief Removes the engine group, invalidates its captures, and queues render-thread removal. */
	void RemoveEngineGroup(RE::BSMultiStreamInstanceTriShape* shape, uint32_t engineGroup,
		void (*remove)(RE::BSMultiStreamInstanceTriShape*, uint32_t));

	/** @brief Invalidates a shape's registration and queues removal of its captured chunks. */
	void StageRemoval(RE::BSMultiStreamInstanceTriShape* shape);

	/** @brief Returns the current draw snapshot; consume its bucket pointers before frame-end updates. */
	const CullSnapshot* GetCullSnapshot() const { return publishedSnapshot.load(std::memory_order_acquire); }

	/** @brief Publishes the proxy set after frame-end lifecycle changes. */
	void PublishCullSnapshot();

	/** @brief Looks up and retains a registration under the lifecycle mutex. */
	std::shared_ptr<GrassShapeRegistration> FindShape(RE::BSMultiStreamInstanceTriShape* shape);

	/** @brief Resolves LOD meshes and updates bounds padding before culling. */
	void PrepareBucket(GrassBucket& bucket);

	/** @brief Adds uploaded slices to cached runs, rebuilding invalidated runs. */
	void MergeSlicesIntoRuns(GrassBucket& bucket);

	/** @brief Recomputes the coarse bounds from the live cached runs. */
	static void RecomputeCoarseBounds(GrassBucket& bucket);

	/** @brief Uploads changed cull groups up to the bucket's available capacity. */
	void UploadCullGroups(GrassBucket& bucket, ID3D11DeviceContext* ctx);

	/** @brief Allocates shared cull output and assigns each visible bucket a range. */
	bool PrepareCullOutputs(std::span<GrassBucket*> visible);

	/** @brief Shared indirect arguments for the current visible-bucket order. */
	std::unique_ptr<Buffer> arguments;

	/** @brief Compacted records written by culling and read by the draws. */
	GrassCullBuffers cullOutput;

	/** @brief Packed source records of every bucket, read by culling and the vertex shader. */
	ID3D11ShaderResourceView* InstancePoolSRV() const { return instancePool.buffer ? instancePool.buffer->srv.get() : nullptr; }
	/** @brief Shared cull groups read by culling and the vertex shader. */
	ID3D11ShaderResourceView* GroupPoolSRV() const { return groupPool.buffer ? groupPool.buffer->srv.get() : nullptr; }

	/** @brief Creates a shader-readable structured buffer with its SRV; throws on failure. */
	static std::unique_ptr<Buffer> CreateStructuredBuffer(uint32_t count, uint32_t stride, const char* name);

	// Records address groups relative to their bucket, so the group pool is bounded only by the 128 MB every device supports;
	// raw views address at most 2^27 DWORDs of instances.
	inline static constexpr uint32_t kMaxPoolGroups = (128u << 20) / sizeof(CullGroup);
	inline static constexpr uint32_t kMaxPoolInstances = (1u << 27) * 4 / kSourceStride;

	std::unordered_map<GrassBucketKey, GrassBucket, GrassBucketKeyHash> buckets;
	GrassMeshLibrary meshLibrary;

private:
	struct Removal
	{
		std::shared_ptr<GrassShapeRegistration> shape;
		uint32_t engineGroup = UINT32_MAX;
		std::shared_ptr<GrassGroupLifetime> lifetime;
	};
	using PendingEvent = std::variant<PendingCapture, Removal>;

	/** @brief Locks the engine's shared instance-group table. */
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

	/** @brief Packs native 32-byte records into the layout decoded by Common/GrassInstance.hlsli. */
	static void PackInstances(GrassInstanceSlice& slice, const void* sourceData);

	static std::optional<PendingCapture> CaptureMetadata(RE::BSMultiStreamInstanceTriShape* shape, uint32_t count, uint32_t stride);

	// Requires pendingMutex and EngineGroupLock while reading native group data.
	void CaptureEngineGroup(const std::shared_ptr<GrassShapeRegistration>& registration, uint32_t engineGroup);

	// Requires pendingMutex while invalidating lifetimes and updating the queues.
	void QueueEvent(PendingEvent event);

	/** @brief Packs current captures on the worker and transfers them to readyEvents. */
	void PreparePending();

	/** @brief Applies ready events and prunes detached chunks on the render thread. */
	void DrainPending();

	void ApplyCapture(PendingCapture& capture, std::unordered_set<GrassBucket*>& affected, std::vector<std::unique_ptr<GrassInstanceSlice>>& retired);

	void ApplyRemoval(const Removal& removal, std::unordered_set<GrassBucket*>& affected, std::vector<std::unique_ptr<GrassInstanceSlice>>& retired);

	void AppendSlice(GrassBucket& bucket, GrassInstanceSlice& chunk);

	/** @brief Removes detached chunk references and repairs the bucket's resident prefixes. */
	void PruneSlices(GrassBucket& bucket);

	/** @brief Removes a slice's groups from its cached run, emptying the run once none remain. */
	static void RetireRunGroups(GrassBucket& bucket, uint32_t firstGroup, uint32_t groupCount);

	void MigrateShapeSlices(GrassBucket& source, GrassBucket& destination, GrassShapeRegistration& shape);

	void EraseBucket(GrassBucket& bucket);

	static uint32_t ReservedInstanceCapacity(uint32_t needed);
	static bool CanReduceInstanceCapacity(uint32_t capacity, uint32_t needed);

	/** @brief Starts creating a pool buffer with room for `extra` more elements off the render thread, unless one is
	    already pending or `maxCapacity` cannot fit them. */
	void RequestPoolGrowth(const GrassPool& pool, std::shared_ptr<GrassPoolGrowth>& growth, uint32_t extra, uint32_t minCapacity, uint32_t maxCapacity,
		std::unique_ptr<Buffer> (*create)(uint32_t));

	/** @brief Adopts a finished pool buffer, copying every existing range to the same offset. */
	static void AdoptPoolGrowth(GrassPool& pool, std::shared_ptr<GrassPoolGrowth>& growth, uint32_t stride, ID3D11DeviceContext* ctx);

	void RetireSlices(std::vector<std::unique_ptr<GrassInstanceSlice>>& slices);

	static std::unique_ptr<Buffer> CreateRawBuffer(uint64_t bytes, uint32_t bindings, uint32_t misc, const char* name);

	static std::unique_ptr<Buffer> CreateInstancePoolBuffer(uint32_t capacity);
	static std::unique_ptr<Buffer> CreateGroupPoolBuffer(uint32_t capacity);

	static bool CreateCullOutput(GrassCullBuffers& buffers, uint32_t capacity);

	bool EnsureArgsCapacity(uint32_t count);

	/** @brief Returns the end slice index that fits the budget, and outputs the selected byte count. */
	static uint32_t SelectUploadSlices(const GrassBucket& bucket, uint64_t& budgetBytes, uint64_t& uploadBytes);
	/** @brief Uploads pending slices within the frame's budget through one staging map, into per-slice pool ranges. */
	void UploadNewSlices(ID3D11DeviceContext* ctx);
	/** @brief Maps room for `bytes` in the staging ring and outputs its offset; null when it cannot be mapped. */
	uint8_t* MapUploadStaging(ID3D11DeviceContext* ctx, uint64_t bytes, uint64_t& offset);
	/** @brief Queues a staging-to-pool copy, extending the previous copy when both ranges continue it. */
	void AddUploadCopy(uint64_t stagingOffset, uint32_t poolOffset, uint32_t count);

	FrameParams frameParams;
	uint32_t argsCapacity = 0;
	std::unordered_set<GrassBucket*> pendingUploads;
	std::unique_ptr<BS::thread_pool<>> allocator;

	std::vector<PendingCapture> pendingCaptures;
	std::vector<PendingEvent> readyEvents;
	std::unordered_map<RE::BSMultiStreamInstanceTriShape*, std::shared_ptr<GrassShapeRegistration>> shapes;
	std::vector<std::shared_ptr<GrassShapeRegistration>> activeShapes;

	// Protects registrations, group lifetimes, and both event queues.
	std::mutex pendingMutex;
	bool preparing = false;
	bool stopping = false;
	bool snapshotDirty = true;

	std::vector<RE::NiPointer<RE::BSMultiStreamInstanceTriShape>> retiredProxies;
	// Retain the previous snapshot across one publication for readers finishing the last frame.
	std::unique_ptr<const CullSnapshot> cullSnapshot;
	std::unique_ptr<const CullSnapshot> retiredSnapshot;
	std::atomic<const CullSnapshot*> publishedSnapshot{ nullptr };

	// Stage new slices in a dynamic ring, then copy them into the shared pool.
	std::unique_ptr<Buffer> uploadStaging;
	uint64_t uploadStagingOffset = 0;  // End of the bytes written since the ring was last discarded.

	struct UploadBatch
	{
		GrassBucket* bucket;
		uint32_t endSlice;
		uint32_t instances;
	};
	std::vector<UploadBatch> uploadBatches;

	struct UploadCopy
	{
		uint64_t stagingOffset;
		uint32_t poolOffset;
		uint32_t count;
	};
	std::vector<UploadCopy> uploadCopies;

	GrassPool instancePool;
	GrassPool groupPool;

	std::shared_ptr<GrassPoolGrowth> instancePoolGrowth;
	std::shared_ptr<GrassPoolGrowth> groupPoolGrowth;
	std::unique_ptr<BS::thread_pool<>> preparation;
};
