#include <DirectXPackedVector.h>
#include "Features/SnowDeformation.h"
#include "Features/SnowDeformation/DevSwitches.h"

#include "Globals.h"
#include "ShaderCache.h"
#include "State.h"
#include "Utils/D3D.h"
#include "Utils/Format.h"

#include <bit>

static bool HasActiveLight(RE::NiAVObject* a_obj)
{
	if (!a_obj || a_obj->GetAppCulled())
		return false;
	if (netimmerse_cast<RE::NiPointLight*>(a_obj))
		return true;
	if (auto* node = a_obj->AsNode())
		for (auto& child : node->GetChildren())
			if (HasActiveLight(child.get()))
				return true;
	return false;
}

static RE::TESObjectREFR* OwningRefOf(RE::NiAVObject* a_object)
{
	RE::TESObjectREFR* refr = nullptr;
	for (RE::NiAVObject* node = a_object; node && !refr; node = node->parent)
		refr = static_cast<RE::TESObjectREFR*>(node->GetUserData());
	return refr;
}

static const char* DiffusePathOf(RE::BSLightingShaderMaterialBase* a_material)
{
	auto* textureSet = a_material ? a_material->textureSet.get() : nullptr;
	return textureSet ? textureSet->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse) : nullptr;
}

static void SetWorldRows(const RE::NiTransform& a_world, SnowDeformation::StaticsCB& a_cb)
{
	const auto& rot = a_world.rotate;
	const float scale = a_world.scale;
	a_cb.WorldRow0 = { rot.entry[0][0] * scale, rot.entry[0][1] * scale, rot.entry[0][2] * scale, a_world.translate.x };
	a_cb.WorldRow1 = { rot.entry[1][0] * scale, rot.entry[1][1] * scale, rot.entry[1][2] * scale, a_world.translate.y };
	a_cb.WorldRow2 = { rot.entry[2][0] * scale, rot.entry[2][1] * scale, rot.entry[2][2] * scale, a_world.translate.z };
}

static bool ContainsNoCase(const char* a_text, const char* a_needle)
{
	return a_text && a_needle && clib_util::string::icontains(a_text, a_needle);
}

static constexpr std::array kNotSnowKeywords{ "sand", "moss", "dirt", "mud", "gravel", "ash", "coast", "falmer", "glow", "sulfur", "leaves", "dust" };

const char* SnowDeformation::NotSnowKeywordIn(const char* a_name)
{
	for (const auto* keyword : kNotSnowKeywords)
		if (ContainsNoCase(a_name, keyword))
			return keyword;
	return nullptr;
}

struct GeometryNameFacts
{
	uint32_t length = 0;
	uint64_t head = 0;
	bool bridge = false;
	bool road = false;
	bool mountainCliff = false;
	bool iceFamily = false;
	bool drift = false;
	bool lodNotSnow = false;
	bool bloodPool = false;
	bool engineDecal = false;
	bool capturedLogged = false;
	bool lodVetoLogged = false;
};

struct SD_PointerHash
{
	size_t operator()(const void* a_pointer) const noexcept
	{
		const uint64_t h = (uint64_t)(uintptr_t)a_pointer * 0x9E3779B97F4A7C15ull;
		return (size_t)(h ^ (h >> 32));
	}
};

template <class V>
class SD_InlinePointerTable
{
public:
	static constexpr uint32_t kSlots = 32768;

	size_t size() const { return count; }

	void clear()
	{
		if (slots)
			for (uint32_t i = 0; i < kSlots; i++)
				slots[i].key = nullptr;
		count = 0;
	}

	std::pair<V*, bool> try_emplace(const void* a_key)
	{
		if (!slots)
			slots = std::make_unique<Slot[]>(kSlots);
		for (uint32_t i = static_cast<uint32_t>(SD_PointerHash{}(a_key));; i++) {
			Slot& slot = slots[i & (kSlots - 1)];
			if (slot.key == a_key)
				return { &slot.value, false };
			if (!slot.key) {
				slot.key = a_key;
				slot.value = V{};
				count++;
				return { &slot.value, true };
			}
		}
	}

private:
#pragma warning(push)
#pragma warning(disable: 4324)
	struct alignas(64) Slot
	{
		const void* key = nullptr;
		V value{};
	};
#pragma warning(pop)
	std::unique_ptr<Slot[]> slots;
	uint32_t count = 0;
};

static SD_InlinePointerTable<GeometryNameFacts> sNameFactsCache;

bool SnowDeformation::IsEngineDecalName(const char* a_name)
{
	return a_name && (_stricmp(a_name, "Decal") == 0 || _stricmp(a_name, "Permanent Decal") == 0 || _stricmp(a_name, "SkinnedDecal") == 0);
}

static GeometryNameFacts& NameFactsOf(RE::BSGeometry* a_geometry)
{
	auto& cache = sNameFactsCache;
	static GeometryNameFacts empty;
	static const void* lastGeometry = nullptr;
	static const char* lastName = nullptr;
	static GeometryNameFacts* lastFacts = nullptr;
	const char* name = a_geometry ? a_geometry->name.c_str() : nullptr;
	if (!name || !*name)
		return empty;
	if (lastFacts && lastGeometry == a_geometry && lastName == name)
		return *lastFacts;
	if (cache.size() > 16384) {
		cache.clear();
		lastFacts = nullptr;
	}
	const uint32_t length = (uint32_t)strlen(name);
	uint64_t head = 0;
	memcpy(&head, name, std::min<size_t>(length, sizeof(head)));
	auto [facts, inserted] = cache.try_emplace(name);
	auto& f = *facts;
	if (inserted || f.length != length || f.head != head) {
		f = {};
		f.length = length;
		f.head = head;
		f.bridge = ContainsNoCase(name, "bridge");
		f.road = ContainsNoCase(name, "road");
		f.mountainCliff = ContainsNoCase(name, "mountain") || ContainsNoCase(name, "cliff");
		f.iceFamily = (std::tolower((unsigned char)name[0]) == 'i' && std::tolower((unsigned char)name[1]) == 'c' && std::tolower((unsigned char)name[2]) == 'e') ||
		              ContainsNoCase(name, "glacier") || ContainsNoCase(name, "iceberg");
		f.drift = ContainsNoCase(name, "drift") && !ContainsNoCase(name, "driftwood");
		f.bloodPool = _strnicmp(name, "Decal:", 6) == 0 || _strnicmp(name, "DecalExt:", 9) == 0;
		f.engineDecal = SnowDeformation::IsEngineDecalName(name);
		f.lodNotSnow = false;
		if (length > 3 && std::tolower((unsigned char)name[0]) == 'o' && std::tolower((unsigned char)name[1]) == 'b' && std::tolower((unsigned char)name[2]) == 'j') {
			for (const auto* keyword : kNotSnowKeywords)
				f.lodNotSnow = f.lodNotSnow || ContainsNoCase(name, keyword);
		}
	}
	lastGeometry = a_geometry;
	lastName = name;
	lastFacts = &f;
	return f;
}

struct SD_DrawBinds
{
	ID3D11InputLayout* layout = nullptr;
	ID3D11Buffer* vb = nullptr;
	UINT stride = 0;
	ID3D11Buffer* ib = nullptr;
	DXGI_FORMAT ibFormat = DXGI_FORMAT_UNKNOWN;

	void Layout(ID3D11DeviceContext* a_context, ID3D11InputLayout* a_layout)
	{
		if (layout == a_layout)
			return;
		a_context->IASetInputLayout(a_layout);
		layout = a_layout;
	}
	void Vertex(ID3D11DeviceContext* a_context, ID3D11Buffer* a_vb, UINT a_stride)
	{
		if (vb == a_vb && stride == a_stride)
			return;
		UINT offset = 0;
		a_context->IASetVertexBuffers(0, 1, &a_vb, &a_stride, &offset);
		vb = a_vb;
		stride = a_stride;
	}
	void Index(ID3D11DeviceContext* a_context, ID3D11Buffer* a_ib, DXGI_FORMAT a_format)
	{
		if (ib == a_ib && ibFormat == a_format)
			return;
		a_context->IASetIndexBuffer(a_ib, a_format, 0);
		ib = a_ib;
		ibFormat = a_format;
	}
};

struct RigidDraw
{
	RE::BSTriShape* triShape;
	ID3D11Buffer* vb;
	ID3D11Buffer* ib;
	uint32_t indexCount;
	RE::BSGraphics::VertexDesc desc;
	uint64_t descKey;
};

static bool RigidDrawOf(RE::BSGeometry* a_geometry, RigidDraw& a_out)
{
	auto triShape = a_geometry->AsTriShape();
	if (!triShape)
		return false;
	auto rendererData = a_geometry->GetGeometryRuntimeData().rendererData;
	if (!rendererData || !rendererData->vertexBuffer || !rendererData->indexBuffer)
		return false;
	const uint32_t indexCount = uint32_t(triShape->GetTrishapeRuntimeData().triangleCount) * 3;
	if (indexCount == 0)
		return false;
	auto desc = rendererData->vertexDesc;
	if (!desc.HasFlag(RE::BSGraphics::Vertex::VF_VERTEX) || !desc.HasFlag(RE::BSGraphics::Vertex::VF_NORMAL))
		return false;
	a_out = { triShape, reinterpret_cast<ID3D11Buffer*>(rendererData->vertexBuffer), reinterpret_cast<ID3D11Buffer*>(rendererData->indexBuffer), indexCount, desc, 0 };
	memcpy(&a_out.descKey, &desc, sizeof(a_out.descKey));
	return true;
}

static void SampleLODDecision(RE::BSGeometry* a_geometry, float a_radius, bool a_rejected, bool a_cameraInside)
{
	static std::atomic<uint32_t> loggedKept{ 0 };
	static std::atomic<uint32_t> loggedRejected{ 0 };
	auto& logged = a_rejected ? loggedRejected : loggedKept;
	if (logged.load(std::memory_order_relaxed) >= 8)
		return;
	if (logged.fetch_add(1, std::memory_order_relaxed) >= 8)
		return;
	logger::info("[SNOW DEFORMATION] object LOD {}: radius {:.0f}, cameraInside={}, '{}'",
		a_rejected ? "REJECTED (merged sheet, camera inside)" : "kept",
		a_radius, a_cameraInside ? "yes" : "no",
		a_geometry->name.empty() ? "<unnamed>" : a_geometry->name.c_str());
}
namespace
{
	enum class MatoClass
	{
		kNoReference,
		kNoMato,
		kSnow,
		kSnowSeasons,
		kNotSnow
	};

	MatoClass ClassifyProjectedMato(RE::BSGeometry* a_geometry)
	{
		auto* refr = OwningRefOf(a_geometry);
		if (!refr)
			return MatoClass::kNoReference;
		auto* base = refr->GetBaseObject();
		if (!base)
			return MatoClass::kNoReference;
		auto* stat = base->As<RE::TESObjectSTAT>();
		auto* mato = stat ? stat->data.materialObj : nullptr;
		static std::unordered_map<RE::FormID, MatoClass> matoClassCache;
		if (matoClassCache.size() > 4096)
			matoClassCache.clear();
		auto [it, inserted] = matoClassCache.try_emplace(mato ? mato->GetFormID() : 0, MatoClass::kNoMato);
		if (inserted) {
			if (mato) {
				std::string matoPath;
				if (const char* edid = mato->GetFormEditorID())
					matoPath = edid;
				matoPath += '|';
				matoPath += mato->GetModel();
				matoPath = Util::ToLower(std::move(matoPath));
				if (matoPath.starts_with("sos_win_snowmaterialobject")) {
					it->second = MatoClass::kSnowSeasons;
				} else if (matoPath.find("snow") != std::string::npos) {
					it->second = MatoClass::kSnow;
				} else {
					it->second = MatoClass::kNoMato;
					for (const auto* keyword : kNotSnowKeywords) {
						if (matoPath.find(keyword) != std::string::npos) {
							it->second = MatoClass::kNotSnow;
							break;
						}
					}
				}
				logger::info("[SNOW DEFORMATION] Projected MATO {:08X} (base {:08X}): '{}' -> {}", mato->GetFormID(), base->GetFormID(), matoPath,
					it->second == MatoClass::kSnow ? "snow" : (it->second == MatoClass::kSnowSeasons ? "snow (Seasons, unauthored)" : (it->second == MatoClass::kNotSnow ? "NOT snow (vetoed)" : "neutral")));
			} else {
				logger::info("[SNOW DEFORMATION] Projected base {:08X}: no STAT MATO -> neutral", base->GetFormID());
			}
		}
		return it->second;
	}

	struct SnowPathMatch
	{
		bool base = false;
		bool naturalFeature = false;
		bool mountain = false;
		bool snowNamed = false;
		bool groundSnow = false;
		bool architecture = false;
	};

	bool GeometryBelongsToLoadedReference(RE::BSGeometry* a_geometry)
	{
		static std::unordered_map<const void*, bool> ownershipCache;
		if (ownershipCache.size() > 4096)
			ownershipCache.clear();
		auto [it, inserted] = ownershipCache.try_emplace(a_geometry, false);
		if (!inserted)
			return it->second;
		auto* tes = globals::game::tes;
		if (!tes)
			return false;
		const RE::NiPoint3 center = a_geometry->worldBound.center;
		const float reach = a_geometry->worldBound.radius + 1024.0f;
		bool found = false;
		tes->ForEachReference([&](RE::TESObjectREFR* a_ref) {
			if (!a_ref || a_ref->GetPosition().GetDistance(center) > reach)
				return RE::BSContainer::ForEachResult::kContinue;
			auto* root = a_ref->Get3D();
			if (!root)
				return RE::BSContainer::ForEachResult::kContinue;
			std::vector<RE::NiAVObject*> stack{ root };
			while (!stack.empty()) {
				RE::NiAVObject* node = stack.back();
				stack.pop_back();
				if (node == a_geometry) {
					found = true;
					return RE::BSContainer::ForEachResult::kStop;
				}
				if (auto* niNode = node->AsNode()) {
					for (auto& child : niNode->GetChildren())
						if (child)
							stack.push_back(child.get());
				}
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		it->second = found;
		return found;
	}

	const SnowPathMatch& ClassifySnowPath(RE::BSLightingShaderMaterialBase* a_material)
	{
		static std::unordered_map<const void*, SnowPathMatch> pathCache;
		if (pathCache.size() > 4096)
			pathCache.clear();
		auto [it, inserted] = pathCache.try_emplace(a_material, SnowPathMatch{});
		if (inserted) {
			if (auto path = DiffusePathOf(a_material)) {
				const std::string lowered = Util::ToLower(path);
				it->second.base = lowered.find("drift") != std::string::npos ||
				                  (lowered.find("landscape") != std::string::npos && lowered.find("snow") != std::string::npos);
				it->second.naturalFeature = lowered.find("glacier") != std::string::npos ||
				                            lowered.find("iceberg") != std::string::npos;
				it->second.mountain = lowered.find("mountain") != std::string::npos ||
				                      lowered.find("cliff") != std::string::npos;
				const size_t slash = lowered.find_last_of("\\/");
				const std::string file = slash == std::string::npos ? lowered : lowered.substr(slash + 1);
				it->second.snowNamed = file.find("snow") != std::string::npos &&
				                       file.find("snowberr") == std::string::npos &&
				                       file.find("nosnow") == std::string::npos;
				const std::string folder = slash == std::string::npos ? std::string() : lowered.substr(0, slash);
				it->second.groundSnow = it->second.snowNamed && file.rfind("snow", 0) == 0 &&
				                        folder.find("landscape") != std::string::npos &&
				                        folder.find("tree") == std::string::npos && folder.find("grass") == std::string::npos &&
				                        folder.find("plant") == std::string::npos && folder.find("road") == std::string::npos &&
				                        folder.find("flower") == std::string::npos;
				it->second.architecture = lowered.find("architecture") != std::string::npos &&
				                          lowered.find("ivy") == std::string::npos && lowered.find("vine") == std::string::npos &&
				                          lowered.find("leaf") == std::string::npos && lowered.find("leaves") == std::string::npos &&
				                          lowered.find("grass") == std::string::npos && lowered.find("branch") == std::string::npos &&
				                          lowered.find("moss") == std::string::npos;
			}
		}
		return it->second;
	}

	bool ModelRoadPassesGate(RE::BSGeometry* a_geometry)
	{
		RE::NiAVObject* root = a_geometry->parent;
		for (RE::NiAVObject* node = a_geometry; node; node = node->parent) {
			if (node->GetUserData()) {
				root = node;
				break;
			}
		}
		if (!root)
			return false;
		bool found = false;
		RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* a_mate) -> RE::BSVisit::BSVisitControl {
			const char* name = a_mate->name.c_str();
			if (a_mate == a_geometry || !name || !ContainsNoCase(name, "road") || ContainsNoCase(name, "bridge"))
				return RE::BSVisit::BSVisitControl::kContinue;
			auto* property = netimmerse_cast<RE::BSLightingShaderProperty*>(a_mate->GetGeometryRuntimeData().shaderProperty.get());
			auto* material = property ? static_cast<RE::BSLightingShaderMaterialBase*>(property->material) : nullptr;
			const char* diffuse = DiffusePathOf(material);
			if (!diffuse || ContainsNoCase(diffuse, "bridge"))
				return RE::BSVisit::BSVisitControl::kContinue;
			const bool paving = (ContainsNoCase(diffuse, "floor") || ContainsNoCase(diffuse, "slate")) && !ContainsNoCase(diffuse, "roof");
			if (!ContainsNoCase(diffuse, "landscape") && !paving)
				return RE::BSVisit::BSVisitControl::kContinue;
			found = property->flags.all(RE::BSShaderProperty::EShaderPropertyFlag::kProjectedUV) || ClassifySnowPath(material).base;
			return found ? RE::BSVisit::BSVisitControl::kStop : RE::BSVisit::BSVisitControl::kContinue;
		});
		return found;
	}
}

struct GeometryRecord
{
	const void* material = nullptr;
	const char* name = nullptr;
	bool pathBase = false;
	bool pathNatural = false;
	bool pathMountain = false;
	bool pathArchitecture = false;
	bool pathSnowNamed = false;
	bool pathGroundSnow = false;
	bool iceName = false;
	bool shard = false;
	bool bloodTex = false;
	uint8_t roadTex = 0;
	bool roadLayer = false;
	bool landscapeTex = false;
	bool pavingTex = false;
	MatoClass mato = MatoClass::kNoReference;
	bool seasonsProj = false;
	bool seasonsStatic = false;
	float depthX = 0.0f;
	float depthY = 0.0f;
	float depth = 0.0f;
	uint32_t depthVersion = 0;
};

static SD_InlinePointerTable<GeometryRecord> sGeometryRecordCache;

static bool PassBlendsOver(RE::BSRenderPass* a_pass)
{
	using Flag = RE::BSShaderProperty::EShaderPropertyFlag;
	if (a_pass->shaderProperty->flags.any(Flag::kDecal, Flag::kDynamicDecal))
		return true;
	auto* alpha = a_pass->geometry->GetGeometryRuntimeData().alphaProperty.get();
	return alpha && alpha->GetAlphaBlending();
}

static GeometryRecord& RecordOf(RE::BSGeometry* a_geometry, RE::BSLightingShaderMaterialBase* a_material)
{
	auto& cache = sGeometryRecordCache;
	static const void* lastGeometry = nullptr;
	static GeometryRecord* lastRecord = nullptr;
	const char* name = a_geometry->name.c_str();
	if (lastRecord && lastGeometry == a_geometry && lastRecord->material == a_material && lastRecord->name == name)
		return *lastRecord;
	if (cache.size() > 16384) {
		cache.clear();
		lastRecord = nullptr;
	}
	auto [record, inserted] = cache.try_emplace(a_geometry);
	auto& r = *record;
	if (inserted || r.material != a_material || r.name != name) {
		r = {};
		r.material = a_material;
		r.name = name;
		auto& nameFacts = NameFactsOf(a_geometry);
		r.iceName = nameFacts.iceFamily;
		if (a_material) {
			const SnowPathMatch& path = ClassifySnowPath(a_material);
			r.pathBase = path.base;
			r.pathNatural = path.naturalFeature;
			r.pathMountain = path.mountain;
			r.pathArchitecture = path.architecture;
			r.pathSnowNamed = path.snowNamed;
			r.pathGroundSnow = path.groundSnow;
			if (auto diffuse = DiffusePathOf(a_material)) {
				r.shard = ContainsNoCase(diffuse, "branchpile") || ContainsNoCase(diffuse, "driftwood");
				r.roadTex = ContainsNoCase(diffuse, "bridge") ? 2 : (ContainsNoCase(diffuse, "road") ? 1 : 0);
				r.landscapeTex = ContainsNoCase(diffuse, "landscape");
				r.pavingTex = (ContainsNoCase(diffuse, "floor") || ContainsNoCase(diffuse, "slate")) && !ContainsNoCase(diffuse, "roof");
				r.bloodTex = ContainsNoCase(diffuse, "blood");
			}
		}
		r.mato = ClassifyProjectedMato(a_geometry);
		if (r.mato == MatoClass::kNoReference && nameFacts.lodNotSnow) {
			r.mato = MatoClass::kNotSnow;
			if (!nameFacts.lodVetoLogged) {
				nameFacts.lodVetoLogged = true;
				logger::info("[SNOW DEFORMATION] LOD batch '{}' -> NOT snow (vetoed by name)", name);
			}
		}
		r.seasonsProj = r.mato == MatoClass::kSnowSeasons;
		if (!r.seasonsProj) {
			static const RE::BSFixedString seasonsMarker("SOS_SNOW_SHADER");
			for (RE::NiAVObject* node = a_geometry; node && !r.seasonsProj; node = node->parent)
				r.seasonsProj = node->GetExtraData(seasonsMarker) != nullptr;
		}
		if (!r.seasonsProj) {
			static std::unordered_map<uint32_t, bool> sosBase;
			auto* refr = OwningRefOf(a_geometry);
			if (auto* base = refr ? refr->GetBaseObject() : nullptr) {
				auto [sosIt, sosInserted] = sosBase.try_emplace(base->GetFormID(), false);
				if (sosInserted) {
					if (auto* file = base->GetFile(0); file && file->fileName)
						sosIt->second = ContainsNoCase(file->fileName, "snowoverskyrim.esp");
				}
				r.seasonsStatic = sosIt->second;
				r.seasonsProj = sosIt->second;
			}
		}
		auto* alpha = a_geometry->GetGeometryRuntimeData().alphaProperty.get();
		if (alpha && alpha->GetAlphaTesting() && !nameFacts.road && !nameFacts.bridge && r.roadTex != 2)
			r.roadLayer = ModelRoadPassesGate(a_geometry);
	}
	lastGeometry = a_geometry;
	lastRecord = &r;
	return r;
}

void SnowDeformation::SetProjectedSnowBit(RE::BSLightingShader* a_shader, RE::BSRenderPass* a_pass)
{
	auto& extraDescriptor = globals::state->permutationData.ExtraFeatureDescriptor;
	extraDescriptor &= ~(uint32_t(State::ExtraFeatureDescriptors::SnowProjectedIsSnow) | uint32_t(State::ExtraFeatureDescriptors::SnowLODBakedIsSnow) |
						 uint32_t(State::ExtraFeatureDescriptors::SnowProjectedUnauthored) | uint32_t(State::ExtraFeatureDescriptors::SnowProjectedNoAlpha) |
						 uint32_t(State::ExtraFeatureDescriptors::SnowMultipassBase) | uint32_t(State::ExtraFeatureDescriptors::SnowObjectBarePass) |
						 uint32_t(State::ExtraFeatureDescriptors::SnowTexturedOverlay) | uint32_t(State::ExtraFeatureDescriptors::SnowTexturedGround));
	if (!a_shader || !a_pass || !a_pass->shaderProperty || !a_pass->geometry)
		return;
	if (!settings.EnableSnowDeformation || !shellSnowDiffuseSRV)
		return;
	using Flag = RE::BSShaderProperty::EShaderPropertyFlag;
	bool bindSnowSet = false;
	if (settings.ProjSnowMatch) {
		const bool passProjected = (a_shader->currentRawTechnique & static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::ProjectedUV)) != 0;
		const bool treeAnim = a_pass->shaderProperty->flags.all(Flag::kTreeAnim);
		const auto& rec = RecordOf(a_pass->geometry, static_cast<RE::BSLightingShaderMaterialBase*>(a_pass->shaderProperty->material));
		if (rec.seasonsProj && !treeAnim)
			extraDescriptor |= uint32_t(State::ExtraFeatureDescriptors::SnowProjectedUnauthored);
		if (rec.seasonsStatic && !treeAnim)
			extraDescriptor |= uint32_t(State::ExtraFeatureDescriptors::SnowProjectedNoAlpha);
		if (settings.MultipassSnowFollowsPaint && !rec.seasonsProj && !treeAnim && rec.mato != MatoClass::kNotSnow &&
			a_pass->shaderProperty->flags.all(Flag::kMultiIndexSnow))
			extraDescriptor |= uint32_t(State::ExtraFeatureDescriptors::SnowMultipassBase);
		if (settings.UnprojectedShapesBare && !passProjected && !treeAnim &&
			(rec.mato == MatoClass::kSnow || rec.mato == MatoClass::kSnowSeasons) &&
			!a_pass->shaderProperty->flags.any(Flag::kLODObjects, Flag::kHDLODObjects) &&
			!NameFactsOf(a_pass->geometry).engineDecal)
			extraDescriptor |= uint32_t(State::ExtraFeatureDescriptors::SnowObjectBarePass);
		if (passProjected && !treeAnim && rec.mato != MatoClass::kNotSnow && !NameFactsOf(a_pass->geometry).engineDecal) {
			extraDescriptor |= uint32_t(State::ExtraFeatureDescriptors::SnowProjectedIsSnow);
			bindSnowSet = true;
		}
	}
	if (settings.LODObjectSnow && !bindSnowSet &&
		a_pass->shaderProperty->flags.any(Flag::kLODObjects, Flag::kHDLODObjects) &&
		!a_pass->shaderProperty->flags.all(Flag::kTreeAnim) &&
		(a_shader->currentRawTechnique & static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::ProjectedUV)) == 0) {
		extraDescriptor |= uint32_t(State::ExtraFeatureDescriptors::SnowLODBakedIsSnow);
		bindSnowSet = true;
	}
	if (settings.SnowTexturedRecolor && settings.ProjSnowMatch && !bindSnowSet &&
		!a_pass->shaderProperty->flags.any(Flag::kLODObjects, Flag::kHDLODObjects, Flag::kTreeAnim) &&
		(a_shader->currentRawTechnique & static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::ProjectedUV)) == 0) {
		const auto& rec = RecordOf(a_pass->geometry, static_cast<RE::BSLightingShaderMaterialBase*>(a_pass->shaderProperty->material));
		if (rec.pathSnowNamed && rec.mato != MatoClass::kNotSnow) {
			extraDescriptor |= uint32_t(State::ExtraFeatureDescriptors::SnowLODBakedIsSnow);
			if (rec.pathGroundSnow)
				extraDescriptor |= uint32_t(State::ExtraFeatureDescriptors::SnowTexturedGround);
			if (PassBlendsOver(a_pass))
				extraDescriptor |= uint32_t(State::ExtraFeatureDescriptors::SnowTexturedOverlay);
			bindSnowSet = true;
		}
	}
	if (bindSnowSet) {
		auto* context = globals::d3d::context;
		ID3D11ShaderResourceView* albedoSRV = shellSnowDiffuseSRV.get();
		ID3D11ShaderResourceView* normalSRV = shellSnowNormalSRV.get();
		ID3D11ShaderResourceView* waterSRV = waterFineTexture ? waterFineTexture->srv.get() : nullptr;
		ID3D11ShaderResourceView* rmaosSRV = shellSnowRmaosSRV.get();
		context->PSSetShaderResources(kHorizonSnowAlbedoSlot, 1, &albedoSRV);
		context->PSSetShaderResources(kHorizonSnowNormalSlot, 1, &normalSRV);
		context->PSSetShaderResources(kWaterWindowSlot, 1, &waterSRV);
		context->PSSetShaderResources(kHorizonSnowRmaosSlot, 1, &rmaosSRV);
	}
}

void SnowDeformation::BSLightingShader_SetupGeometry(RE::BSRenderPass* a_pass)
{
	if (!a_pass || !a_pass->shaderProperty || !a_pass->geometry)
		return;
	if (!settings.EnableSnowDeformation)
		return;
	if (!globals::state->inWorld)
		return;

	auto& rec = RecordOf(a_pass->geometry, static_cast<RE::BSLightingShaderMaterialBase*>(a_pass->shaderProperty->material));
	const auto& passFacts = NameFactsOf(a_pass->geometry);
	using Flag = RE::BSShaderProperty::EShaderPropertyFlag;
	const auto& flags = a_pass->shaderProperty->flags;
	const bool engineDecal = !rec.bloodTex && passFacts.engineDecal &&
	                         !flags.any(Flag::kLODObjects, Flag::kHDLODObjects, Flag::kLODLandscape);
	if (engineDecal)
		return;
	if (rec.bloodTex) {
		const bool poolQuad = NameFactsOf(a_pass->geometry).bloodPool;
		const bool decalMode = globals::game::shadowState->GetRuntimeData().rasterStateDepthBiasMode != 0 ||
		                       flags.any(Flag::kDecal, Flag::kDynamicDecal);
		if (decalMode || poolQuad) {
			return;
		}
	}
	if (flags.all(Flag::kTreeAnim)) {
		return;
	}
	if (a_pass->geometry->GetGeometryRuntimeData().skinInstance != nullptr) {
		return;
	}
	if (rec.mato == MatoClass::kNotSnow) {
		return;
	}
	constexpr float kMergedLODRadius = 4096.0f;

	auto eye = globals::game::frameBufferCached.GetCameraPosAdjust();

	auto& nameFacts = NameFactsOf(a_pass->geometry);
	if (flags.any(Flag::kLODObjects, Flag::kHDLODObjects))
		return;
	const uint32_t depthBiasMode = globals::game::shadowState->GetRuntimeData().rasterStateDepthBiasMode;
	const bool decalDepth = depthBiasMode != 0 || flags.any(Flag::kDecal, Flag::kDynamicDecal);
	const bool snowTexOverlay = settings.SnowTexturedRecolor && rec.pathSnowNamed && PassBlendsOver(a_pass);

	{
		const auto& wb = a_pass->geometry->worldBound;
		const float bx = wb.center.x - eye.x;
		const float by = wb.center.y - eye.y;
		const bool cameraInside = (bx * bx + by * by) < (wb.radius * wb.radius);
		if (cameraInside && wb.radius > kMergedLODRadius) {
			bool referenced = OwningRefOf(a_pass->geometry) != nullptr;
			if (!referenced)
				referenced = GeometryBelongsToLoadedReference(a_pass->geometry);
			const bool iceSheet = rec.pathNatural;
			if (!referenced && !iceSheet) {
				SampleLODDecision(a_pass->geometry, wb.radius, true, false);
				return;
			}
		}
		if (flags.any(Flag::kLODLandscape))
			SampleLODDecision(a_pass->geometry, wb.radius, false, cameraInside);
	}
	if (!flags.all(Flag::kProjectedUV)) {
		auto* material = static_cast<RE::BSLightingShaderMaterialBase*>(a_pass->shaderProperty->material);
		if (!material)
			return;

		const auto& wbCenter = a_pass->geometry->worldBound.center;
		bool mountainFeature = rec.pathMountain || nameFacts.mountainCliff;
		if (mountainFeature) {
			const uint32_t depthVersion = snowDepthVersion.load(std::memory_order_acquire);
			if (rec.depthVersion != depthVersion || rec.depthX != wbCenter.x || rec.depthY != wbCenter.y) {
				rec.depth = GetNominalSnowDepthAt(wbCenter.x, wbCenter.y, 0.0f);
				rec.depthVersion = depthVersion;
				rec.depthX = wbCenter.x;
				rec.depthY = wbCenter.y;
			}
			mountainFeature = rec.depth > 0.5f;
		}
		const bool naturalFeature = rec.pathNatural || rec.iceName || mountainFeature;
		const bool snowNamedCapture = settings.SnowTexturedRecolor && rec.pathSnowNamed && !snowTexOverlay;
		if (!(rec.pathBase || naturalFeature || snowNamedCapture || rec.roadLayer)) {
			return;
		}
	}

	if (rec.shard) {
		return;
	}
	auto* alphaProperty = a_pass->geometry->GetGeometryRuntimeData().alphaProperty.get();
	const bool alphaTested = alphaProperty && alphaProperty->GetAlphaTesting();
	if (!decalDepth && !rec.pathArchitecture && alphaTested && !rec.roadLayer) {
		return;
	}

	const bool fadeExempt = rec.pathNatural || rec.iceName;
	const bool fullCoat = nameFacts.drift;
	const bool bridge = nameFacts.bridge || rec.roadTex == 2;
	bool road = !bridge && nameFacts.road && (rec.landscapeTex || rec.pavingTex);
	const char* roadVia = road ? "name" : (bridge ? "bridge excluded" : "no");
	if (!road && !bridge && rec.roadLayer) {
		road = true;
		roadVia = "road model layer";
	}
	if (!road && !bridge && rec.roadTex == 1) {
		road = true;
		roadVia = "texture";
	}
	if (road && GroundShellsSuspended()) {
		road = false;
		roadVia = "city";
	}
	if (rec.roadLayer && !road) {
		return;
	}

	const auto& translate = a_pass->geometry->world.translate;
	const auto& rangeBound = a_pass->geometry->worldBound;
	float dx = rangeBound.center.x - eye.x;
	float dy = rangeBound.center.y - eye.y;
	const float captureRange = kMaxRangeSkinsM * kUnitsPerMeter + std::max(rangeBound.radius, 0.0f);
	if (!fadeExempt && !fullCoat && !road && dx * dx + dy * dy > captureRange * captureRange) {
		return;
	}

	if (!fadeExempt && !fullCoat && !flags.any(Flag::kProjectedUV) && !GroundNearHasSnow(translate.x, translate.y)) {
		return;
	}

	if (!capturedStaticsSet.insert(a_pass->geometry).second)
		return;

	if (!nameFacts.capturedLogged) {
		nameFacts.capturedLogged = true;
		const char* diffusePath = "";
		if (auto path = DiffusePathOf(static_cast<RE::BSLightingShaderMaterialBase*>(a_pass->shaderProperty->material)))
			diffusePath = path;
		logger::info("[SNOW DEFORMATION] captured '{}' road={} (via {}) tex='{}'",
			a_pass->geometry->name.c_str(), road ? "yes" : "no", roadVia, diffusePath);
	}

	capturedStatics.push_back({ RE::NiPointer<RE::BSGeometry>(a_pass->geometry), a_pass->geometry->world, road || flags.any(Flag::kProjectedUV) || !fadeExempt });
}

struct SD_BSLightingShader_SetupGeometry
{
	static void thunk(RE::BSLightingShader* shader, RE::BSRenderPass* a_pass, uint32_t a_flags)
	{
		auto& snowDeformation = globals::features::snowDeformation;
		if (snowDeformation.loaded) {
			snowDeformation.SetProjectedSnowBit(shader, a_pass);
		}

		func(shader, a_pass, a_flags);

		if (snowDeformation.loaded) {
			snowDeformation.BSLightingShader_SetupGeometry(a_pass);
			snowDeformation.LandscapeSnowSetup(a_pass, true);
		}
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

void SnowDeformation::InstallStaticsCaptureHook()
{
	logger::info("[SNOW DEFORMATION] Hooking BSLightingShader::SetupGeometry");
	stl::write_vfunc<0x6, SD_BSLightingShader_SetupGeometry>(RE::VTABLE_BSLightingShader[0]);
}

ID3D11DeviceChild* SD_CompileShader(const wchar_t* a_path, const char* a_target, std::initializer_list<const char*> a_defines, winrt::com_ptr<ID3DBlob>* a_blob)
{
	std::vector<std::pair<const char*, const char*>> defines;
	for (const auto* define : a_defines)
		if (define)
			defines.emplace_back(define, "");
	if (a_blob)
		*a_blob = nullptr;
	return Util::CompileShader(a_path, defines, a_target, "main", a_blob ? a_blob->put() : nullptr);
}

bool SnowDeformation::EnsureHeightRasterShaders()
{
	if (heightVS && heightPS && heightScrollRasterCS && heightCombineCS)
		return true;
	if (staticsShadersFailed)
		return false;

	constexpr auto heightPath = L"Data\\Shaders\\SnowDeformation\\SnowHeightCapture.hlsl";
	if (!heightVS) {
		heightVS.attach(static_cast<ID3D11VertexShader*>(SD_CompileShader(heightPath, "vs_5_0", {}, &heightVSBlob)));
		if (heightVS)
			Util::SetResourceName(heightVS.get(), "SnowDeformation::HeightCaptureVS");
	}
	if (!heightPS) {
		heightPS.attach(static_cast<ID3D11PixelShader*>(SD_CompileShader(heightPath, "ps_5_0")));
		if (heightPS)
			Util::SetResourceName(heightPS.get(), "SnowDeformation::HeightCapturePS");
	}
	constexpr auto processPath = L"Data\\Shaders\\SnowDeformation\\HeightMapProcessCS.hlsl";
	if (!heightScrollRasterCS)
		heightScrollRasterCS.attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(processPath, {}, "cs_5_0", "ScrollCS")));
	if (!heightCombineCS)
		heightCombineCS.attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(processPath, {}, "cs_5_0", "CombineCS")));
	if (!heightWaterCoverCS)
		heightWaterCoverCS.attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(processPath, {}, "cs_5_0", "WaterCoverCS")));

	if (!heightVS || !heightPS || !heightScrollRasterCS || !heightCombineCS) {
		staticsShadersFailed = true;
		logger::warn("[SNOW DEFORMATION] Object height raster disabled (shader compilation failed)");
		return false;
	}
	return true;
}

void SnowDeformation::CreateCaptureLayouts()
{
	if (SnowShadersPending(2) || capturedStatics.empty() || globals::state->isMapMenuOpen || !EnsureHeightRasterShaders())
		return;
	for (const auto& cap : capturedStatics) {
		RigidDraw draw;
		if (cap.makesLayout && cap.geometry && RigidDrawOf(cap.geometry.get(), draw))
			StaticsInputLayoutFor(draw.descKey, draw.desc);
	}
}

void SnowDeformation::CreateHeightFieldResources()
{
	auto makeHeightTexture = [](const char* a_name) {
		return SD_MakeTexture2D(kHeightMapDim, kHeightMapDim, DXGI_FORMAT_R32_FLOAT, a_name, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS);
	};
	heightTopRaw[0] = makeHeightTexture("SnowDeformation::HeightTopRaw0");
	heightTopRaw[1] = makeHeightTexture("SnowDeformation::HeightTopRaw1");
	heightBottomRaw[0] = makeHeightTexture("SnowDeformation::HeightBottomRaw0");
	heightBottomRaw[1] = makeHeightTexture("SnowDeformation::HeightBottomRaw1");
	heightRaster = SD_MakeTexture2D(kHeightMapDim, kHeightMapDim, DXGI_FORMAT_R32G32_FLOAT, "SnowDeformation::HeightRaster", D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
	heightBottomFiltered = SD_MakeTexture2D(kHeightMapDim, kHeightMapDim, DXGI_FORMAT_R16G16_FLOAT, "SnowDeformation::HeightShelterMask", D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS);
	static_assert(kHeightMapDim % 8 == 0, "HeightMapProcessCS clamps tiles to dims / 8");
	heightTileEmpty = SD_MakeTexture2D(kHeightMapDim / 8, kHeightMapDim / 8, DXGI_FORMAT_R8_UINT, "SnowDeformation::HeightTileEmpty");
}

static constexpr UINT kHeightVerifySamples = 8;
static constexpr UINT kHeightVerifySlotWords = 4 + kHeightVerifySamples * 8;
static_assert(kHeightVerifySlotWords * sizeof(uint32_t) % D3D11_RAW_UAV_SRV_BYTE_ALIGNMENT == 0);
static constexpr UINT kHeightVerifySlots = 7;
static constexpr UINT kHeightVerifyRasterSlots = 1;
static constexpr uint32_t kHeightVerifyTargetLevel[kHeightVerifySlots] = { 0, 0, 1, 2, 3, 4, 5 };

bool SnowDeformation::EnsureHeightRasterVerify()
{
	if (heightRasterVerifyStaging[2] && heightRasterCompareCS)
		return true;
	auto device = globals::d3d::device;
	if (!heightRasterCompareCS)
		heightRasterCompareCS.attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\SnowDeformation\\HeightMapProcessCS.hlsl", { { "RASTER_COMPARE", "" } }, "cs_5_0", "RasterCompareCS")));
	if (!heightRasterCompareCS || !heightRaster)
		return false;
	Texture2D* const twins[kHeightVerifyRasterSlots] = { heightRaster };
	static constexpr const char* kNames[kHeightVerifyRasterSlots] = { "SnowDeformation::HeightVerifyRaster" };
	for (UINT i = 0; i < kHeightVerifyRasterSlots; i++) {
		if (heightRasterVerifyTex[i])
			continue;
		D3D11_TEXTURE2D_DESC desc = twins[i]->desc;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = { .Format = desc.Format, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D, .Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 } };
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = { .Format = desc.Format, .ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D, .Texture2D = { .MipSlice = 0 } };
		heightRasterVerifyTex[i] = new Texture2D(desc, kNames[i]);
		heightRasterVerifyTex[i]->CreateSRV(srvDesc);
		heightRasterVerifyTex[i]->CreateRTV(rtvDesc);
	}
	if (!heightRasterVerifyCount) {
		D3D11_BUFFER_DESC countDesc{};
		countDesc.ByteWidth = kHeightVerifySlots * kHeightVerifySlotWords * sizeof(uint32_t);
		countDesc.Usage = D3D11_USAGE_DEFAULT;
		countDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		countDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
		winrt::com_ptr<ID3D11Buffer> count;
		winrt::com_ptr<ID3D11UnorderedAccessView> allUAV, slotUAVs[kHeightVerifySlots];
		if (FAILED(device->CreateBuffer(&countDesc, nullptr, count.put())))
			return false;
		Util::SetResourceName(count.get(), "SnowDeformation::HeightVerifyCount");
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
		uavDesc.Buffer.NumElements = kHeightVerifySlots * kHeightVerifySlotWords;
		if (FAILED(device->CreateUnorderedAccessView(count.get(), &uavDesc, allUAV.put())))
			return false;
		uavDesc.Buffer.NumElements = kHeightVerifySlotWords;
		for (UINT slot = 0; slot < kHeightVerifySlots; slot++) {
			uavDesc.Buffer.FirstElement = slot * kHeightVerifySlotWords;
			if (FAILED(device->CreateUnorderedAccessView(count.get(), &uavDesc, slotUAVs[slot].put())))
				return false;
		}
		Util::SetResourceName(count.get(), "SnowDeformation::HeightVerifyCount");
		heightRasterVerifyCount = count;
		heightRasterVerifyAllUAV = allUAV;
		for (UINT slot = 0; slot < kHeightVerifySlots; slot++)
			heightRasterVerifySlotUAV[slot] = slotUAVs[slot];
	}
	for (int i = 0; i < 3; i++) {
		if (heightRasterVerifyStaging[i])
			continue;
		D3D11_BUFFER_DESC stagingDesc{};
		stagingDesc.ByteWidth = kHeightVerifySlots * kHeightVerifySlotWords * sizeof(uint32_t);
		stagingDesc.Usage = D3D11_USAGE_STAGING;
		stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		if (FAILED(device->CreateBuffer(&stagingDesc, nullptr, heightRasterVerifyStaging[i].put())))
			return false;
		Util::SetResourceName(heightRasterVerifyStaging[i].get(), "SnowDeformation::HeightVerifyStaging");
	}
	return true;
}

bool SnowDeformation::EnsureCombineVerify()
{
	if (combineVerifyTex)
		return true;
	if (!heightBottomFiltered)
		return false;
	D3D11_TEXTURE2D_DESC desc = heightBottomFiltered->desc;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = { .Format = desc.Format, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D, .Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 } };
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = { .Format = desc.Format, .ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D, .Texture2D = { .MipSlice = 0 } };
	combineVerifyTex = new Texture2D(desc, "SnowDeformation::ShelterMaskVerify");
	combineVerifyTex->CreateSRV(srvDesc);
	combineVerifyTex->CreateUAV(uavDesc);
	return true;
}

bool SnowDeformation::EnsureStaticsRecordCB(uint32_t a_count)
{
	auto* device = globals::d3d::device;
	if (!staticsRecordChecked) {
		staticsRecordChecked = true;
		static_assert(sizeof(StaticsCB) <= kStaticsRecordStride);
		auto* context = globals::d3d::context;
		if (!device || !context)
			return false;
		D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
		if (FAILED(device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options))) || !options.ConstantBufferOffsetting) {
			logger::info("[SNOW DEFORMATION] statics record buffer: no constant-buffer offsetting; per-draw updates stay");
			return false;
		}
		if (FAILED(context->QueryInterface(IID_PPV_ARGS(staticsContext1.put())))) {
			logger::info("[SNOW DEFORMATION] statics record buffer: no ID3D11DeviceContext1; per-draw updates stay");
			return false;
		}
		logger::info("[SNOW DEFORMATION] statics record buffer: ID3D11DeviceContext1 {:X} (base context {:X}, {})",
			reinterpret_cast<uintptr_t>(staticsContext1.get()), reinterpret_cast<uintptr_t>(context),
			static_cast<void*>(staticsContext1.get()) == static_cast<void*>(context) ? "same object" : "different object");
	}
	if (!staticsContext1 || !device)
		return false;
	if (a_count <= staticsRecordCapacity)
		return true;
	uint32_t capacity = std::max(staticsRecordCapacity, kStaticsRecordStart);
	while (capacity < a_count)
		capacity *= 2;
	if (staticsRecordRefused && capacity >= staticsRecordRefused)
		return false;
	winrt::com_ptr<ID3D11Buffer> buffers[2];
	for (int i = 0; i < 2; i++) {
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = capacity * kStaticsRecordStride;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(device->CreateBuffer(&desc, nullptr, buffers[i].put()))) {
			staticsRecordRefused = capacity;
			if (!staticsRecordCapacity) {
				logger::info("[SNOW DEFORMATION] statics record buffer: creation failed; per-draw updates stay");
				staticsContext1 = nullptr;
			} else {
				logger::info("[SNOW DEFORMATION] statics record buffer: {} records refused; larger frames update per draw", capacity);
			}
			return false;
		}
		Util::SetResourceName(buffers[i].get(), i ? "SnowDeformation::StaticsRecords1" : "SnowDeformation::StaticsRecords0");
	}
	staticsRecordCB[0] = buffers[0];
	staticsRecordCB[1] = buffers[1];
	staticsRecordCapacity = capacity;
	logger::info("[SNOW DEFORMATION] statics record buffer: {} records x {} bytes, two copies", capacity, kStaticsRecordStride);
	return true;
}

bool SnowDeformation::UploadStaticsRecords(const StaticsCB* a_records, uint32_t a_count, const uint8_t* a_needed)
{
	if (a_count == 0)
		return false;
	if (a_count > kStaticsRecordMax || !EnsureStaticsRecordCB(a_count))
		return false;
	auto* context = globals::d3d::context;
	for (int i = 0; i < 2; i++) {
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(context->Map(staticsRecordCB[i].get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			return false;
		auto* dst = static_cast<uint8_t*>(mapped.pData);
		for (uint32_t r = 0; r < a_count; r++)
			if (!a_needed || a_needed[r])
				memcpy(dst + size_t(r) * kStaticsRecordStride, &a_records[r], sizeof(StaticsCB));
		context->Unmap(staticsRecordCB[i].get(), 0);
	}
	return true;
}

void SnowDeformation::BindStaticsRecord(uint32_t a_index, bool a_pixelStage, bool a_tessStages, uint32_t& a_parity)
{
	ID3D11Buffer* buffer = staticsRecordCB[a_parity & 1].get();
	a_parity++;
	const UINT first = a_index * (kStaticsRecordStride / 16);
	const UINT count = kStaticsRecordStride / 16;
	staticsContext1->VSSetConstantBuffers1(1, 1, &buffer, &first, &count);
	if (a_pixelStage)
		staticsContext1->PSSetConstantBuffers1(1, 1, &buffer, &first, &count);
	if (a_tessStages) {
		staticsContext1->HSSetConstantBuffers1(1, 1, &buffer, &first, &count);
		staticsContext1->DSSetConstantBuffers1(1, 1, &buffer, &first, &count);
	}
}

void SnowDeformation::SkipObjectHeightMap()
{
	heightMapValid = false;
	globals::profiler->MarkPassSkipped("SnowDeformation::Objects - Height Maps");
	globals::profiler->MarkPassSkipped("SnowDeformation::Ground - Field Bakes");
}

bool SnowDeformation::HeightMapIdleReady() const
{
	return !heightGhostsLive && heightMapValid && objectRasterHalfExtentBuilt == ObjectRasterHalfExtent();
}

void SnowDeformation::IdleObjectHeightMap()
{
	auto eye = globals::game::frameBufferCached.GetCameraPosAdjust();
	const float halfExtent = ObjectRasterHalfExtent();
	const float texel = halfExtent * 2.0f / kHeightMapDim;
	const float2 newCenter = {
		std::floor(eye.x / texel) * texel,
		std::floor(eye.y / texel) * texel
	};
	const int scrollX = (int)std::lround((newCenter.x - heightWindowCenter.x) / texel);
	const int scrollY = (int)std::lround((newCenter.y - heightWindowCenter.y) / texel);
	heightWindowCenter = newCenter;
	const bool windowJumped = std::abs(scrollX) > kGatherJumpTexels || std::abs(scrollY) > kGatherJumpTexels;
	if (windowJumped || (doorRefreshCounter++ % 60) == 0)
		StartExclusionGather();
	StepExclusionGather();
	globals::profiler->MarkPassSkipped("SnowDeformation::Objects - Height Maps");
	globals::profiler->MarkPassSkipped("SnowDeformation::Ground - Field Bakes");
}

void SnowDeformation::RenderObjectHeightMap()
{
	auto context = globals::d3d::context;

	auto eye = globals::game::frameBufferCached.GetCameraPosAdjust();
	const float halfExtent = ObjectRasterHalfExtent();
	const float texel = halfExtent * 2.0f / kHeightMapDim;
	float2 newCenter = {
		std::floor(eye.x / texel) * texel,
		std::floor(eye.y / texel) * texel
	};

	HeightProcessCB processData{};
	processData.ScrollDelta = {
		(int)std::lround((newCenter.x - heightWindowCenter.x) / texel),
		-(int)std::lround((newCenter.y - heightWindowCenter.y) / texel)
	};
	if (objectRasterHalfExtentBuilt != halfExtent) {
		objectRasterHalfExtentBuilt = halfExtent;
		heightMapValid = false;
	}
	processData.ClearAll = heightMapValid ? 0u : 1u;
	processData.HeightWindowCenter = newCenter;
	processData.HeightHalfExtent = halfExtent;
	constexpr float shellCellSize = kShellVertexSpacing * kShellTexelsPerCell;
	processData.TerrainWindowOrigin = { shellWindowCellX * shellCellSize, shellWindowCellY * shellCellSize };
	processData.TerrainTexelSize = kShellVertexSpacing;
	processData.TerrainDim = kShellWindowDim;
	processData.GhostDecay = 0.5f;
	processData.ShelterMaxHeight = std::clamp(settings.ShelterMaxHeight, 100.0f, 2000.0f);
	processData.ShelterBandHeight = std::max(kShelterBandHeight, processData.ShelterMaxHeight);
	processData.ShelterConeTan = std::tan(std::clamp(settings.ShelterDriftAngle, 5.0f, 70.0f) * (DirectX::XM_PI / 180.0f));
	processData.DeadGhostTiles = 1u;
	heightProcessCB->Update(processData);
	heightWindowCenter = newCenter;
	heightMapValid = true;
	if (processData.ClearAll || heightGhostBox.x > newCenter.x + halfExtent || heightGhostBox.z < newCenter.x - halfExtent ||
		heightGhostBox.y > newCenter.y + halfExtent || heightGhostBox.w < newCenter.y - halfExtent)
		heightGhostsLive = false;

	const bool windowJumped = processData.ClearAll != 0 ||
	                          std::abs(processData.ScrollDelta.x) > kGatherJumpTexels || std::abs(processData.ScrollDelta.y) > kGatherJumpTexels;
	if (windowJumped || (doorRefreshCounter++ % 60) == 0)
		StartExclusionGather();
	StepExclusionGather();

	{
		ExclusionsCB exclusionData{};
		uint32_t exclusionCount = 0;
		for (const auto& [posRadius, dirExtType] : staticExclusions) {
			if (exclusionCount >= kMaxExclusions)
				break;
			exclusionData.PosRadius[exclusionCount] = posRadius;
			exclusionData.DirExtType[exclusionCount] = dirExtType;
			exclusionCount++;
		}
		exclusionData.ExclusionCount = exclusionCount;
		{
			uint64_t h = 1469598103934665603ull;
			auto mix = [&](const void* a_p, size_t a_n) {
				const auto* b = static_cast<const uint8_t*>(a_p);
				for (size_t i = 0; i < a_n; i++) {
					h ^= b[i];
					h *= 1099511628211ull;
				}
			};
			mix(exclusionData.PosRadius, sizeof(float4) * exclusionCount);
			mix(exclusionData.DirExtType, sizeof(float4) * exclusionCount);
			mix(&exclusionCount, sizeof(exclusionCount));
			if (h != exclusionListHash || !doorsCBUploaded) {
				doorsCB->Update(exclusionData);
				exclusionListBuffer->Update(&exclusionData, sizeof(float4) * kMaxExclusions * 2);
				doorsCBUploaded = true;
			}
			exclusionListHash = h;
		}
		RenderExclusionField(exclusionData);
	}

	auto outsideWindow = [&](const RE::BSGeometry* a_geometry, float a_halfExtent) {
		const auto& wb = a_geometry->worldBound;
		return std::abs(wb.center.x - heightWindowCenter.x) > a_halfExtent + wb.radius ||
		       std::abs(wb.center.y - heightWindowCenter.y) > a_halfExtent + wb.radius;
	};
	const bool emptyWindow = !heightGhostsLive && std::ranges::none_of(capturedStatics, [&](const auto& a_cap) {
		return a_cap.geometry && !outsideWindow(a_cap.geometry.get(), halfExtent);
	});
	const bool keepEmpty = emptyWindow && heightMapsEmpty && !processData.ClearAll && !heightRasterCacheOff;

	uint previous = heightCurrent;
	if (!keepEmpty)
		heightCurrent ^= 1;

	ID3D11Buffer* processCB = heightProcessCB->CB();
	ID3D11ShaderResourceView* nullCsSRVs[2] = { nullptr, nullptr };
	ID3D11UnorderedAccessView* nullCsUAVs[2] = { nullptr, nullptr };

	{
		auto& fb = globals::game::frameBufferCached;
		HeightGhostCB ghostData{};
		ghostData.GhostViewProj = fb.GetCameraViewProj();
		ghostData.GhostCameraPosAdjust = fb.GetCameraPosAdjust();
		heightGhostCB->Update(ghostData);
	}
	ID3D11Buffer* ghostCB = heightGhostCB->CB();
	auto mergeGhost = [&](Texture2D* a_prevTop, Texture2D* a_curTop, Texture2D* a_prevBottom, Texture2D* a_curBottom, Texture2D* a_tiles, Texture2D* a_raster) {
		if (!a_prevTop || !a_curTop || !a_prevBottom || !a_curBottom || !a_raster)
			return;
		ID3D11ShaderResourceView* mergeSRVs[2] = { a_prevTop->srv.get(), a_prevBottom->srv.get() };
		ID3D11UnorderedAccessView* mergeUAVs[2] = { a_curTop->uav.get(), a_curBottom->uav.get() };
		ID3D11UnorderedAccessView* tileUAV = a_tiles ? a_tiles->uav.get() : nullptr;
		context->CSSetConstantBuffers(0, 1, &processCB);
		context->CSSetConstantBuffers(2, 1, &ghostCB);
		context->CSSetShaderResources(0, 2, mergeSRVs);
		ID3D11ShaderResourceView* rasterSRV = a_raster->srv.get();
		context->CSSetShaderResources(6, 1, &rasterSRV);
		context->CSSetUnorderedAccessViews(0, 2, mergeUAVs, nullptr);
		context->CSSetUnorderedAccessViews(3, 1, &tileUAV, nullptr);
		context->CSSetShader(heightScrollRasterCS.get(), nullptr, 0);
		context->Dispatch((kHeightMapDim + 7) / 8, (kHeightMapDim + 7) / 8, 1);
		context->CSSetShaderResources(6, 1, nullCsSRVs);
		context->CSSetShaderResources(0, 2, nullCsSRVs);
		context->CSSetUnorderedAccessViews(0, 2, nullCsUAVs, nullptr);
		context->CSSetUnorderedAccessViews(3, 1, nullCsUAVs, nullptr);
		context->CSSetShader(nullptr, nullptr, 0);
		ID3D11Buffer* nullMergeCB = nullptr;
		context->CSSetConstantBuffers(0, 1, &nullMergeCB);
		context->CSSetConstantBuffers(2, 1, &nullMergeCB);
	};

	const float rasterClear[4] = { kHeightMapEmptyTop, -kHeightMapEmptyBottom, 0.0f, 0.0f };
	context->OMSetBlendState(heightRasterBlendState.get(), nullptr, 0xFFFFFFFF);

	D3D11_VIEWPORT heightViewport{ 0.0f, 0.0f, float(kHeightMapDim), float(kHeightMapDim), 0.0f, 1.0f };
	context->RSSetViewports(1, &heightViewport);

	context->VSSetShader(heightVS.get(), nullptr, 0);
	context->PSSetShader(heightPS.get(), nullptr, 0);
	ID3D11Buffer* cb1 = staticsCB->CB();
	context->VSSetConstantBuffers(1, 1, &cb1);
	context->PSSetConstantBuffers(1, 1, &cb1);
	ID3D11Buffer* captureCB0 = heightProcessCB->CB();
	context->PSSetConstantBuffers(0, 1, &captureCB0);
	ID3D11ShaderResourceView* captureTerrainSRV = shellTerrainTexture->srv.get();
	context->PSSetShaderResources(2, 1, &captureTerrainSRV);

	const uint32_t captureCount = (uint32_t)capturedStatics.size();
	static std::vector<StaticsCB> sCaptureRecords;
	sCaptureRecords.assign(captureCount, StaticsCB{});
	auto& captureRecords = sCaptureRecords;
	std::vector<uint8_t> recordNeeded(captureRecords.size(), 0);
	for (uint32_t ci = 0; ci < captureCount; ci++) {
		auto& cap = capturedStatics[ci];
		auto* geometry = cap.geometry.get();
		if (!geometry)
			continue;
		RigidDraw draw;
		if (!RigidDrawOf(geometry, draw))
			continue;
		auto layoutIt = staticsILCache.find(draw.descKey);
		if (layoutIt == staticsILCache.end() || !layoutIt->second)
			continue;
		if (uint32_t(draw.descKey & 0xF) == 0)
			continue;

		recordNeeded[ci] = uint8_t(!outsideWindow(geometry, halfExtent));

		StaticsCB& scb = captureRecords[ci];
		SetWorldRows(cap.world, scb);
		scb.HeightWindowCenter = heightWindowCenter;
		scb.HeightHalfExtent = halfExtent;
	}
	const bool captureRecordsLive = captureCount > 0 && UploadStaticsRecords(captureRecords.data(), uint32_t(captureRecords.size()), recordNeeded.data());
	uint32_t captureParity = 0;

	struct HeightRasterDraw
	{
		ID3D11InputLayout* layout;
		ID3D11Buffer* vb;
		ID3D11Buffer* ib;
		UINT stride;
		uint32_t indexCount;
		uint32_t capture;
		uint32_t record;
	};
	auto collectDraw = [&](uint32_t a_ci, uint32_t a_record, std::vector<HeightRasterDraw>& a_out) {
		auto* geometry = capturedStatics[a_ci].geometry.get();
		RigidDraw draw;
		if (!geometry || !RigidDrawOf(geometry, draw))
			return;
		auto layoutIt = staticsILCache.find(draw.descKey);
		if (layoutIt == staticsILCache.end() || !layoutIt->second)
			return;
		const UINT stride = uint32_t(draw.descKey & 0xF) * 4;
		if (stride == 0)
			return;
		a_out.push_back({ layoutIt->second.get(), draw.vb, draw.ib, stride, draw.indexCount, a_ci, a_record });
	};
	std::vector<uint8_t> inheritedState;
	{
		auto put = [&](const void* a_p, size_t a_n) {
			const auto* bytes = static_cast<const uint8_t*>(a_p);
			inheritedState.insert(inheritedState.end(), bytes, bytes + a_n);
		};
		winrt::com_ptr<ID3D11RasterizerState> rasterState;
		context->RSGetState(rasterState.put());
		D3D11_RASTERIZER_DESC rasterDesc{};
		const uint32_t hasRasterState = rasterState ? 1u : 0u;
		if (rasterState)
			rasterState->GetDesc(&rasterDesc);
		put(&hasRasterState, sizeof(hasRasterState));
		put(&rasterDesc, sizeof(rasterDesc));
		if (rasterState && rasterDesc.ScissorEnable) {
			D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
			UINT scissorCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
			context->RSGetScissorRects(&scissorCount, scissors);
			put(&scissorCount, sizeof(scissorCount));
			put(scissors, sizeof(D3D11_RECT) * scissorCount);
		}
		D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		context->IAGetPrimitiveTopology(&topology);
		put(&topology, sizeof(topology));
		winrt::com_ptr<ID3D11HullShader> hs;
		winrt::com_ptr<ID3D11DomainShader> ds;
		winrt::com_ptr<ID3D11GeometryShader> gs;
		context->HSGetShader(hs.put(), nullptr, nullptr);
		context->DSGetShader(ds.put(), nullptr, nullptr);
		context->GSGetShader(gs.put(), nullptr, nullptr);
		const uint64_t stages[3] = { reinterpret_cast<uint64_t>(hs.get()), reinterpret_cast<uint64_t>(ds.get()), reinterpret_cast<uint64_t>(gs.get()) };
		put(stages, sizeof(stages));
	}
	auto rasterKey = [&](const std::vector<HeightRasterDraw>& a_draws, std::initializer_list<uint64_t> a_extras) {
		std::vector<uint8_t> key;
		key.reserve(inheritedState.size() + a_extras.size() * 8 + a_draws.size() * (56 + sizeof(StaticsCB)));
		auto put = [&](const void* a_p, size_t a_n) {
			const auto* bytes = static_cast<const uint8_t*>(a_p);
			key.insert(key.end(), bytes, bytes + a_n);
		};
		put(inheritedState.data(), inheritedState.size());
		for (uint64_t extra : a_extras)
			put(&extra, sizeof(extra));
		for (const auto& d : a_draws) {
			const auto& cap = capturedStatics[d.capture];
			const uint64_t drawBinds[4] = { reinterpret_cast<uint64_t>(d.layout), reinterpret_cast<uint64_t>(d.vb), reinterpret_cast<uint64_t>(d.ib),
				reinterpret_cast<uint64_t>(cap.geometry.get()) };
			const uint32_t counts[2] = { d.stride, d.indexCount };
			put(drawBinds, sizeof(drawBinds));
			put(counts, sizeof(counts));
			put(&captureRecords[d.record], sizeof(StaticsCB));
		}
		return key;
	};
	auto levelKept = [&](uint32_t a_level, std::vector<uint8_t> a_key) {
		const bool kept = !heightRasterCacheOff && a_key == heightRasterKey[a_level];
		if (kept) {
			heightRasterKeptBits |= 1u << a_level;
			heightRasterKept[a_level]++;
		} else {
			heightRasterKey[a_level] = std::move(a_key);
		}
		return kept;
	};
	auto drawList = [&](const std::vector<HeightRasterDraw>& a_draws) {
		SD_DrawBinds binds;
		for (const auto& d : a_draws) {
			binds.Layout(context, d.layout);
			binds.Vertex(context, d.vb, d.stride);
			binds.Index(context, d.ib, DXGI_FORMAT_R16_UINT);
			if (captureRecordsLive)
				BindStaticsRecord(d.record, false, false, captureParity);
			else
				staticsCB->Update(captureRecords[d.record]);
			context->DrawIndexed(d.indexCount, 0, 0);
		}
	};
	heightRasterKeptBits = 0;
	heightRasterRuns++;
	const bool verifyOn = heightRasterVerify && EnsureHeightRasterVerify();
	const bool combineVerifyOn = combineVerify && EnsureHeightRasterVerify() && EnsureCombineVerify();
	uint32_t verifiedLevels = 0;
	if (verifyOn || combineVerifyOn) {
		const UINT zero4[4] = {};
		context->ClearUnorderedAccessViewUint(heightRasterVerifyAllUAV.get(), zero4);
	}
	ID3D11Buffer* compareCB = heightProcessCB->CB();
	auto compareKept = [&](Texture2D* a_kept, Texture2D* a_fresh, uint32_t a_slot) {
		ID3D11ShaderResourceView* compareSRVs[2] = { a_kept->srv.get(), a_fresh->srv.get() };
		ID3D11ShaderResourceView* nullCompareSRVs[2] = {};
		ID3D11UnorderedAccessView* countUAV = heightRasterVerifySlotUAV[a_slot].get();
		ID3D11Buffer* nullCompareCB = nullptr;
		context->CSSetConstantBuffers(0, 1, &compareCB);
		context->CSSetShaderResources(8, 2, compareSRVs);
		context->CSSetUnorderedAccessViews(4, 1, &countUAV, nullptr);
		context->CSSetShader(heightRasterCompareCS.get(), nullptr, 0);
		context->Dispatch((a_kept->desc.Width + 7) / 8, (a_kept->desc.Height + 7) / 8, 1);
		context->CSSetShaderResources(8, 2, nullCompareSRVs);
		context->CSSetUnorderedAccessViews(4, 1, nullCsUAVs, nullptr);
		context->CSSetConstantBuffers(0, 1, &nullCompareCB);
		context->CSSetShader(nullptr, nullptr, 0);
	};

	std::vector<HeightRasterDraw> layer1Draws;
	for (uint32_t ci = 0; ci < captureCount; ci++) {
		const auto& cap = capturedStatics[ci];
		if (!cap.geometry || outsideWindow(cap.geometry.get(), halfExtent))
			continue;
		const size_t drawsBefore = layer1Draws.size();
		collectDraw(ci, ci, layer1Draws);
		if (layer1Draws.size() != drawsBefore) {
			const auto& wb = cap.geometry->worldBound;
			const float reach = wb.radius + 4.0f * texel;
			const float4 drawBox = { wb.center.x - reach, wb.center.y - reach, wb.center.x + reach, wb.center.y + reach };
			heightGhostBox = heightGhostsLive ? float4{ std::min(heightGhostBox.x, drawBox.x), std::min(heightGhostBox.y, drawBox.y),
													std::max(heightGhostBox.z, drawBox.z), std::max(heightGhostBox.w, drawBox.w) } :
			                                    drawBox;
			heightGhostsLive = true;
		}
	}
	auto drawLayer1 = [&](ID3D11RenderTargetView* a_raster) {
		context->ClearRenderTargetView(a_raster, rasterClear);
		context->OMSetRenderTargets(1, &a_raster, nullptr);
		context->PSSetShader(heightPS.get(), nullptr, 0);
		drawList(layer1Draws);
	};
	const bool layer1Kept = levelKept(0, rasterKey(layer1Draws, { reinterpret_cast<uint64_t>(heightVS.get()), reinterpret_cast<uint64_t>(heightPS.get()),
															 reinterpret_cast<uint64_t>(captureTerrainSRV), shellTerrainVersion,
															 std::bit_cast<uint32_t>(processData.TerrainWindowOrigin.x), std::bit_cast<uint32_t>(processData.TerrainWindowOrigin.y),
															 std::bit_cast<uint32_t>(processData.TerrainTexelSize), processData.TerrainDim }));
	ID3D11RenderTargetView* nullRTVs[3] = { nullptr, nullptr, nullptr };
	globals::profiler->BeginPass(SnowDevSwitches::HeightMapRow("SnowDeformation::Objects - Height Maps Layer 1"));
	if (!layer1Kept)
		drawLayer1(heightRaster->rtv.get());
	const bool verifyLayer1 = verifyOn && (layer1Kept || heightRasterVerifyMiss);
	if (verifyLayer1)
		drawLayer1(heightRasterVerifyTex[0]->rtv.get());
	globals::profiler->EndPass();

	context->OMSetRenderTargets(3, nullRTVs, nullptr);
	context->OMSetBlendState(heightMaxBlendState.get(), nullptr, 0xFFFFFFFF);
	context->PSSetShader(heightPS.get(), nullptr, 0);
	if (verifyLayer1) {
		compareKept(heightRaster, heightRasterVerifyTex[0], 0);
		verifiedLevels |= 1u;
	}

	globals::profiler->BeginPass(SnowDevSwitches::HeightMapRow("SnowDeformation::Objects - Height Maps Ghost Merge"));
	if (!keepEmpty)
		mergeGhost(heightTopRaw[previous], heightTopRaw[heightCurrent], heightBottomRaw[previous], heightBottomRaw[heightCurrent], heightTileEmpty, heightRaster);
	globals::profiler->EndPass();

	ID3D11Buffer* nullVB = nullptr;
	UINT zero = 0;
	context->IASetVertexBuffers(0, 1, &nullVB, &zero, &zero);
	context->IASetIndexBuffer(nullptr, DXGI_FORMAT_R16_UINT, 0);
	context->IASetInputLayout(nullptr);
	context->VSSetShader(nullptr, nullptr, 0);
	context->PSSetShader(nullptr, nullptr, 0);
	ID3D11Buffer* nullCB1 = nullptr;
	context->VSSetConstantBuffers(1, 1, &nullCB1);
	context->PSSetConstantBuffers(0, 1, &nullCB1);
	ID3D11ShaderResourceView* nullTerrainSRV = nullptr;
	context->PSSetShaderResources(2, 1, &nullTerrainSRV);

	const UINT dispatchDim = (kHeightMapDim + 7) / 8;
	globals::profiler->BeginPass(SnowDevSwitches::HeightMapRow("SnowDeformation::Objects - Height Maps Combine"));
	ID3D11ShaderResourceView* terrainSRV = shellTerrainTexture->srv.get();
	context->CSSetConstantBuffers(0, 1, &processCB);
	context->CSSetShaderResources(2, 1, &terrainSRV);
	{
		ID3D11ShaderResourceView* combineSRVs[2] = { heightTopRaw[heightCurrent]->srv.get(), heightBottomRaw[heightCurrent]->srv.get() };
		ID3D11Buffer* exclusionCB = doorsCB->CB();
		ID3D11ShaderResourceView* tileSRV = heightTileEmpty ? heightTileEmpty->srv.get() : nullptr;
		ID3D11ShaderResourceView* exclusionListSRV = exclusionListBuffer->SRV();
		context->CSSetConstantBuffers(1, 1, &exclusionCB);
		context->CSSetShaderResources(0, 2, combineSRVs);
		context->CSSetShaderResources(5, 1, &exclusionListSRV);
		context->CSSetShader(heightCombineCS.get(), nullptr, 0);
		auto combine = [&](Texture2D* a_mask, ID3D11ShaderResourceView* a_tiles) {
			ID3D11UnorderedAccessView* combineUAVs[3] = { nullptr, nullptr, a_mask->uav.get() };
			context->CSSetShaderResources(4, 1, &a_tiles);
			context->CSSetUnorderedAccessViews(0, 3, combineUAVs, nullptr);
			context->Dispatch(dispatchDim, dispatchDim, 1);
		};
		combine(heightBottomFiltered, tileSRV);
		if (combineVerifyOn)
			combine(combineVerifyTex, nullptr);
		context->CSSetShaderResources(0, 2, nullCsSRVs);
		context->CSSetShaderResources(4, 2, nullCsSRVs);
		ID3D11UnorderedAccessView* nullCombineUAVs[3] = { nullptr, nullptr, nullptr };
		context->CSSetUnorderedAccessViews(0, 3, nullCombineUAVs, nullptr);
		ID3D11Buffer* nullExclusionCB = nullptr;
		context->CSSetConstantBuffers(1, 1, &nullExclusionCB);
	}

	globals::profiler->EndPass();
	if (combineVerifyOn) {
		compareKept(heightBottomFiltered, combineVerifyTex, 6);
		verifiedLevels |= 1u << 5;
	}

	heightMapsEmpty = emptyWindow && keepEmpty;

	if ((verifyOn || combineVerifyOn) && verifiedLevels) {
		auto& ring = heightRasterVerifyRing;
		if (!heightRasterVerifyStagingLevels[ring % 3]) {
			context->CopyResource(heightRasterVerifyStaging[ring % 3].get(), heightRasterVerifyCount.get());
			heightRasterVerifyStagingLevels[ring % 3] = verifiedLevels;
			ring++;
		}
	}
	if (heightRasterVerifyStaging[0]) {
		for (uint32_t slot = 0; slot < 3; slot++) {
			const uint32_t levels = heightRasterVerifyStagingLevels[slot];
			if (!levels)
				continue;
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (FAILED(context->Map(heightRasterVerifyStaging[slot].get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped)))
				continue;
			const auto* words = static_cast<const uint32_t*>(mapped.pData);
			for (uint32_t level = 0; level < 6; level++)
				if (levels & (1u << level))
					heightRasterVerified[level]++;
			bool sampled = false;
			for (uint32_t target = 0; target < kHeightVerifySlots; target++) {
				const uint32_t level = kHeightVerifyTargetLevel[target];
				const uint32_t count = words[target * kHeightVerifySlotWords];
				if (!(levels & (1u << level)) || !count)
					continue;
				heightRasterMismatch[target] += count;
				const std::scoped_lock debugLock(heightRasterDebugLock);
				if (!sampled) {
					heightRasterSamples.clear();
					sampled = true;
				}
				for (uint32_t i = 0; i < std::min(count, kHeightVerifySamples); i++) {
					const uint32_t* s = words + target * kHeightVerifySlotWords + 4 + i * 8;
					heightRasterSamples.push_back({ target, s[0] & 0xFFFF, s[0] >> 16, std::bit_cast<float>(s[1]), std::bit_cast<float>(s[2]),
						std::bit_cast<float>(s[3]), std::bit_cast<float>(s[4]) });
				}
			}
			context->Unmap(heightRasterVerifyStaging[slot].get(), 0);
			heightRasterVerifyStagingLevels[slot] = 0;
		}
	}

	globals::profiler->MarkPassSkipped("SnowDeformation::Objects - Height Maps");

	ID3D11ShaderResourceView* nullTailSRVs[2] = { nullptr, nullptr };
	context->CSSetShaderResources(2, 2, nullTailSRVs);
	ID3D11Buffer* nullProcessCB = nullptr;
	context->CSSetConstantBuffers(0, 1, &nullProcessCB);
	context->CSSetShader(nullptr, nullptr, 0);
}

void SnowDeformation::RenderExclusionField(const ExclusionsCB& a_list)
{
	if (!exclusionFieldTexture || !exclusionFieldCB || !shellTerrainTexture) {
		exclusionFieldValid = false;
		return;
	}
	auto* cs = GetSnowCS(kExclusionFieldCS);
	if (!cs) {
		exclusionFieldValid = false;
		return;
	}

	auto context = globals::d3d::context;
	auto eye = globals::game::frameBufferCached.GetCameraPosAdjust();

	constexpr float texelSize = kExclusionFieldHalfExtent * 2.0f / kExclusionFieldDim;
	const float2 center = {
		std::floor(eye.x / texelSize) * texelSize,
		std::floor(eye.y / texelSize) * texelSize
	};
	const bool same = exclusionFieldValid && exclusionBakeList &&
	                  center.x == exclusionFieldCenter.x && center.y == exclusionFieldCenter.y &&
	                  shellWindowCellX == exclusionBakeCellX && shellWindowCellY == exclusionBakeCellY &&
	                  shellTerrainVersion == exclusionBakeTerrainVersion &&
	                  memcmp(&a_list, exclusionBakeList.get(), sizeof(ExclusionsCB)) == 0;
	if (same) {
		globals::profiler->MarkPassSkipped("SnowDeformation::Ground - Field Bakes");
		return;
	}
	exclusionFieldValid = false;
	exclusionFieldCenter = center;

	constexpr float cellSize = kShellVertexSpacing * kShellTexelsPerCell;
	ExclusionFieldCB cbData{};
	cbData.FieldCenter = exclusionFieldCenter;
	cbData.FieldTexelSize = texelSize;
	cbData.TerrainWindowOrigin = { shellWindowCellX * cellSize, shellWindowCellY * cellSize };
	cbData.TerrainTexelSize = kShellVertexSpacing;
	cbData.TerrainDim = kShellWindowDim;
	exclusionFieldCB->Update(cbData);

	ID3D11Buffer* cbs[2] = { exclusionFieldCB->CB(), doorsCB->CB() };
	context->CSSetConstantBuffers(0, 2, cbs);
	ID3D11ShaderResourceView* srv = shellTerrainTexture->srv.get();
	context->CSSetShaderResources(0, 1, &srv);
	ID3D11UnorderedAccessView* uav = exclusionFieldTexture->uav.get();
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	context->CSSetShader(cs, nullptr, 0);

	globals::profiler->BeginPass("SnowDeformation::Ground - Field Bakes");
	context->Dispatch(kExclusionFieldDim / 16, kExclusionFieldDim / 16, 1);
	globals::profiler->EndPass();

	ID3D11Buffer* nullCBs[2] = { nullptr, nullptr };
	ID3D11ShaderResourceView* nullSRV = nullptr;
	ID3D11UnorderedAccessView* nullUAV = nullptr;
	context->CSSetConstantBuffers(0, 2, nullCBs);
	context->CSSetShaderResources(0, 1, &nullSRV);
	context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	context->CSSetShader(nullptr, nullptr, 0);

	exclusionFieldValid = true;
	exclusionBakeCellX = shellWindowCellX;
	exclusionBakeCellY = shellWindowCellY;
	exclusionBakeTerrainVersion = shellTerrainVersion;
	if (!exclusionBakeList)
		exclusionBakeList = std::make_unique<ExclusionsCB>();
	memcpy(exclusionBakeList.get(), &a_list, sizeof(ExclusionsCB));
}

uint32_t SD_PositionBytes(uint64_t a_descKey, const RE::BSGraphics::VertexDesc& a_desc)
{
	uint32_t positionBytes = uint32_t(a_descKey & 0xF) * 4;
	static constexpr std::pair<RE::BSGraphics::Vertex::Flags, RE::BSGraphics::Vertex::Attribute> kAttrs[] = {
		{ RE::BSGraphics::Vertex::VF_UV, RE::BSGraphics::Vertex::VA_TEXCOORD0 },
		{ RE::BSGraphics::Vertex::VF_UV_2, RE::BSGraphics::Vertex::VA_TEXCOORD1 },
		{ RE::BSGraphics::Vertex::VF_NORMAL, RE::BSGraphics::Vertex::VA_NORMAL },
		{ RE::BSGraphics::Vertex::VF_TANGENT, RE::BSGraphics::Vertex::VA_BINORMAL },
		{ RE::BSGraphics::Vertex::VF_COLORS, RE::BSGraphics::Vertex::VA_COLOR },
		{ RE::BSGraphics::Vertex::VF_SKINNED, RE::BSGraphics::Vertex::VA_SKINNING },
		{ RE::BSGraphics::Vertex::VF_LANDDATA, RE::BSGraphics::Vertex::VA_LANDDATA },
		{ RE::BSGraphics::Vertex::VF_EYEDATA, RE::BSGraphics::Vertex::VA_EYEDATA },
	};
	for (auto [flag, attr] : kAttrs) {
		if (a_desc.HasFlag(flag)) {
			uint32_t attrOffset = a_desc.GetAttributeOffset(attr);
			if (attrOffset > 0 && attrOffset < positionBytes)
				positionBytes = attrOffset;
		}
	}
	return positionBytes;
}

ID3D11InputLayout* SnowDeformation::ContactSkinInputLayoutFor(uint64_t a_descKey, const RE::BSGraphics::VertexDesc& a_desc, uint32_t a_dynamicPositionBytes)
{
	const uint64_t cacheKey = a_descKey | (a_dynamicPositionBytes ? (uint64_t(a_dynamicPositionBytes) << 56) : 0);
	auto& layout = contactSkinILCache[cacheKey];
	if (!layout && contactSkinVSBlob) {
		const uint32_t skinOffset = a_desc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_SKINNING);
		const uint32_t positionBytes = a_dynamicPositionBytes ? a_dynamicPositionBytes : SD_PositionBytes(a_descKey, a_desc);
		const UINT positionSlot = a_dynamicPositionBytes ? 1 : 0;
		D3D11_INPUT_ELEMENT_DESC elements[3] = {
			{ "POSITION", 0, positionBytes >= 16 ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R16G16B16A16_FLOAT, positionSlot, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "BLENDWEIGHT", 0, DXGI_FORMAT_R16G16B16A16_FLOAT, 0, skinOffset, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, skinOffset + 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		globals::d3d::device->CreateInputLayout(elements, 3, contactSkinVSBlob->GetBufferPointer(), contactSkinVSBlob->GetBufferSize(), layout.put());
	}
	return layout.get();
}

ID3D11InputLayout* SnowDeformation::StaticsInputLayoutFor(uint64_t a_descKey, const RE::BSGraphics::VertexDesc& a_desc)
{
	auto& layout = staticsILCache[a_descKey];
	if (!layout && heightVSBlob) {
		const uint32_t positionBytes = SD_PositionBytes(a_descKey, a_desc);

		D3D11_INPUT_ELEMENT_DESC elements[2] = {
			{ "POSITION", 0, positionBytes >= 16 ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R16G16B16A16_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, a_desc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_NORMAL), D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		globals::d3d::device->CreateInputLayout(elements, 2, heightVSBlob->GetBufferPointer(), heightVSBlob->GetBufferSize(), layout.put());
	}
	return layout.get();
}

void SnowDeformation::GatherWaterObjects()
{
	capturedWater.clear();
	auto* waterSystem = RE::TESWaterSystem::GetSingleton();
	if (!waterSystem || !waterSystem->enabled)
		return;
	for (const auto& object : waterSystem->waterObjects) {
		if (!object || !object->shape)
			continue;
		auto* shape = object->shape.get();
		if (shape->GetAppCulled())
			continue;
		bool seen = false;
		for (const auto& water : capturedWater)
			if (water.geometry.get() == shape) {
				seen = true;
				break;
			}
		if (seen)
			continue;
		capturedWater.push_back({ RE::NiPointer<RE::BSGeometry>(shape), shape->world, object->plane.constant });
	}
}

void SnowDeformation::RenderWaterCapture()
{
	auto context = globals::d3d::context;
	if (waterCaptureShadersFailed || !staticsCB || !heightMaxBlendState) {
		capturedWater.clear();
		return;
	}
	if (!waterCaptureVS) {
		const auto path = L"Data\\Shaders\\SnowDeformation\\SnowHeightCapture.hlsl";
		waterCaptureVS.attach(static_cast<ID3D11VertexShader*>(SD_CompileShader(path, "vs_5_0", { "WATER" }, &waterCaptureVSBlob)));
		waterCapturePS.attach(static_cast<ID3D11PixelShader*>(SD_CompileShader(path, "ps_5_0", { "WATER" })));
		if (!waterCaptureVS || !waterCapturePS) {
			logger::error("[SNOW DEFORMATION] Water capture shaders failed; the shell will not end at water");
			waterCaptureShadersFailed = true;
			capturedWater.clear();
			return;
		}
		Util::SetResourceName(waterCaptureVS.get(), "SnowDeformation::WaterCaptureVS");
		Util::SetResourceName(waterCapturePS.get(), "SnowDeformation::WaterCapturePS");
	}
	if (!waterHeightTexture) {
		waterHeightTexture = SD_MakeTexture2D(kShellWindowDim, kShellWindowDim, DXGI_FORMAT_R32_FLOAT, "SnowDeformation::WaterWindow", D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
		waterWindowCellX = INT_MIN;
		waterBakeValid = false;
		waterVisibleTexture = SD_MakeTexture2D(kShellWindowDim, kShellWindowDim, DXGI_FORMAT_R32_FLOAT, "SnowDeformation::WaterVisible");
		waterNearTexture = SD_MakeTexture2D(kShellWindowDim, kShellWindowDim, DXGI_FORMAT_R8_UINT, "SnowDeformation::WaterNear");
		waterCoverDirty = true;
		waterFineTexture = SD_MakeTexture2D(kWaterFineDim, kWaterFineDim, DXGI_FORMAT_R32_FLOAT, "SnowDeformation::WaterFine", D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
		waterFineValid = false;
	}
	GatherWaterObjects();
	waterBakeScratch.clear();
	waterBakeScratch.reserve(capturedWater.size());
	for (const auto& water : capturedWater) {
		WaterBakeKey key;
		memset(&key, 0, sizeof(key));
		auto* geometry = water.geometry.get();
		auto rendererData = geometry ? geometry->GetGeometryRuntimeData().rendererData : nullptr;
		if (rendererData) {
			key.vb = rendererData->vertexBuffer;
			key.ib = rendererData->indexBuffer;
			memcpy(&key.descKey, &rendererData->vertexDesc, sizeof(key.descKey));
		}
		if (auto triShape = geometry ? geometry->AsTriShape() : nullptr)
			key.indexCount = uint32_t(triShape->GetTrishapeRuntimeData().triangleCount) * 3;
		key.world = water.world;
		key.planeConstant = water.planeConstant;
		waterBakeScratch.push_back(key);
	}
	const bool same = waterBakeValid && waterWindowCellX == shellWindowCellX && waterWindowCellY == shellWindowCellY &&
	                  waterBakeKeys.size() == waterBakeScratch.size() &&
	                  (waterBakeScratch.empty() || memcmp(waterBakeKeys.data(), waterBakeScratch.data(), waterBakeScratch.size() * sizeof(WaterBakeKey)) == 0);
	const float2 fineCenter = heightWindowCenter;
	const float fineHalf = ObjectRasterHalfExtent();
	const bool fineNeeded = waterFineTexture && (!same || !waterFineValid ||
											   fineCenter.x != waterFineCenter.x || fineCenter.y != waterFineCenter.y || fineHalf != waterFineHalf);
	if (same && !fineNeeded) {
		globals::profiler->MarkPassSkipped("SnowDeformation::Ground - Field Bakes");
		capturedWater.clear();
		return;
	}
	if (!same) {
		waterBakeKeys.swap(waterBakeScratch);
		waterBakeValid = true;
		waterCoverDirty = true;
		waterWindowVersion++;
	}
	const float clear[4] = { kShellMissingHeight, 0.0f, 0.0f, 0.0f };
	if (!same) {
		context->ClearRenderTargetView(waterHeightTexture->rtv.get(), clear);
		waterWindowCellX = shellWindowCellX;
		waterWindowCellY = shellWindowCellY;
	}
	if (capturedWater.empty()) {
		waterFineValid = false;
		return;
	}

	globals::profiler->BeginPass("SnowDeformation::Ground - Field Bakes");
	ID3D11ShaderResourceView* nullWaterSRV = nullptr;
	context->PSSetShaderResources(kWaterWindowSlot, 1, &nullWaterSRV);
	context->OMSetBlendState(heightMaxBlendState.get(), nullptr, 0xFFFFFFFF);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->VSSetShader(waterCaptureVS.get(), nullptr, 0);
	context->PSSetShader(waterCapturePS.get(), nullptr, 0);
	ID3D11Buffer* cb1 = staticsCB->CB();
	context->VSSetConstantBuffers(1, 1, &cb1);

	auto drawPlanes = [&](ID3D11RenderTargetView* a_rtv, uint32_t a_dim, float2 a_center, float a_halfExtent) {
	context->OMSetRenderTargets(1, &a_rtv, nullptr);
	D3D11_VIEWPORT viewport{ 0.0f, 0.0f, float(a_dim), float(a_dim), 0.0f, 1.0f };
	context->RSSetViewports(1, &viewport);
	StaticsCB rec{};
	rec.HeightWindowCenter = a_center;
	rec.HeightHalfExtent = a_halfExtent;
	for (const auto& water : capturedWater) {
		auto* geometry = water.geometry.get();
		auto rendererData = geometry ? geometry->GetGeometryRuntimeData().rendererData : nullptr;
		if (!rendererData || !rendererData->vertexBuffer || !rendererData->indexBuffer)
			continue;
		auto triShape = geometry->AsTriShape();
		if (!triShape)
			continue;
		const uint32_t indexCount = uint32_t(triShape->GetTrishapeRuntimeData().triangleCount) * 3;
		if (indexCount == 0)
			continue;
		auto desc = rendererData->vertexDesc;
		uint64_t descKey;
		memcpy(&descKey, &desc, sizeof(descKey));
		if (!waterFirstLogged) {
			waterFirstLogged = true;
			logger::info("[SNOW DEFORMATION] water capture: first plane '{}' desc {:#x} vertex {} tris {} at ({:.0f} {:.0f} {:.0f}) window origin cell ({}, {})",
				geometry->name.c_str(), descKey, desc.HasFlag(RE::BSGraphics::Vertex::VF_VERTEX), triShape->GetTrishapeRuntimeData().triangleCount,
				water.world.translate.x, water.world.translate.y, water.world.translate.z, shellWindowCellX, shellWindowCellY);
		}
		if (!desc.HasFlag(RE::BSGraphics::Vertex::VF_VERTEX))
			continue;
		auto& layout = waterILCache[descKey];
		if (!layout) {
			const uint32_t positionBytes = SD_PositionBytes(descKey, desc);
			D3D11_INPUT_ELEMENT_DESC element = { "POSITION", 0, positionBytes >= 16 ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R16G16B16A16_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 };
			globals::d3d::device->CreateInputLayout(&element, 1, waterCaptureVSBlob->GetBufferPointer(), waterCaptureVSBlob->GetBufferSize(), layout.put());
		}
		if (!layout)
			continue;
		context->IASetInputLayout(layout.get());
		const UINT stride = uint32_t(descKey & 0xF) * 4;
		if (stride == 0)
			continue;
		const UINT offset = 0;
		auto* vb = reinterpret_cast<ID3D11Buffer*>(rendererData->vertexBuffer);
		auto* ib = reinterpret_cast<ID3D11Buffer*>(rendererData->indexBuffer);
		context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
		context->IASetIndexBuffer(ib, DXGI_FORMAT_R16_UINT, 0);
		SetWorldRows(water.world, rec);
		staticsCB->Update(rec);
		context->DrawIndexed(indexCount, 0, 0);
	}
	};
	if (!same) {
		const float cellSize = kShellVertexSpacing * kShellTexelsPerCell;
		const float span = kShellVertexSpacing * kShellWindowDim;
		drawPlanes(waterHeightTexture->rtv.get(), kShellWindowDim,
			{ shellWindowCellX * cellSize + span * 0.5f, shellWindowCellY * cellSize + span * 0.5f }, span * 0.5f);
	}
	if (fineNeeded) {
		context->ClearRenderTargetView(waterFineTexture->rtv.get(), clear);
		drawPlanes(waterFineTexture->rtv.get(), kWaterFineDim, fineCenter, fineHalf);
		waterFineCenter = fineCenter;
		waterFineHalf = fineHalf;
		waterFineValid = true;
	}
	ID3D11RenderTargetView* nullRTV = nullptr;
	context->OMSetRenderTargets(1, &nullRTV, nullptr);
	globals::profiler->EndPass();
	capturedWater.clear();
}

void SnowDeformation::CoverWaterWindow()
{
	if (!waterHeightTexture || !waterVisibleTexture || !waterVisibleTexture->uav || !waterNearTexture || !waterNearTexture->uav)
		return;
	if (!waterCoverDirty && waterCoverTerrainVersion == shellTerrainVersion)
		return;
	waterCoverDirty = false;
	waterCoverTerrainVersion = shellTerrainVersion;
	waterWindowVersion++;
	auto context = globals::d3d::context;
	if (!heightWaterCoverCS || !shellTerrainTexture || !shellTerrainTexture->srv || !heightProcessCB) {
		context->CopyResource(waterVisibleTexture->resource.get(), waterHeightTexture->resource.get());
		const UINT farNear[4] = { 10, 10, 10, 10 };
		context->ClearUnorderedAccessViewUint(waterNearTexture->uav.get(), farNear);
		return;
	}
	globals::profiler->BeginPass("SnowDeformation::Ground - Field Bakes");
	ID3D11ShaderResourceView* srvs[3] = { waterHeightTexture->srv.get(), nullptr, shellTerrainTexture->srv.get() };
	ID3D11UnorderedAccessView* uavs[2] = { waterVisibleTexture->uav.get(), waterNearTexture->uav.get() };
	ID3D11Buffer* processCB = heightProcessCB->CB();
	context->CSSetConstantBuffers(0, 1, &processCB);
	context->CSSetShaderResources(0, 3, srvs);
	context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
	context->CSSetShader(heightWaterCoverCS.get(), nullptr, 0);
	context->Dispatch((kShellWindowDim + 7) / 8, (kShellWindowDim + 7) / 8, 1);
	ID3D11ShaderResourceView* nullSRVs[3] = { nullptr, nullptr, nullptr };
	ID3D11UnorderedAccessView* nullUAVs[2] = { nullptr, nullptr };
	context->CSSetShaderResources(0, 3, nullSRVs);
	context->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
	context->CSSetShader(nullptr, nullptr, 0);
	globals::profiler->EndPass();
}

bool SnowDeformation::EnsureContactResources()
{
	if (contactShadersFailed)
		return false;
	auto* device = globals::d3d::device;
	constexpr auto path = L"Data\\Shaders\\SnowDeformation\\SnowContactCapture.hlsl";
	if (!contactVS) {
		contactVS.attach(static_cast<ID3D11VertexShader*>(SD_CompileShader(path, "vs_5_0")));
		if (contactVS)
			Util::SetResourceName(contactVS.get(), "SnowDeformation::ContactCaptureVS");
	}
	if (!contactPS) {
		contactPS.attach(static_cast<ID3D11PixelShader*>(SD_CompileShader(path, "ps_5_0")));
		if (contactPS)
			Util::SetResourceName(contactPS.get(), "SnowDeformation::ContactCapturePS");
	}
	if (!contactSkinVS) {
		contactSkinVS.attach(static_cast<ID3D11VertexShader*>(SD_CompileShader(path, "vs_5_0", { "SKINNED" }, &contactSkinVSBlob)));
		if (contactSkinVS)
			Util::SetResourceName(contactSkinVS.get(), "SnowDeformation::ContactCaptureSkinVS");
	}
	if (!contactSkinCB) {
		D3D11_BUFFER_DESC cbDesc{};
		cbDesc.ByteWidth = sizeof(ContactSkinCB);
		cbDesc.Usage = D3D11_USAGE_DYNAMIC;
		cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		contactSkinCB = new ConstantBuffer(cbDesc, "SnowDeformation::ContactSkinCB");
	}
	if (!contactVS || !contactPS) {
		contactShadersFailed = true;
		logger::warn("[SNOW DEFORMATION] Prop contact capture disabled (shader compilation failed)");
		return false;
	}
	if (!contactHeight)
		contactHeight = SD_MakeTexture2D(kContactDim, kContactDim, DXGI_FORMAT_R32G32_FLOAT, "SnowDeformation::ContactHeight", D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
	auto makeMinBlend = [&](winrt::com_ptr<ID3D11BlendState>& a_state, UINT8 a_mask) {
		if (a_state)
			return true;
		D3D11_BLEND_DESC blendDesc{};
		auto& rt = blendDesc.RenderTarget[0];
		rt.BlendEnable = TRUE;
		rt.SrcBlend = D3D11_BLEND_ONE;
		rt.DestBlend = D3D11_BLEND_ONE;
		rt.BlendOp = D3D11_BLEND_OP_MIN;
		rt.SrcBlendAlpha = D3D11_BLEND_ONE;
		rt.DestBlendAlpha = D3D11_BLEND_ONE;
		rt.BlendOpAlpha = D3D11_BLEND_OP_MIN;
		rt.RenderTargetWriteMask = a_mask;
		return SUCCEEDED(device->CreateBlendState(&blendDesc, a_state.put()));
	};
	if (!makeMinBlend(contactMinBlendBodies, D3D11_COLOR_WRITE_ENABLE_RED) || !makeMinBlend(contactMinBlendProps, D3D11_COLOR_WRITE_ENABLE_GREEN)) {
		contactShadersFailed = true;
		return false;
	}
	if (!contactRasterState) {
		D3D11_RASTERIZER_DESC rasterDesc{};
		rasterDesc.FillMode = D3D11_FILL_SOLID;
		rasterDesc.CullMode = D3D11_CULL_NONE;
		rasterDesc.DepthClipEnable = TRUE;
		if (FAILED(device->CreateRasterizerState(&rasterDesc, contactRasterState.put()))) {
			contactShadersFailed = true;
			return false;
		}
	}
	return true;
}

void SnowDeformation::DrawContactCapture(ID3D11DeviceContext* a_context, bool a_coatOnly)
{
	contactDrawsLast = 0;
	const bool coatFall = false;
	if ((contactProps.empty() && contactActors.empty() && !coatFall) || !EnsureContactResources())
		return;
	auto* context = a_context;

	const float clearValue[4] = { kContactNone, kContactNone, 0.0f, 0.0f };
	context->ClearRenderTargetView(contactHeight->rtv.get(), clearValue);
	ID3D11RenderTargetView* rtv = contactHeight->rtv.get();
	context->OMSetRenderTargets(1, &rtv, nullptr);
	context->OMSetBlendState(contactMinBlendProps.get(), nullptr, 0xFFFFFFFF);
	winrt::com_ptr<ID3D11RasterizerState> savedRaster;
	context->RSGetState(savedRaster.put());
	context->RSSetState(contactRasterState.get());
	D3D11_VIEWPORT viewport{ 0.0f, 0.0f, float(kContactDim), float(kContactDim), 0.0f, 1.0f };
	context->RSSetViewports(1, &viewport);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->VSSetShader(contactVS.get(), nullptr, 0);
	context->PSSetShader(contactPS.get(), nullptr, 0);
	ID3D11Buffer* cb1 = staticsCB->CB();
	context->VSSetConstantBuffers(1, 1, &cb1);

	constexpr float kContactSweepTeleport = 200.0f;
	auto drawRigid = [&](RE::BSGeometry* a_geometry, bool a_sweep = false) -> bool {
		RigidDraw draw;
		if (!RigidDrawOf(a_geometry, draw))
			return false;
		auto* layout = StaticsInputLayoutFor(draw.descKey, draw.desc);
		if (!layout)
			return false;
		const UINT stride = uint32_t(draw.descKey & 0xF) * 4;
		if (stride == 0)
			return false;
		UINT offset = 0;
		context->IASetInputLayout(layout);
		context->IASetVertexBuffers(0, 1, &draw.vb, &stride, &offset);
		context->IASetIndexBuffer(draw.ib, DXGI_FORMAT_R16_UINT, 0);

		const auto& world = a_geometry->world;
		auto rowsFrom = [&](const RE::NiTransform& a_x, StaticsCB& a_cb) {
			SetWorldRows(a_x, a_cb);
			a_cb.HeightWindowCenter = contactCenter;
			a_cb.HeightHalfExtent = kContactHalfExtent;
		};

		uint32_t steps = 1;
		RE::NiTransform previous;
		if (a_sweep) {
			auto& state = contactSweepStates[a_geometry];
			const bool fresh = state.frame + 1 != contactSweepFrame || state.buffer != draw.vb;
			previous = state.world;
			state.world = world;
			state.frame = contactSweepFrame;
			state.buffer = draw.vb;
			if (!fresh) {
				const float radius = a_geometry->worldBound.radius;
				float trace = 0.0f;
				for (int r = 0; r < 3; ++r)
					for (int c = 0; c < 3; ++c)
						trace += world.rotate.entry[r][c] * previous.rotate.entry[r][c];
				const float angle = std::acos(std::clamp((trace - 1.0f) * 0.5f, -1.0f, 1.0f));
				const float travel = world.translate.GetDistance(previous.translate) + radius * angle;
				if (travel < kContactSweepStep)
					return false;
				if (travel < kContactSweepTeleport)
					steps = std::clamp(uint32_t(std::ceil(travel / kContactSweepStep)), 1u, kContactMaxSweep);
			}
		}

		StaticsCB scb{};
		for (uint32_t step = 1; step <= steps; ++step) {
			if (step == steps) {
				rowsFrom(world, scb);
			} else {
				const float t = float(step) / float(steps);
				RE::NiTransform blend = world;
				for (int r = 0; r < 3; ++r)
					for (int c = 0; c < 3; ++c)
						blend.rotate.entry[r][c] = std::lerp(previous.rotate.entry[r][c], world.rotate.entry[r][c], t);
				blend.translate = previous.translate * (1.0f - t) + world.translate * t;
				blend.scale = std::lerp(previous.scale, world.scale, t);
				rowsFrom(blend, scb);
			}
			staticsCB->Update(scb);
			context->DrawIndexed(draw.indexCount, 0, 0);
		}
		return true;
	};

	globals::profiler->BeginPass("SnowDeformation::Trails - Gather & Contact");
	static const std::vector<ContactProp> noProps;
	for (const auto& prop : a_coatOnly ? noProps : contactProps) {
		auto ref = prop.ref.get();
		auto* root = ref ? ref->Get3D(false) : nullptr;
		if (!root)
			continue;
		RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* a_geometry) -> RE::BSVisit::BSVisitControl {
			if (a_geometry->GetGeometryRuntimeData().skinInstance)
				return RE::BSVisit::BSVisitControl::kContinue;
			if (drawRigid(a_geometry))
				contactDrawsLast++;
			return RE::BSVisit::BSVisitControl::kContinue;
		});
	}
	globals::profiler->EndPass();

	contactSkinDrawsLast = 0;
	contactCarriedLast = 0;
	if (contactSkinIndexing.size() > 1024)
		contactSkinIndexing.clear();
	contactSweepFrame++;
	if (contactSweepStates.size() > 512)
		contactSweepStates.clear();
	if ((!contactActors.empty() || coatFall) && contactSkinVS && contactSkinCB) {
		globals::profiler->BeginPass("SnowDeformation::Trails - Gather & Contact");
		context->OMSetBlendState(contactMinBlendBodies.get(), nullptr, 0xFFFFFFFF);
		context->VSSetShader(contactSkinVS.get(), nullptr, 0);
		ID3D11Buffer* skinCB = contactSkinCB->CB();
		context->VSSetConstantBuffers(2, 1, &skinCB);
		bool skinnedVSBound = true;
		const bool coatWrite = false;
		bool coatPass = false;
		bool coatTopPass = false;
		bool coatAll = false;
		float4 coatParams{};
		float4 coatTop{};
		uint32_t coatTriangles = 0;
		const auto* playerCamera = RE::PlayerCamera::GetSingleton();
		const bool firstPerson = playerCamera && playerCamera->IsInFirstPerson();
		const auto& coatOnly = noProps;
		const size_t walks = contactActors.size() + (coatWrite ? coatOnly.size() : 0);
		for (size_t walk = a_coatOnly ? contactActors.size() : 0; walk < walks; ++walk) {
			const bool coatOnlyWalk = walk >= contactActors.size();
			const auto& actor = coatOnlyWalk ? coatOnly[walk - contactActors.size()] : contactActors[walk];
			auto ref = actor.ref.get();
			auto* root = ref ? ref->Get3D(false) : nullptr;
			if (!root)
				continue;
			const uint drawsBeforeActor = contactSkinDrawsLast;
			const bool firstPersonPlayer = firstPerson && ref->IsPlayerRef();
			struct
			{
				uint hidden = 0, unlit = 0, proxy = 0, above = 0, skinned = 0, dynamic = 0, dynamicUnread = 0;
			} tally;
			int geometryIndex = -1;
			auto visitGeometry = [&](RE::BSGeometry* a_geometry) -> RE::BSVisit::BSVisitControl {
				auto& runtime = a_geometry->GetGeometryRuntimeData();
				auto* skin = runtime.skinInstance.get();
				if (!coatAll) {
					const auto& gb = a_geometry->worldBound;
					if (gb.radius > 0.0f && gb.center.z - gb.radius > actor.groundZ + actor.layer + kContactSkipMargin) {
						tally.above++;
						return RE::BSVisit::BSVisitControl::kContinue;
					}
				}
				if (const char* name = a_geometry->name.c_str(); name && (std::strstr(name, "[Ovl") || std::strstr(name, "[SOvl"))) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				if (auto* alpha = runtime.alphaProperty.get()) {
					const char* name = a_geometry->name.c_str();
					const bool shell = alpha->GetAlphaTesting() && name && (std::strstr(name, "shell") || std::strstr(name, "Shell"));
					if (shell || alpha->GetAlphaBlending()) {
						return RE::BSVisit::BSVisitControl::kContinue;
					}
				}
				{
					bool hidden = false;
					for (const RE::NiAVObject* n = a_geometry; n && !hidden; n = n->parent)
						hidden = n->GetAppCulled() && !(firstPersonPlayer && n == root);
					auto* property = runtime.shaderProperty.get();
					const bool lit = property && property->GetRTTI() == globals::rtti::BSLightingShaderPropertyRTTI.get();
					const bool refraction = lit && property->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kRefraction);
					if (hidden || !lit || refraction) {
						if (contactSkinHiddenLogged.size() < 32 && contactSkinHiddenLogged.insert(a_geometry).second)
							logger::info("[SNOW DEFORMATION] contact geometry '{}' on '{}' is {} (bound radius {:.0f}); not drawn",
								a_geometry->name.c_str() ? a_geometry->name.c_str() : "", ref->GetDisplayFullName(),
								hidden ? "hidden by the game" : (refraction ? "a refraction volume, not a surface" : (property ? "effect art, not a surface" : "without a shader")),
								a_geometry->worldBound.radius);
						++(hidden ? tally.hidden : tally.unlit);
						return RE::BSVisit::BSVisitControl::kContinue;
					}
					if (IsProxyOrOverlayGeometry(a_geometry, property)) {
						tally.proxy++;
						return RE::BSVisit::BSVisitControl::kContinue;
					}
				}
				if (!skin) {
					if (coatPass || coatTopPass)
						return RE::BSVisit::BSVisitControl::kContinue;
					if (skinnedVSBound) {
						context->VSSetShader(contactVS.get(), nullptr, 0);
						skinnedVSBound = false;
					}
					if (drawRigid(a_geometry, true))
						contactCarriedLast++;
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				tally.skinned++;
				if (!skinnedVSBound) {
					context->VSSetShader(contactSkinVS.get(), nullptr, 0);
					skinnedVSBound = true;
				}
				++geometryIndex;
				auto* skinData = skin->skinData.get();
				auto* skinPartition = skin->skinPartition.get();
				if (!skinData || !skinPartition || !skin->bones || !skinPartition->partitions.data())
					return RE::BSVisit::BSVisitControl::kContinue;
				uint32_t dynamicPositionBytes = 0;
				if (auto* dynamicShape = a_geometry->AsDynamicTriShape()) {
					tally.dynamic++;
					auto& dd = dynamicShape->GetDynamicTrishapeRuntimeData();
					const uint32_t vertexCount = dynamicShape->GetTrishapeRuntimeData().vertexCount;
					const uint32_t perVertex = vertexCount ? dd.dataSize / vertexCount : 0;
					const bool readable = dd.dynamicData && vertexCount > 0 && dd.dataSize == vertexCount * perVertex && (perVertex == 16 || perVertex == 8);
					if (contactDynamicLogged.size() < 16 && contactDynamicLogged.insert(a_geometry).second) {
						std::string firstPart = "no partition buffer";
						if (skinPartition->numPartitions > 0)
							if (auto* buff = skinPartition->partitions[0].buffData) {
								uint64_t key;
								memcpy(&key, &buff->vertexDesc, sizeof(key));
								uint32_t vbBytes = 0;
								if (auto* vb = reinterpret_cast<ID3D11Buffer*>(buff->vertexBuffer)) {
									D3D11_BUFFER_DESC bd{};
									vb->GetDesc(&bd);
									vbBytes = bd.ByteWidth;
								}
								firstPart = std::format("desc {:016X} size nibble {} dynamic nibble {} skin offset {} flags{}{} | static VB {} bytes = {:.1f}/vertex, raw copy {}",
									key, uint32_t(key & 0xF) * 4, uint32_t((key >> 4) & 0xF) * 4,
									buff->vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_SKINNING),
									buff->vertexDesc.HasFlag(RE::BSGraphics::Vertex::VF_VERTEX) ? " VERTEX" : "",
									buff->vertexDesc.HasFlag(RE::BSGraphics::Vertex::VF_FULLPREC) ? " FULLPREC" : "",
									vbBytes, vertexCount ? float(vbBytes) / vertexCount : 0.0f, buff->rawVertexData ? "yes" : "no");
							}
						logger::info("[SNOW DEFORMATION] dynamic tri shape '{}' on '{}': {} vertices, dynamicData {} bytes = {}/vertex -> {} | {}",
							a_geometry->name.c_str() ? a_geometry->name.c_str() : "", ref->GetDisplayFullName(),
							vertexCount, dd.dataSize, perVertex, readable ? "drawn from dynamic positions" : "NOT drawn (size does not read as positions)", firstPart);
					}
					if (!readable) {
						tally.dynamicUnread++;
						return RE::BSVisit::BSVisitControl::kContinue;
					}
					if (contactDynamicVBBytes < dd.dataSize) {
						contactDynamicVB = nullptr;
						D3D11_BUFFER_DESC vbDesc{};
						vbDesc.ByteWidth = std::max(dd.dataSize, 64u * 1024u);
						vbDesc.Usage = D3D11_USAGE_DYNAMIC;
						vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
						vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
						if (FAILED(globals::d3d::device->CreateBuffer(&vbDesc, nullptr, contactDynamicVB.put()))) {
							contactDynamicVBBytes = 0;
							tally.dynamicUnread++;
							return RE::BSVisit::BSVisitControl::kContinue;
						}
						Util::SetResourceName(contactDynamicVB.get(), "SnowDeformation::ContactDynamicPositions");
						contactDynamicVBBytes = vbDesc.ByteWidth;
					}
					D3D11_MAPPED_SUBRESOURCE mapped{};
					if (FAILED(context->Map(contactDynamicVB.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
						tally.dynamicUnread++;
						return RE::BSVisit::BSVisitControl::kContinue;
					}
					{
						RE::BSSpinLockGuard guard(dd.lock);
						std::memcpy(mapped.pData, dd.dynamicData, dd.dataSize);
					}
					context->Unmap(contactDynamicVB.get(), 0);
					dynamicPositionBytes = perVertex;
				}
				const uint32_t boneCount = skinData->GetBoneCount();
				auto* dismember = netimmerse_cast<RE::BSDismemberSkinInstance*>(skin);
				const RE::BSDismemberSkinInstance::Data* dismemberParts = nullptr;
				uint32_t dismemberCount = 0;
				if (dismember) {
					const auto& rd = dismember->GetRuntimeData();
					if (rd.partitions && rd.numPartitions > 0) {
						dismemberParts = rd.partitions;
						dismemberCount = uint32_t(rd.numPartitions);
					}
				}
				uint8_t indexing = 0;
				if (auto it = contactSkinIndexing.find(skin); it != contactSkinIndexing.end() && it->second.partition == skinPartition) {
					indexing = it->second.indexing;
				} else {
					indexing = 1;
					std::string audit;
					for (uint32_t p = 0; p < skinPartition->numPartitions; ++p) {
						const auto& part = skinPartition->partitions[p];
						auto* buff = part.buffData;
						if (!buff || !buff->rawVertexData || !part.bones || part.numBones == 0)
							continue;
						auto partDesc = buff->vertexDesc;
						if (!partDesc.HasFlag(RE::BSGraphics::Vertex::VF_SKINNED))
							continue;
						uint64_t key;
						memcpy(&key, &partDesc, sizeof(key));
						const uint32_t stride = uint32_t(key & 0xF) * 4;
						const uint32_t skinOffset = partDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_SKINNING);
						if (stride == 0)
							continue;
						uint32_t over = 0, maxIndex = 0;
						for (uint32_t v = 0; v < part.vertices; ++v) {
							const uint8_t* base = buff->rawVertexData + size_t(v) * stride;
							uint16_t wh[4];
							std::memcpy(wh, base + skinOffset, sizeof(wh));
							uint8_t idx[4];
							std::memcpy(idx, base + skinOffset + 8, sizeof(idx));
							bool vOver = false;
							for (int k = 0; k < 4; ++k) {
								if (wh[k] == 0)
									continue;
								maxIndex = std::max(maxIndex, uint32_t(idx[k]));
								vOver |= idx[k] >= part.numBones;
							}
							over += vOver;
						}
						if (over > 0)
							indexing = 2;
						audit += std::format("[p{}: {} bones, max index {}, {} of {} verts past the partition] ", p, part.numBones, maxIndex, over, part.vertices);
					}
					contactSkinIndexing[skin] = { skinPartition, indexing };
					if (indexing == 2 || contactSkinIndexing.size() <= 24)
						logger::info("[SNOW DEFORMATION] index audit '{}' on '{}': {} bones in the skin -> {} {}",
							a_geometry->name.c_str() ? a_geometry->name.c_str() : "", ref->GetDisplayFullName(), boneCount,
							indexing == 2 ? "GLOBAL (whole-skin palette)" : "local", audit);
				}
				bool globalUploaded = false;
				auto boneResolves = [&](uint16_t a_bone) -> bool {
					if (a_bone >= boneCount)
						return false;
					if (skin->bones[a_bone])
						return true;
					if (!skin->boneMatrices || a_bone >= skin->numMatrices)
						return false;
					const float* m = reinterpret_cast<const float*>(skin->boneMatrices) + size_t(a_bone) * 12;
					float rot = 0.0f, pos = 0.0f;
					for (int r = 0; r < 3; ++r) {
						for (int c = 0; c < 3; ++c)
							rot += std::abs(m[r * 4 + c]);
						pos += std::abs(m[r * 4 + 3]);
					}
					return rot > 1e-3f && pos > 1.0f;
				};
				contactPaletteScratch.resize(boneCount);
				contactPaletteBuilt.assign(boneCount, 0);
				auto composedFor = [&](uint16_t a_bone, uint32_t a_partition, uint16_t a_slot) -> const RE::NiTransform& {
					static RE::NiTransform standIn;
					auto* boneNode = a_bone < boneCount ? skin->bones[a_bone] : nullptr;
					const RE::NiTransform* boneWorld = boneNode ? &boneNode->world : nullptr;
					if (!boneWorld && a_bone < skin->numMatrices && skin->boneMatrices) {
						if (!contactPaletteBuilt[a_bone]) {
							const float* m = reinterpret_cast<const float*>(skin->boneMatrices) + size_t(a_bone) * 12;
							RE::NiTransform& t = contactPaletteScratch[a_bone];
							for (int r = 0; r < 3; ++r) {
								for (int c = 0; c < 3; ++c)
									t.rotate.entry[r][c] = m[r * 4 + c];
								t.translate[r] = m[r * 4 + 3];
							}
							t.scale = 1.0f;
							contactPaletteBuilt[a_bone] = 1;
						}
						return contactPaletteScratch[a_bone];
					}
					if (!boneWorld) {
						RE::NiAVObject* stand = skin->rootParent ? skin->rootParent : root;
						standIn = a_bone < boneCount ? stand->world * skinData->GetBoneDataSkinToBone(a_bone) : stand->world;
						if (contactSkinMissingLogged.size() < 32 && contactSkinMissingLogged.insert(skin).second)
							logger::info("[SNOW DEFORMATION] contact skin '{}' on '{}': partition {} slot {} names bone {} of {} which the skeleton lacks; standing in with '{}'",
								a_geometry->name.c_str() ? a_geometry->name.c_str() : "", ref->GetDisplayFullName(), a_partition, a_slot, a_bone, boneCount,
								stand->name.c_str() ? stand->name.c_str() : "");
						return standIn;
					}
					if (!contactPaletteBuilt[a_bone]) {
						contactPaletteScratch[a_bone] = *boneWorld * skinData->GetBoneDataSkinToBone(a_bone);
						contactPaletteBuilt[a_bone] = 1;
					}
					return contactPaletteScratch[a_bone];
				};
				ContactSkinCB cb{};
				cb.SkinWindowCenter = contactCenter;
				cb.SkinHalfExtent = kContactHalfExtent;
				const uint16_t* paletteBones = nullptr;
				uint16_t paletteCount = 0;
				if (contactSkinLogged.size() < 16 && contactSkinLogged.insert(skin).second) {
					std::string layout;
					for (uint32_t p = 0; p < skinPartition->numPartitions; ++p) {
						const auto& part = skinPartition->partitions[p];
						layout += std::format("[{} tris, {} bones, buf {:p}] ", part.triangles, part.numBones, (const void*)part.buffData);
					}
					logger::info("[SNOW DEFORMATION] contact skin '{}': {} partitions {}",
						a_geometry->name.c_str() ? a_geometry->name.c_str() : "", skinPartition->numPartitions, layout);
				}
				RE::BSGraphics::TriShape* rangeBuff = nullptr;
				uint32_t indexStart = 0;
				for (uint32_t p = 0; p < skinPartition->numPartitions; ++p) {
					const auto& part = skinPartition->partitions[p];
					auto* buff = part.buffData;
					if (!buff || !buff->vertexBuffer || !buff->indexBuffer)
						continue;
					if (buff != rangeBuff) {
						rangeBuff = buff;
						indexStart = 0;
					}
					const uint32_t indexCount = uint32_t(part.triangles) * 3;
					const uint32_t thisStart = indexStart;
					indexStart += indexCount;
					if (!part.bones || part.numBones == 0 || part.numBones > kContactMaxBones || indexCount == 0)
						continue;
					if (dismemberParts && p < dismemberCount && !dismemberParts[p].editorVisible) {
						continue;
					}
					if (indexing != 2) {
						bool orphan = false;
						for (uint16_t j = 0; j < part.numBones && !orphan; ++j)
							orphan = !boneResolves(part.bones[j]);
						if (orphan) {
							continue;
						}
					}
					auto partDesc = buff->vertexDesc;
					if ((!dynamicPositionBytes && !partDesc.HasFlag(RE::BSGraphics::Vertex::VF_VERTEX)) ||
						!partDesc.HasFlag(RE::BSGraphics::Vertex::VF_SKINNED))
						continue;
					uint64_t descKey;
					memcpy(&descKey, &partDesc, sizeof(descKey));
					auto* layout = ContactSkinInputLayoutFor(descKey, partDesc, dynamicPositionBytes);
					if (!layout)
						continue;
					const UINT stride = uint32_t(descKey & 0xF) * 4;
					if (stride == 0)
						continue;

					if (indexing == 2 && boneCount <= kContactMaxBones) {
						if (!globalUploaded) {
							for (uint32_t b = 0; b < boneCount; ++b) {
								const RE::NiTransform& m = composedFor(uint16_t(b), p, uint16_t(b));
								const auto& rot = m.rotate;
								const float sc = m.scale;
								cb.BoneRows[b * 3 + 0] = { rot.entry[0][0] * sc, rot.entry[0][1] * sc, rot.entry[0][2] * sc, m.translate.x };
								cb.BoneRows[b * 3 + 1] = { rot.entry[1][0] * sc, rot.entry[1][1] * sc, rot.entry[1][2] * sc, m.translate.y };
								cb.BoneRows[b * 3 + 2] = { rot.entry[2][0] * sc, rot.entry[2][1] * sc, rot.entry[2][2] * sc, m.translate.z };
							}
							cb.SkinBoneCount = float(boneCount);
							contactSkinCB->Update(cb);
							globalUploaded = true;
							paletteBones = nullptr;
						}
					} else {
					const bool samePalette = paletteBones && paletteCount == part.numBones &&
					                         std::memcmp(paletteBones, part.bones, size_t(part.numBones) * sizeof(uint16_t)) == 0;
					if (!samePalette) {
						for (uint16_t j = 0; j < part.numBones; ++j) {
							const RE::NiTransform& m = composedFor(part.bones[j], p, j);
							const auto& rot = m.rotate;
							const float sc = m.scale;
							cb.BoneRows[j * 3 + 0] = { rot.entry[0][0] * sc, rot.entry[0][1] * sc, rot.entry[0][2] * sc, m.translate.x };
							cb.BoneRows[j * 3 + 1] = { rot.entry[1][0] * sc, rot.entry[1][1] * sc, rot.entry[1][2] * sc, m.translate.y };
							cb.BoneRows[j * 3 + 2] = { rot.entry[2][0] * sc, rot.entry[2][1] * sc, rot.entry[2][2] * sc, m.translate.z };
						}
						cb.SkinBoneCount = float(part.numBones);
						contactSkinCB->Update(cb);
						paletteBones = part.bones;
						paletteCount = part.numBones;
					}
					}

					UINT offset = 0;
					auto* vb = reinterpret_cast<ID3D11Buffer*>(buff->vertexBuffer);
					auto* ib = reinterpret_cast<ID3D11Buffer*>(buff->indexBuffer);
					context->IASetInputLayout(layout);
					context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
					if (dynamicPositionBytes) {
						auto* dynamicVB = contactDynamicVB.get();
						context->IASetVertexBuffers(1, 1, &dynamicVB, &dynamicPositionBytes, &offset);
					}
					context->IASetIndexBuffer(ib, DXGI_FORMAT_R16_UINT, 0);
					context->DrawIndexed(indexCount, thisStart, 0);
					contactSkinDrawsLast++;
					coatTriangles += coatPass ? indexCount / 3 : 0;
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			};
			if (!coatOnlyWalk)
				RE::BSVisit::TraverseScenegraphGeometries(root, std::ref(visitGeometry));
			const bool rootHidden = root->GetAppCulled() && !firstPersonPlayer;
			if (!coatOnlyWalk && !actor.corpse && contactSkinDrawsLast == drawsBeforeActor && !rootHidden) {
				if (auto it = stampBoneCache.find(ref->GetFormID()); it != stampBoneCache.end()) {
					auto& cache = it->second;
					cache.contactStarved = kContactStarveFrames;
					if (!cache.contactStarveLogged && contactStarveLines < 32) {
						cache.contactStarveLogged = true;
						contactStarveLines++;
						logger::info("[SNOW DEFORMATION] contact draw printed nothing for '{}' ({:08X}){}; stamping by bones. skinned drawn-eligible {}, hidden {}, unshaded/effect {}, proxy {}, above the layer {}, dynamic {} (unreadable {})",
							ref->GetDisplayFullName(), ref->GetFormID(), firstPersonPlayer ? " in first person" : "",
							tally.skinned, tally.hidden, tally.unlit, tally.proxy, tally.above, tally.dynamic, tally.dynamicUnread);
					}
				}
			}
		}
		globals::profiler->EndPass();
	}

	ID3D11RenderTargetView* nullRTV = nullptr;
	context->OMSetRenderTargets(1, &nullRTV, nullptr);
	context->RSSetState(savedRaster.get());
}

void SnowDeformation::StartExclusionGather()
{
	auto* player = globals::game::player;
	auto* tes = globals::game::tes;
	if (!player || !tes) {
		exclusionGatherSlot = 0;
		return;
	}
	if (!survivalHeatSourcesResolved) {
		survivalHeatSourcesResolved = true;
		if (auto* dataHandler = RE::TESDataHandler::GetSingleton())
			survivalHeatSources = dataHandler->LookupForm<RE::BGSListForm>(0x0008AA, "ccQDRSSE001-SurvivalMode.esl");
	}
	exclusionPending.clear();
	exclusionPendingFlames.clear();
	exclusionPendingGuards.clear();
	exclusionPendingHeat.clear();
	exclusionGatherOrigin = player->GetPosition();
	exclusionGatherInterior = tes->interiorCell != nullptr;
	const uint32_t gridLength = tes->gridCells ? tes->gridCells->length : 0;
	exclusionGatherSlots = exclusionGatherInterior ? 1 : gridLength * gridLength + 1;
	exclusionGatherSlot = 1;
}

void SnowDeformation::StepExclusionGather()
{
	if (exclusionGatherSlot == 0)
		return;
	auto* tes = globals::game::tes;
	if (!tes || (tes->interiorCell != nullptr) != exclusionGatherInterior) {
		exclusionGatherSlot = 0;
		return;
	}
	const float radius = kExclusionFieldHalfExtent + 512.0f;
	const auto& origin = exclusionGatherOrigin;
	auto visit = [this](RE::TESObjectREFR* a_ref) { return ExclusionVisit(a_ref); };
	const uint32_t perStep = std::max(1u, (exclusionGatherSlots + 49) / 50);
	const uint32_t gridLength = tes->gridCells ? tes->gridCells->length : 0;
	for (uint32_t n = 0; n < perStep && exclusionGatherSlot <= exclusionGatherSlots; n++, exclusionGatherSlot++) {
		const uint32_t i = exclusionGatherSlot - 1;
		if (exclusionGatherInterior) {
			if (tes->interiorCell)
				tes->interiorCell->ForEachReferenceInRange(origin, radius, visit);
		} else if (i < gridLength * gridLength) {
			auto* cell = tes->gridCells->GetCell(i / gridLength, i % gridLength);
			if (cell && cell->IsAttached()) {
				if (const auto* coords = cell->GetCoordinates()) {
					if (coords->worldX < origin.x + radius && coords->worldX + 4096.0f > origin.x - radius &&
						coords->worldY < origin.y + radius && coords->worldY + 4096.0f > origin.y - radius)
						cell->ForEachReferenceInRange(origin, radius, visit);
				}
			}
		} else if (auto* worldSpace = tes->GetRuntimeData2().worldSpace) {
			if (auto* skyCell = worldSpace->GetSkyCell())
				skyCell->ForEachReferenceInRange(origin, radius, visit);
		}
	}
	if (exclusionGatherSlot > exclusionGatherSlots) {
		FinishExclusionGather();
		exclusionGatherSlot = 0;
	}
}

RE::BSContainer::ForEachResult SnowDeformation::ExclusionVisit(RE::TESObjectREFR* a_ref)
{
	auto* tes = globals::game::tes;
	if (exclusionPending.size() >= kMaxExclusions * 4)
		return RE::BSContainer::ForEachResult::kStop;
	if (!a_ref || a_ref->IsDisabled() || !a_ref->Is3DLoaded())
		return RE::BSContainer::ForEachResult::kContinue;
	auto* base = a_ref->GetBaseObject();
	if (!base)
		return RE::BSContainer::ForEachResult::kContinue;

	if (base->Is(RE::FormType::Door))
		return RE::BSContainer::ForEachResult::kContinue;

	if (auto* light = base->As<RE::TESObjectLIGH>(); light && light->CanBeCarried()) {
		if (HasActiveLight(a_ref->Get3D())) {
			auto pos = a_ref->GetPosition();
			exclusionPending.push_back({ { pos.x, pos.y, pos.z, kTorchClearRadius }, { 0.0f, 0.0f, 1.0f, 1.0f } });
			exclusionPendingHeat.push_back({ pos.x, pos.y, pos.z, kTorchClearRadius });
		}
		return RE::BSContainer::ForEachResult::kContinue;
	}

	const char* modelPath = nullptr;
	if (auto* acti = base->As<RE::TESObjectACTI>())
		modelPath = acti->GetModel();
	else if (auto* stat = base->As<RE::TESObjectSTAT>())
		modelPath = stat->GetModel();
	else if (auto* movable = base->As<RE::BGSMovableStatic>())
		modelPath = movable->GetModel();
	float heatRadius = 0.0f;
	bool genericFlame = false;
	std::string lowered;
	if (modelPath && modelPath[0]) {
		lowered = Util::ToLower(modelPath);
		if (lowered.find("torchbug") == std::string::npos) {
			if (lowered.find("fxfire") != std::string::npos) {
				genericFlame = true;
			} else {
				for (const auto& spec : kHeatSpecs) {
					if (lowered.find(spec.substring) != std::string::npos) {
						heatRadius = spec.radius;
						break;
					}
				}
			}
		}
	}
	if (heatRadius > 0.0f || genericFlame) {
		auto pos = a_ref->GetPosition();
		if (genericFlame) {
			float landZ = pos.z;
			tes->GetLandHeight(pos, landZ);
			heatRadius = pos.z - landZ < kGroundFireBand ? kFireClearRadius : kRaisedFlameClearRadius;
		}
		heatRadius *= a_ref->GetScale();
		if (genericFlame)
			exclusionPendingFlames.push_back(exclusionPending.size());
		exclusionPending.push_back({ { pos.x, pos.y, pos.z, heatRadius }, { 0.0f, 0.0f, 1.0f, 1.0f } });
		exclusionPendingHeat.push_back({ pos.x, pos.y, pos.z, heatRadius });
		return RE::BSContainer::ForEachResult::kContinue;
	}

	if (!lowered.empty()) {
		for (const auto& spec : kTrampleSpecs) {
			if (lowered.find(spec.substring) != std::string::npos) {
				auto pos = a_ref->GetPosition();
				float angleZ = a_ref->GetAngleZ();
				float scale = a_ref->GetScale();
				float zoneScale = std::max(settings.TrampleZoneScale, 0.0f);
				const float meltStrength = spec.meltStrength > 0.0f ?
			                                   spec.meltStrength :
			                                   1.0f - std::clamp(settings.TrampleZoneHeight, 0.0f, 100.0f) / 100.0f;
				const float elongation = std::max(spec.aspect, 1.0f) - 1.0f;
				exclusionPending.push_back({ { pos.x + std::sin(angleZ) * spec.forwardBias * scale,
												 pos.y + std::cos(angleZ) * spec.forwardBias * scale,
												 pos.z, spec.radius * scale * zoneScale },
					{ std::sin(angleZ) * elongation, std::cos(angleZ) * elongation, meltStrength,
						spec.smoothEdge ? 2.0f : 1.0f } });
				if (spec.ownsFlames)
					exclusionPendingGuards.push_back({ pos.x, pos.y, pos.z, spec.radius * scale * 0.5f });
				return RE::BSContainer::ForEachResult::kContinue;
			}
		}
	}

	if (!lowered.empty()) {
		bool sealed = false;
		for (const char* substring : kSealedContainerSubstrings)
			sealed |= lowered.find(substring) != std::string::npos;
		auto* sealedBound = sealed ? base->As<RE::TESBoundObject>() : nullptr;
		if (sealedBound) {
			const auto& bounds = sealedBound->boundData;
			const float scale = a_ref->GetScale();
			const float halfX = (bounds.boundMax.x - bounds.boundMin.x) * 0.5f * scale;
			const float halfY = (bounds.boundMax.y - bounds.boundMin.y) * 0.5f * scale;
			if (halfX >= 1.0f && halfY >= 1.0f) {
				const float angleZ = a_ref->GetAngleZ();
				const float sinZ = std::sin(angleZ), cosZ = std::cos(angleZ);
				const float localX = (bounds.boundMax.x + bounds.boundMin.x) * 0.5f * scale;
				const float localY = (bounds.boundMax.y + bounds.boundMin.y) * 0.5f * scale;
				auto pos = a_ref->GetPosition();
				exclusionPending.push_back({ { pos.x + localX * cosZ + localY * sinZ,
												 pos.y - localX * sinZ + localY * cosZ,
												 pos.z, halfX },
					{ sinZ, cosZ, halfY, 3.0f } });
			}
			return RE::BSContainer::ForEachResult::kContinue;
		}
	}

	if (survivalHeatSources && survivalHeatSources->HasForm(base)) {
		float halfExtent = 20.0f;
		if (auto* bound = base->As<RE::TESBoundObject>()) {
			float extentX = (bound->boundData.boundMax.x - bound->boundData.boundMin.x) * 0.5f;
			float extentY = (bound->boundData.boundMax.y - bound->boundData.boundMin.y) * 0.5f;
			halfExtent = std::max(extentX, extentY) * a_ref->GetScale();
		}
		auto pos = a_ref->GetPosition();
		float landZ = pos.z;
		tes->GetLandHeight(pos, landZ);
		float radius = pos.z - landZ < kGroundFireBand ? kUnknownHeatClearRadius :
	                                                     std::clamp(halfExtent * 1.5f, kHeatClearRadiusMin, kHeatClearRadiusMax);
		exclusionPending.push_back({ { pos.x, pos.y, pos.z, radius * a_ref->GetScale() }, { 0.0f, 0.0f, 1.0f, 1.0f } });
		exclusionPendingHeat.push_back({ pos.x, pos.y, pos.z, radius * a_ref->GetScale() });
	}
	return RE::BSContainer::ForEachResult::kContinue;
}

void SnowDeformation::FinishExclusionGather()
{
	if (!exclusionPendingGuards.empty() && !exclusionPendingFlames.empty()) {
		for (auto it = exclusionPendingFlames.rbegin(); it != exclusionPendingFlames.rend(); ++it) {
			const auto& flame = exclusionPending[*it].first;
			for (const auto& guard : exclusionPendingGuards) {
				const float dx = flame.x - guard.x, dy = flame.y - guard.y;
				if (dx * dx + dy * dy < guard.w * guard.w && std::abs(flame.z - guard.z) < 300.0f) {
					exclusionPending.erase(exclusionPending.begin() + *it);
					break;
				}
			}
		}
	}

	if (exclusionPending.size() > kMaxExclusions) {
		const auto playerPos = exclusionGatherOrigin;
		std::partial_sort(exclusionPending.begin(), exclusionPending.begin() + kMaxExclusions, exclusionPending.end(),
			[&](const auto& a_lhs, const auto& a_rhs) {
				const float lhsDx = a_lhs.first.x - playerPos.x, lhsDy = a_lhs.first.y - playerPos.y;
				const float rhsDx = a_rhs.first.x - playerPos.x, rhsDy = a_rhs.first.y - playerPos.y;
				return lhsDx * lhsDx + lhsDy * lhsDy < rhsDx * rhsDx + rhsDy * rhsDy;
			});
		exclusionPending.resize(kMaxExclusions);
	}
	staticExclusions.swap(exclusionPending);
	exclusionPending.clear();
	exclusionPendingFlames.clear();
	exclusionPendingGuards.clear();
	heatSources.swap(exclusionPendingHeat);
	exclusionPendingHeat.clear();
}
