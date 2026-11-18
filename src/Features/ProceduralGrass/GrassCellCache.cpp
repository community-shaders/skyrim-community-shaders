#include "GrassCellCache.h"

#include "Utils/Game.h"

#include <algorithm>
#include <bit>
#include <thread>

namespace
{
	// LAND subrecord four-character codes.
	constexpr uint32_t kVHGT = Util::FCC("VHGT");
	constexpr uint32_t kBTXT = Util::FCC("BTXT");
	constexpr uint32_t kATXT = Util::FCC("ATXT");
	constexpr uint32_t kVTXT = Util::FCC("VTXT");

	constexpr uint32_t kQuadrantPitch = PGrassCommon::QuadrantGrassPitch;      // 17 vertices per side
	constexpr uint32_t kQuadrantSamples = PGrassCommon::QuadrantGrassSamples;  // 17 x 17 = 289 samples
	constexpr uint32_t kCellVertexPitch = 33;

	// Swaps only fire on the rare big-endian file, matching the water cache.
	uint32_t Swap32(uint32_t v) { return _byteswap_ulong(v); }
	uint16_t Swap16(uint16_t v) { return _byteswap_ushort(v); }
	float SwapF(float v) { return std::bit_cast<float>(_byteswap_ulong(std::bit_cast<uint32_t>(v))); }

	// BTXT/ATXT share this 8-byte header: texture FormID, target quadrant, and layer index.
	struct LandscapeTextureHeader
	{
		RE::FormID landTexture;
		uint8_t quadrant;
		uint8_t unused;
		uint16_t layer;
	};
	static_assert(sizeof(LandscapeTextureHeader) == 8);

	// VTXT alpha entry of a quadrant-local vertex index and its blend opacity.
	struct VertexTextureAlpha
	{
		uint16_t position;
		uint8_t unused[2];
		float opacity;
	};
	static_assert(sizeof(VertexTextureAlpha) == 8);

	RE::TESLandTexture* ResolveLandTexture(RE::TESFile* file, RE::FormID rawFormID)
	{
		if (!rawFormID)
			return PGrassCommon::GetDefaultLandTexture();
		return RE::TESForm::LookupByID<RE::TESLandTexture>(file->GetRuntimeFormID(rawFormID));
	}
}

void GrassCellCache::BeginFrame(RE::TESWorldSpace* landWorldSpace)
{
	frame++;

	if (!pool) {
		// A small pool suffices since reads are light and cache once, without competing with the game's own workers.
		const unsigned hw = std::thread::hardware_concurrency();
		pool = std::make_unique<BS::thread_pool<>>(std::clamp(hw / 4u, 1u, 4u));
	}

	if (landWorldSpace == worldSpace)
		return;

	// Worldspace changed, so bump the generation to drop in-flight worker results on drain, then clear.
	++generation;
	worldSpace = landWorldSpace;

	auto loadedFiles = std::make_shared<std::vector<RE::TESFile*>>();
	if (landWorldSpace) {
		if (const auto dataHandler = RE::TESDataHandler::GetSingleton()) {
			for (auto* file : dataHandler->files) {
				if (file && file->compileIndex != 0xFF)
					loadedFiles->push_back(file);
			}
		}
	}

	files = std::move(loadedFiles);
	ClearCells();
}

void GrassCellCache::ClearCells()
{
	ready.clear();
	pending.clear();
	++readyVersion;
	std::scoped_lock lock(completedMutex);
	completed.clear();
}

void GrassCellCache::SetGrassMapPolicy(GrassTexturePolicyMap newTextureOverrides, bool newIgnoreGrassMap)
{
	if (ignoreGrassMap == newIgnoreGrassMap && *textureOverrides == newTextureOverrides)
		return;

	++generation;
	textureOverrides = std::make_shared<const GrassTexturePolicyMap>(std::move(newTextureOverrides));
	ignoreGrassMap = newIgnoreGrassMap;
	ClearCells();
}

void GrassCellCache::DrainCompleted(const size_t maxCount)
{
	std::vector<std::tuple<uint64_t, uint64_t, std::unique_ptr<CellGrass>>> drained;
	{
		std::scoped_lock lock(completedMutex);
		const size_t count = std::min(maxCount, completed.size());
		drained.reserve(count);
		for (size_t i = 0; i < count; ++i) {
			drained.emplace_back(std::move(completed.front()));
			completed.pop_front();
		}
	}

	bool changed = false;
	for (auto& [key, taskGen, data] : drained) {
		if (taskGen != generation)
			continue;  // read belongs to a previous worldspace
		pending.erase(key);
		for (auto& cacheVersion : data->quadrantCacheVersions)
			cacheVersion = nextCacheVersion++;
		ready.insert_or_assign(key, CachedCell{ std::move(data), frame });
		changed = true;
	}
	if (changed)
		++readyVersion;
}

const CellGrass* GrassCellCache::Get(int32_t cellX, int32_t cellY)
{
	const uint64_t key = PGrassCommon::GrassCellKey(cellX, cellY);

	if (const auto it = ready.find(key); it != ready.end()) {
		it->second.lastTouched = frame;
		return it->second.data.get();
	}

	return nullptr;
}

bool GrassCellCache::Request(int32_t cellX, int32_t cellY)
{
	const uint64_t key = PGrassCommon::GrassCellKey(cellX, cellY);
	if (ready.contains(key) || !worldSpace || !files || files->empty() || !pool || pending.contains(key))
		return false;

	pending.insert(key);
	const uint64_t gen = generation;
	RE::TESWorldSpace* ws = worldSpace;
	const auto fileList = files;
	const auto grassTextureOverrides = textureOverrides;
	const bool ignoreMap = ignoreGrassMap;

	pool->detach_task([this, key, cellX, cellY, ws, fileList, grassTextureOverrides, ignoreMap, gen] {
		auto data = ReadCell(ws, fileList, cellX, cellY, grassTextureOverrides, ignoreMap);
		std::scoped_lock lock(completedMutex);
		completed.emplace_back(key, gen, std::move(data));
	});

	return true;
}

void GrassCellCache::EvictUntouched(const int32_t centerX, const int32_t centerY, const int32_t keepRadius)
{
	constexpr size_t kMaxCachedCells = 2048;
	if (ready.size() <= kMaxCachedCells)
		return;

	std::vector<std::pair<uint64_t, uint64_t>> byAge;  // (last-touched frame, key)
	byAge.reserve(ready.size());
	for (const auto& [key, cell] : ready) {
		const int32_t cellX = static_cast<int32_t>(key >> 32);
		const int32_t cellY = static_cast<int32_t>(key & 0xFFFFFFFFu);
		if (std::max(std::abs(cellX - centerX), std::abs(cellY - centerY)) <= keepRadius)
			continue;
		byAge.emplace_back(cell.lastTouched, key);
	}
	std::ranges::sort(byAge);  // oldest first

	const size_t toRemove = std::min(ready.size() - kMaxCachedCells, byAge.size());
	bool changed = false;
	for (size_t i = 0; i < toRemove; ++i) {
		if (byAge[i].first == frame)
			break;

		ready.erase(byAge[i].second);
		changed = true;
	}
	if (changed)
		++readyVersion;
}

void GrassCellCache::Shutdown()
{
	if (pool) {
		pool->wait();
		pool.reset();
	}
	ClearCells();
}

std::unique_ptr<CellGrass> GrassCellCache::ReadCell(RE::TESWorldSpace* worldSpace, std::shared_ptr<const std::vector<RE::TESFile*>> files, int32_t cellX, int32_t cellY,
	std::shared_ptr<const GrassTexturePolicyMap> textureOverrides, bool ignoreGrassMap)
{
	auto cell = std::make_unique<CellGrass>();
	for (auto& quad : cell->heights)
		quad.fill(PGrassCommon::QuadrantNoHeight);
	cell->minHeights.fill(PGrassCommon::QuadrantNoHeight);
	cell->maxHeights.fill(PGrassCommon::QuadrantNoHeight);

	if (!files || files->empty())
		return cell;

	const int32_t fileCount = static_cast<int32_t>(files->size());
	auto* const fileData = files->data();

	// Highest load order wins, so search files back to front for the one that overrides this cell's LAND. On a worker
	// thread Duplicate returns this thread's cached copy of the file, as TESObjectLAND::LoadVerticesIntoArrays uses it.
	for (int32_t i = fileCount - 1; i >= 0; --i) {
		RE::TESFile* file = fileData[i]->Duplicate();
		if (file && file->SeekCell(worldSpace, cellX, cellY) && file->SeekLandscapeForCurrentCell()) {
			ParseLandscape(file, *cell, cellX, cellY, *textureOverrides, ignoreGrassMap);
			break;
		}
	}

	return cell;
}

void GrassCellCache::ParseLandscape(RE::TESFile* file, CellGrass& out, int32_t cellX, int32_t cellY,
	const GrassTexturePolicyMap& textureOverrides, bool ignoreGrassMap)
{
	const bool bigEndian = file->isBigEndian;

	// Decoded as TESObjectLAND::LoadVerticesIntoArrays does, so streamed cells match loaded ones sample for sample.
	std::array<PGrassCommon::QuadrantLandTextures, 4> textures{};
	std::array<std::array<std::array<uint8_t, PGrassCommon::LandscapeOverlayCount>, kQuadrantSamples>, 4> percents{};

	int32_t activeQuadrant = -1;
	int32_t activeLayer = -1;

	while (file->SeekNextSubrecord()) {
		const uint32_t recordType = file->GetCurrentSubRecordType();
		const uint32_t recordSize = file->GetCurrentSubRecordSize();

		if (recordType == kVHGT) {
			if (recordSize < 4 + kCellVertexPitch * kCellVertexPitch + 3)
				continue;

			struct VHGT
			{
				float offset;
				int8_t deltas[kCellVertexPitch * kCellVertexPitch];
				uint8_t pad[3];
			} data{};

			file->ReadData(&data, sizeof(VHGT));

			// VHGT stores a row-start delta followed by deltas across that row. Decode it to world units first.
			float accumulatedRowHeight = bigEndian ? SwapF(data.offset) : data.offset;
			float cellHeights[kCellVertexPitch * kCellVertexPitch];

			for (uint32_t y = 0; y < kCellVertexPitch; ++y) {
				accumulatedRowHeight += static_cast<float>(data.deltas[y * kCellVertexPitch]);
				float accumulatedHeight = accumulatedRowHeight;

				for (uint32_t x = 0; x < kCellVertexPitch; ++x) {
					if (x != 0)
						accumulatedHeight += static_cast<float>(data.deltas[y * kCellVertexPitch + x]);

					cellHeights[y * kCellVertexPitch + x] = accumulatedHeight * 8.0f;
				}
			}

			// Split the 33x33 cell grid into four 17x17 quadrants that share the middle row and column.
			for (uint32_t qy = 0; qy < 2; ++qy) {
				for (uint32_t qx = 0; qx < 2; ++qx) {
					const uint32_t quad = qy * 2 + qx;
					for (uint32_t localY = 0; localY < kQuadrantPitch; ++localY) {
						for (uint32_t localX = 0; localX < kQuadrantPitch; ++localX) {
							const uint32_t heightX = qx * (kQuadrantPitch - 1) + localX;
							const uint32_t heightY = qy * (kQuadrantPitch - 1) + localY;
							out.heights[quad][localY * kQuadrantPitch + localX] = cellHeights[heightY * kCellVertexPitch + heightX];
						}
					}
				}
			}

		} else if (recordType == kBTXT || recordType == kATXT) {
			if (recordSize < sizeof(LandscapeTextureHeader))
				continue;

			LandscapeTextureHeader header{};
			file->ReadData(&header, sizeof(header));
			const RE::FormID formID = bigEndian ? Swap32(header.landTexture) : header.landTexture;

			if (recordType == kBTXT) {
				if (header.quadrant < 4)
					textures[header.quadrant].base = ResolveLandTexture(file, formID);
				continue;
			}

			// The engine clamps the layer to its sixth slot, which VTXT never fills, and keeps the layer active even
			// when its texture fails to resolve.
			const uint32_t layer = std::min<uint32_t>(bigEndian ? Swap16(header.layer) : header.layer, PGrassCommon::LandscapeOverlayCount);
			activeQuadrant = header.quadrant;
			activeLayer = static_cast<int32_t>(layer);
			if (header.quadrant < 4 && layer < PGrassCommon::LandscapeOverlayCount)
				textures[header.quadrant].layers[layer] = ResolveLandTexture(file, formID);

		} else if (recordType == kVTXT) {
			const bool validTarget = activeQuadrant >= 0 && activeQuadrant < 4 && activeLayer >= 0 && activeLayer < static_cast<int32_t>(PGrassCommon::LandscapeOverlayCount);
			if (validTarget && recordSize % sizeof(VertexTextureAlpha) == 0) {
				const uint32_t pointCount = recordSize / sizeof(VertexTextureAlpha);
				std::vector<VertexTextureAlpha> points(pointCount);
				file->ReadData(points.data(), pointCount * sizeof(VertexTextureAlpha));

				for (const VertexTextureAlpha& point : points) {
					const uint16_t position = bigEndian ? Swap16(point.position) : point.position;
					const float opacity = bigEndian ? SwapF(point.opacity) : point.opacity;
					if (position < kQuadrantSamples)
						percents[activeQuadrant][position][activeLayer] = opacity <= 0.0f ? uint8_t{ 0 } : static_cast<uint8_t>(static_cast<int32_t>(opacity * 255.0f));
				}
			}
			activeQuadrant = -1;
			activeLayer = -1;
		}
	}

	const auto getSelection = [&](const RE::TESLandTexture* texture) -> const PGrassCommon::GrassTextureSelection* {
		const auto it = textureOverrides.find(texture);
		return it != textureOverrides.end() ? &it->second : nullptr;
	};

	for (uint32_t quad = 0; quad < 4; ++quad) {
		out.ids[quad] = PGrassCommon::BuildQuadrantGrassIds(textures[quad], percents[quad][0].data(), PGrassCommon::LandscapeOverlayCount,
			cellX, cellY, quad, ignoreGrassMap, getSelection);

		const auto [minIt, maxIt] = std::minmax_element(out.heights[quad].begin(), out.heights[quad].end());
		if (*maxIt > PGrassCommon::QuadrantNoHeight) {
			out.minHeights[quad] = *minIt;
			out.maxHeights[quad] = *maxIt;
		}
	}
}
