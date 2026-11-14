#include "Features/SnowDeformation.h"

#include <DDSTextureLoader.h>
#include <DirectXPackedVector.h>

#include "Deferred.h"
#include "Features/TerrainShadows.h"
#include "Globals.h"
#include "Profiler.h"
#include "ShaderCache.h"
#include "State.h"
#include "TruePBR.h"
#include "Utils/D3D.h"
#include "Utils/Format.h"
#include "Utils/Game.h"

static constexpr uint32_t kSnowLandIsSnowShift = 11;
static_assert(uint32_t(State::ExtraFeatureDescriptors::SnowLandIsSnowMask) == (0x3Fu << kSnowLandIsSnowShift));

static std::string LandTexturePath(RE::BGSTextureSet* a_textureSet)
{
	if (auto path = a_textureSet->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse))
		return Util::ToLower(path);
	return {};
}

static RE::MATERIAL_ID LandHavokMaterial(const RE::TESLandTexture* a_landTexture)
{
	static const REL::Relocation<RE::MATERIAL_ID (*)(const RE::TESLandTexture*)> func{ RELOCATION_ID(18418, 18849) };
	return func(a_landTexture);
}

static RE::TESLandTexture* QuadBaseTexture(RE::TESLandTexture* a_base)
{
	return a_base ? a_base : GetDefaultLandTexture();
}

static int ClassifySnowClass(const RE::TESLandTexture* a_landTexture, const std::string& a_path)
{
	for (uint32_t classI = 0; classI < SnowDeformation::kSnowClassCount; ++classI) {
		const char* match = SnowDeformation::kSnowClasses[classI].match;
		if (match[0] != '\0' && a_path.find(match) != std::string::npos)
			return (int)classI;
	}

	const auto material = LandHavokMaterial(a_landTexture);
	const bool snowMaterial = material == RE::MATERIAL_ID::kSnow || material == RE::MATERIAL_ID::kSnowStairs;
	return snowMaterial ? 3 : (int)SnowDeformation::kSnowClassCount - 1;
}

static std::string LandTextureLabel(const std::string& a_path)
{
	size_t begin = a_path.find_last_of("\\/");
	begin = begin == std::string::npos ? 0 : begin + 1;
	size_t end = a_path.find_last_of('.');
	if (end == std::string::npos || end < begin)
		end = a_path.size();
	return a_path.substr(begin, end - begin);
}

static uint64_t CellKey(int a_x, int a_y)
{
	return (uint64_t(uint32_t(a_x)) << 32) | uint32_t(a_y);
}

template <class Planes>
static bool BoxInsidePlanes(const double (&a_box)[2][3], const Planes& a_planes)
{
	for (const auto& p : a_planes)
		if (p[0] * a_box[p[0] >= 0.0][0] + p[1] * a_box[p[1] >= 0.0][1] + p[2] * a_box[p[2] >= 0.0][2] + p[3] < 0.0)
			return false;
	return true;
}

uint16_t SnowDeformation::RegisterLandTexture(RE::TESLandTexture* a_landTexture)
{
	if (!a_landTexture)
		return kNoLandTexture;

	auto* textureSet = Util::GetSeasonalSwap(a_landTexture->textureSet);
	if (!textureSet || (a_landTexture->formID == 0 && textureSet == a_landTexture->textureSet))
		return kNoLandTexture;
	const uint32_t key = textureSet->formID;

	{
		const std::shared_lock lock(landTextureMutex);
		if (auto it = landTextureByTextureSet.find(key); it != landTextureByTextureSet.end())
			return it->second;
	}

	const std::string path = LandTexturePath(textureSet);
	if (path.empty())
		return kNoLandTexture;

	const std::unique_lock lock(landTextureMutex);
	if (auto it = landTextureByTextureSet.find(key); it != landTextureByTextureSet.end())
		return it->second;

	for (size_t i = 0; i < landTextures.size(); ++i) {
		if (landTextures[i].path == path) {
			landTextureByTextureSet[key] = (uint16_t)i;
			return (uint16_t)i;
		}
	}

	if (landTextures.size() >= kMaxLandTextures) {
		static bool warned = false;
		if (!std::exchange(warned, true))
			logger::warn("[SNOW DEFORMATION] Land texture registry full at {}; further textures read as bare ground", kMaxLandTextures);
		return kNoLandTexture;
	}

	LandTextureEntry entry;
	entry.path = path;
	entry.label = LandTextureLabel(path);
	entry.classIndex = ClassifySnowClass(a_landTexture, path);
	for (const auto& shipped : kTextureDefaults) {
		if (path.find(shipped.match) != std::string::npos) {
			entry.shipped = true;
			entry.shippedDepth = shipped.depth;
			break;
		}
	}
	ResolveLandTextureDepthLocked(entry);

	const uint16_t index = (uint16_t)landTextures.size();
	landTextures.push_back(entry);
	landTextureByTextureSet[key] = index;
	snowDepthVersion.fetch_add(1, std::memory_order_release);

	logger::info("[SNOW DEFORMATION] LTEX {:08X} TXST {:08X} [{}] {:.0f} units{} {}", a_landTexture->formID, key,
		kSnowClasses[entry.classIndex].label, entry.depth, entry.overridden ? " (override)" : "", path);
	return index;
}

void SnowDeformation::ResolveLandTextureDepthLocked(LandTextureEntry& a_entry)
{
	auto it = settings.TextureDepths.find(a_entry.path);
	a_entry.overridden = it != settings.TextureDepths.end();
	a_entry.depth = a_entry.overridden ? it->second :
	                a_entry.shipped    ? a_entry.shippedDepth :
	                                     settings.SnowClassDepths[a_entry.classIndex];
}

void SnowDeformation::ResolveLandTextureDepthsLocked()
{
	for (auto& entry : landTextures)
		ResolveLandTextureDepthLocked(entry);
	snowDepthVersion.fetch_add(1, std::memory_order_release);
}

void SnowDeformation::RefreshLandTextureDepths()
{
	const std::unique_lock lock(landTextureMutex);
	ResolveLandTextureDepthsLocked();
}

void SnowDeformation::SetLandTextureOverride(const std::string& a_path, std::optional<float> a_depth)
{
	const std::unique_lock lock(landTextureMutex);
	if (a_depth)
		settings.TextureDepths[a_path] = *a_depth;
	else
		settings.TextureDepths.erase(a_path);
	ResolveLandTextureDepthsLocked();
}

std::vector<float> SnowDeformation::LandTextureDepthSnapshot()
{
	const std::shared_lock lock(landTextureMutex);
	std::vector<float> depths(landTextures.size());
	for (size_t i = 0; i < landTextures.size(); ++i)
		depths[i] = landTextures[i].depth;
	return depths;
}

namespace
{
	bool IsSnowDepth(float a_depth) { return a_depth > 0.0f; }
}

void SnowDeformation::TESObjectLAND_SetupMaterial(RE::TESObjectLAND* land)
{
	if (land == nullptr || land->loadedData == nullptr || land->loadedData->mesh[0] == nullptr)
		return;

	for (uint32_t quadI = 0; quadI < 4; ++quadI) {
		if (land->loadedData->mesh[quadI] == nullptr)
			continue;

		const auto& children = land->loadedData->mesh[quadI]->GetChildren();
		auto geometry = children.empty() ? nullptr : static_cast<RE::BSGeometry*>(children[0].get());
		if (geometry == nullptr)
			continue;

		const auto shaderProp = static_cast<RE::BSLightingShaderProperty*>(geometry->GetGeometryRuntimeData().shaderProperty.get());
		if (shaderProp == nullptr || shaderProp->material == nullptr)
			continue;

		uint16_t quadTextures[6] = { RegisterLandTexture(QuadBaseTexture(land->loadedData->defQuadTextures[quadI])) };
		for (uint32_t textureI = 0; textureI < 5; ++textureI)
			quadTextures[textureI + 1] = RegisterLandTexture(land->loadedData->quadTextures[quadI][textureI]);

		uint8_t mask = 0;
		{
			const std::shared_lock textureLock(landTextureMutex);
			for (uint32_t textureI = 0; textureI < 6; ++textureI) {
				const uint16_t index = quadTextures[textureI];
				if (index != kNoLandTexture && IsSnowDepth(landTextures[index].depth))
					mask |= uint8_t(1 << textureI);
			}
		}

		const std::unique_lock lock(snowMaskMutex);
		if (snowMasks.size() > 16384)
			snowMasks.clear();
		snowMasks[reinterpret_cast<uintptr_t>(shaderProp->material)] = mask;
	}

	BakeShellCell(land);
}

uint32_t SnowDeformation::LandQuadLevel(RE::BSTriShape* a_geometry)
{
	const uint32_t n = a_geometry ? a_geometry->GetTrishapeRuntimeData().vertexCount : 0;
	return n >= 65 * 65 ? 4 : n >= 33 * 33 ? 2 :
	                      n >= 17 * 17     ? 1 :
	                                         0;
}

bool SnowDeformation::ReadLandMeshHeights(RE::TESObjectLAND* a_land, ShellCellData& a_data)
{
	a_data.fine.clear();
	a_data.fineLevel = { 0, 0, 0, 0 };
	auto loadedData = a_land->loadedData;
	if (!loadedData)
		return false;
	auto refuse = [&](uint32_t quadI, const std::string& why) {
		if (!fineMeshWarned.exchange(true))
			logger::info("[SNOW DEFORMATION] Land mesh not used for the fine layer (quad {}): {}", quadI, why);
		return false;
	};
	std::vector<float> fine(size_t(129) * 129, kShellMissingHeight);
	for (uint32_t quadI = 0; quadI < 4; ++quadI) {
		RE::BSTriShape* geometry = LandQuadGeometry(loadedData, quadI);
		if (!geometry)
			return refuse(quadI, "no geometry");
		auto* rendererData = geometry->GetGeometryRuntimeData().rendererData;
		if (!rendererData)
			return refuse(quadI, "no renderer data");
		if (!rendererData->rawVertexData)
			return refuse(quadI, "no CPU vertex copy");
		const auto desc = rendererData->vertexDesc;
		uint64_t descBits;
		memcpy(&descBits, &desc, sizeof(descBits));
		if (!desc.HasFlag(RE::BSGraphics::Vertex::VF_VERTEX))
			return refuse(quadI, std::format("no position attribute (desc {:016X})", descBits));
		const uint32_t stride = uint32_t(descBits & 0xF) * 4;
		const uint32_t posOffset = desc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_POSITION);
		const uint32_t level = LandQuadLevel(geometry);
		const uint32_t vertexCount = geometry->GetTrishapeRuntimeData().vertexCount;
		if (stride == 0 || level == 0)
			return refuse(quadI, std::format("stride {} vertices {} (desc {:016X})", stride, vertexCount, descBits));
		const uint32_t quadX = quadI & 1;
		const uint32_t quadY = quadI >> 1;
		const uint32_t side = 16 * level;
		const float spacing = 128.0f / float(level);
		std::vector<float> quad;
		double baseSum = 0.0;
		uint32_t baseCount = 0, offGrid = 0, outside = 0;
		float firstX = 0.0f, firstY = 0.0f;
		bool usedHalf = false;
		for (int pass = 0; pass < 2 && baseCount < 289; ++pass) {
			const bool half = pass == 1;
			usedHalf = half;
			quad.assign(size_t(side + 1) * (side + 1), kShellMissingHeight);
			baseSum = 0.0;
			baseCount = offGrid = outside = 0;
			for (uint32_t v = 0; v < vertexCount; ++v) {
				const uint8_t* base = rendererData->rawVertexData + size_t(v) * stride + posOffset;
				float px, py, pz;
				if (!half) {
					float p[3];
					memcpy(p, base, sizeof(p));
					px = p[0];
					py = p[1];
					pz = p[2];
				} else {
					uint16_t h[3];
					memcpy(h, base, sizeof(h));
					px = DirectX::PackedVector::XMConvertHalfToFloat(h[0]);
					py = DirectX::PackedVector::XMConvertHalfToFloat(h[1]);
					pz = DirectX::PackedVector::XMConvertHalfToFloat(h[2]);
				}
				if (v == 0) {
					firstX = px;
					firstY = py;
				}
				const float kx = px / spacing + float(side) - float(quadX) * float(side);
				const float ky = py / spacing + float(side) - float(quadY) * float(side);
				if (!std::isfinite(kx) || !std::isfinite(ky) || std::abs(kx) > 1e6f || std::abs(ky) > 1e6f) {
					offGrid++;
					continue;
				}
				const int ix = int(std::lround(kx)), iy = int(std::lround(ky));
				if (std::abs(kx - ix) > 0.01f || std::abs(ky - iy) > 0.01f) {
					offGrid++;
					continue;
				}
				if (ix < 0 || ix > int(side) || iy < 0 || iy > int(side)) {
					outside++;
					continue;
				}
				quad[size_t(iy) * (side + 1) + ix] = pz;
				if (ix % level == 0 && iy % level == 0) {
					baseSum += double(loadedData->heights[quadI][(iy / level) * 17 + ix / level]) - pz;
					baseCount++;
				}
			}
		}
		if (baseCount < 289)
			return refuse(quadI, std::format("{} of 289 lattice vertices found ({} off-grid, {} outside; {} vertices, level {}, first at {:.1f}, {:.1f}, stride {}, pos offset {}, {})",
									 baseCount, offGrid, outside, vertexCount, level, firstX, firstY, stride, posOffset, usedHalf ? "half" : "float"));
		const float baseZ = float(baseSum / baseCount) + (loadedData->heightExtents.x + loadedData->heightExtents.y) * 0.5f;
		for (uint32_t iy = 0; iy <= side; ++iy)
			for (uint32_t ix = 0; ix <= side; ++ix) {
				const float z = quad[size_t(iy) * (side + 1) + ix];
				if (z < -50000.0f)
					return refuse(quadI, std::format("vertex ({}, {}) missing at level {}", ix, iy, level));
				if (ix % level == 0 && iy % level == 0) {
					const float d = z + baseZ - a_data.height[(quadY * 16 + iy / level) * 33 + quadX * 16 + ix / level];
					if (std::abs(d) > 0.5f)
						return refuse(quadI, std::format("lattice vertex ({}, {}) off by {:.2f} after the base", ix, iy, d));
				}
			}
		const uint32_t up = 4 / level;
		for (uint32_t fy = 0; fy < 65; ++fy)
			for (uint32_t fx = 0; fx < 65; ++fx) {
				const uint32_t ix0 = std::min(fx / up, side - 1), iy0 = std::min(fy / up, side - 1);
				const float tx = float(fx) / float(up) - float(ix0), ty = float(fy) / float(up) - float(iy0);
				const float z00 = quad[size_t(iy0) * (side + 1) + ix0], z10 = quad[size_t(iy0) * (side + 1) + ix0 + 1];
				const float z01 = quad[size_t(iy0 + 1) * (side + 1) + ix0], z11 = quad[size_t(iy0 + 1) * (side + 1) + ix0 + 1];
				const float z = (z00 * (1.0f - tx) + z10 * tx) * (1.0f - ty) + (z01 * (1.0f - tx) + z11 * tx) * ty;
				fine[size_t(quadY * 64 + fy) * 129 + quadX * 64 + fx] = z + baseZ;
			}
		a_data.fineLevel[quadI] = uint8_t(level);
	}
	a_data.fine = std::move(fine);
	return true;
}

static RE::BSTriShape* LargestTriShapeUnder(RE::NiAVObject* a_object, std::string* a_names)
{
	if (!a_object)
		return nullptr;
	RE::BSTriShape* best = nullptr;
	if (auto* node = a_object->AsNode()) {
		for (const auto& child : node->GetChildren()) {
			auto* c = LargestTriShapeUnder(child.get(), a_names);
			if (c && (!best || c->GetTrishapeRuntimeData().vertexCount > best->GetTrishapeRuntimeData().vertexCount))
				best = c;
		}
	} else if (auto* geometry = a_object->AsGeometry()) {
		if (auto* tri = geometry->AsTriShape()) {
			if (a_names)
				*a_names += std::format("{}:{} ", tri->name.c_str() ? tri->name.c_str() : "?", tri->GetTrishapeRuntimeData().vertexCount);
			best = tri;
		}
	}
	return best;
}

RE::BSTriShape* SnowDeformation::LandQuadGeometry(RE::TESObjectLAND::LoadedLandData* a_loaded, uint32_t a_quad)
{
	auto* geometry = a_loaded->geom[a_quad].get();
	if (geometry && geometry->GetTrishapeRuntimeData().vertexCount >= 65 * 65)
		return geometry;
	auto* under = LargestTriShapeUnder(a_loaded->mesh[a_quad], nullptr);
	return under ? under : geometry;
}

void SnowDeformation::RefreshLandMeshHeights()
{
	auto* tes = globals::game::tes;
	auto* grid = tes ? tes->gridCells : nullptr;
	if (!grid)
		return;
	const uint32_t worldspace = activeWorldspace.load(std::memory_order_acquire);
	const uint32_t length = grid->length;
	const bool report = shellFineMeshCells == 0 && (++fineRefreshFrame % 300) == 1;
	uint32_t nAttached = 0, nLand = 0, nReady = 0, nCached = 0, nWorldspace = 0, nRefused = 0, nHave = 0;
	std::string first;
	for (uint32_t x = 0; x < length; x++) {
		for (uint32_t y = 0; y < length; y++) {
			auto* cell = grid->GetCell(x, y);
			if (!cell || !cell->IsAttached())
				continue;
			nAttached++;
			auto* land = cell->GetRuntimeData().cellLand;
			auto* coords = cell->GetCoordinates();
			if (!land || !land->loadedData || !coords)
				continue;
			nLand++;
			const uint64_t key = CellKey(coords->cellX, coords->cellY);
			std::array<uint8_t, 4> levels{};
			bool ready = true;
			for (uint32_t quadI = 0; quadI < 4; ++quadI) {
				levels[quadI] = uint8_t(LandQuadLevel(LandQuadGeometry(land->loadedData, quadI)));
				ready = ready && levels[quadI] != 0;
			}
			const uint32_t signature = uint32_t(levels[0]) | uint32_t(levels[1]) << 8 | uint32_t(levels[2]) << 16 | uint32_t(levels[3]) << 24;
			if (report && first.empty()) {
				first = std::format("cell ({}, {}) quads:", coords->cellX, coords->cellY);
				for (uint32_t quadI = 0; quadI < 4; ++quadI) {
					auto* g = land->loadedData->geom[quadI].get();
					std::string names;
					LargestTriShapeUnder(land->loadedData->mesh[quadI], &names);
					first += std::format(" [geom {} | under mesh: {}]", g ? int(g->GetTrishapeRuntimeData().vertexCount) : -1, names.empty() ? "none" : names);
				}
			}
			if (!ready)
				continue;
			nReady++;
			ShellCellData work;
			{
				const std::shared_lock lock(shellCellMutex);
				auto it = shellCells.find(key);
				if (it == shellCells.end())
					continue;
				nCached++;
				if (it->second.worldspaceID != worldspace) {
					nWorldspace++;
					continue;
				}
				if (!it->second.fine.empty() && it->second.fineLevel == levels) {
					nHave++;
					continue;
				}
				if (auto r = shellFineRefused.find(key); r != shellFineRefused.end() && r->second == signature) {
					nRefused++;
					continue;
				}
				work = it->second;
			}
			const bool ok = ReadLandMeshHeights(land, work);
			const std::unique_lock lock(shellCellMutex);
			if (!ok) {
				if (shellFineRefused.size() > 4096)
					shellFineRefused.clear();
				shellFineRefused[key] = signature;
				continue;
			}
			auto it = shellCells.find(key);
			if (it == shellCells.end() || it->second.worldspaceID != worldspace)
				continue;
			const bool hadFine = !it->second.fine.empty();
			it->second.fine = std::move(work.fine);
			it->second.fineLevel = work.fineLevel;
			if (!hadFine)
				shellFineOrder.push_back(key);
			while (shellFineOrder.size() > kShellFineCacheCells) {
				if (auto old = shellCells.find(shellFineOrder.front()); old != shellCells.end())
					old->second.fine.clear();
				shellFineOrder.pop_front();
			}
			shellFineDirty.store(true, std::memory_order_release);
		}
	}
	if (report)
		logger::info("[SNOW DEFORMATION] land mesh refresh: {} attached, {} with land, {} ready, {} cached, {} other worldspace ({}), {} already have, {} refused; {}",
			nAttached, nLand, nReady, nCached, nWorldspace, worldspace, nHave, nRefused, first.empty() ? "no land cell" : first);
}

void SnowDeformation::BakeShellCell(RE::TESObjectLAND* land)
{
	auto cell = land->GetSaveParentCell();
	if (!cell)
		return;
	auto coords = cell->GetCoordinates();
	if (!coords)
		return;

	ShellCellData data{};
	data.height.fill(kShellMissingHeight);
	for (auto& vertexTextures : data.layerTexture)
		vertexTextures.fill(kNoLandTexture);
	for (auto& vertexWeights : data.layerWeight)
		vertexWeights.fill(0);
	data.vertexAO.fill(255);
	if (auto* worldspace = cell->GetRuntimeData().worldSpace)
		data.worldspaceID = worldspace->GetFormID();

	auto loadedData = land->loadedData;

	float cellBaseZ = (loadedData->heightExtents.x + loadedData->heightExtents.y) * 0.5f;

	bool painted = false;
	float minHeight = FLT_MAX;
	float maxHeight = -FLT_MAX;

	for (uint32_t quadI = 0; quadI < 4; ++quadI) {
		uint32_t quadX = quadI & 1;
		uint32_t quadY = quadI >> 1;

		uint16_t baseTexture = RegisterLandTexture(QuadBaseTexture(loadedData->defQuadTextures[quadI]));
		uint16_t layerTexture[6];
		for (uint32_t layerI = 0; layerI < 6; ++layerI)
			layerTexture[layerI] = RegisterLandTexture(loadedData->quadTextures[quadI][layerI]);

		for (uint32_t vertexI = 0; vertexI < 289; ++vertexI) {
			uint32_t vx = vertexI % 17;
			uint32_t vy = vertexI / 17;
			uint32_t cellX = quadX * 16 + vx;
			uint32_t cellY = quadY * 16 + vy;
			uint32_t cellIdx = cellY * 33 + cellX;

			data.height[cellIdx] = loadedData->heights[quadI][vertexI] + cellBaseZ;
			minHeight = std::min(minHeight, data.height[cellIdx]);
			maxHeight = std::max(maxHeight, data.height[cellIdx]);

			const auto& vertexColor = loadedData->colors[quadI][vertexI];
			data.vertexAO[cellIdx] = std::max({ static_cast<uint8_t>(vertexColor[0]),
				static_cast<uint8_t>(vertexColor[1]), static_cast<uint8_t>(vertexColor[2]) });

			uint16_t vertexTexture[7];
			float vertexWeight[7];
			uint32_t textureCount = 0;
			auto addWeight = [&](uint16_t texture, float weight) {
				if (texture == kNoLandTexture || weight <= 0.0f)
					return;
				for (uint32_t i = 0; i < textureCount; ++i) {
					if (vertexTexture[i] == texture) {
						vertexWeight[i] += weight;
						return;
					}
				}
				vertexTexture[textureCount] = texture;
				vertexWeight[textureCount] = weight;
				textureCount++;
			};

			float layerSum = 0.0f;
			for (uint32_t layerI = 0; layerI < 6; ++layerI) {
				float weight = static_cast<uint8_t>(loadedData->percents[quadI][vertexI][layerI]) / 255.0f;
				layerSum += weight;
				addWeight(layerTexture[layerI], weight);
			}
			painted = painted || layerSum > 0.0f;
			addWeight(baseTexture, std::max(0.0f, 1.0f - layerSum));

			for (uint32_t slot = 0; slot < kShellVertexLayers && slot < textureCount; ++slot) {
				uint32_t best = slot;
				for (uint32_t i = slot + 1; i < textureCount; ++i)
					if (vertexWeight[i] > vertexWeight[best])
						best = i;
				std::swap(vertexTexture[slot], vertexTexture[best]);
				std::swap(vertexWeight[slot], vertexWeight[best]);
				data.layerTexture[cellIdx][slot] = vertexTexture[slot];
				data.layerWeight[cellIdx][slot] = (uint8_t)std::clamp(vertexWeight[slot] * 255.0f + 0.5f, 0.0f, 255.0f);
			}
		}
	}

	if (!painted && maxHeight - minHeight < 1.0f) {
		const std::unique_lock lock(shellCellMutex);
		uint64_t fillerKey = CellKey(coords->cellX, coords->cellY);
		if (shellFillerCells.size() > 4096)
			shellFillerCells.clear();
		shellFillerCells[fillerKey] = data.worldspaceID;
		snowDepthVersion.fetch_add(1, std::memory_order_release);
		if (auto it = shellCells.find(fillerKey); it != shellCells.end() && it->second.worldspaceID == data.worldspaceID) {
			shellCells.erase(it);
			snowDepthVersion.fetch_add(1, std::memory_order_release);
			shellDataDirty.store(true, std::memory_order_release);
		}
		return;
	}

	uint64_t key = CellKey(coords->cellX, coords->cellY);
	ReadLandMeshHeights(land, data);
	{
		const std::unique_lock lock(shellCellMutex);
		if (shellCells.size() > 4096) {
			shellCells.clear();
			shellFineOrder.clear();
			shellFineRefused.clear();
			snowDepthVersion.fetch_add(1, std::memory_order_release);
		}
		auto it = shellCells.find(key);
		if (it != shellCells.end() && it->second.worldspaceID == data.worldspaceID &&
			it->second.height == data.height && it->second.layerTexture == data.layerTexture &&
			it->second.layerWeight == data.layerWeight && it->second.vertexAO == data.vertexAO &&
			it->second.fine == data.fine && it->second.fineLevel == data.fineLevel)
			return;
		if (!data.fine.empty()) {
			shellFineOrder.push_back(key);
			while (shellFineOrder.size() > kShellFineCacheCells) {
				if (auto old = shellCells.find(shellFineOrder.front()); old != shellCells.end())
					old->second.fine.clear();
				shellFineOrder.pop_front();
			}
		}
		shellCells[key] = data;
		snowDepthVersion.fetch_add(1, std::memory_order_release);
		if (auto ft = shellFillerCells.find(key); ft != shellFillerCells.end() && ft->second == data.worldspaceID)
			shellFillerCells.erase(ft);
	}
	shellDataDirty.store(true, std::memory_order_release);
}

float SnowDeformation::GetNominalSnowDepthAt(float a_x, float a_y, float a_missing)
{
	constexpr float kCellSize = kShellVertexSpacing * 32.0f;
	const int cellX = (int)std::floor(a_x / kCellSize);
	const int cellY = (int)std::floor(a_y / kCellSize);
	const int vx = std::clamp((int)std::lround((a_x - cellX * kCellSize) / kShellVertexSpacing), 0, 32);
	const int vy = std::clamp((int)std::lround((a_y - cellY * kCellSize) / kShellVertexSpacing), 0, 32);

	const uint64_t key = CellKey(cellX, cellY);
	const std::shared_lock lock(shellCellMutex);
	const auto it = shellCells.find(key);
	if (it == shellCells.end() || it->second.worldspaceID != activeWorldspace.load(std::memory_order_acquire))
		return a_missing;

	const uint32_t idx = uint32_t(vy) * 33 + uint32_t(vx);
	float depth = 0.0f;
	float snowWeight = 0.0f;
	const std::shared_lock textureLock(landTextureMutex);
	for (uint32_t slot = 0; slot < kShellVertexLayers; ++slot) {
		const uint16_t texture = it->second.layerTexture[idx][slot];
		if (texture < landTextures.size()) {
			const float w = it->second.layerWeight[idx][slot] / 255.0f;
			depth += w * landTextures[texture].depth;
			if (IsSnowDepth(landTextures[texture].depth))
				snowWeight += w;
		}
	}
	return LandBlendDepth(depth, snowWeight);
}

float SnowDeformation::LandSnowShareAround(float a_x, float a_y)
{
	constexpr float kCellSize = kShellVertexSpacing * 32.0f;
	const int cellX = (int)std::floor(a_x / kCellSize);
	const int cellY = (int)std::floor(a_y / kCellSize);
	const uint32_t worldspace = activeWorldspace.load(std::memory_order_acquire);
	double snow = 0.0;
	uint32_t samples = 0;
	const std::shared_lock lock(shellCellMutex);
	const std::shared_lock textureLock(landTextureMutex);
	for (int dy = -2; dy <= 2; ++dy) {
		for (int dx = -2; dx <= 2; ++dx) {
			const uint64_t key = CellKey(cellX + dx, cellY + dy);
			const auto it = shellCells.find(key);
			if (it == shellCells.end() || it->second.worldspaceID != worldspace)
				continue;
			for (uint32_t vy = 0; vy < 33; vy += 4) {
				for (uint32_t vx = 0; vx < 33; vx += 4) {
					const uint32_t idx = vy * 33 + vx;
					float weight = 0.0f;
					for (uint32_t slot = 0; slot < kShellVertexLayers; ++slot) {
						const uint16_t texture = it->second.layerTexture[idx][slot];
						if (texture < landTextures.size() && IsSnowDepth(landTextures[texture].depth))
							weight += it->second.layerWeight[idx][slot] / 255.0f;
					}
					snow += std::min(weight, 1.0f);
					samples++;
				}
			}
		}
	}
	return samples ? float(snow / samples) : -1.0f;
}

float SnowDeformation::LandBlendDepth(float a_depth, float a_snowWeight) const
{
	if (a_depth <= 0.0f || !settings.EnableSnowDeformation)
		return a_depth;
	const float start = kLandBlendDepthStart;
	const float t = std::clamp((std::min(a_snowWeight, 1.0f) - start) / std::max(1.0f - start, 1e-3f), 0.0f, 1.0f);
	return a_depth * t * t * (3.0f - 2.0f * t);
}

void SnowDeformation::UpdateActiveWorldspace()
{
	auto* tes = globals::game::tes;
	auto* worldspace = tes ? tes->GetRuntimeData2().worldSpace : nullptr;
	if (!worldspace)
		return;

	const uint32_t id = worldspace->GetFormID();
	if (id == activeWorldspace.exchange(id, std::memory_order_acq_rel))
		return;
	snowDepthVersion.fetch_add(1, std::memory_order_release);

	shellDataDirty.store(true, std::memory_order_release);
	heightMapValid = false;
	clearRequested = true;
	logger::debug("[SNOW DEFORMATION] Worldspace changed to {:08X}: dropping world-anchored caches", id);
}

void SnowDeformation::UpdateShellTerrainWindow()
{
	auto eyeFB = globals::game::frameBufferCached.GetCameraPosAdjust();
	int camCellX = (int)std::floor(eyeFB.x / (kShellVertexSpacing * kShellTexelsPerCell));
	int camCellY = (int)std::floor(eyeFB.y / (kShellVertexSpacing * kShellTexelsPerCell));

	int desiredOriginX = camCellX - kShellWindowCells / 2;
	int desiredOriginY = camCellY - kShellWindowCells / 2;

	bool originChanged = desiredOriginX != shellWindowCellX || desiredOriginY != shellWindowCellY;
	auto& terrainShadows = globals::features::terrainShadows;
	const std::string fillWorldspace = (terrainShadows.loaded && terrainShadows.IsHeightMapReady()) ? terrainShadows.cachedHeightmap->worldspace : std::string{};
	bool fillChanged = lastFillWorldspace != fillWorldspace;
	const uint32_t worldspace = activeWorldspace.load(std::memory_order_acquire);
	bool worldspaceChanged = worldspace != shellWindowWorldspace;
	const bool landBlend = settings.EnableSnowDeformation;
	const float landBlendStart = landBlend ? kLandBlendDepthStart : 1.0f;
	if (landBlend != shellWindowLandBlend || landBlendStart != shellWindowLandBlendStart)
		shellDataDirty.store(true, std::memory_order_release);
	RefreshLandMeshHeights();
	if (!originChanged && !fillChanged && !worldspaceChanged && !shellDataDirty.exchange(false, std::memory_order_acq_rel)) {
		if (shellFineDirty.exchange(false, std::memory_order_acq_rel) && shellTerrainTexture)
			BuildTerrainFineWindow();
		return;
	}
	shellFineDirty.store(false, std::memory_order_release);

	shellWindowCellX = desiredOriginX;
	shellWindowCellY = desiredOriginY;
	shellWindowWorldspace = worldspace;
	shellDataDirty.store(false, std::memory_order_release);

	shellUploadScratch.resize(size_t(kShellWindowDim) * kShellWindowDim * 4);
	shellBareScratch.assign(size_t(kShellWindowDim) * kShellWindowDim, 0);
	const std::vector<float> textureDepths = LandTextureDepthSnapshot();

	uint32_t statSnowTexels = 0;
	std::unordered_set<uint64_t> snowyCells;
	float statMinH = FLT_MAX;
	float statMaxH = -FLT_MAX;
	std::unordered_set<uint64_t> statCells;

	{
		const std::shared_lock lock(shellCellMutex);
		for (int ty = 0; ty < kShellWindowDim; ++ty) {
			int cellY = shellWindowCellY + ty / kShellTexelsPerCell;
			int vy = ty % kShellTexelsPerCell;
			const ShellCellData* rowCell = nullptr;
			uint64_t rowKey = ~0ull;
			bool rowSnowy = false;
			auto flushRowCell = [&] {
				if (!rowCell)
					return;
				statCells.insert(rowKey);
				if (rowSnowy)
					snowyCells.insert(rowKey);
			};
			for (int tx = 0; tx < kShellWindowDim; ++tx) {
				int cellX = shellWindowCellX + tx / kShellTexelsPerCell;
				int vx = tx % kShellTexelsPerCell;

				uint64_t key = CellKey(cellX, cellY);
				if (key != rowKey) {
					flushRowCell();
					rowSnowy = false;
					auto it = shellCells.find(key);
					rowCell = it != shellCells.end() && it->second.worldspaceID == worldspace ? &it->second : nullptr;
					rowKey = key;
				}

				float* texel = &shellUploadScratch[(size_t(ty) * kShellWindowDim + tx) * 4];
				if (rowCell) {
					uint32_t idx = uint32_t(vy) * 33 + uint32_t(vx);
					float rampDepth = 0.0f;
					float coverage = 0.0f;
					float snowWeight = 0.0f;
					for (uint32_t slot = 0; slot < kShellVertexLayers; ++slot) {
						const uint16_t texture = rowCell->layerTexture[idx][slot];
						if (texture >= textureDepths.size())
							continue;
						float w = rowCell->layerWeight[idx][slot] / 255.0f;
						rampDepth += w * textureDepths[texture];
						coverage += w;
						if (IsSnowDepth(textureDepths[texture]))
							snowWeight += w;
					}
					snowWeight = std::min(snowWeight, 1.0f);
					const bool snowy = rampDepth > 0.0f || (landBlend && snowWeight > 0.0f);
					rampDepth = LandBlendDepth(rampDepth, snowWeight);
					texel[0] = rowCell->height[idx];
					texel[1] = rampDepth;
					texel[2] = std::min(coverage, 1.0f);
					texel[3] = rowCell->vertexAO[idx] * (0.499f / 255.0f);
					shellBareScratch[size_t(ty) * kShellWindowDim + tx] = uint8_t(std::lround((1.0f - snowWeight) * 255.0f));

					rowSnowy |= snowy;
					statMinH = std::min(statMinH, texel[0]);
					statMaxH = std::max(statMaxH, texel[0]);
					if (texel[2] > 0.05f)
						statSnowTexels++;
				} else {
					texel[0] = kShellMissingHeight;
					texel[1] = 0.0f;
					texel[2] = 0.0f;
					texel[3] = 0.0f;
				}
			}
			flushRowCell();
		}
	}

	{
		const std::unique_lock snowLock(shellSnowyCellMutex);
		shellSnowyCells = std::move(snowyCells);
	}
	snowDepthVersion.fetch_add(1, std::memory_order_release);
	shellStatCellsInWindow = (uint32_t)statCells.size();
	shellStatSnowTexels = statSnowTexels;
	shellStatMinHeight = statMinH == FLT_MAX ? 0.0f : statMinH;
	shellStatMaxHeight = statMaxH == -FLT_MAX ? 0.0f : statMaxH;

	logger::debug("[SNOW DEFORMATION] Shell window rebuilt: origin cell ({}, {}), {} cells in window, {} snow texels, height range [{:.0f}, {:.0f}]",
		shellWindowCellX, shellWindowCellY, shellStatCellsInWindow, shellStatSnowTexels, shellStatMinHeight, shellStatMaxHeight);

	globals::d3d::context->UpdateSubresource(shellTerrainTexture->resource.get(), 0, nullptr,
		shellUploadScratch.data(), kShellWindowDim * 4 * sizeof(float), 0);
	if (shellTerrainBare)
		globals::d3d::context->UpdateSubresource(shellTerrainBare->resource.get(), 0, nullptr, shellBareScratch.data(), kShellWindowDim, 0);
	shellWindowLandBlend = landBlend;
	shellWindowLandBlendStart = landBlendStart;
	shellTerrainVersion++;

	FillShellWindowFromHeightmap();
	BuildTerrainFineWindow();
}

void SnowDeformation::BuildTerrainFineWindow()
{
	shellFineValid = false;
	auto* cs = GetSnowCS(kTerrainFineCS);
	if (!cs || !shellTerrainTexture || !shellTerrainTexture->srv)
		return;
	auto context = globals::d3d::context;
	if (!shellTerrainFine)
		shellTerrainFine = SD_MakeTexture2D(kShellFineDim, kShellFineDim, DXGI_FORMAT_R32_FLOAT, "SnowDeformation::ShellTerrainFine");
	if (!terrainFineCB)
		terrainFineCB = new ConstantBuffer(ConstantBufferDesc<TerrainFineCB>(), "SnowDeformation::TerrainFineCB");
	if (!shellTerrainFine->srv || !shellTerrainFine->uav)
		return;

	constexpr float cellSize = kShellVertexSpacing * kShellTexelsPerCell;
	auto eye = globals::game::frameBufferCached.GetCameraPosAdjust();
	const int camCellX = (int)std::floor(eye.x / cellSize);
	const int camCellY = (int)std::floor(eye.y / cellSize);
	shellFineOriginX = float(camCellX - kShellFineCells / 2) * cellSize;
	shellFineOriginY = float(camCellY - kShellFineCells / 2) * cellSize;

	TerrainFineCB cb{};
	cb.FineOriginWorld = { shellFineOriginX, shellFineOriginY };
	cb.WindowOriginWorld = { shellWindowCellX * cellSize, shellWindowCellY * cellSize };
	cb.FineDim = kShellFineDim;
	cb.WindowDim = kShellWindowDim;
	cb.TexelSize = kShellVertexSpacing;
	cb.FineTexel = kShellFineTexel;
	terrainFineCB->Update(cb);

	ID3D11Buffer* cbuf = terrainFineCB->CB();
	ID3D11ShaderResourceView* src = shellTerrainTexture->srv.get();
	ID3D11UnorderedAccessView* dst = shellTerrainFine->uav.get();
	context->CSSetConstantBuffers(1, 1, &cbuf);
	context->CSSetShaderResources(8, 1, &src);
	context->CSSetUnorderedAccessViews(5, 1, &dst, nullptr);
	context->CSSetShader(cs, nullptr, 0);
	globals::profiler->BeginPass("SnowDeformation::Ground - Field Bakes");
	context->Dispatch((kShellFineDim + 7) / 8, (kShellFineDim + 7) / 8, 1);
	globals::profiler->EndPass();
	ID3D11Buffer* nullCB = nullptr;
	ID3D11ShaderResourceView* nullSRV = nullptr;
	ID3D11UnorderedAccessView* nullUAV = nullptr;
	context->CSSetConstantBuffers(1, 1, &nullCB);
	context->CSSetShaderResources(8, 1, &nullSRV);
	context->CSSetUnorderedAccessViews(5, 1, &nullUAV, nullptr);

	context->CSSetShader(nullptr, nullptr, 0);

	{
		const int cell0X = camCellX - kShellFineCells / 2;
		const int cell0Y = camCellY - kShellFineCells / 2;
		const uint32_t worldspace = activeWorldspace.load(std::memory_order_acquire);
		uint32_t meshCells = 0;
		const std::shared_lock lock(shellCellMutex);
		for (int cy = 0; cy < kShellFineCells; ++cy)
			for (int cx = 0; cx < kShellFineCells; ++cx) {
				const uint64_t key = CellKey(cell0X + cx, cell0Y + cy);
				auto it = shellCells.find(key);
				if (it == shellCells.end() || it->second.worldspaceID != worldspace || it->second.fine.empty())
					continue;
				const uint32_t x0 = uint32_t(cx) * 128, y0 = uint32_t(cy) * 128;
				const uint32_t w = std::min<uint32_t>(129, kShellFineDim - x0), h = std::min<uint32_t>(129, kShellFineDim - y0);
				D3D11_BOX box{ x0, y0, 0, x0 + w, y0 + h, 1 };
				context->UpdateSubresource(shellTerrainFine->resource.get(), 0, &box, it->second.fine.data(), 129 * sizeof(float), 0);
				meshCells++;
			}
		shellFineMeshCells = meshCells;
	}
	shellFineValid = true;
}

ID3D11ShaderResourceView* SnowDeformation::GetLODTile(const std::string& a_worldspace, int a_cellX, int a_cellY)
{
	const uint64_t key = CellKey(a_cellX, a_cellY);
	if (auto it = lodTileCache.find(key); it != lodTileCache.end())
		return it->second.get();
	if (lodTileMisses.contains(key))
		return nullptr;

	std::string path = std::format("Data\\textures\\terrain\\{}\\{}.32.{}.{}.dds", a_worldspace, a_worldspace, a_cellX, a_cellY);
	std::wstring widePath(path.begin(), path.end());
	winrt::com_ptr<ID3D11ShaderResourceView> srv;
	if (FAILED(DirectX::CreateDDSTextureFromFileEx(globals::d3d::device, widePath.c_str(), 0,
			D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0,
			DirectX::DDS_LOADER_IGNORE_SRGB, nullptr, srv.put()))) {
		logger::info("[SNOW DEFORMATION] LOD tile missing: {}", path);
		lodTileMisses.insert(key);
		return nullptr;
	}
	logger::debug("[SNOW DEFORMATION] LOD tile loaded: {}", path);
	winrt::com_ptr<ID3D11Resource> tile;
	srv->GetResource(tile.put());
	Util::SetResourceName(tile.get(), "SnowDeformation::LODTile");
	lodTileCache[key] = srv;
	return srv.get();
}

void SnowDeformation::FillShellWindowFromHeightmap()
{
	auto& terrainShadows = globals::features::terrainShadows;
	if (!terrainShadows.loaded || !terrainShadows.texHeightMap || !terrainShadows.IsHeightMapReady()) {
		lastFillWorldspace.clear();
		return;
	}
	auto cs = GetSnowCS(kWindowFillCS);
	if (!cs || !shellTerrainTexture->uav)
		return;

	const auto* heightmap = terrainShadows.cachedHeightmap;
	if (lastFillWorldspace != heightmap->worldspace) {
		lodTileCache.clear();
		lodTileMisses.clear();
	}

	WindowFillCB cbData{};
	constexpr float cellSize = kShellVertexSpacing * kShellTexelsPerCell;
	cbData.WindowOriginWorld = { shellWindowCellX * cellSize, shellWindowCellY * cellSize };
	cbData.TexelSize = kShellVertexSpacing;
	cbData.WindowDim = kShellWindowDim;
	cbData.HeightMapScale = { 1.0f / (heightmap->pos1.x - heightmap->pos0.x), 1.0f / (heightmap->pos1.y - heightmap->pos0.y) };
	cbData.HeightMapOffset = { -heightmap->pos0.x * cbData.HeightMapScale.x, -heightmap->pos0.y * cbData.HeightMapScale.y };
	cbData.HeightRange = { heightmap->pos0.z, heightmap->pos1.z };
	cbData.SnowDepthUnits = std::max(settings.SnowClassDepths[3], 0.0f);

	constexpr int kTileCells = 32;
	const int tileX0 = (int)std::floor((float)shellWindowCellX / kTileCells) * kTileCells;
	const int tileY0 = (int)std::floor((float)shellWindowCellY / kTileCells) * kTileCells;
	cbData.LODTileBase = { tileX0 * cellSize, tileY0 * cellSize };
	cbData.LODTileSpan = kTileCells * cellSize;
	cbData.LODSnowSensitivity = std::clamp(settings.LODSnowSensitivity, 0.0f, 1.0f);
	ID3D11ShaderResourceView* tileSRVs[4] = {};
	for (int tileI = 0; tileI < 4; ++tileI) {
		tileSRVs[tileI] = GetLODTile(heightmap->worldspace, tileX0 + (tileI & 1) * kTileCells, tileY0 + (tileI >> 1) * kTileCells);
		(&cbData.LODTileValid.x)[tileI] = tileSRVs[tileI] ? 1.0f : 0.0f;
	}
	windowFillCB->Update(cbData);

	auto context = globals::d3d::context;
	ID3D11Buffer* cb = windowFillCB->CB();
	ID3D11ShaderResourceView* fillSRVs[5] = { terrainShadows.texHeightMap->srv.get(), tileSRVs[0], tileSRVs[1], tileSRVs[2], tileSRVs[3] };
	ID3D11UnorderedAccessView* windowUAV = shellTerrainTexture->uav.get();
	ID3D11SamplerState* sampler = shellLinearSampler.get();
	context->CSSetConstantBuffers(0, 1, &cb);
	context->CSSetShaderResources(0, 5, fillSRVs);
	context->CSSetSamplers(0, 1, &sampler);
	context->CSSetUnorderedAccessViews(0, 1, &windowUAV, nullptr);
	context->CSSetShader(cs, nullptr, 0);
	globals::profiler->BeginPass("SnowDeformation::Ground - Field Bakes");
	context->Dispatch((kShellWindowDim + 7) / 8, (kShellWindowDim + 7) / 8, 1);
	globals::profiler->EndPass();
	shellTerrainVersion++;

	ID3D11Buffer* nullCB = nullptr;
	ID3D11ShaderResourceView* nullSRVs[5] = {};
	ID3D11UnorderedAccessView* nullUAV = nullptr;
	ID3D11SamplerState* nullSampler = nullptr;
	context->CSSetConstantBuffers(0, 1, &nullCB);
	context->CSSetShaderResources(0, 5, nullSRVs);
	context->CSSetSamplers(0, 1, &nullSampler);
	context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	context->CSSetShader(nullptr, nullptr, 0);

	lastFillWorldspace = heightmap->worldspace;
}

void SnowDeformation::BSLightingShader_SetupMaterial(RE::BSLightingShaderMaterialBase const* material)
{
	auto state = globals::state;

	state->permutationData.ExtraFeatureDescriptor &= ~uint(State::ExtraFeatureDescriptors::SnowLandIsSnowMask);

	if (material == nullptr || !settings.EnableSnowDeformation)
		return;

	const auto feature = material->GetFeature();
	const bool landFeature = feature == RE::BSShaderMaterial::Feature::kMultiTexLand ||
	                         feature == RE::BSShaderMaterial::Feature::kMultiTexLandLODBlend ||
	                         feature == static_cast<RE::BSShaderMaterial::Feature>(33);
	if (!landFeature)
		return;
	uint8_t mask = 0;
	{
		const std::shared_lock lock(snowMaskMutex);
		auto it = snowMasks.find(reinterpret_cast<uintptr_t>(material));
		if (it == snowMasks.end()) {
			return;
		}
		mask = it->second;
	}

	state->permutationData.ExtraFeatureDescriptor |= uint32_t(mask) << kSnowLandIsSnowShift;

	if (mask != 0 && LandRecolorActive()) {
		auto* context = globals::d3d::context;
		ID3D11ShaderResourceView* albedoSRV = shellSnowDiffuseSRV.get();
		ID3D11ShaderResourceView* normalSRV = shellSnowNormalSRV.get();
		ID3D11ShaderResourceView* rmaosSRV = shellSnowRmaosSRV.get();
		context->PSSetShaderResources(kHorizonSnowAlbedoSlot, 1, &albedoSRV);
		context->PSSetShaderResources(kHorizonSnowNormalSlot, 1, &normalSRV);
		context->PSSetShaderResources(kHorizonSnowRmaosSlot, 1, &rmaosSRV);
		ID3D11ShaderResourceView* bermSRV = GetBermFieldSRV();
		context->PSSetShaderResources(kLandBermSlot, 1, &bermSRV);
	}
}

struct SD_TESObjectLAND_SetupMaterial
{
	static bool thunk(RE::TESObjectLAND* land)
	{
		bool result = func(land);

		auto& snowDeformation = globals::features::snowDeformation;
		if (result && snowDeformation.loaded)
			snowDeformation.TESObjectLAND_SetupMaterial(land);

		return result;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct SD_BSLightingShader_SetupMaterial
{
	static void thunk(RE::BSLightingShader* shader, RE::BSLightingShaderMaterialBase const* material)
	{
		func(shader, material);

		auto& snowDeformation = globals::features::snowDeformation;
		if (snowDeformation.loaded)
			snowDeformation.BSLightingShader_SetupMaterial(material);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

void SnowDeformation::PostPostLoad()
{
	logger::info("[SNOW DEFORMATION] Hooking TESObjectLAND");
	stl::detour_thunk<SD_TESObjectLAND_SetupMaterial>(REL::RelocationID(18368, 18791));

	logger::info("[SNOW DEFORMATION] Hooking BSLightingShader::SetupMaterial");
	stl::write_vfunc<0x4, SD_BSLightingShader_SetupMaterial>(RE::VTABLE_BSLightingShader[0]);

	InstallStaticsCaptureHook();
	InstallCameraHook();
	InstallLandscapeSnow();
}

bool SnowDeformation::WindowHasSnow(float a_halfExtentUnits, bool a_unknownIsSnowy, uint32_t* a_verdictOut) const
{
	if (InInteriorCell()) {
		if (a_verdictOut)
			*a_verdictOut = kSnowGateBare;
		return false;
	}
	constexpr float kCellSize = kShellVertexSpacing * 32.0f;
	constexpr float kResumeMargin = kCellSize * 2.0f;
	const float centreX = windowOrigin.x + deformWorldSize * 0.5f;
	const float centreY = windowOrigin.y + deformWorldSize * 0.5f;
	const float reach = a_halfExtentUnits + kResumeMargin;
	const float minX = centreX - reach;
	const float minY = centreY - reach;
	const float maxX = centreX + reach;
	const float maxY = centreY + reach;

	int cellMinX = (int)std::floor(minX / kCellSize);
	int cellMaxX = (int)std::floor(maxX / kCellSize);
	int cellMinY = (int)std::floor(minY / kCellSize);
	int cellMaxY = (int)std::floor(maxY / kCellSize);
	{
		int sqMinX, sqMinY, sqMaxX, sqMaxY;
		if (LoadedCellSquare(sqMinX, sqMinY, sqMaxX, sqMaxY)) {
			cellMinX = std::max(cellMinX, sqMinX);
			cellMaxX = std::min(cellMaxX, sqMaxX);
			cellMinY = std::max(cellMinY, sqMinY);
			cellMaxY = std::min(cellMaxY, sqMaxY);
		}
	}

	const uint32_t verdict = CellWalkSnowVerdict(cellMinX, cellMinY, cellMaxX, cellMaxY, a_unknownIsSnowy);
	if (a_verdictOut)
		*a_verdictOut = verdict;
	return verdict != kSnowGateBare;
}

bool SnowDeformation::DeformationWindowHasReaders() const
{
	if (!GroundShellsSuspended())
		return DeformationWindowHasSnow();
	deformSnowVerdict = kSnowGateBare;
	return false;
}

bool SnowDeformation::LoadedCellSquare(int& a_minX, int& a_minY, int& a_maxX, int& a_maxY) const
{
	auto* player = globals::game::player;
	if (!player)
		return false;
	static const int uGrids = [] {
		if (auto* ini = RE::INISettingCollection::GetSingleton())
			if (auto* setting = ini->GetSetting("uGridsToLoad:General"))
				return std::max((int)setting->GetInteger(), 3);
		return 5;
	}();
	const auto pos = player->GetPosition();
	const int cellX = (int)std::floor(pos.x / 4096.0f);
	const int cellY = (int)std::floor(pos.y / 4096.0f);
	const int half = (uGrids - 1) / 2;
	a_minX = cellX - half;
	a_maxX = cellX + half;
	a_minY = cellY - half;
	a_maxY = cellY + half;
	return true;
}

uint32_t SnowDeformation::CellRangeSnowVerdict(int a_minX, int a_minY, int a_maxX, int a_maxY) const
{
	if (a_minX > a_maxX || a_minY > a_maxY)
		return kSnowGateBare;
	return CellWalkSnowVerdict(a_minX, a_minY, a_maxX, a_maxY, true);
}

uint32_t SnowDeformation::CellWalkSnowVerdict(int a_minX, int a_minY, int a_maxX, int a_maxY, bool a_unknownIsSnowy) const
{
	{
		const std::shared_lock lock(shellSnowyCellMutex);
		if (shellSnowyCells.empty() && shellStatCellsInWindow == 0)
			return kSnowGateUnknown;
		for (int cy = a_minY; cy <= a_maxY; ++cy) {
			for (int cx = a_minX; cx <= a_maxX; ++cx) {
				const uint64_t key = CellKey(cx, cy);
				if (shellSnowyCells.find(key) != shellSnowyCells.end())
					return kSnowGateSnowy;
			}
		}
	}
	if (!a_unknownIsSnowy)
		return kSnowGateBare;
	const uint32_t worldspace = activeWorldspace.load(std::memory_order_acquire);
	const std::shared_lock lock(shellCellMutex);
	for (int cy = a_minY; cy <= a_maxY; ++cy) {
		for (int cx = a_minX; cx <= a_maxX; ++cx) {
			const uint64_t key = CellKey(cx, cy);
			if (auto it = shellCells.find(key); it != shellCells.end() && it->second.worldspaceID == worldspace)
				continue;
			if (auto ft = shellFillerCells.find(key); ft != shellFillerCells.end() && ft->second == worldspace)
				continue;
			return kSnowGateUnknown;
		}
	}
	return kSnowGateBare;
}

bool SnowDeformation::SunCascadeFootprints(FootprintPlanes* a_out, size_t& a_count) const
{
	a_count = 0;
	auto* shadowSceneNode = globals::game::smState->shadowSceneNode[0];
	auto* sun = shadowSceneNode ? shadowSceneNode->GetRuntimeData().sunShadowDirLight : nullptr;
	if (!sun)
		return false;
	const auto& descriptors = sun->GetRuntimeData().shadowmapDescriptors;
	for (uint32_t i = 0; i < descriptors.size() && a_count < kMaxSunFootprints; ++i) {
		const auto& L = reinterpret_cast<const DirectX::XMFLOAT4X4&>(descriptors[i].lightTransform);
		auto& planes = a_out[a_count++];
		for (int k = 0; k < 2; ++k) {
			planes[2 * k] = { L.m[0][k], L.m[1][k], L.m[2][k], L.m[3][k] };
			planes[2 * k + 1] = { L.m[0][3] - L.m[0][k], L.m[1][3] - L.m[1][k], L.m[2][3] - L.m[2][k], L.m[3][3] - L.m[3][k] };
		}
	}
	return a_count > 0;
}

bool SnowDeformation::SnowCellsMeetFootprints(const FootprintPlanes* a_planes, size_t a_count, float a_rise, bool a_soon)
{
	int sqMinX, sqMinY, sqMaxX, sqMaxY;
	if (!LoadedCellSquare(sqMinX, sqMinY, sqMaxX, sqMaxY))
		return true;
	constexpr float kCellSize = kShellVertexSpacing * 32.0f;
	const uint32_t stamp = snowDepthVersion.load(std::memory_order_acquire);
	if (viewGateCellZVersion != stamp) {
		viewGateCellZ.clear();
		viewGateCellZVersion = stamp;
	}
	const uint32_t worldspace = activeWorldspace.load(std::memory_order_acquire);
	const std::shared_lock snowLock(shellSnowyCellMutex);
	const std::shared_lock cellLock(shellCellMutex);
	if (shellSnowyCells.empty() && shellStatCellsInWindow == 0)
		return true;
	auto landRange = [&](int a_cx, int a_cy) -> const std::pair<float, float>* {
		const uint64_t key = CellKey(a_cx, a_cy);
		if (auto z = viewGateCellZ.find(key); z != viewGateCellZ.end())
			return &z->second;
		auto it = shellCells.find(key);
		if (it == shellCells.end() || it->second.worldspaceID != worldspace)
			return nullptr;
		const auto [lo, hi] = std::minmax_element(it->second.height.begin(), it->second.height.end());
		return &viewGateCellZ.try_emplace(key, std::pair{ *lo - kViewGateMarginZ, *hi + kViewGateMarginZ }).first->second;
	};
	std::optional<std::pair<float, float>> known;
	auto knownRange = [&]() -> const std::pair<float, float>& {
		if (!known) {
			known = std::pair{ std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest() };
			for (int cy = sqMinY - 1; cy <= sqMaxY + 1; ++cy)
				for (int cx = sqMinX - 1; cx <= sqMaxX + 1; ++cx)
					if (const auto* z = landRange(cx, cy))
						known = std::pair{ std::min(known->first, z->first), std::max(known->second, z->second) };
		}
		return *known;
	};
	auto blockRange = [&](int a_cx, int a_cy) {
		std::pair<float, float> r{ std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest() };
		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				const auto* z = landRange(a_cx + dx, a_cy + dy);
				const auto& use = z ? *z : knownRange();
				r = { std::min(r.first, use.first), std::max(r.second, use.second) };
			}
		}
		return r;
	};
	const float grow = kShellVertexSpacing + a_rise;
	auto meets = [&](int a_cx, int a_cy, const std::pair<float, float>& a_z) {
		if (a_z.first > a_z.second)
			return true;
		const double box[2][3] = { { a_cx * double(kCellSize) - grow, a_cy * double(kCellSize) - grow, a_z.first },
			{ (a_cx + 1) * double(kCellSize) + grow, (a_cy + 1) * double(kCellSize) + grow, double(a_z.second) + a_rise } };
		for (size_t f = 0; f < a_count; ++f)
			if (BoxInsidePlanes(box, a_planes[f]))
				return true;
		return false;
	};
	const int ring = a_soon ? 1 : 0;
	for (int cy = sqMinY - ring; cy <= sqMaxY + ring; ++cy) {
		for (int cx = sqMinX - ring; cx <= sqMaxX + ring; ++cx) {
			const uint64_t key = CellKey(cx, cy);
			if (shellSnowyCells.contains(key)) {
				if (meets(cx, cy, blockRange(cx, cy)))
					return true;
				continue;
			}
			if (!a_soon || cx < sqMinX || cx > sqMaxX || cy < sqMinY || cy > sqMaxY)
				continue;
			if (auto it = shellCells.find(key); it != shellCells.end() && it->second.worldspaceID == worldspace)
				continue;
			if (auto ft = shellFillerCells.find(key); ft != shellFillerCells.end() && ft->second == worldspace)
				continue;
			if (meets(cx, cy, knownRange()))
				return true;
		}
	}
	return false;
}

bool SnowDeformation::CascadeSnowSoon()
{
	int sqMinX, sqMinY, sqMaxX, sqMaxY;
	if (!LoadedCellSquare(sqMinX, sqMinY, sqMaxX, sqMaxY))
		return true;
	FootprintPlanes footprints[kMaxSunFootprints];
	size_t footprintCount = 0;
	if (SunCascadeFootprints(footprints, footprintCount))
		return SnowCellsMeetFootprints(footprints, footprintCount, LsRiseReach(), true);
	if (CellRangeSnowVerdict(sqMinX, sqMinY, sqMaxX, sqMaxY) != kSnowGateBare)
		return true;
	const std::shared_lock lock(shellSnowyCellMutex);
	for (int cy = sqMinY - 1; cy <= sqMaxY + 1; ++cy) {
		for (int cx = sqMinX - 1; cx <= sqMaxX + 1; ++cx) {
			const uint64_t key = CellKey(cx, cy);
			if (shellSnowyCells.contains(key))
				return true;
		}
	}
	return false;
}

uint32_t SnowDeformation::ViewSnowVerdict(int a_minX, int a_minY, int a_maxX, int a_maxY, const ShellCB& a_cb)
{
	const uint32_t verdict = CellRangeSnowVerdict(a_minX, a_minY, a_maxX, a_maxY);
	if (verdict != kSnowGateSnowy)
		return verdict;
	const auto& m = a_cb.CameraViewProj;
	const double cam[3] = { a_cb.CameraPosAdjust.x, a_cb.CameraPosAdjust.y, a_cb.CameraPosAdjust.z };
	std::array<double, 4> planes[5];
	const int rows[5] = { 0, 0, 1, 1, 3 };
	const double signs[5] = { 1.0, -1.0, 1.0, -1.0, 0.0 };
	for (int i = 0; i < 5; i++) {
		for (int c = 0; c < 4; c++)
			planes[i][c] = double(m.m[3][c]) + signs[i] * (rows[i] < 3 ? double(m.m[rows[i]][c]) : 0.0);
		planes[i][3] -= planes[i][0] * cam[0] + planes[i][1] * cam[1] + planes[i][2] * cam[2];
	}
	const uint32_t stamp = snowDepthVersion.load(std::memory_order_acquire);
	if (viewGateCellZVersion != stamp) {
		viewGateCellZ.clear();
		viewGateCellZVersion = stamp;
	}
	constexpr float kCellSize = kShellVertexSpacing * 32.0f;
	const uint32_t worldspace = activeWorldspace.load(std::memory_order_acquire);
	const std::shared_lock snowLock(shellSnowyCellMutex);
	const std::shared_lock cellLock(shellCellMutex);
	for (int cy = a_minY; cy <= a_maxY; ++cy) {
		for (int cx = a_minX; cx <= a_maxX; ++cx) {
			const uint64_t key = CellKey(cx, cy);
			if (!shellSnowyCells.contains(key))
				continue;
			auto [z, inserted] = viewGateCellZ.try_emplace(key, std::pair{ -1e9f, 1e9f });
			if (inserted) {
				auto it = shellCells.find(key);
				if (it == shellCells.end() || it->second.worldspaceID != worldspace)
					return kSnowGateSnowy;
				const auto [lo, hi] = std::minmax_element(it->second.height.begin(), it->second.height.end());
				z->second = { *lo - kViewGateMarginZ, *hi + kViewGateMarginZ };
			}
			const double box[2][3] = { { cx * kCellSize, cy * kCellSize, z->second.first },
				{ (cx + 1) * kCellSize, (cy + 1) * kCellSize, z->second.second } };
			if (BoxInsidePlanes(box, planes))
				return kSnowGateSnowy;
		}
	}
	return kSnowGateBare;
}

void SnowDeformation::CullUnseenCaptures(const ShellCB& a_cb)
{
	captureViewCulled = 0;
	if (capturedStatics.empty() || InInteriorCell())
		return;
	int sqMinX, sqMinY, sqMaxX, sqMaxY;
	if (!LoadedCellSquare(sqMinX, sqMinY, sqMaxX, sqMaxY))
		return;
	constexpr float kCellSize = kShellVertexSpacing * 32.0f;
	const uint32_t stamp = snowDepthVersion.load(std::memory_order_acquire);
	if (stamp != squareFloorVersion || sqMinX != squareFloorKey[0] || sqMinY != squareFloorKey[1] ||
		sqMaxX != squareFloorKey[2] || sqMaxY != squareFloorKey[3]) {
		squareFloorVersion = stamp;
		squareFloorKey[0] = sqMinX;
		squareFloorKey[1] = sqMinY;
		squareFloorKey[2] = sqMaxX;
		squareFloorKey[3] = sqMaxY;
		squareFloorValid = true;
		squareFloorZ = std::numeric_limits<float>::max();
		const uint32_t worldspace = activeWorldspace.load(std::memory_order_acquire);
		const std::shared_lock lock(shellCellMutex);
		for (int cy = sqMinY; cy <= sqMaxY && squareFloorValid; ++cy) {
			for (int cx = sqMinX; cx <= sqMaxX; ++cx) {
				const uint64_t key = CellKey(cx, cy);
				auto it = shellCells.find(key);
				if (it == shellCells.end() || it->second.worldspaceID != worldspace) {
					squareFloorValid = false;
					break;
				}
				squareFloorZ = std::min(squareFloorZ, *std::min_element(it->second.height.begin(), it->second.height.end()));
			}
		}
	}
	if (!squareFloorValid)
		return;
	const double cam[3] = { a_cb.CameraPosAdjust.x, a_cb.CameraPosAdjust.y, a_cb.CameraPosAdjust.z };
	const double drop = cam[2] - squareFloorZ;
	if (drop <= 0.0)
		return;
	const auto& m = a_cb.CameraViewProj;
	using V = std::array<double, 3>;
	auto row = [&](int a_r, double a_s) {
		return V{ double(m.m[3][0]) + a_s * double(m.m[a_r][0]), double(m.m[3][1]) + a_s * double(m.m[a_r][1]), double(m.m[3][2]) + a_s * double(m.m[a_r][2]) };
	};
	const V sides[4] = { row(0, 1.0), row(0, -1.0), row(1, 1.0), row(1, -1.0) };
	const V w = { double(m.m[3][0]), double(m.m[3][1]), double(m.m[3][2]) };
	const int pairs[4][2] = { { 0, 2 }, { 0, 3 }, { 1, 2 }, { 1, 3 } };
	double lo[3] = { cam[0], cam[1], cam[2] };
	double hi[3] = { cam[0], cam[1], cam[2] };
	for (const auto& pr : pairs) {
		const V& a = sides[pr[0]];
		const V& b = sides[pr[1]];
		V d = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
		if (d[0] * w[0] + d[1] * w[1] + d[2] * w[2] < 0.0)
			d = { -d[0], -d[1], -d[2] };
		const double len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
		if (len < 1e-12 || d[2] > -1e-3 * len)
			return;
		const double t = drop / -d[2];
		for (int c = 0; c < 3; c++) {
			lo[c] = std::min(lo[c], cam[c] + d[c] * t);
			hi[c] = std::max(hi[c], cam[c] + d[c] * t);
		}
	}
	if (lo[0] < sqMinX * double(kCellSize) || lo[1] < sqMinY * double(kCellSize) ||
		hi[0] > (sqMaxX + 1) * double(kCellSize) || hi[1] > (sqMaxY + 1) * double(kCellSize))
		return;
	for (int c = 0; c < 3; c++) {
		lo[c] -= kViewGateMarginZ;
		hi[c] += kViewGateMarginZ;
	}
	auto unseen = [&](const CapturedSnowStatic& a_cap) {
		const auto& bound = a_cap.geometry->worldBound;
		if (!(bound.radius > 0.0f))
			return false;
		const double centre[3] = { bound.center.x, bound.center.y, bound.center.z };
		double dist2 = 0.0;
		for (int c = 0; c < 3; c++) {
			const double off = centre[c] < lo[c] ? lo[c] - centre[c] : (centre[c] > hi[c] ? centre[c] - hi[c] : 0.0);
			dist2 += off * off;
		}
		return dist2 > double(bound.radius) * double(bound.radius);
	};
	bool loadedUnseen = true;
	for (const auto& cap : capturedStatics)
		if (!unseen(cap)) {
			loadedUnseen = false;
			break;
		}
	const size_t before = capturedStatics.size();
	if (loadedUnseen)
		capturedStatics.clear();
	captureViewCulled = uint32_t(before - capturedStatics.size());
}

bool SnowDeformation::GroundNearHasSnow(float a_x, float a_y)
{
	if (groundSnowFrame != globals::state->frameCount) {
		groundSnowFrame = globals::state->frameCount;
		groundSnowFrameInterior = InInteriorCell();
		groundSnowFrameHasSquare = LoadedCellSquare(groundSnowFrameSquare[0], groundSnowFrameSquare[1], groundSnowFrameSquare[2], groundSnowFrameSquare[3]);
	}
	if (groundSnowFrameInterior || !groundSnowFrameHasSquare)
		return true;
	const int sqMinX = groundSnowFrameSquare[0];
	const int sqMinY = groundSnowFrameSquare[1];
	const int sqMaxX = groundSnowFrameSquare[2];
	const int sqMaxY = groundSnowFrameSquare[3];
	const uint32_t stamp = snowDepthVersion.load(std::memory_order_acquire);
	const bool squareMoved = sqMinX != groundSnowCacheSquare[0] || sqMinY != groundSnowCacheSquare[1] ||
	                         sqMaxX != groundSnowCacheSquare[2] || sqMaxY != groundSnowCacheSquare[3];
	if (groundSnowCellCacheVersion != stamp || squareMoved) {
		groundSnowCellCache.clear();
		groundSnowCellCacheVersion = stamp;
		groundSnowCacheSquare[0] = sqMinX;
		groundSnowCacheSquare[1] = sqMinY;
		groundSnowCacheSquare[2] = sqMaxX;
		groundSnowCacheSquare[3] = sqMaxY;
		groundSnowSquareValid = false;
	}
	constexpr float kCellSize = kShellVertexSpacing * 32.0f;
	const int cellX = (int)std::floor(a_x / kCellSize);
	const int cellY = (int)std::floor(a_y / kCellSize);
	const int minX = std::max(cellX - 2, sqMinX);
	const int maxX = std::min(cellX + 2, sqMaxX);
	const int minY = std::max(cellY - 2, sqMinY);
	const int maxY = std::min(cellY + 2, sqMaxY);
	if (minX > maxX || minY > maxY) {
		if (!groundSnowSquareValid) {
			groundSnowSquareVerdict = (uint8_t)CellRangeSnowVerdict(sqMinX, sqMinY, sqMaxX, sqMaxY);
			groundSnowSquareValid = true;
		}
		return groundSnowSquareVerdict != kSnowGateBare;
	}
	const uint64_t key = CellKey(cellX, cellY);
	auto [it, inserted] = groundSnowCellCache.try_emplace(key, uint8_t(kSnowGateUnknown));
	if (inserted)
		it->second = (uint8_t)CellRangeSnowVerdict(minX, minY, maxX, maxY);
	return it->second != kSnowGateBare;
}

bool SnowDeformation::SampleShellSurface(float a_x, float a_y, float& a_surface) const
{
	if (shellWindowCellX == INT_MIN || shellUploadScratch.size() < size_t(kShellWindowDim) * kShellWindowDim * 4)
		return false;
	const float cellSize = float(kShellVertexSpacing) * kShellTexelsPerCell;
	float tx = (a_x - shellWindowCellX * cellSize) / kShellVertexSpacing;
	float ty = (a_y - shellWindowCellY * cellSize) / kShellVertexSpacing;
	if (tx < 0.0f || ty < 0.0f || tx >= kShellWindowDim - 1 || ty >= kShellWindowDim - 1)
		return false;
	const int x0 = int(tx), y0 = int(ty);
	const float fx = tx - x0, fy = ty - y0;
	float height = 0.0f, depth = 0.0f, coverage = 0.0f;
	for (int j = 0; j < 2; ++j)
		for (int i = 0; i < 2; ++i) {
			const float* t = &shellUploadScratch[(size_t(y0 + j) * kShellWindowDim + (x0 + i)) * 4];
			if (t[0] < -50000.0f)
				return false;
			const float w = (i ? fx : 1.0f - fx) * (j ? fy : 1.0f - fy);
			height += w * t[0];
			depth += w * std::max(t[1], 0.0f);
			coverage += w * t[2];
		}
	if (coverage < 0.05f || depth <= 0.0f)
		return false;
	a_surface = height + depth * GetAccumulationDepthScale();
	return true;
}

void SnowDeformation::ClampCameraAboveSnow()
{
	auto* camera = RE::PlayerCamera::GetSingleton();
	auto* player = globals::game::player;
	if (!settings.CameraAboveSnow || !settings.EnableSnowDeformation || !player || !player->Is3DLoaded())
		return;
	if (!camera || !camera->cameraRoot || !camera->currentState)
		return;
	const uint32_t thirdPerson = REL::Module::IsVR() ? RE::CameraState::kVRThirdPerson : RE::CameraState::kThirdPerson;
	const uint32_t state = camera->currentState->id;
	const bool followState = (state >= thirdPerson && state <= thirdPerson + 3) || state == RE::CameraState::kPCTransition;
	const bool down = player->IsInRagdollState() || player->IsDead() ||
	                  player->AsActorState()->GetKnockState() != RE::KNOCK_STATE_ENUM::kNormal;
	if (!followState && !(down && state != RE::CameraState::kFree))
		return;
	auto* root = camera->cameraRoot.get();
	const RE::NiPoint3 cam = root->world.translate;
	const float clearance = 24.0f + std::clamp(settings.UndulationStrength, 0.0f, 32.0f);
	float surface;
	if (!SampleShellSurface(cam.x, cam.y, surface)) {
		return;
	}
	if (cam.z >= surface + clearance) {
		return;
	}
	RE::NiPoint3 target = cam;
	if (down) {
		target.z = surface + clearance;
	} else {
		const RE::NiPoint3 anchor = player->GetPosition() + RE::NiPoint3{ 0.0f, 0.0f, 120.0f };
		float anchorSurface;
		if (SampleShellSurface(anchor.x, anchor.y, anchorSurface) && anchor.z < anchorSurface + clearance)
			return;
		float lo = 0.0f, hi = 1.0f;
		for (int i = 0; i < 10; ++i) {
			const float mid = (lo + hi) * 0.5f;
			const RE::NiPoint3 p = anchor + (cam - anchor) * mid;
			float s;
			if (!SampleShellSurface(p.x, p.y, s) || p.z >= s + clearance)
				lo = mid;
			else
				hi = mid;
		}
		target = anchor + (cam - anchor) * lo;
	}
	root->local.translate += target - cam;
	RE::NiUpdateData data{ 0.0f, RE::NiUpdateData::Flag::kNone };
	root->Update(data);
}

struct SD_PlayerCamera_Update
{
	static void thunk(RE::PlayerCamera* a_camera)
	{
		func(a_camera);
		auto& snowDeformation = globals::features::snowDeformation;
		if (snowDeformation.loaded)
			snowDeformation.ClampCameraAboveSnow();
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

void SnowDeformation::InstallCameraHook()
{
	logger::info("[SNOW DEFORMATION] Hooking PlayerCamera::Update");
	stl::detour_thunk<SD_PlayerCamera_Update>(REL::RelocationID(49852, 50784));
}
