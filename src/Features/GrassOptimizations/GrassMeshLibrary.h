#pragma once

#include <BS_thread_pool.hpp>

#include "Buffer.h"
#include "TruePBR.h"

struct GrassAsset;
struct GrassMaterialKey;

/** @brief Returns the vertex stride in bytes from a descriptor that stores it in four-byte units. */
constexpr uint32_t VertexStrideFromDesc(uint64_t descVal) { return static_cast<uint32_t>((4 * descVal) & 0x3C); }

/** @brief Manages grass mesh identities, draw assets, texture classification and optional LOD meshes. */
class GrassMeshLibrary
{
public:
	/** @brief Stops mesh loading before cached resources and their locks are destroyed. */
	~GrassMeshLibrary();

	/** @brief Identifies the distance band served by an LOD mesh. */
	enum class LODTier : uint32_t
	{
		kMiddle = 0,
		kFar = 1,
		kCount = 2
	};

	/** @brief Tracks a tier's asynchronous load and cached result. */
	enum class LoadState : uint8_t
	{
		kUnrequested,
		kQueued,
		kUnavailable,
		kReady
	};

	/** @brief Retains a mesh loaded from meshes\LOD\Grass\<stem>_LOD0.nif or <stem>_LOD1.nif. */
	struct LODMesh
	{
		RE::NiPointer<RE::NiAVObject> keepAlive;
		ID3D11Buffer* vertexBuffer = nullptr;
		ID3D11Buffer* indexBuffer = nullptr;
		uint32_t indexCount = 0;
		float boundExtent = 0.0f;
		// The LOD .nif is authored separately, so its vertex format need not match the source mesh's.
		uint32_t meshStride = 0;
		uint64_t descVal = 0;
		LoadState state = LoadState::kUnrequested;
	};

	/** @brief Stable identity and render-thread LOD state for a mesh stem. */
	struct MeshRecord
	{
		std::string stem;
		uint32_t activeBuckets = 0;
		std::array<LODMesh, (size_t)LODTier::kCount> lodMeshes;
	};

	/** @brief Returns a stable mesh record for a model path. */
	MeshRecord& RegisterMesh(const char* modelPath);
	/** @brief Creates a bucket's mesh proxy and retains its mesh and texture users. */
	bool CreateAsset(GrassAsset& asset, RE::BSMultiStreamInstanceTriShape* geometryTemplate);
	/** @brief Releases a bucket's mesh and texture users. */
	void ReleaseAsset(const GrassAsset& asset);
	/** @brief Captures the material inputs shared by an optimized draw. */
	static GrassMaterialKey CaptureMaterial(RE::BSShaderMaterial* material, bool pbr);
	/** @brief Returns cached texture classification, or nullptr while it is unavailable. */
	ID3D11ShaderResourceView* GetComplexGrass(RE::NiSourceTexture* texture, ID3D11DeviceContext* context);
	/** @brief Releases the classification shader so it recompiles on next use. */
	void ClearShaderCache();

	/** @brief Queues missing LODs without blocking instance preparation or allocation. */
	void EnsureLODMeshes(MeshRecord& mesh);
	/** @brief Publishes completed model loads on the render thread. */
	void CommitLoadedLODMeshes();
	/** @brief Returns a ready tier, with the middle mesh serving an unavailable far tier. */
	static const LODMesh* GetLODMesh(const MeshRecord& mesh, LODTier tier);
	/** @brief Evicts ready LODs after the last bucket releases the mesh, retaining failed-load results. */
	static void ReleaseMesh(MeshRecord& mesh);

private:
	static bool FindLODShape(RE::NiAVObject* object, RE::BSTriShape*& shape);
	static std::string StemOf(const char* path);

	/** @brief Loads one tier's .nif into an entry. Runs on the loader thread. */
	static void LoadLODMesh(LODMesh& entry, const std::string& stem, LODTier tier);

	ID3D11ComputeShader* GetDetectCS();

	struct LoadResult
	{
		MeshRecord* target;
		LODTier tier;
		LODMesh mesh;
	};

	std::unordered_map<std::string, MeshRecord> meshes;
	std::mutex meshMutex;
	std::mutex resultMutex;
	std::vector<LoadResult> results;
	std::unique_ptr<BS::thread_pool<>> loader;

	// Buckets own texture references, so cache entries remain valid until their last bucket is removed.
	struct ComplexEntry
	{
		uint32_t activeUsers = 0;
		ID3D11ShaderResourceView* resourceView = nullptr;
		std::unique_ptr<Buffer> normalLength;
	};
	std::unordered_map<RE::NiSourceTexture*, ComplexEntry> complexCache;

	winrt::com_ptr<ID3D11ComputeShader> detectCS;
	bool detectCSFailed = false;  // A failed compile is retried after the shader cache is cleared.
};

/** @brief Property and geometry inputs shared by every instance in an optimized draw. */
struct GrassDrawParams
{
	float wavePeriod = 1.0f;
	std::array<float, 2> windDirection{};
	bool uniformScale = false;
	bool pbrMaterial = false;
	bool sphereNormal = false;
	std::array<float, 3> boundCenter{};
	float modelRadius = 0.0f;
	bool operator==(const GrassDrawParams&) const = default;
};

/** @brief Captures the material values consumed by grass drawing. */
struct GrassMaterialKey
{
	std::array<RE::NiSourceTexture*, 4> textures{};
	uint32_t clampMode = 0;
	TruePBR::GrassPBRConstants constants{};
	bool operator==(const GrassMaterialKey&) const = default;
};

/** @brief Identifies a fully registered mesh variant and its immutable draw state. */
struct BucketKey
{
	GrassMeshLibrary::MeshRecord* mesh = nullptr;
	uint32_t triCount = 0;
	uint64_t descVal = 0;
	GrassMaterialKey material;
	GrassDrawParams draw;
	bool operator==(const BucketKey&) const = default;
};

struct BucketKeyHash
{
	size_t operator()(const BucketKey& k) const
	{
		size_t hash = 0;
		const auto combine = [&hash](auto value) { hash = hash * 31 ^ std::hash<decltype(value)>{}(value); };

		combine(k.mesh);
		combine(k.triCount);
		combine(k.descVal);

		const auto& m = k.material;
		for (auto value : m.textures)
			combine(value);

		combine(m.clampMode);
		combine(m.constants.flags);

		for (float value : m.constants.params1)
			combine(value);
		for (float value : m.constants.params2)
			combine(value);

		combine(k.draw.wavePeriod);

		for (float value : k.draw.windDirection)
			combine(value);

		combine(k.draw.uniformScale);
		combine(k.draw.pbrMaterial);
		combine(k.draw.sphereNormal);

		for (float value : k.draw.boundCenter)
			combine(value);

		combine(k.draw.modelRadius);

		return hash;
	}
};

/** @brief Retains a bucket's mesh proxy and material independently of native shape lifetimes. */
struct GrassAsset
{
	BucketKey key;
	winrt::com_ptr<ID3D11Buffer> vertexBuffer;
	winrt::com_ptr<ID3D11Buffer> indexBuffer;
	RE::BSTSmartPointer<RE::BSShaderMaterial> material;
	RE::NiPointer<RE::NiSourceTexture> diffuseTexture;
	RE::NiPointer<RE::BSMultiStreamInstanceTriShape> proxy;
};
