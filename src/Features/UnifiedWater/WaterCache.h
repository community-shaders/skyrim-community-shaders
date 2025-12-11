#pragma once
#include <BS_thread_pool.hpp>

class WaterCache
{
public:
	struct Instruction
	{
		union Form
		{
			uint32_t id;
			RE::TESWaterForm* ptr = nullptr;
		};

		uint32_t lodLevel{};
		Form form{};
		int32_t x{};
		int32_t y{};
		uint32_t size{};
		float waterHeight{};
	};

	struct BuildProgressSnapshot
	{
		uint32_t total{};
		uint32_t completed{};
		uint32_t failed{};
		uint32_t done{};
		bool active{};
		int64_t elapsedMs{};
	};

	bool SetCurrentWorldSpace(const RE::TESWorldSpace* worldSpace);
	std::vector<Instruction>* GetInstructions(const RE::TESWorldSpace* worldSpace, uint32_t lodLevel, uint32_t x, uint32_t y);

	static void GenerateTamrielPrecache();
	bool LoadOrGenerateCaches();
	bool RegenerateCaches();
	bool GenerateCaches();

	bool IsBuildRunning() const { return async.running.load(); }
	bool HasBuildFailed() const { return async.failed.load(); }
	BuildProgressSnapshot GetBuildProgressSnapshot() const { return buildProgress.Snapshot(); }

private:
	struct WorldSpaceHeader
	{
		struct MinMax
		{
			int32_t minX{};
			int32_t minY{};
			int32_t maxX{};
			int32_t maxY{};
		};

		uint32_t label = Util::FCC("WTCH");
		int32_t width{};
		int32_t height{};
		MinMax bounds{};
		int32_t dataCount{};
	};

	struct Heights
	{
		float land = FLT_MAX;
		float water = FLT_MAX;
	};

	struct CellData
	{
		Heights heights{};
		RE::FormID formID{};
		RE::TESWaterForm* form = nullptr;
	};

	// Precache contains height data generated with sheson's extended Tamriel data set
	struct PreCache
	{
		WorldSpaceHeader header;
		// Cell XY map
		std::vector<Heights> heights;
	};

	// Per WorldSpace DiskCache that contains a list of instructions for water placement for all LOD levels in series
	struct DiskCache
	{
		WorldSpaceHeader header;
		std::vector<Instruction> instructions;
	};

	// Per WorldSpace RuntimeCache that is the disk cache processed into a runtime optimised format with fast lookups
	struct RuntimeCache
	{
		WorldSpaceHeader header;
		// LODLevel -> LODChunk -> Instructions
		std::vector<std::vector<std::vector<Instruction>>> instructions;

		std::vector<Instruction>* GetInstructions(int32_t lodLevel, int32_t x, int32_t y);
	};

	struct AsyncBuild
	{
		std::unique_ptr<BS::thread_pool> pool;
		std::jthread monitor;
		std::atomic<bool> running{ false };
		std::atomic<bool> failed{ false };
	};

	struct BuildProgress
	{
		std::atomic<uint32_t> total{ 0 };
		std::atomic<uint32_t> completed{ 0 };
		std::atomic<uint32_t> failed{ 0 };
		std::atomic<bool> active{ false };
		std::chrono::steady_clock::time_point t0{};

		void Start(uint32_t tot)
		{
			total.store(tot, std::memory_order_relaxed);
			completed.store(0, std::memory_order_relaxed);
			failed.store(0, std::memory_order_relaxed);
			t0 = std::chrono::steady_clock::now();
			active.store(true, std::memory_order_relaxed);
		}

		void Done(bool ok)
		{
			if (ok)
				completed.fetch_add(1, std::memory_order_relaxed);
			else
				failed.fetch_add(1, std::memory_order_relaxed);
		}

		void Stop() { active.store(false, std::memory_order_relaxed); }

		uint32_t DoneCount() const
		{
			return completed.load(std::memory_order_relaxed) + failed.load(std::memory_order_relaxed);
		}

		int64_t ElapsedMs() const
		{
			return duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
		}

		BuildProgressSnapshot Snapshot() const
		{
			BuildProgressSnapshot s;
			s.total = total.load(std::memory_order_relaxed);
			s.completed = completed.load(std::memory_order_relaxed);
			s.failed = failed.load(std::memory_order_relaxed);
			s.done = s.completed + s.failed;
			s.active = active.load(std::memory_order_relaxed);
			s.elapsedMs = duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
			return s;
		}
	};

	BuildProgress buildProgress;
	AsyncBuild async;

	using CacheMap = std::unordered_map<std::string, std::shared_ptr<RuntimeCache>>;

	std::atomic<std::shared_ptr<const CacheMap>> cacheMap{ std::make_shared<CacheMap>() };

	std::shared_ptr<RuntimeCache> currentCache;
	std::string currentWorldSpace;

	bool LoadCaches();

	static void BuildPreCache(RE::TESWorldSpace* worldSpace, PreCache& cache);
	static bool BuildDiskCache(RE::TESWorldSpace* worldSpace, DiskCache& diskCache);
	static void GenerateInstructions(int32_t lodLevel, DiskCache& diskCache, const std::vector<CellData>& cellData, int32_t& instructionCount);
	static bool TryBuildRuntimeCache(const DiskCache& diskCache, RuntimeCache& cache);
	static std::vector<RE::TESWorldSpace*> GetValidWorldSpaces();
	static void GetLODCoords(int32_t lodLevel, int32_t x, int32_t y, int32_t& outX, int32_t& outY);

	static bool TryGetCellData(RE::TESWorldSpace* worldSpace, RE::TESFileArray* files, int32_t x, int32_t y, RE::FormID& outFormID, float& outWaterHeight, float& outLandHeight, bool resolveFormID);
	static void ReadWaterData(RE::TESFile* file, float& waterHeight, RE::FormID& formID);
	static void ReadMinLandHeightData(RE::TESFile* file, float& minHeight);

	template <class T>
	static bool TryWriteCacheToFile(const std::string& name, const WorldSpaceHeader& header, const std::vector<T>& vec);

	template <class T>
	static bool TryReadCacheFromFile(const std::string& name, WorldSpaceHeader& header, std::vector<T>& vec);
};