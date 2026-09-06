#pragma once

/** @brief Byte stride of the per-instance records. */
constexpr uint32_t kGrassStride = 32;

/** @brief Mesh vertex stride in bytes, from nibble 0 of a packed vertexDesc (stored as bytes/4). */
constexpr uint32_t VertexStrideFromDesc(uint64_t descVal) { return static_cast<uint32_t>((4 * descVal) & 0x3C); }

/** @brief Maps grass shapes to their source .nif and caches the optional LOD meshes. Assigns integer IDs to each unique source .nif to avoid string lookups. */
class GrassMeshLibrary
{
public:
	/** @brief Which of the two mesh-swap distance bands an LOD mesh serves. */
	enum class LODTier : uint32_t
	{
		kMiddle = 0,
		kFar = 1,
		kCount = 2
	};

	/** @brief An LOD mesh, loaded from meshes\LOD\Grass\<source-mesh-stem>_LOD0.nif (middle) or _LOD1.nif (far). When `valid` is false, the full mesh is drawn then. */
	struct LODMesh
	{
		RE::NiPointer<RE::NiAVObject> keepAlive;  // owns the loaded model tree
		ID3D11Buffer* vertexBuffer = nullptr;
		ID3D11Buffer* indexBuffer = nullptr;
		uint32_t indexCount = 0;
		// The LOD .nif is authored separately, so its vertex format need not match the source mesh's.
		uint32_t meshStride = 0;
		uint64_t descVal = 0;
		bool attemptedLoad = false;
		bool valid = false;
	};

	/** @brief Records a shape's source model path, from the LoadGrassType hook. */
	void RecordModelPath(RE::BSMultiStreamInstanceTriShape* shape, const char* modelPath);

	/** @brief Resolves a shape to a source-mesh id (0 = unresolved) and caches the result. */
	uint32_t ResolveMeshId(RE::BSMultiStreamInstanceTriShape* shape);

	/** @brief Loads both LOD .nifs for this mesh id. */
	void EnsureLODMeshes(uint32_t meshId);

	/** @brief Returns the cached LOD mesh for a tier, or nullptr when none is loaded or it is unusable. The far tier falls back to the middle mesh. */
	const LODMesh* GetLODMesh(uint32_t meshId, LODTier tier) const;

	/** @brief Removes no longer valid shapes from the lookup caches to prevent IDs and LOD meshes from being associated with the wrong shape. */
	void ForgetShape(RE::BSMultiStreamInstanceTriShape* shape);
		
private:
	/** @brief Loads one tier's .nif into an entry, once. */
	static void LoadLODMesh(LODMesh& entry, const std::string& stem, LODTier tier);

	// Resolves shapes by id to their source-mesh stems, with stems[id - 1] since zero is invalid.
	std::vector<std::string> stems;
	std::unordered_map<RE::BSMultiStreamInstanceTriShape*, uint32_t> idByShape;

	// The only state here that crosses a thread boundary: RecordModelPath writes it from the game
	// thread's LoadGrassType hook while ResolveMeshId reads it from the render thread. Every other
	// member is reached from the render thread alone, under the store's bucketMutex, so it needs no
	// lock of its own.
	std::unordered_map<RE::BSMultiStreamInstanceTriShape*, std::string> stemByShape;
	mutable std::mutex stemMutex;

	// Parallel to stems, indexed by meshId - 1 then by LODTier. A deque to keep GetLODMesh's pointers valid after growth.
	std::deque<std::array<LODMesh, (size_t)LODTier::kCount>> lodMeshes;
};
