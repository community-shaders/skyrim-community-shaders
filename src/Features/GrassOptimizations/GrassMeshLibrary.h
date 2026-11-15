#pragma once

#include <BS_thread_pool.hpp>

#include "Buffer.h"
#include "TruePBR.h"

struct GrassDrawResources;
struct GrassMaterialKey;

/** @brief Returns the vertex stride in bytes from a descriptor that stores it in four-byte units. */
constexpr uint32_t VertexStrideFromDesc(uint64_t descVal) { return static_cast<uint32_t>((4 * descVal) & 0x3C); }

/** @brief Manages grass mesh identities, draw resources, texture classification and optional LOD meshes. */
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
		RE::NiPointer<RE::NiAVObject> modelRoot;
		ID3D11Buffer* vertexBuffer = nullptr;
		ID3D11Buffer* indexBuffer = nullptr;
		uint32_t indexCount = 0;
		float boundExtent = 0.0f;
		float canopyHeight = 1.0f;
		float occlusionRadius = 0.0f;
		// The LOD .nif is authored separately, so its vertex format need not match the source mesh's.
		uint32_t vertexStride = 0;
		uint64_t vertexDesc = 0;
		LoadState state = LoadState::kUnrequested;

		/** @brief Returns the mesh-cost scale used by CPU and GPU culling. */
		float GetMeshCost() const { return MeshCost(indexCount); }
	};

	/** @brief Mesh-cost scale for a mesh drawing this many indices; eight triangles cost one. */
	static float MeshCost(uint32_t indexCount) { return std::sqrt(std::max(1.0f, (float)indexCount / 24.0f)); }

	/** @brief Stable identity and render-thread LOD state for a mesh stem. */
	struct RegisteredMesh
	{
		std::string stem;
		uint32_t activeBuckets = 0;
		std::array<LODMesh, (size_t)LODTier::kCount> lodMeshes;
	};

	/** @brief Returns a stable mesh registration for a model path. */
	RegisteredMesh& RegisterMesh(const char* modelPath);
	/** @brief Creates a bucket's mesh proxy and retains its mesh and texture users. */
	bool CreateDrawResources(GrassDrawResources& asset, RE::BSMultiStreamInstanceTriShape* geometryTemplate);
	/** @brief Releases a bucket's mesh and texture users. */
	void ReleaseDrawResources(const GrassDrawResources& asset);
	/** @brief Captures the material inputs shared by an optimized draw. */
	static GrassMaterialKey CaptureMaterial(RE::BSShaderMaterial* material, bool pbr);
	/** @brief Returns cached texture classification, or nullptr while it is unavailable. */
	ID3D11ShaderResourceView* GetTextureClassification(RE::NiSourceTexture* texture, ID3D11DeviceContext* context);
	/** @brief Releases the classification shader and allows a failed compile to retry. */
	void ClearShaderCache();

	/** @brief Queues missing LODs without blocking instance preparation or allocation. */
	void EnsureLODMeshes(RegisteredMesh& mesh);
	/** @brief Publishes completed model loads on the render thread. */
	void CommitLoadedLODMeshes();
	/** @brief Advances whenever committed loads change which LOD meshes are available. */
	uint32_t GetLODGeneration() const { return lodGeneration; }
	/** @brief Returns a ready tier, with the middle mesh serving an unavailable far tier. */
	static const LODMesh* GetLODMesh(const RegisteredMesh& mesh, LODTier tier);
	/** @brief Evicts ready LODs after the last bucket releases the mesh, retaining failed-load results. */
	static void ReleaseMesh(RegisteredMesh& mesh);

private:
	static bool FindLODShape(RE::NiAVObject* object, RE::BSTriShape*& shape);
	static std::string StemOf(const char* path);

	/** @brief Loads one tier's .nif into an entry. Runs on the loader thread. */
	static void LoadLODMesh(LODMesh& entry, const std::string& stem, LODTier tier);

	ID3D11ComputeShader* GetDetectCS();

	struct LODLoadResult
	{
		RegisteredMesh* target;
		LODTier tier;
		LODMesh mesh;
	};

	std::unordered_map<std::string, RegisteredMesh> meshes;
	std::mutex meshMutex;
	std::mutex resultMutex;
	std::vector<LODLoadResult> results;
	std::unique_ptr<BS::thread_pool<>> loader;

	// Buckets own texture references, so cache entries remain valid until their last bucket is removed.
	struct TextureClassification
	{
		uint32_t activeUsers = 0;
		ID3D11ShaderResourceView* resourceView = nullptr;
		std::unique_ptr<Buffer> normalLength;
	};
	std::unordered_map<RE::NiSourceTexture*, TextureClassification> complexCache;

	winrt::com_ptr<ID3D11ComputeShader> detectCS;
	bool detectCSFailed = false;  // A failed compile is retried after the shader cache is cleared.
	uint32_t lodGeneration = 0;
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
	std::array<RE::NiSourceTexture*, 6> textures{};
	uint32_t clampMode = 0;
	TruePBR::GrassPBRConstants constants{};
	bool operator==(const GrassMaterialKey&) const = default;
};

/** @brief Identifies a fully registered mesh variant and its immutable draw state. */
struct GrassBucketKey
{
	GrassMeshLibrary::RegisteredMesh* mesh = nullptr;
	uint32_t triCount = 0;
	uint64_t vertexDesc = 0;
	GrassMaterialKey material;
	GrassDrawParams draw;
	bool operator==(const GrassBucketKey&) const = default;
};

struct GrassBucketKeyHash
{
	size_t operator()(const GrassBucketKey& k) const
	{
		size_t hash = 0;
		const auto combine = [&hash](auto value) { hash = hash * 31 ^ std::hash<decltype(value)>{}(value); };

		combine(k.mesh);
		combine(k.triCount);
		combine(k.vertexDesc);

		const auto& m = k.material;
		for (auto value : m.textures)
			combine(value);

		combine(m.clampMode);
		combine(m.constants.flags);

		for (float value : m.constants.params1)
			combine(value);
		for (float value : m.constants.params2)
			combine(value);
		for (float value : m.constants.params3)
			combine(value);
		for (float value : m.constants.params4)
			combine(value);
		for (float value : m.constants.params5)
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
struct GrassDrawResources
{
	GrassBucketKey key;
	winrt::com_ptr<ID3D11Buffer> vertexBuffer;
	winrt::com_ptr<ID3D11Buffer> indexBuffer;
	RE::BSTSmartPointer<RE::BSShaderMaterial> material;
	RE::NiPointer<RE::NiSourceTexture> diffuseTexture;
	RE::NiPointer<RE::BSMultiStreamInstanceTriShape> proxy;
};
