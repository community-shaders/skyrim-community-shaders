#include "WaterCache.h"

#include <BS_thread_pool.hpp>

bool WaterCache::SetCurrentWorldSpace(const RE::TESWorldSpace* worldSpace)
{
	if (!worldSpace)
		return false;

	while (worldSpace->parentWorld && worldSpace->parentUseFlags.all(RE::TESWorldSpace::ParentUseFlag::kUseWaterData))
		worldSpace = worldSpace->parentWorld;

	const auto newWorldSpace = worldSpace->GetFormEditorID();
	if (currentWorldSpace == newWorldSpace) {
		logger::debug("[Unified Water] [Cache] Runtime cache for {} already active", currentWorldSpace);
		return true;
	}

	currentWorldSpace = newWorldSpace;

	const auto snap = std::atomic_load_explicit(&cacheMap, std::memory_order_acquire);
	if (!snap) {
		logger::error("[Unified Water] [Cache] Failed to get cache snapshot");
		return false;
	}

	const auto it = snap->find(newWorldSpace);
	if (it == snap->end()) {
		logger::error("[Unified Water] [Cache] Failed to get runtime cache for {}", newWorldSpace);
		currentCache.reset();
		currentWorldSpace.clear();
		return false;
	}

	currentCache = it->second;
	currentWorldSpace = std::move(newWorldSpace);
	logger::debug("[Unified Water] [Cache] Runtime cache for {} activated", currentWorldSpace);

	return true;
}

std::vector<WaterCache::Instruction>* WaterCache::GetInstructions(const RE::TESWorldSpace* worldSpace, const uint32_t lodLevel, const uint32_t x, const uint32_t y)
{
	if (!SetCurrentWorldSpace(worldSpace)) {
		logger::error("[Unified Water] [Cache] Failed to set current cache to {} while getting instructions", worldSpace->GetFormEditorID());
		return nullptr;
	}

	return currentCache->GetInstructions(lodLevel, x, y);
}

std::vector<WaterCache::Instruction>* WaterCache::RuntimeCache::GetInstructions(const int32_t lodLevel, const int32_t x, const int32_t y)
{
	if (lodLevel <= 0)
		return nullptr;

	const auto lodIndex = std::countr_zero(static_cast<uint32_t>(lodLevel)) - 2;
	if (lodIndex < 0 || lodIndex >= instructions.size())
		return nullptr;

	auto& lodInstructions = instructions[lodIndex];

	const auto& bounds = header.bounds;
	int32_t lodCellX, lodCellY, lodMinX, lodMinY, lodMaxX, lodMaxY;
	GetLODCoords(lodLevel, x, y, lodCellX, lodCellY);
	GetLODCoords(lodLevel, bounds.minX, bounds.minY, lodMinX, lodMinY);
	GetLODCoords(lodLevel, bounds.maxX, bounds.maxY, lodMaxX, lodMaxY);
	const int32_t lodWidth = lodMaxX - lodMinX + 1;

	const auto lodCellIndex = (lodCellY - lodMinY) * lodWidth + lodCellX - lodMinX;
	if (lodCellIndex < 0 || lodCellIndex >= lodInstructions.size())
		return nullptr;

	return &lodInstructions[lodCellIndex];
}

void WaterCache::GenerateTamrielPrecache()
{
	// This function is strictly for developer use for building an offline pre-cache using sheson's SSE Terrain Tamriel Full Extend
	// which adds back missing terrain mesh data
	const auto tamriel = RE::TESForm::LookupByEditorID<RE::TESWorldSpace>("Tamriel");
	if (!tamriel) {
		logger::error("[Unified Water] [Cache] Failed to load Tamriel WorldSpace");
		return;
	}

	PreCache cache;
	WorldSpaceHeader& hdr = cache.header;
	hdr.bounds.minX = -96;
	hdr.bounds.minY = -96;
	hdr.bounds.maxX = 95;
	hdr.bounds.maxY = 96;
	hdr.width = cache.header.bounds.maxX - cache.header.bounds.minX + 1;
	hdr.height = cache.header.bounds.maxY - cache.header.bounds.minY + 1;
	hdr.dataCount = hdr.width * hdr.height;

	cache.heights.resize(hdr.dataCount);

	BuildPreCache(tamriel, cache);

	TryWriteCacheToFile("Tamriel_precache.wpc", hdr, cache.heights);
}

bool WaterCache::LoadOrGenerateCaches()
{
	if (!LoadCaches()) {
		logger::info("[Unified Water] [Cache] Could not load caches - regenerating...");
		return RegenerateCaches();
	}

	return true;
}

bool WaterCache::RegenerateCaches()
{
	logger::info("[Unified Water] [Cache] Clearing and regenerating caches...");

	namespace fs = std::filesystem;
	const fs::path dir = Util::PathHelpers::GetDataPath() / "UnifiedWaterCache";

	std::error_code ec;
	fs::create_directories(dir, ec);

	for (const auto& entry : fs::directory_iterator(dir, ec)) {
		if (ec)
			break;
		if (!entry.is_regular_file())
			continue;

		const auto& path = entry.path();
		if (path.extension() != ".wc")
			continue;
		std::error_code rec;
		fs::remove(path, rec);
		if (rec) {
			logger::warn("[Unified Water] [Cache] Failed to remove '{}': {}", path.string(), rec.message());
		}
	}

	return GenerateCaches();
}

bool WaterCache::LoadCaches()
{
	const auto t0 = std::chrono::steady_clock::now();

	auto worldSpaces = GetValidWorldSpaces();

	auto newCacheMap = std::make_shared<CacheMap>();

	for (auto& worldSpace : worldSpaces) {
		const auto editorID = worldSpace ? worldSpace->GetFormEditorID() : nullptr;
		if (!worldSpace || !editorID) {
			logger::warn("[Unified Water] [Cache] WorldSpace has no EditorID - skipping");
			continue;
		}

		std::string key{ editorID };

		if (newCacheMap->contains(key))
			continue;

		DiskCache diskCache;
		const auto fileName = std::format("{}_cache.wc", key);
		if (!TryReadCacheFromFile(fileName, diskCache.header, diskCache.instructions)) {
			logger::info("[Unified Water] [Cache] Could not locate disk cache for {}", key);
			return false;
		}

		logger::debug("[Unified Water] [Cache] Loaded cache for {} - Bounds {},{}  {},{} - Instructions {}", editorID, diskCache.header.bounds.minX, diskCache.header.bounds.minY, diskCache.header.bounds.maxX, diskCache.header.bounds.maxY, diskCache.header.dataCount);

		auto newCache = std::make_unique<RuntimeCache>();
		if (!TryBuildRuntimeCache(diskCache, *newCache)) {
			logger::warn("[Unified Water] [Cache] Failed to build runtime cache for {}", key);
			return false;
		}

		newCacheMap->emplace(std::move(key), std::move(newCache));
	}

	std::atomic_store_explicit(&cacheMap, std::const_pointer_cast<const CacheMap>(newCacheMap), std::memory_order_release);

	if (!currentWorldSpace.empty()) {
		if (const auto snap = std::atomic_load_explicit(&cacheMap, std::memory_order_acquire)) {
			if (const auto it = snap->find(currentWorldSpace); it != snap->end()) {
				currentCache = it->second;
			}
		}
	}

	const auto t1 = std::chrono::steady_clock::now();
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
	logger::info("[Unified Water] [Cache] Caches loaded in {} ms", ms);

	return true;
}

bool WaterCache::GenerateCaches()
{
	if (async.running.load()) {
		logger::warn("[Unified Water] [Cache] Build already running");
		return false;
	}

	const auto worldSpaces = GetValidWorldSpaces();

	{
		namespace fs = std::filesystem;
		std::error_code ec;
		fs::create_directories(Util::PathHelpers::GetDataPath() / "UnifiedWaterCache", ec);
		if (ec) {
			logger::error("[Unified Water] [Cache] Failed to ensure output directory: {}", ec.message());
			return false;
		}
	}

	const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
	const unsigned threads = std::max(1u, hw > 4 ? hw - 4 : (hw * 3) / 4);
	async.pool = std::make_unique<BS::thread_pool>(threads);

	async.failed.store(false);
	buildProgress.Start(static_cast<uint32_t>(worldSpaces.size()));
	async.running.store(true);

	logger::info("[Unified Water] [Cache] Starting async disk cache build for {} WorldSpaces on {} threads", buildProgress.total.load(), threads);

	for (auto& worldSpace : worldSpaces) {
		const char* editorID = worldSpace && worldSpace->editorID.c_str() ? worldSpace->editorID.c_str() : "";
		if (!worldSpace || !editorID || !*editorID) {
			logger::warn("[Unified Water] [Cache] WorldSpace has no EditorID - skipping");
			buildProgress.Done(true);
			continue;
		}

		async.pool->push_task([this, worldSpace, editorID] {
			DiskCache cache = {};
			const auto name = std::format("{}_cache.wc", editorID);
			const auto success = BuildDiskCache(worldSpace, cache) && TryWriteCacheToFile(name, cache.header, cache.instructions);
			if (success) {
				logger::debug("[Unified Water] [Cache] {} generation complete", editorID);
			} else {
				async.failed.store(true, std::memory_order_relaxed);
				logger::error("[Unified Water] [Cache] {} generation failed", editorID);
			}

			buildProgress.Done(success);
		});
	}

	async.monitor = std::jthread([this](const std::stop_token& st) {
		using namespace std::chrono_literals;
		while (!st.stop_requested()) {
			const auto bp = this->GetBuildProgressSnapshot();
			if (bp.done >= bp.total)
				break;
			std::this_thread::sleep_for(50ms);
		}

		if (async.pool)
			async.pool->wait_for_tasks();

		buildProgress.Stop();
		async.running.store(false);

		logger::info("[Unified Water] [Cache] Disk caches generated in {} ms  ({} / {} complete - {} failed)", buildProgress.ElapsedMs(), buildProgress.completed.load(), buildProgress.total.load(), buildProgress.failed.load());
		LoadCaches();
	});

	return true;
}

void WaterCache::BuildPreCache(RE::TESWorldSpace* worldSpace, PreCache& cache)
{
	const auto& hdr = cache.header;
	const auto& [minX, minY, maxX, maxY] = hdr.bounds;
	auto& heights = cache.heights;

	const auto files = worldSpace->sourceFiles.array;

	for (auto y = minY; y <= maxY; ++y) {
		for (auto x = minX; x <= maxX; ++x) {
			const int32_t idx = (y - minY) * hdr.width + x - minX;
			RE::FormID rawFormID;
			float landHeight, waterHeight;

			TryGetCellData(worldSpace, files, x, y, rawFormID, waterHeight, landHeight, false);
			heights[idx] = { landHeight, waterHeight };
		}
	}
}

bool WaterCache::BuildDiskCache(RE::TESWorldSpace* worldSpace, DiskCache& diskCache)
{
	const auto t0 = std::chrono::steady_clock::now();

	diskCache.header = {};
	diskCache.instructions = {};
	auto& hdr = diskCache.header;
	auto& [minX, minY, maxX, maxY] = diskCache.header.bounds;

	const auto editorID = worldSpace->editorID;

	bool hasPrecache = false;
	PreCache preCache;

	const auto wsMin = worldSpace->minimumCoords;
	const auto wsMax = worldSpace->maximumCoords;

	if (editorID == "Tamriel") {
		if (TryReadCacheFromFile("Tamriel_precache.wpc", preCache.header, preCache.heights)) {
			diskCache.header = preCache.header;
			hasPrecache = true;
		} else {
			logger::warn("[Unified Water] [Cache] Tamriel: Failed to load precache, falling back to generation");
			Util::WorldToCell(wsMin, minX, minY);
			Util::WorldToCell(wsMax, maxX, maxY);
			maxX -= 1;
			maxY -= 1;
			hdr.width = maxX - minX + 1;
			hdr.height = maxY - minY + 1;
			hdr.dataCount = 0;
		}
	} else {
		Util::WorldToCell(wsMin, minX, minY);
		Util::WorldToCell(wsMax, maxX, maxY);
		maxX -= 1;
		maxY -= 1;
		hdr.width = maxX - minX + 1;
		hdr.height = maxY - minY + 1;
		hdr.dataCount = 0;
	}

	bool invalidWS = wsMin.x == FLT_MIN || wsMin.y == FLT_MIN || wsMax.x == FLT_MAX || wsMax.y == FLT_MAX || wsMin.x == FLT_MAX || wsMin.y == FLT_MAX || wsMax.x == FLT_MIN || wsMax.y == FLT_MIN;
	invalidWS = invalidWS || hdr.width <= 0 || hdr.height <= 0 || hdr.width > 512 || hdr.height > 512;

	if (invalidWS) {
		logger::warn("[Unified Water] [Cache] {}: Invalid WorldSpace - skipping", editorID.c_str());
		diskCache = {};
		return true;
	}

	logger::debug("[Unified Water] [Cache] {}: Building disk cache...", editorID.c_str());

	const auto files = worldSpace->sourceFiles.array;

	auto cellData = std::vector<CellData>(hdr.width * hdr.height);

	int32_t waterCellCount = 0;

	for (auto y = minY; y <= maxY; ++y) {
		for (auto x = minX; x <= maxX; ++x) {
			const int32_t idx = (y - minY) * hdr.width + x - minX;

			RE::FormID formID;
			float landHeight, waterHeight;

			if (!TryGetCellData(worldSpace, files, x, y, formID, waterHeight, landHeight, true) && hasPrecache) {
				auto [land, water] = preCache.heights[idx];
				landHeight = land;
				waterHeight = water;
			}

			if (waterHeight > landHeight && fabs(waterHeight) < 50000.0f) {
				if (!formID)
					formID = worldSpace->worldWater->formID;  // Use default world water if no water form set
			} else
				formID = 0;

			RE::TESWaterForm* form = formID ? RE::TESWaterForm::LookupByID<RE::TESWaterForm>(formID) : nullptr;
			if ((formID && !form) || (form && form->formType != RE::FormType::Water)) {
				logger::warn("[Unified Water] [Cache] {}: Failed to load WaterForm {:08X}", editorID.c_str(), formID);
				return false;
			}

			if (form)
				waterCellCount++;

			cellData[idx] = { landHeight, waterHeight, formID, form };
		}
	}

	logger::debug("[Unified Water] [Cache] {}: Generating instructions for {} water cells...", editorID.c_str(), waterCellCount);

	int32_t instructionCount;
	GenerateInstructions(4, diskCache, cellData, instructionCount);
	logger::debug("[Unified Water] [Cache] {}: LOD4 - {} instructions generated", editorID.c_str(), instructionCount);

	GenerateInstructions(8, diskCache, cellData, instructionCount);
	logger::debug("[Unified Water] [Cache] {}: LOD8 - {} instructions generated", editorID.c_str(), instructionCount);

	GenerateInstructions(16, diskCache, cellData, instructionCount);
	logger::debug("[Unified Water] [Cache] {}: LOD16 - {} instructions generated", editorID.c_str(), instructionCount);

	GenerateInstructions(32, diskCache, cellData, instructionCount);
	logger::debug("[Unified Water] [Cache] {}: LOD32 - {} instructions generated", editorID.c_str(), instructionCount);

	diskCache.header.dataCount = static_cast<int32_t>(diskCache.instructions.size());

	const auto t1 = std::chrono::steady_clock::now();
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
	logger::info("[Unified Water] [Cache] {}: Cache generated in {} ms", worldSpace->GetFormEditorID(), ms);
	return true;
}

void WaterCache::GenerateInstructions(const int32_t lodLevel, DiskCache& diskCache, const std::vector<CellData>& cellData, int32_t& instructionCount)
{
	instructionCount = 0;
	const auto& hdr = diskCache.header;
	const auto& [minX, minY, maxX, maxY] = hdr.bounds;
	int32_t lodMinX, lodMinY, lodMaxX, lodMaxY;
	GetLODCoords(lodLevel, minX, minY, lodMinX, lodMinY);
	GetLODCoords(lodLevel, maxX, maxY, lodMaxX, lodMaxY);

	auto& instructions = diskCache.instructions;
	instructions.reserve(lodLevel == 4 ? cellData.size() : cellData.size() / 5);
	std::vector<bool> processed(lodLevel * lodLevel);

	// Loop over all lod chunks
	for (int32_t lodY = lodMinY; lodY <= lodMaxY; ++lodY) {
		for (int32_t lodX = lodMinX; lodX <= lodMaxX; ++lodX) {
			// Chunk base coordinates
			const int32_t baseX = lodX * lodLevel - minX;
			const int32_t baseY = lodY * lodLevel - minY;

			int32_t cellsRemaining = lodLevel * lodLevel;
			std::ranges::fill(processed, false);

			// Loop over all cells within the chunk
			for (int32_t localY = 0; localY < lodLevel; ++localY) {
				for (int32_t localX = 0; localX < lodLevel; ++localX) {
					const size_t localIndex = localY * lodLevel + localX;
					// Skip any processed cells
					if (processed[localIndex])
						continue;

					// Array relative cell coordinates
					const int32_t cellX = baseX + localX;
					const int32_t cellY = baseY + localY;

					// Skip any cells outside the map
					if (cellX < 0 || cellX >= hdr.width || cellY < 0 || cellY >= hdr.height) {
						processed[localIndex] = true;
						cellsRemaining--;
						continue;
					}

					const int32_t cellIndex = cellY * hdr.width + cellX;
					const auto& [heights, formID, form] = cellData[cellIndex];

					// Skip any cells without water
					if (!form) {
						processed[localIndex] = true;
						cellsRemaining--;
						continue;
					}

					// Neighbouring cells must match target values to merge
					const float targetHeight = heights.water;
					const auto targetType = formID;
					const uint8_t targetFlags = form->flags.underlying();

					int32_t size = 1;
					int32_t maxSize = 1;
					if (lodLevel > 4) {
						// For LOD 4 we don't want combined meshes, they need to be toggled off as real cells load
						// For others, size can only grow to what's left in the chunk or the maximum map dimensions
						maxSize = std::min(lodLevel - localX, lodLevel - localY);
						maxSize = std::min(maxSize, hdr.width - cellX);
						maxSize = std::min(maxSize, hdr.height - cellY);
					}

					// Try to grow to the largest square possible starting at localX/localY
					for (int32_t candidateSize = 2; candidateSize <= maxSize; ++candidateSize) {
						bool failed = false;

						// Check new top row
						const int32_t newRowY = localY + candidateSize - 1;
						for (int32_t dx = 0; dx < candidateSize; ++dx) {
							const int32_t rx = baseX + (localX + dx);
							const int32_t ry = baseY + newRowY;
							const int32_t idx = ry * hdr.width + rx;
							const int32_t cLocalIdx = newRowY * lodLevel + localX + dx;
							const auto& [cHeights, cFormID, cForm] = cellData[idx];
							if (processed[cLocalIdx] || !cForm || cHeights.water != targetHeight || cFormID != targetType || cForm->flags.underlying() != targetFlags) {
								failed = true;
								break;
							}
						}

						if (failed)
							break;

						// Check new right column, excluding the corner already checked above
						const int32_t newColX = localX + candidateSize - 1;
						for (int32_t dy = 0; dy < candidateSize - 1; ++dy) {
							const int32_t rx = baseX + newColX;
							const int32_t ry = baseY + (localY + dy);
							const int32_t idx = ry * hdr.width + rx;
							const int32_t cLocalIdx = (localY + dy) * lodLevel + newColX;
							const auto& [cHeights, cFormID, cForm] = cellData[idx];
							if (processed[cLocalIdx] || !cForm || cHeights.water != targetHeight || cFormID != targetType || cForm->flags.underlying() != targetFlags) {
								failed = true;
								break;
							}
						}

						if (failed)
							break;

						// Success for this candidate size
						size = candidateSize;
					}

					// Mark all cells within this group as processed
					for (int32_t yy = localY; yy < localY + size; ++yy) {
						for (int32_t xx = localX; xx < localX + size; ++xx) {
							processed[yy * lodLevel + xx] = true;
						}
					}
					cellsRemaining -= size * size;
					instructionCount++;

					Instruction instruction;
					instruction.lodLevel = lodLevel;
					instruction.form.id = formID;
					// Convert back to game cell coords
					instruction.x = cellX + minX;
					instruction.y = cellY + minY;
					instruction.size = size;
					instruction.waterHeight = targetHeight;

					// Add new instruction to list
					instructions.push_back(instruction);

					if (cellsRemaining <= 0)
						break;
				}

				if (cellsRemaining <= 0)
					break;
			}
		}
	}
}

bool WaterCache::TryBuildRuntimeCache(const DiskCache& diskCache, RuntimeCache& cache)
{
	// For LOD4, 8, 16 and 32
	cache.instructions = std::vector<std::vector<std::vector<Instruction>>>(4);

	const auto& hdr = diskCache.header;
	const auto& [minX, minY, maxX, maxY] = hdr.bounds;

	cache.header = hdr;

	int32_t diskReadIndex = 0;

	for (int32_t lodLevelIdx = 0; lodLevelIdx < 4; ++lodLevelIdx) {
		auto& lodInstructions = cache.instructions[lodLevelIdx];

		const int32_t lodLevel = 1 << (lodLevelIdx + 2);
		int32_t lodMinX, lodMinY, lodMaxX, lodMaxY;
		GetLODCoords(lodLevel, minX, minY, lodMinX, lodMinY);
		GetLODCoords(lodLevel, maxX, maxY, lodMaxX, lodMaxY);
		const int32_t lodWidth = lodMaxX - lodMinX + 1;
		const int32_t lodHeight = lodMaxY - lodMinY + 1;

		lodInstructions.resize(lodWidth * lodHeight);

		while (diskReadIndex < diskCache.instructions.size()) {
			auto instruction = diskCache.instructions[diskReadIndex];
			if (instruction.lodLevel != static_cast<uint32_t>(lodLevel))
				break;

			int32_t lodX, lodY;
			GetLODCoords(lodLevel, instruction.x, instruction.y, lodX, lodY);
			lodX -= lodMinX;
			lodY -= lodMinY;

			instruction.form.ptr = RE::TESForm::LookupByID<RE::TESWaterForm>(instruction.form.id);
			if (!instruction.form.ptr || instruction.form.ptr->formType != RE::FormType::Water) {
				logger::warn("[Unified Water] [Cache] Failed to load WaterForm {:08X}", instruction.form.id);
				return false;
			}

			if (!instruction.form.ptr->IsInitialized()) {
				logger::warn("[Unified Water] [Cache] WaterForm {:08X} is not initialized", instruction.form.id);
				instruction.form.ptr->InitItemImpl();
			}

			lodInstructions[lodY * lodWidth + lodX].push_back(instruction);

			diskReadIndex++;
		}
	}

	return true;
}

std::vector<RE::TESWorldSpace*> WaterCache::GetValidWorldSpaces()
{
	auto worldSpaces = RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESWorldSpace>();
	auto validWorldSpaces = std::vector<RE::TESWorldSpace*>();

	for (auto& worldSpace : worldSpaces) {
		while (worldSpace->parentWorld && worldSpace->parentUseFlags.all(RE::TESWorldSpace::ParentUseFlag::kUseWaterData))
			worldSpace = worldSpace->parentWorld;

		if (!worldSpace->worldWater)
			continue;

		if (std::ranges::find(validWorldSpaces, worldSpace) != validWorldSpaces.end())
			continue;

		validWorldSpaces.push_back(worldSpace);
	}

	return validWorldSpaces;
}

void WaterCache::GetLODCoords(const int32_t lodLevel, const int32_t x, const int32_t y, int32_t& outX, int32_t& outY)
{
	outX = x >= 0 ? x / lodLevel : (x - (lodLevel - 1)) / lodLevel;
	outY = y >= 0 ? y / lodLevel : (y - (lodLevel - 1)) / lodLevel;
}

bool WaterCache::TryGetCellData(RE::TESWorldSpace* worldSpace, RE::TESFileArray* files, const int32_t x, const int32_t y, RE::FormID& outFormID, float& outWaterHeight, float& outLandHeight, bool resolveFormID)
{
	outFormID = 0;
	outWaterHeight = FLT_MAX;
	outLandHeight = FLT_MAX;

	const auto size = static_cast<int32_t>(files->size());
	const auto arrayData = files->data();

	int32_t fileIndex = size - 1;
	RE::TESFile* file = arrayData[fileIndex]->Duplicate();
	bool foundWaterData = false;
	bool foundLandData = false;

	// Search through the files in reverse load order to find the cell and read the water height and waterForm FormID
	do {
		if (file && file->SeekCell(worldSpace, x, y)) {
			ReadWaterData(file, outWaterHeight, outFormID);
			foundWaterData = true;
			break;
		}
		file = --fileIndex >= 0 ? arrayData[fileIndex]->Duplicate() : nullptr;
	} while (fileIndex >= 0);

	if (resolveFormID && outFormID)
		outFormID = file->GetRuntimeFormID(outFormID);

	// Continue searching from the previous file to find the original record for the cell, this always has the landscape data - extract land height
	do {
		if (file && file->SeekCell(worldSpace, x, y) && file->SeekLandscapeForCurrentCell()) {
			ReadMinLandHeightData(file, outLandHeight);
			foundLandData = true;
			break;
		}
		file = --fileIndex >= 0 ? arrayData[fileIndex]->Duplicate() : nullptr;
	} while (fileIndex >= 0);

	if (!foundWaterData || !foundLandData) {
		outFormID = 0;
		outWaterHeight = FLT_MIN;
		outLandHeight = 0;
		return false;
	}

	if (outWaterHeight == FLT_MAX)
		outWaterHeight = worldSpace->defaultWaterHeight;
	if (outLandHeight == FLT_MAX)
		outLandHeight = worldSpace->defaultLandHeight;

	return foundWaterData && foundLandData;
}

void WaterCache::ReadWaterData(RE::TESFile* file, float& waterHeight, RE::FormID& formID)
{
	if (!file->SeekNextSubrecordType(Util::FCC("XCLW")))
		return;

	file->ReadData(&waterHeight, 4);
	if (file->isBigEndian)
		waterHeight = std::bit_cast<float>(_byteswap_ulong(std::bit_cast<uint32_t>(waterHeight)));

	if (!file->SeekNextSubrecordType(Util::FCC("XCWT")))
		return;

	file->ReadData(&formID, 4);
	if (file->isBigEndian)
		formID = _byteswap_ulong(formID);
}

void WaterCache::ReadMinLandHeightData(RE::TESFile* file, float& minHeight)
{
	if (!file->SeekNextSubrecordType(Util::FCC("VHGT")))
		return;

	struct VHGTData
	{
		float offset;
		int8_t deltas[1089];
		byte padding[3];
	};
	VHGTData data;

	file->ReadData(&data, 1096);

	float offset = data.offset;
	if (file->isBigEndian)
		offset = std::bit_cast<float>(_byteswap_ulong(std::bit_cast<std::uint32_t>(offset)));

	for (size_t y = 0; y < 33; y++) {
		float rowOffset = 0;
		offset += static_cast<float>(data.deltas[y * 33]);

		for (size_t x = 0; x < 33; x++) {
			const size_t index = y * 33 + x;

			if (x != 0)
				rowOffset += static_cast<float>(data.deltas[index]);

			const float height = (rowOffset + offset) * 8.0f;
			minHeight = std::min(height, minHeight);
		}
	}
}

template <typename T>
bool WaterCache::TryWriteCacheToFile(const std::string& name, const WorldSpaceHeader& header, const std::vector<T>& vec)
{
	namespace fs = std::filesystem;
	const fs::path path = Util::PathHelpers::GetDataPath() / "UnifiedWaterCache" / name;

	std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
	if (!ofs) {
		logger::error("[Unified Water] [Cache] Failed to open '{}' for writing", path.string());
		return false;
	}

	ofs.write(reinterpret_cast<const char*>(&header), sizeof(header));
	ofs.write(reinterpret_cast<const char*>(vec.data()), vec.size() * sizeof(T));

	if (!ofs.good()) {
		logger::error("[Unified Water] [Cache] Failed to write cache '{}'", path.string());
		return false;
	}
	return true;
}

template <typename T>
bool WaterCache::TryReadCacheFromFile(const std::string& name, WorldSpaceHeader& header, std::vector<T>& vec)
{
	namespace fs = std::filesystem;
	const fs::path path = Util::PathHelpers::GetDataPath() / "UnifiedWaterCache" / name;
	if (!fs::exists(path))
		return false;

	std::ifstream ifs(path, std::ios::binary);
	if (!ifs) {
		logger::error("[Unified Water] [Cache] Failed to open '{}' for reading", path.string());
		return false;
	}

	ifs.read(reinterpret_cast<char*>(&header), sizeof(header));
	if (!ifs || header.label != Util::FCC("WTCH")) {
		logger::error("[Unified Water] [Cache] Invalid or corrupt header for '{}'", path.string());
		return false;
	}

	vec.resize(header.dataCount);
	if (!vec.empty()) {
		ifs.read(reinterpret_cast<char*>(vec.data()), vec.size() * sizeof(T));
		if (!ifs.good()) {
			logger::error("[Unified Water] [Cache] Failed to read payload for '{}'", path.string());
			return false;
		}
	}
	return true;
}