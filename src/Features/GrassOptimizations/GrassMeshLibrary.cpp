#include "GrassMeshLibrary.h"

namespace
{
	RE::BSTriShape* FindFirstTriShape(RE::NiAVObject* obj)
	{
		if (!obj)
			return nullptr;
		if (auto* ts = obj->AsTriShape())
			return ts;
		if (auto* node = obj->AsNode()) {
			for (auto& child : node->GetChildren()) {
				if (auto* ts = FindFirstTriShape(child.get()))
					return ts;
			}
		}
		return nullptr;
	}

	/** @brief Returns the filename without directories or extension. */
	std::string StemOf(const char* path)
	{
		const std::string p = path;
		const size_t slash = p.find_last_of("\\/");
		const size_t start = slash == std::string::npos ? 0 : slash + 1;
		const size_t dot = p.find_last_of('.');
		const size_t len = (dot == std::string::npos || dot < start) ? std::string::npos : dot - start;
		return p.substr(start, len);
	}
}

void GrassMeshLibrary::RecordModelPath(RE::BSMultiStreamInstanceTriShape* shape, const char* modelPath)
{
	if (!shape || !modelPath || !*modelPath)
		return;

	std::scoped_lock lk(stemMutex);
	stemByShape.insert_or_assign(shape, StemOf(modelPath));
}

uint32_t GrassMeshLibrary::ResolveMeshId(RE::BSMultiStreamInstanceTriShape* shape)
{
	if (!shape)
		return 0;
	if (auto it = idByShape.find(shape); it != idByShape.end())
		return it->second;

	// LoadGrassType records every grass type at creation, before any instance of it is captured.
	std::string stem;
	{
		std::scoped_lock lk(stemMutex);
		if (auto it = stemByShape.find(shape); it != stemByShape.end())
			stem = it->second;
	}

	uint32_t meshId = 0;
	if (!stem.empty()) {
		const auto it = std::find(stems.begin(), stems.end(), stem);
		meshId = static_cast<uint32_t>(std::distance(stems.begin(), it)) + 1;  // ids are 1-based, with 0 reserved for unresolved shapes
		if (it == stems.end())
			stems.push_back(stem);
	}

	idByShape.emplace(shape, meshId);
	return meshId;
}

void GrassMeshLibrary::EnsureLODMeshes(uint32_t meshId)
{
	if (meshId == 0 || meshId > stems.size())
		return;

	if (lodMeshes.size() < stems.size())
		lodMeshes.resize(stems.size());

	for (uint32_t tier = 0; tier < (uint32_t)LODTier::kCount; ++tier)
		LoadLODMesh(lodMeshes[meshId - 1][tier], stems[meshId - 1], (LODTier)tier);
}

void GrassMeshLibrary::LoadLODMesh(LODMesh& entry, const std::string& stem, LODTier tier)
{
	if (entry.attemptedLoad)
		return;
	entry.attemptedLoad = true;

	const std::string modelPath = "LOD\\Grass\\" + stem + (tier == LODTier::kFar ? "_LOD1.nif" : "_LOD0.nif");

	RE::BSModelDB::DBTraits::ArgsType args{};
	args.unk8 = false;
	args.unkA = false;
	args.postProcess = false;
	RE::NiPointer<RE::NiNode> root;
	if (RE::BSModelDB::Demand(modelPath.c_str(), root, args) == RE::BSResource::ErrorCode::kNone && root) {
		if (auto* ts = FindFirstTriShape(root.get())) {
			auto& grd = ts->GetGeometryRuntimeData();
			auto* rd = grd.rendererData;
			if (rd && rd->vertexBuffer && rd->indexBuffer) {
				entry.keepAlive = RE::NiPointer<RE::NiAVObject>(root.get());
				entry.vertexBuffer = reinterpret_cast<ID3D11Buffer*>(rd->vertexBuffer);
				entry.indexBuffer = reinterpret_cast<ID3D11Buffer*>(rd->indexBuffer);
				// Alter the descriptor to match the BSMultistreaminstanceTrishapes of normal grass
				entry.descVal = *reinterpret_cast<const uint64_t*>(&grd.vertexDesc) | 0x8000000000000080ull;
				entry.meshStride = VertexStrideFromDesc(entry.descVal);
				entry.indexCount = 3u * ts->GetTrishapeRuntimeData().triangleCount;
				// A zero stride means the descriptor decoded to nothing, which would draw garbage vertices.
				entry.valid = entry.meshStride != 0;
				if (!entry.valid)
					logger::warn("[GRASS OPTIMIZATIONS] LOD mesh {} rejected: vertex stride decoded to 0 from descVal={:016X}", modelPath, entry.descVal);
				else
					logger::info("[GRASS OPTIMIZATIONS] LOD mesh {} loaded: tris={} stride={} descVal={:016X}",
						modelPath, entry.indexCount / 3, entry.meshStride, entry.descVal);
			} else {
				logger::warn("[GRASS OPTIMIZATIONS] LOD mesh {} has no GPU buffers", modelPath);
			}
		} else {
			logger::warn("[GRASS OPTIMIZATIONS] LOD mesh {} has no TriShape", modelPath);
		}
	}
	// If the file absent, that grass type keeps its full mesh.
}

const GrassMeshLibrary::LODMesh* GrassMeshLibrary::GetLODMesh(uint32_t meshId, LODTier tier) const
{
	if (meshId == 0 || meshId > lodMeshes.size())
		return nullptr;

	const auto& entries = lodMeshes[meshId - 1];
	if (entries[(size_t)tier].valid)
		return &entries[(size_t)tier];

	const LODMesh& middle = entries[(size_t)LODTier::kMiddle];
	return (tier == LODTier::kFar && middle.valid) ? &middle : nullptr;
}

void GrassMeshLibrary::ForgetShape(RE::BSMultiStreamInstanceTriShape* shape)
{
	idByShape.erase(shape);
	std::scoped_lock lk(stemMutex);
	stemByShape.erase(shape);
}
