#include "Features/ProceduralGrass.h"

#include "Globals.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace
{
	/** @brief Floor division by 2, so negative cell coordinates map to the correct quadrant. */
	constexpr int32_t FloorDiv2(int32_t a)
	{
		return a >> 1;
	}

	/** @brief Index of a world quadrant within its cell's four quadrants. */
	constexpr uint32_t CellQuadrantIndex(int32_t quadX, int32_t quadY)
	{
		return static_cast<uint32_t>(quadY & 1) * 2 + static_cast<uint32_t>(quadX & 1);
	}

	/** @brief Builds a quadrant slot for one of a cell's four quadrants. */
	PGrassCommon::Quadrant MakeQuadrant(int32_t cellX, int32_t cellY, uint32_t index, uint64_t cacheVersion,
		const uint8_t* grassIds, const float* heights, float minHeight, float maxHeight)
	{
		PGrassCommon::Quadrant quadrant{};
		quadrant.cellX = cellX;
		quadrant.cellY = cellY;
		quadrant.x = index % 2;
		quadrant.y = index / 2;
		quadrant.cacheVersion = cacheVersion;
		quadrant.inUse = true;
		quadrant.grassIds = grassIds;
		quadrant.heights = heights;
		quadrant.worldPos = float2{ quadrant.WorldX() * 2048.0f, quadrant.WorldY() * 2048.0f };
		quadrant.minHeight = minHeight;
		quadrant.maxHeight = maxHeight;
		return quadrant;
	}

	/** @brief Builds a quadrant from streamed LAND data for one of a cell's four quadrants. */
	PGrassCommon::Quadrant MakeStreamedQuadrant(const CellGrass& cellGrass, int32_t cellX, int32_t cellY, uint32_t index)
	{
		return MakeQuadrant(cellX, cellY, index, cellGrass.quadrantCacheVersions[index], cellGrass.ids[index].data(),
			cellGrass.heights[index].data(), cellGrass.minHeights[index], cellGrass.maxHeights[index]);
	}
}

int32_t ProceduralGrass::NearCoverageIndex(int32_t worldQuadrantX, int32_t worldQuadrantY, const VisibilityOrigin& origin)
{
	const int32_t x = worldQuadrantX - origin.quadrantX + NearCoverageRadius;
	const int32_t y = worldQuadrantY - origin.quadrantY + NearCoverageRadius;
	if (x < 0 || y < 0 || x >= NearCoverageDiameter || y >= NearCoverageDiameter)
		return -1;
	return y * NearCoverageDiameter + x;
}

RE::TESLandTexture* PGrassCommon::GetDefaultLandTexture()
{
	static const auto defaultLandTextureAddress = REL::Relocation<RE::TESLandTexture**>(RELOCATION_ID(514783, 400936));
	return *defaultLandTextureAddress;
}

float PGrassCommon::GetGrassTexturePctThreshold()
{
	if (const auto setting = RE::GetINISetting("fTexturePctThreshold:Grass"))
		return std::max(setting->GetFloat(), 0.0f);
	return 0.005f;
}

const ProceduralGrass::LoadedCellGrass& ProceduralGrass::GetCellCache(RE::TESObjectLAND* land, int32_t cellX, int32_t cellY)
{
	auto& cell = grassMapCache[PGrassCommon::GrassCellKey(cellX, cellY)];
	cell.lastSeenFrame = grassMapFrame;
	const auto landData = land->loadedData;

	if (cell.land == land && cell.loadedData == landData)
		return cell;

	cell.land = land;
	cell.loadedData = landData;

	// Loaded heights can be record-relative; one anchor for all four quadrants moves the lowest sample to heightExtents.x without seams.
	float rawMin = (std::numeric_limits<float>::max)();
	for (uint32_t quadIndex = 0; quadIndex < 4; ++quadIndex)
		rawMin = std::min(rawMin, *std::min_element(landData->heights[quadIndex], landData->heights[quadIndex] + PGrassCommon::QuadrantGrassSamples));
	const float anchor = landData->heightExtents.x - rawMin;

	const auto getSelection = [this](const RE::TESLandTexture* texture) -> const PGrassCommon::GrassTextureSelection* {
		if (const auto cached = textureSelectionByTexture.find(texture); cached != textureSelectionByTexture.end())
			return cached->second;
		const auto selection = textureSelection.find(LandTextureKey(texture));
		const auto result = selection != textureSelection.end() ? &selection->second : nullptr;
		textureSelectionByTexture.emplace(texture, result);
		return result;
	};

	for (uint32_t quadIndex = 0; quadIndex < 4; ++quadIndex) {
		auto& quadrant = cell.quadrants[quadIndex];
		quadrant.cacheVersion = nextGrassCacheVersion++;
		std::transform(landData->heights[quadIndex], landData->heights[quadIndex] + PGrassCommon::QuadrantGrassSamples, quadrant.heights.begin(),
			[anchor](const float height) { return height + anchor; });
		const auto [minIt, maxIt] = std::minmax_element(quadrant.heights.begin(), quadrant.heights.end());
		quadrant.minHeight = *minIt;
		quadrant.maxHeight = *maxIt;

		PGrassCommon::QuadrantLandTextures textures{};
		textures.base = landData->defQuadTextures[quadIndex];
		for (uint32_t layer = 0; layer < PGrassCommon::LandscapeOverlayCount; ++layer)
			textures.layers[layer] = landData->quadTextures[quadIndex][layer];
		quadrant.ids = PGrassCommon::BuildQuadrantGrassIds(textures, reinterpret_cast<const uint8_t*>(landData->percents[quadIndex][0]),
			static_cast<uint32_t>(std::size(landData->percents[quadIndex][0])), cellX, cellY, quadIndex, settings.debugIgnoreGrassMap, getSelection);
	}

	return cell;
}

void ProceduralGrass::SyncGrassCellCachePolicy()
{
	const auto dataHandler = RE::TESDataHandler::GetSingleton();
	if (!dataHandler)
		return;

	GrassTexturePolicyMap textureOverrides;
	textureOverrides.reserve(textureSelection.size());
	for (const auto texture : dataHandler->GetFormArray<RE::TESLandTexture>()) {
		if (!texture)
			continue;

		const auto selection = textureSelection.find(LandTextureKey(texture));
		if (selection == textureSelection.end() || selection->second.total <= 0.0f)
			continue;

		textureOverrides.emplace(texture, selection->second);
	}

	grassCellCache.SetGrassMapPolicy(std::move(textureOverrides), settings.debugIgnoreGrassMap);
	grassCellCachePolicyDirty = false;
}

void ProceduralGrass::ClearGrassMapCache()
{
	grassCellCachePolicyDirty = true;
	SyncGrassCellCachePolicy();
	grassMapCache.clear();
	++grassMapCacheVersion;
	nearVisibleStamp = (std::numeric_limits<uint64_t>::max)();
	quadrantsHighLOD.clear();
	quadrantsMidLOD.clear();
	quadrantsLowLOD.clear();
	quadrantsPresence.clear();
	nearCoveredQuadrants.fill(false);
	++quadrantsHighVersion;
	++quadrantsMidVersion;
	++quadrantsLowVersion;
}

void ProceduralGrass::EvictGrassMapCache()
{
	if (grassMapCache.size() <= grassMapCacheCapacity)
		return;

	std::vector<std::pair<uint64_t, uint64_t>> byAge;
	byAge.reserve(grassMapCache.size());
	for (const auto& [key, cell] : grassMapCache)
		byAge.emplace_back(cell.lastSeenFrame, key);
	std::ranges::sort(byAge);

	const size_t removeCount = grassMapCache.size() - grassMapCacheCapacity;
	for (size_t i = 0; i < removeCount; ++i) {
		if (byAge[i].first == grassMapFrame)
			break;
		grassMapCache.erase(byAge[i].second);
	}
}

std::optional<float> ProceduralGrass::GetLandHeightAt(const float worldX, const float worldY) const
{
	const int32_t quadX = static_cast<int32_t>(std::floor(worldX / 2048.0f));
	const int32_t quadY = static_cast<int32_t>(std::floor(worldY / 2048.0f));
	const auto entry = grassMapCache.find(PGrassCommon::GrassCellKey(FloorDiv2(quadX), FloorDiv2(quadY)));
	if (entry == grassMapCache.end())
		return std::nullopt;
	const auto& quadrant = entry->second.quadrants[CellQuadrantIndex(quadX, quadY)];
	if (quadrant.heights[0] <= PGrassCommon::QuadrantNoHeight)
		return std::nullopt;

	const float localX = worldX - quadX * 2048.0f;
	const float localY = worldY - quadY * 2048.0f;
	const float spacing = 2048.0f / (PGrassCommon::QuadrantGrassPitch - 1);
	const float maxSample = PGrassCommon::QuadrantGrassPitch - 1.001f;
	const float sampleX = std::clamp(localX / spacing, 0.0f, maxSample);
	const float sampleY = std::clamp(localY / spacing, 0.0f, maxSample);

	const auto baseX = static_cast<uint32_t>(sampleX);
	const auto baseY = static_cast<uint32_t>(sampleY);
	const float fracX = sampleX - baseX;
	const float fracY = sampleY - baseY;

	const auto& h = quadrant.heights;
	const uint32_t i = baseY * PGrassCommon::QuadrantGrassPitch + baseX;

	return std::lerp(
		std::lerp(h[i], h[i + 1], fracX),
		std::lerp(h[i + PGrassCommon::QuadrantGrassPitch], h[i + PGrassCommon::QuadrantGrassPitch + 1], fracX),
		fracY);
}

void ProceduralGrass::GetVisibleQuadrants()
{
	globals::profiler->BeginPass("ProceduralGrass::Visible Quadrants");
	if (grassCellCachePolicyDirty)
		SyncGrassCellCachePolicy();

	grassMapFrame++;
	constexpr size_t streamCompletionBudget = 12;

	const auto tes = globals::game::tes;
	RE::TESWorldSpace* landWorldSpace = Util::GetLandDataWorldspace(tes ? tes->GetRuntimeData2().worldSpace : nullptr);

	grassCellCache.BeginFrame(landWorldSpace);
	grassCellCache.DrainCompleted(streamCompletionBudget);

	const VisibilityOrigin origin = GetVisibilityOrigin();

	// Centre the terrain-darkening grass-id window on the player.
	const int32_t presenceOriginQuadX = origin.quadrantX - PGrassCommon::LowTierQuadrantRadius;
	const int32_t presenceOriginQuadY = origin.quadrantY - PGrassCommon::LowTierQuadrantRadius;
	grassPresenceOrigin = float2{ presenceOriginQuadX * 2048.0f, presenceOriginQuadY * 2048.0f };

	const auto cells = tes ? tes->gridCells : nullptr;
	const uint64_t currentNearStamp = ComputeNearVisibilityStamp(landWorldSpace, cells, origin);
	if (currentNearStamp != nearVisibleStamp) {
		RebuildNearQuadrants(cells, origin);
		nearVisibleStamp = currentNearStamp;
	}
	// Runs every frame: it returns early unless its window or content changed, and a type-table change invalidates it.
	RebuildGrassPresence(presenceOriginQuadX, presenceOriginQuadY);

	RequestStreamedCells(landWorldSpace, origin);
	grassCellCache.EvictUntouched(origin.cellX, origin.cellY, StreamCellRadius);

	globals::profiler->EndPass();
}

uint64_t ProceduralGrass::ComputeNearVisibilityStamp(RE::TESWorldSpace* landWorldSpace, const RE::GridCellArray* cells, const VisibilityOrigin& origin)
{
	const uint32_t cellCount = cells ? cells->length * cells->length : 0u;
	uint64_t stamp = PGrassCommon::GrassHashOffsetBasis;
	PGrassCommon::GrassHashValue(stamp, reinterpret_cast<uintptr_t>(landWorldSpace));
	PGrassCommon::GrassHashValue(stamp, reinterpret_cast<uintptr_t>(cells));
	PGrassCommon::GrassHashValue(stamp, origin.quadrantX);
	PGrassCommon::GrassHashValue(stamp, origin.quadrantY);
	PGrassCommon::GrassHashValue(stamp, grassMapCacheVersion);
	PGrassCommon::GrassHashValue(stamp, settings.debugIgnorePreProcessedFlag);
	PGrassCommon::GrassHashValue(stamp, cellCount);

	for (uint32_t i = 0; i < cellCount; ++i) {
		const auto cell = cells->cells[i];
		PGrassCommon::GrassHashValue(stamp, reinterpret_cast<uintptr_t>(cell));
		if (!cell)
			continue;

		const auto& runtimeData = cell->GetRuntimeData();
		const auto exterior = runtimeData.cellData.exterior;
		const auto land = runtimeData.cellLand;
		PGrassCommon::GrassHashValue(stamp, reinterpret_cast<uintptr_t>(exterior));
		PGrassCommon::GrassHashValue(stamp, reinterpret_cast<uintptr_t>(land));
		if (exterior) {
			PGrassCommon::GrassHashValue(stamp, exterior->cellX);
			PGrassCommon::GrassHashValue(stamp, exterior->cellY);
		}
		if (!land || !land->loadedData)
			continue;

		PGrassCommon::GrassHashValue(stamp, reinterpret_cast<uintptr_t>(land->loadedData));
		for (uint32_t j = 0; j < 4; ++j) {
			const auto mesh = land->loadedData->mesh[j];
			PGrassCommon::GrassHashValue(stamp, reinterpret_cast<uintptr_t>(mesh));
			if (mesh) {
				const bool preProcessed = mesh->GetFlags().all(RE::NiAVObject::Flag::kPreProcessedNode);
				PGrassCommon::GrassHashValue(stamp, preProcessed);
			}
		}
	}

	// Cached LAND fills Low's outer ring when Skyrim unloads a nearby cell before it leaves Low range.
	for (int32_t cy = origin.cellY - LowCellRadius; cy <= origin.cellY + LowCellRadius; ++cy) {
		for (int32_t cx = origin.cellX - LowCellRadius; cx <= origin.cellX + LowCellRadius; ++cx) {
			const CellGrass* cellGrass = grassCellCache.Get(cx, cy);
			PGrassCommon::GrassHashValue(stamp, PGrassCommon::GrassCellKey(cx, cy));
			if (!cellGrass) {
				PGrassCommon::GrassHashValue(stamp, uint64_t{ 0 });
				continue;
			}

			for (const auto cacheVersion : cellGrass->quadrantCacheVersions)
				PGrassCommon::GrassHashValue(stamp, cacheVersion);
		}
	}
	return stamp;
}

void ProceduralGrass::RebuildNearQuadrants(const RE::GridCellArray* cells, const VisibilityOrigin& origin)
{
	quadrantsHighLOD.clear();
	quadrantsMidLOD.clear();
	quadrantsLowLOD.clear();
	quadrantsPresence.clear();
	nearCoveredQuadrants.fill(false);
	quadrantReject = {};

	// Overlap max-distance bands so adjacent tiers cross-fade.
	const auto addToTiers = [&](const PGrassCommon::Quadrant& quadrant, const int32_t md, const auto& append) {
		if (md <= PGrassCommon::HighTierQuadrantRadius)
			append(quadrantsHighLOD, quadrant, PGrassCommon::HighTierQuadrantCap);
		if (md >= PGrassCommon::HighTierQuadrantRadius - 1 && md <= PGrassCommon::MidTierQuadrantRadius)
			append(quadrantsMidLOD, quadrant, PGrassCommon::MidTierQuadrantCap);
		if (md >= PGrassCommon::MidTierQuadrantRadius - 2 && md <= NearCoverageRadius)
			append(quadrantsLowLOD, quadrant, PGrassCommon::LowTierQuadrantCap);
	};
	const auto appendBounded = [](auto& quadrants, const auto& quadrant, const size_t capacity) {
		if (quadrants.size() < capacity)
			quadrants.push_back(quadrant);
	};

	const uint32_t cellCount = cells ? cells->length * cells->length : 0u;
	for (uint32_t i = 0; i < cellCount; i++) {
		const auto cell = cells->cells[i];
		if (!cell)
			continue;
		quadrantReject.cells++;

		const auto& runtimeData = cell->GetRuntimeData();
		const auto exterior = runtimeData.cellData.exterior;
		if (!exterior)
			continue;
		quadrantReject.withExterior++;

		const auto land = runtimeData.cellLand;
		if (!land)
			continue;
		quadrantReject.withLand++;
		if (!land->loadedData)
			continue;
		quadrantReject.withLoadedData++;

		const LoadedCellGrass* cellCache = nullptr;
		for (uint32_t j = 0; j < 4; j++) {
			const auto mesh = land->loadedData->mesh[j];
			if (!mesh)
				continue;
			quadrantReject.withMesh++;
			if (!settings.debugIgnorePreProcessedFlag && !mesh->GetFlags().all(RE::NiAVObject::Flag::kPreProcessedNode))
				continue;
			quadrantReject.preProcessed++;

			if (!cellCache)
				cellCache = &GetCellCache(land, exterior->cellX, exterior->cellY);
			const auto& quadrantCache = cellCache->quadrants[j];
			const auto quadrant = MakeQuadrant(exterior->cellX, exterior->cellY, j, quadrantCache.cacheVersion, quadrantCache.ids.data(),
				quadrantCache.heights.data(), quadrantCache.minHeight, quadrantCache.maxHeight);

			const int32_t worldQuadrantX = quadrant.WorldX();
			const int32_t worldQuadrantY = quadrant.WorldY();
			const int32_t md = std::max(std::abs(origin.quadrantX - worldQuadrantX), std::abs(origin.quadrantY - worldQuadrantY));

			if (const int32_t coverageIndex = NearCoverageIndex(worldQuadrantX, worldQuadrantY, origin); coverageIndex >= 0) {
				// Record only near quadrants with accepted LAND geometry.
				nearCoveredQuadrants[coverageIndex] = true;
				if (md <= PGrassCommon::LowTierQuadrantRadius)
					quadrantsPresence.push_back(quadrant);
			}
			addToTiers(quadrant, md, appendBounded);
		}
	}

	// Fill Low's ring from streamed LAND where the loaded grid no longer covers it.
	for (int32_t cy = origin.cellY - LowCellRadius; cy <= origin.cellY + LowCellRadius; ++cy) {
		for (int32_t cx = origin.cellX - LowCellRadius; cx <= origin.cellX + LowCellRadius; ++cx) {
			const CellGrass* cellGrass = grassCellCache.Get(cx, cy);
			if (!cellGrass)
				continue;

			for (uint32_t j = 0; j < 4; ++j) {
				const int32_t worldQuadrantX = cx * 2 + static_cast<int32_t>(j % 2);
				const int32_t worldQuadrantY = cy * 2 + static_cast<int32_t>(j / 2);
				const int32_t md = std::max(std::abs(origin.quadrantX - worldQuadrantX), std::abs(origin.quadrantY - worldQuadrantY));
				if (md < PGrassCommon::MidTierQuadrantRadius - 2 || md > NearCoverageRadius)
					continue;

				const int32_t coverageIndex = NearCoverageIndex(worldQuadrantX, worldQuadrantY, origin);
				if (nearCoveredQuadrants[coverageIndex])
					continue;

				const auto quadrant = MakeStreamedQuadrant(*cellGrass, cx, cy, j);
				nearCoveredQuadrants[coverageIndex] = true;
				quadrantsLowLOD.push_back(quadrant);
				if (md <= PGrassCommon::LowTierQuadrantRadius)
					quadrantsPresence.push_back(quadrant);
			}
		}
	}

	++quadrantsHighVersion;
	++quadrantsMidVersion;
	++quadrantsLowVersion;

	// Retain recently unloaded LAND cells across grid boundaries.
	EvictGrassMapCache();
}

uint8_t ProceduralGrass::LimitGrassIdToSlope(const uint8_t id, const PGrassCommon::Quadrant& quadrant, const uint32_t column, const uint32_t row) const
{
	if (!id || !quadrant.heights || !quadrant.HasLand())
		return id;

	// Forward differences, taken backward on the quadrant's last row and column.
	constexpr uint32_t pitch = PGrassCommon::QuadrantGrassPitch;
	const uint32_t sample = std::min(row, pitch - 2u) * pitch + std::min(column, pitch - 2u);
	const float slopeX = (quadrant.heights[sample + 1] - quadrant.heights[sample]) / 128.0f;
	const float slopeY = (quadrant.heights[sample + pitch] - quadrant.heights[sample]) / 128.0f;
	const float normalZ = 1.0f / std::sqrt(slopeX * slopeX + slopeY * slopeY + 1.0f);
	const auto& type = resolvedGeneratorTypes.grassType[id];
	return normalZ < type.maxSlope || normalZ > type.minSlope ? uint8_t{ 0 } : id;
}

void ProceduralGrass::RebuildGrassPresence(const int32_t originQuadX, const int32_t originQuadY)
{
	uint64_t contentHash = PGrassCommon::GrassHashOffsetBasis;
	PGrassCommon::GrassHashValue(contentHash, quadrantsPresence.size());
	for (const auto& quadrant : quadrantsPresence) {
		PGrassCommon::GrassHashValue(contentHash, PGrassCommon::GrassQuadrantKey(quadrant.WorldX(), quadrant.WorldY()));
		PGrassCommon::GrassHashValue(contentHash, quadrant.cacheVersion);
	}

	// Rebuild the presence texture only when its window or cached LAND data changes.
	if (grassPresenceOriginQuadX == originQuadX && grassPresenceOriginQuadY == originQuadY && grassPresenceContentHash == contentHash)
		return;

	std::fill(grassPresenceStaging.begin(), grassPresenceStaging.end(), uint8_t{ 0 });
	constexpr int32_t pitch = static_cast<int32_t>(PGrassCommon::QuadrantGrassPitch);
	const int32_t dim = static_cast<int32_t>(grassPresenceDim);
	for (const auto& quadrant : quadrantsPresence) {
		if (!quadrant.grassIds)
			continue;

		const int32_t sx0 = (quadrant.WorldX() - originQuadX) * (pitch - 1);
		const int32_t sy0 = (quadrant.WorldY() - originQuadY) * (pitch - 1);
		if (sx0 < 0 || sy0 < 0 || sx0 + pitch > dim || sy0 + pitch > dim)
			continue;

		// Cached ids carry the generator's quadrant-local fill; union only the shared border samples. Filling the
		// assembled window would reach into bare cells that the generator never fills.
		for (int32_t row = 0; row < pitch; ++row) {
			uint8_t* dstRow = grassPresenceStaging.data() + static_cast<size_t>(sy0 + row) * grassPresenceDim + sx0;
			for (int32_t column = 0; column < pitch; ++column) {
				const uint8_t id = LimitGrassIdToSlope(quadrant.grassIds[row * pitch + column], quadrant, column, row);
				dstRow[column] = PGrassCommon::MergeGrassIds(dstRow[column], id);
			}
		}
	}

	grassPresenceOriginQuadX = originQuadX;
	grassPresenceOriginQuadY = originQuadY;
	grassPresenceContentHash = contentHash;
	grassPresenceUploadDirty = true;
}

void ProceduralGrass::RequestStreamedCells(RE::TESWorldSpace* landWorldSpace, const VisibilityOrigin& origin)
{
	if (!landWorldSpace) {
		streamRequestWorldSpace = nullptr;
		streamRequestCursor = 0;
		return;
	}

	// Spiral outward from the player's cell so the nearest missing LAND streams first.
	static const std::vector<std::pair<int32_t, int32_t>> requestOffsets = [] {
		std::vector<std::pair<int32_t, int32_t>> offsets;
		offsets.reserve(static_cast<size_t>((StreamCellRadius * 2 + 1) * (StreamCellRadius * 2 + 1)));
		offsets.emplace_back(0, 0);
		for (int32_t ring = 1; ring <= StreamCellRadius; ++ring)
			for (int32_t y = -ring; y <= ring; ++y)
				for (int32_t x = -ring; x <= ring; ++x)
					if (std::max(std::abs(x), std::abs(y)) == ring)
						offsets.emplace_back(x, y);
		return offsets;
	}();

	if (streamRequestWorldSpace != landWorldSpace || streamRequestCenterX != origin.cellX || streamRequestCenterY != origin.cellY) {
		streamRequestWorldSpace = landWorldSpace;
		streamRequestCenterX = origin.cellX;
		streamRequestCenterY = origin.cellY;
		streamRequestCursor = 0;
	}

	// Bound the request work per frame.
	constexpr size_t requestBudget = 12;
	constexpr size_t probeBudget = 64;
	size_t queued = 0;
	const size_t probeCount = std::min(requestOffsets.size(), probeBudget);
	for (size_t probe = 0; probe < probeCount && queued < requestBudget; ++probe) {
		if (streamRequestCursor >= requestOffsets.size())
			streamRequestCursor = 0;
		const auto [dx, dy] = requestOffsets[streamRequestCursor++];
		queued += grassCellCache.Request(origin.cellX + dx, origin.cellY + dy) ? 1u : 0u;
	}
}
