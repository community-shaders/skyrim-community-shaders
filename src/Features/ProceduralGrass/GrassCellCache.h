#pragma once

#include "PGrassCommon.h"

#include <array>
#include <deque>
#include <memory>
#include <mutex>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <BS_thread_pool.hpp>

/**
 * @brief One exterior cell's grass inputs for the far tier, read from plugin LAND records for cells beyond the loaded grid.
 * Four quadrants, each a 17x17 grid of world Z and grass-or-bare ids, laid out as the runtime path produces.
 */
struct CellGrass
{
	std::array<uint64_t, 4> quadrantCacheVersions{};
	std::array<std::array<uint8_t, PGrassCommon::QuadrantGrassSamples>, 4> ids{};  // filled grass ids
	std::array<std::array<float, PGrassCommon::QuadrantGrassSamples>, 4> heights{};
	std::array<float, 4> minHeights{};
	std::array<float, 4> maxHeights{};
};

using GrassTexturePolicyMap = std::unordered_map<const RE::TESLandTexture*, PGrassCommon::GrassTextureSelection>;

/**
 * @brief Background reader + cache of per-cell grass data, mirroring the water cache's file-seeking.
 *
 * Reads run on a worker pool so first sight never hitches the render thread. Workers hand finished cells
 * to the main thread through a locked queue. Each worker reads through TESFile::Duplicate, which on a non-main thread
 * returns that thread's copy of the file, created once and owned by the file's per-thread map; the copy is reused by
 * later reads on the same thread and must not be closed or freed here.
 */
class GrassCellCache
{
public:
	~GrassCellCache() { Shutdown(); }

	/** @brief Per-frame setup on the main thread. Rebuilds from scratch when the worldspace changes. */
	void BeginFrame(RE::TESWorldSpace* landWorldSpace);

	/** @brief Updates LTEX grass overrides used by streamed LAND reads and invalidates stale cells. */
	void SetGrassMapPolicy(GrassTexturePolicyMap textureOverrides, bool ignoreGrassMap);

	/** @brief Folds up to maxCount finished background reads into the readable map. Main thread only. */
	void DrainCompleted(size_t maxCount);

	/** @brief Returns a cached cell and marks it used this frame. */
	const CellGrass* Get(int32_t cellX, int32_t cellY);

	/** @brief Enqueues one missing cell. Returns true only when a new request was queued. */
	bool Request(int32_t cellX, int32_t cellY);

	uint64_t GetReadyVersion() const { return readyVersion; }

	/**
	 * @brief Drops the least recently used cells over the cache limit. Main thread only.
	 * Cells within @p keepRadius of the centre are kept: tier lists hold pointers into them between rebuilds.
	 */
	void EvictUntouched(int32_t centerX, int32_t centerY, int32_t keepRadius);

	void Shutdown();

private:
	/** @brief Drops every cached, pending and completed cell. Main thread only. */
	void ClearCells();

	static std::unique_ptr<CellGrass> ReadCell(RE::TESWorldSpace* worldSpace, std::shared_ptr<const std::vector<RE::TESFile*>> files, int32_t cellX, int32_t cellY,
		std::shared_ptr<const GrassTexturePolicyMap> textureOverrides, bool ignoreGrassMap);
	static void ParseLandscape(RE::TESFile* file, CellGrass& out, int32_t cellX, int32_t cellY,
		const GrassTexturePolicyMap& textureOverrides, bool ignoreGrassMap);

	struct CachedCell
	{
		std::unique_ptr<CellGrass> data;
		uint64_t lastTouched;
	};

	std::unordered_map<uint64_t, CachedCell> ready;  // main-thread only
	std::unordered_set<uint64_t> pending;            // enqueued keys, main-thread only

	std::mutex completedMutex;
	std::deque<std::tuple<uint64_t, uint64_t, std::unique_ptr<CellGrass>>> completed;  // key, generation, data

	std::unique_ptr<BS::thread_pool<>> pool;
	RE::TESWorldSpace* worldSpace = nullptr;
	std::shared_ptr<const std::vector<RE::TESFile*>> files = std::make_shared<const std::vector<RE::TESFile*>>();
	std::shared_ptr<const GrassTexturePolicyMap> textureOverrides =
		std::make_shared<const GrassTexturePolicyMap>();
	bool ignoreGrassMap = false;
	uint64_t generation = 0;  // Main-thread epoch captured by each request; stale completions are dropped.
	uint64_t frame = 0;
	uint64_t nextCacheVersion = 1;
	uint64_t readyVersion = 1;
};
