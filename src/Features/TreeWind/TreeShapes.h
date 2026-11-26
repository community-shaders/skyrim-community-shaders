#pragma once

#include <BS_thread_pool.hpp>

/**
 * @brief Finds the DynDOLOD tree LOD shapes and builds their per-vertex bend data.
 *
 * Object LOD merges many trees into one shape with no per-tree origin, so the bend data comes from
 * the CPU copy of the vertices: trunk cards give each tree's base and top, and each crown vertex
 * takes the nearest trunk card of its LOD block. Billboard-only shapes bend each card or piece
 * from its own foot. Builds run on one worker thread, so new LOD blocks do not stall the frame.
 *
 * Tree-mod support covers 3D LOD built differently from DynDOLOD's own models (Happy Little Trees,
 * Nature of the Wild Lands, Ulvenwald): trunk cards mixed with 3D trunks and stumps in one shape,
 * and crowns without rim or soft lighting.
 */
namespace TreeShapes
{
	enum class Kind
	{
		None,
		Crown,
		Trunk
	};

	/** @brief Classifies a Lighting or Utility pass by its shader flags, geometry type, shape name and layout. */
	Kind Classify(const RE::BSRenderPass* a_pass, bool a_treeModSupport);

	class BendWeights
	{
	public:
		BendWeights() = default;
		BendWeights(const BendWeights&) = delete;
		BendWeights& operator=(const BendWeights&) = delete;
		~BendWeights();

		/**
		 * @brief Returns the bend data for a shape; a new shape is queued for the build thread.
		 * @return nullptr while its first build runs, or when the shape has nothing to sway or its block's trunks have not been drawn yet.
		 */
		ID3D11ShaderResourceView* Get(RE::BSGeometry* a_geometry, Kind a_kind, bool a_treeModSupport);

		/** @brief Takes over the finished builds, once per frame. */
		void Commit();

		/** @brief Frees the bend data of shapes not drawn for a while, and the blocks left without shapes. */
		void Evict();

		/** @brief Frees all bend data, so it is rebuilt with new settings. */
		void Clear();

		size_t Count() const
		{
			// Shapes still waiting for their first build have no data yet
			return static_cast<size_t>(std::ranges::count_if(entries, [](const auto& a_entry) { return static_cast<bool>(a_entry.second.srv); }));
		}

		struct TrunkCard
		{
			float x, y, base, top;
			float random = 0.0f;  // timing offset and flutter choice, shared by a tree's crossed cards
			float scale = 1.0f;   // sway scale from the tree height
		};

		struct Block
		{
			std::vector<TrunkCard> cards;
			std::unordered_map<int64_t, std::vector<int>> grid;  // cell -> card indices
			std::unordered_set<const void*> trunkShapes;
			uint32_t generation = 0;  // bumped when cards are added, so tree-mod crowns built before rebuild
		};

	private:
		/** @brief A shape copied off the render thread for its build. */
		struct Job
		{
			uint64_t id = 0;
			uint32_t epoch = 0;
			const void* vertexBuffer = nullptr;
			uint64_t blockKey = 0;
			Kind kind = Kind::None;
			bool treeModSupport = false;
			bool allQuads = false;
			bool treeShapeName = false;
			float worldScale = 1.0f;
			std::vector<RE::NiPoint3> positions;
			std::vector<uint16_t> indices;  // empty without a CPU index copy
		};

		struct Result
		{
			uint64_t id = 0;
			uint32_t epoch = 0;
			const void* vertexBuffer = nullptr;
			uint64_t blockKey = 0;
			uint32_t generation = 0;  // the block's generation after this build
			bool waitsForCards = false;
			bool gpuFailed = false;  // the buffer or its view could not be created
			winrt::com_ptr<ID3D11ShaderResourceView> srv;
		};

		struct Entry
		{
			winrt::com_ptr<ID3D11ShaderResourceView> srv;  // nullptr while building, or for a shape left still
			uint32_t vertexCount = 0;
			float firstVertex[3]{};
			float lastSeen = 0.0f;  // game seconds
			uint64_t blockKey = 0;
			uint64_t pendingJob = 0;  // the build in flight, 0 for none
			float retryAt = 0.0f;     // game seconds; after a failed buffer creation, 0 for none
			uint32_t generation = 0;
			bool crown = false;
			bool waitsForCards = false;  // a crown built before its block had trunk cards
		};

		/** @brief Queues a shape's build, with its positions already copied. @return the job id. */
		uint64_t Queue(RE::BSGeometry* a_geometry, const void* a_vertexBuffer, std::vector<RE::NiPoint3>&& a_positions, Kind a_kind, bool a_treeModSupport);

		/** @brief Builds a shape's bend data and buffer. Build thread only. */
		Result Build(const Job& a_job);

		std::unordered_map<uint64_t, Block> blocks;  // build thread only; key = the block's world translate

		std::unordered_map<const void*, Entry> entries;           // key = the shape's vertex buffer
		std::unordered_map<uint64_t, uint32_t> blockGenerations;  // the latest generation each block reported
		uint64_t nextJob = 1;
		uint32_t epoch = 0;  // bumped by Clear, so builds already queued are dropped

		std::mutex resultsMutex;
		std::vector<Result> results;

		// Declared last, so it finishes its queue before the members it touches are destroyed
		std::unique_ptr<BS::thread_pool<>> builder;
	};
}
