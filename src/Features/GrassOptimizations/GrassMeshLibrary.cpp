#include "GrassMeshLibrary.h"
#include "TruePBR/BSLightingShaderMaterialPBR.h"

GrassMeshLibrary::~GrassMeshLibrary()
{
	loader.reset();
}

std::string GrassMeshLibrary::StemOf(const char* path)
{
	const std::string_view p = path;
	const size_t slash = p.find_last_of("\\/");
	const size_t start = slash == std::string::npos ? 0 : slash + 1;
	const size_t dot = p.find_last_of('.');
	const size_t len = (dot == std::string::npos || dot < start) ? std::string::npos : dot - start;
	return std::string(p.substr(start, len));
}

GrassMeshLibrary::MeshRecord& GrassMeshLibrary::RegisterMesh(const char* modelPath)
{
	const auto stem = StemOf(modelPath);
	std::scoped_lock lock(meshMutex);
	auto [entry, inserted] = meshes.try_emplace(stem);
	if (inserted)
		entry->second.stem = stem;

	return entry->second;
}

bool GrassMeshLibrary::CreateAsset(GrassAsset& asset, RE::BSMultiStreamInstanceTriShape* geometryTemplate)
{
	asset.proxy.reset(static_cast<RE::BSMultiStreamInstanceTriShape*>(geometryTemplate->Clone()));
	if (!asset.proxy)
		return false;

	auto& geometry = asset.proxy->GetGeometryRuntimeData();
	geometry.vertexDesc = std::bit_cast<RE::BSGraphics::VertexDesc>(asset.key.descVal);
	geometry.shaderProperty.reset(static_cast<RE::BSShaderProperty*>(geometry.shaderProperty->Clone()));

	auto* property = static_cast<RE::BSGrassShaderProperty*>(geometry.shaderProperty.get());
	property->SetMaterial(asset.material.get(), false);
	property->wavePeriod = asset.key.draw.wavePeriod;

	if (asset.key.draw.uniformScale)
		property->flags.set(RE::BSShaderProperty::EShaderPropertyFlag::kUniformScale);
	else
		property->flags.reset(RE::BSShaderProperty::EShaderPropertyFlag::kUniformScale);

	if (asset.key.draw.pbrMaterial)
		property->flags.set(RE::BSShaderProperty::EShaderPropertyFlag::kMenuScreen);
	else
		property->flags.reset(RE::BSShaderProperty::EShaderPropertyFlag::kMenuScreen);

	if (asset.key.draw.sphereNormal)
		property->flags.set(RE::BSShaderProperty::EShaderPropertyFlag::kEffectLighting);
	else
		property->flags.reset(RE::BSShaderProperty::EShaderPropertyFlag::kEffectLighting);

	++asset.key.mesh->activeBuckets;
	++complexCache[asset.diffuseTexture.get()].activeUsers;

	return true;
}

void GrassMeshLibrary::ReleaseAsset(const GrassAsset& asset)
{
	auto* tex = asset.diffuseTexture.get();
	if (auto it = complexCache.find(tex); it != complexCache.end()) {
		if (--it->second.activeUsers == 0)
			complexCache.erase(it);
	}

	ReleaseMesh(*asset.key.mesh);
}

GrassMaterialKey GrassMeshLibrary::CaptureMaterial(RE::BSShaderMaterial* material, bool pbr)
{
	auto* m = static_cast<RE::BSLightingShaderMaterialBase*>(material);
	GrassMaterialKey key;
	key.textures[0] = m->diffuseTexture.get();

	if (pbr) {
		auto* pm = static_cast<BSLightingShaderMaterialPBR*>(material);
		key.textures = { m->diffuseTexture.get(), m->normalTexture.get(), pm->rmaosTexture.get(), pm->featuresTexture0.get() };
		key.clampMode = (uint32_t)m->textureClampMode;
		key.constants = TruePBR::GetGrassPBRConstants(pm);
	}

	return key;
}

void GrassMeshLibrary::ClearShaderCache()
{
	detectCS = nullptr;
	detectCSFailed = false;
}

ID3D11ShaderResourceView* GrassMeshLibrary::GetComplexGrass(RE::NiSourceTexture* tex, ID3D11DeviceContext* ctx)
{
	// Without a classification the pixel shader tests the texture itself.
	const auto found = complexCache.find(tex);
	if (found == complexCache.end())
		return nullptr;

	auto& entry = found->second;
	auto* rt = tex ? tex->rendererTexture : nullptr;
	auto* resourceView = rt ? rt->resourceView : nullptr;
	if (entry.normalLength && entry.resourceView == resourceView) {
		return entry.normalLength->srv.get();
	}

	if (!resourceView || !GetDetectCS())
		return nullptr;  // Retry textures whose renderer resource is not ready yet.

	if (!entry.normalLength) {
		try {
			const float unknown = 0.0f;
			D3D11_SUBRESOURCE_DATA init{ &unknown, 0, 0 };
			entry.normalLength = std::make_unique<Buffer>(StructuredBufferDesc<float>(uint64_t{ 1 }), &init, "GrassOptimizations::NormalLength");

			D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
			sv.Format = DXGI_FORMAT_UNKNOWN;
			sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			sv.Buffer.NumElements = 1;
			entry.normalLength->CreateSRV(sv);

			D3D11_UNORDERED_ACCESS_VIEW_DESC uv{};
			uv.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			uv.Buffer.NumElements = 1;
			entry.normalLength->CreateUAV(uv);

		} catch (...) {
			entry.normalLength.reset();
			return nullptr;
		}
	}
	// The previous grass draw may still have this result bound for reading.
	ID3D11ShaderResourceView* nullSRV = nullptr;
	ctx->VSSetShaderResources(5, 1, &nullSRV);

	ID3D11UnorderedAccessView* resultUAV = entry.normalLength->uav.get();
	ctx->CSSetUnorderedAccessViews(0, 1, &resultUAV, nullptr);

	ctx->CSSetShader(detectCS.get(), nullptr, 0);
	ctx->CSSetShaderResources(0, 1, &resourceView);
	ctx->Dispatch(1, 1, 1);

	ID3D11UnorderedAccessView* nullUAV = nullptr;
	ctx->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	ctx->CSSetShaderResources(0, 1, &nullSRV);

	entry.resourceView = resourceView;
	return entry.normalLength->srv.get();
}

ID3D11ComputeShader* GrassMeshLibrary::GetDetectCS()
{
	if (!detectCS && !detectCSFailed) {
		detectCS.attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\GrassOptimizations\\DetectComplexCS.hlsl", {}, "cs_5_0")));

		if (!detectCS) {
			detectCSFailed = true;
			logger::error("[GRASS OPTIMIZATIONS] detect CS load failed — complex detection disabled");
		}
	}

	return detectCS.get();
}

void GrassMeshLibrary::EnsureLODMeshes(MeshRecord& mesh)
{
	for (uint32_t tier = 0; tier < (uint32_t)LODTier::kCount; ++tier) {
		auto& entry = mesh.lodMeshes[tier];
		if (entry.state != LoadState::kUnrequested)
			continue;

		entry.state = LoadState::kQueued;
		if (!loader)
			loader = std::make_unique<BS::thread_pool<>>(1);

		loader->detach_task([this, target = &mesh, tier = (LODTier)tier] {
			LoadResult result{ target, tier, {} };
			LoadLODMesh(result.mesh, target->stem, tier);
			std::scoped_lock lock(resultMutex);
			results.push_back(std::move(result));
		});
	}
}

void GrassMeshLibrary::CommitLoadedLODMeshes()
{
	if (!loader)
		return;

	static thread_local std::vector<LoadResult> done;
	done.clear();
	{
		std::scoped_lock lk(resultMutex);
		done.swap(results);
	}

	for (auto& result : done) {
		auto& entry = result.target->lodMeshes[(size_t)result.tier];
		if (!result.target->activeBuckets || result.mesh.state != LoadState::kReady) {
			// Release unused model trees and keep failed loads cached.
			entry = {};
			if (result.mesh.state != LoadState::kReady)
				entry.state = LoadState::kUnavailable;
			continue;
		}
		entry = std::move(result.mesh);
	}

	done.clear();
}

const GrassMeshLibrary::LODMesh* GrassMeshLibrary::GetLODMesh(const MeshRecord& mesh, LODTier tier)
{
	const auto& entries = mesh.lodMeshes;
	if (entries[(size_t)tier].state == LoadState::kReady)
		return &entries[(size_t)tier];

	const auto& middle = entries[(size_t)LODTier::kMiddle];
	return tier == LODTier::kFar && middle.state == LoadState::kReady ? &middle : nullptr;
}

void GrassMeshLibrary::ReleaseMesh(MeshRecord& mesh)
{
	if (--mesh.activeBuckets)
		return;

	for (auto& entry : mesh.lodMeshes) {
		if (entry.state == LoadState::kReady)
			entry = {};
	}
}

bool GrassMeshLibrary::FindLODShape(RE::NiAVObject* object, RE::BSTriShape*& shape)
{
	if (!object)
		return true;

	if (object->local != RE::NiTransform{})
		return false;

	if (auto* geometry = object->AsTriShape()) {
		if (shape)
			return false;

		shape = geometry;
	} else if (auto* node = object->AsNode()) {
		for (const auto& child : node->GetChildren()) {
			if (!FindLODShape(child.get(), shape))
				return false;
		}
	}

	return true;
}

void GrassMeshLibrary::LoadLODMesh(LODMesh& entry, const std::string& stem, LODTier tier)
{
	entry.state = LoadState::kUnavailable;

	const std::string modelPath = "LOD\\Grass\\" + stem + (tier == LODTier::kFar ? "_LOD1.nif" : "_LOD0.nif");

	RE::BSModelDB::DBTraits::ArgsType args{};
	args.unk8 = false;
	args.unkA = false;
	args.postProcess = false;
	RE::NiPointer<RE::NiNode> root;

	if (RE::BSModelDB::Demand(modelPath.c_str(), root, args) == RE::BSResource::ErrorCode::kNone && root) {
		entry.keepAlive = RE::NiPointer<RE::NiAVObject>(root.get());
		RE::BSTriShape* ts = nullptr;

		if (FindLODShape(root.get(), ts) && ts) {
			auto& grd = ts->GetGeometryRuntimeData();
			auto* rd = grd.rendererData;

			if (rd && rd->vertexBuffer && rd->indexBuffer) {
				entry.vertexBuffer = reinterpret_cast<ID3D11Buffer*>(rd->vertexBuffer);
				entry.indexBuffer = reinterpret_cast<ID3D11Buffer*>(rd->indexBuffer);
				// Match the source grass instancing flags while retaining the LOD vertex format.
				entry.descVal = *reinterpret_cast<const uint64_t*>(&grd.vertexDesc) | 0x8000000000000080ull;
				entry.meshStride = VertexStrideFromDesc(entry.descVal);
				entry.indexCount = 3u * ts->GetTrishapeRuntimeData().triangleCount;
				const auto& bound = ts->GetModelData().modelBound;
				entry.boundExtent = bound.center.Length() + bound.radius;

				if (entry.meshStride != 0 && entry.indexCount != 0)
					entry.state = LoadState::kReady;
				else
					logger::warn("[GRASS OPTIMIZATIONS] LOD mesh {} rejected: indices={} stride={} descVal={:016X}", modelPath, entry.indexCount, entry.meshStride, entry.descVal);

			} else {
				logger::warn("[GRASS OPTIMIZATIONS] LOD mesh {} has no GPU buffers", modelPath);
			}
		} else {
			logger::warn("[GRASS OPTIMIZATIONS] LOD mesh {} requires one TriShape with baked node and geometry transforms", modelPath);
		}
	}
}
