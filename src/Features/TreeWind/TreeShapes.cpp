#include "TreeShapes.h"

#include <DirectXPackedVector.h>
#include <cfloat>
#include <numeric>

#include "Globals.h"
#include "State.h"
#include "Utils/D3D.h"

namespace TreeShapes
{
	namespace
	{
		// A tall LOD pine in world units sways at scale 1
		constexpr float kReferenceTreeHeight = 1600.0f;
		// Crown vertices sit within this many model units (at LOD4 scale) of their trunk
		constexpr float kCrownRadius = 100.0f;
		constexpr float kCellSize = 128.0f;
		// Bend data of a shape not drawn for this many frames is freed
		constexpr uint kEvictFrames = 600;

		bool IsTreeShapeName(const RE::BSGeometry* a_geometry)
		{
			// DynDOLOD names the tree LOD shape of an object LOD block 'treepassthru', whatever billboard type made it
			const char* name = a_geometry->name.c_str();
			return name && std::strcmp(name, "treepassthru") == 0;
		}

		bool IsAllQuads(RE::BSGeometry* a_geometry)
		{
			auto triShape = a_geometry->AsTriShape();
			if (!triShape)
				return false;
			const auto& data = triShape->GetTrishapeRuntimeData();
			return data.triangleCount > 0 && data.vertexCount == 2u * data.triangleCount;
		}

		// Sway scale from the tree height: the offset is in model units, so LOD8 and LOD16 shapes are scaled back to LOD4
		float TreeScale(float a_heightModel, float a_worldScale, float a_random)
		{
			const float worldScale = std::max(a_worldScale, 0.01f);
			return std::clamp(a_heightModel * worldScale / kReferenceTreeHeight, 0.25f, 1.25f) * (0.8f + 0.4f * a_random) * (4.0f / worldScale);
		}

		float Hash01(float a_x, float a_y)
		{
			uint32_t h = static_cast<uint32_t>(static_cast<int32_t>(std::floor(a_x))) * 73856093u ^
			             static_cast<uint32_t>(static_cast<int32_t>(std::floor(a_y))) * 19349663u;
			h ^= h >> 13;
			h *= 0x5bd1e995u;
			h ^= h >> 15;
			return static_cast<float>(h & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
		}

		uint64_t BlockKey(const RE::BSGeometry* a_geometry)
		{
			const auto x = static_cast<int32_t>(a_geometry->world.translate.x);
			const auto y = static_cast<int32_t>(a_geometry->world.translate.y);
			return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) | static_cast<uint32_t>(y);
		}

		int64_t CellKey(int a_cx, int a_cy)
		{
			return (static_cast<int64_t>(a_cx) << 32) ^ static_cast<uint32_t>(a_cy);
		}

		int CellOf(float a_value)
		{
			return static_cast<int>(std::floor(a_value / kCellSize));
		}

		struct VertexView
		{
			const uint8_t* raw = nullptr;
			uint32_t stride = 0;
			uint32_t count = 0;
			const void* vertexBuffer = nullptr;

			RE::NiPoint3 operator()(uint32_t a_index) const
			{
				const auto* f = reinterpret_cast<const float*>(raw + static_cast<size_t>(a_index) * stride);
				return { f[0], f[1], f[2] };
			}
		};

		bool GetVertices(RE::BSGeometry* a_geometry, VertexView& a_out)
		{
			auto rendererData = a_geometry->GetGeometryRuntimeData().rendererData;
			auto triShape = a_geometry->AsTriShape();
			if (!rendererData || !rendererData->rawVertexData || !rendererData->vertexBuffer || !triShape)
				return false;

			// Positions must be full-precision float4 at offset 0
			auto& desc = rendererData->vertexDesc;
			const uint32_t stride = desc.GetSize();
			if (stride < 16 || desc.GetAttributeOffset(RE::BSGraphics::Vertex::Attribute::VA_TEXCOORD0) != 16)
				return false;

			a_out = { rendererData->rawVertexData, stride, triShape->GetTrishapeRuntimeData().vertexCount, rendererData->vertexBuffer };
			return a_out.count > 0;
		}

		// Connected pieces of a shape from its CPU index copy: the root vertex of each vertex's piece.
		// Without an index copy every vertex is its own piece.
		std::vector<uint32_t> PieceRoots(const uint16_t* a_indices, uint32_t a_triangleCount, uint32_t a_vertexCount)
		{
			std::vector<uint32_t> parent(a_vertexCount);
			std::iota(parent.begin(), parent.end(), 0u);
			const auto find = [&](uint32_t a) {
				while (parent[a] != a) {
					parent[a] = parent[parent[a]];
					a = parent[a];
				}
				return a;
			};
			for (uint32_t t = 0; a_indices && t < a_triangleCount; ++t) {
				const uint32_t a = a_indices[t * 3], b = a_indices[t * 3 + 1], c = a_indices[t * 3 + 2];
				if (a >= a_vertexCount || b >= a_vertexCount || c >= a_vertexCount)
					continue;
				parent[find(b)] = find(a);
				parent[find(c)] = find(a);
			}
			for (uint32_t i = 0; i < a_vertexCount; ++i)
				parent[i] = find(i);
			return parent;
		}

		void AddCard(BendWeights::Block& a_block, BendWeights::TrunkCard a_card, float a_worldScale)
		{
			// Crossed card pairs of one tree share its random and scale
			const BendWeights::TrunkCard* twin = nullptr;
			if (const auto it = a_block.grid.find(CellKey(CellOf(a_card.x), CellOf(a_card.y))); it != a_block.grid.end()) {
				for (const int i : it->second) {
					const auto& other = a_block.cards[i];
					if ((other.x - a_card.x) * (other.x - a_card.x) + (other.y - a_card.y) * (other.y - a_card.y) < 64.0f) {
						twin = &other;
						break;
					}
				}
			}
			if (twin) {
				a_card.random = twin->random;
				a_card.scale = twin->scale;
			} else {
				a_card.random = Hash01(a_card.x, a_card.y);
				a_card.scale = TreeScale(a_card.top - a_card.base, a_worldScale, Hash01(a_card.y * 1.37f + 11.0f, a_card.x * 0.71f - 5.0f));
			}
			a_block.grid[CellKey(CellOf(a_card.x), CellOf(a_card.y))].push_back(static_cast<int>(a_block.cards.size()));
			a_block.cards.push_back(a_card);
		}

		const BendWeights::TrunkCard* NearestCard(const BendWeights::Block& a_block, float a_x, float a_y)
		{
			const int cx = CellOf(a_x), cy = CellOf(a_y);
			const BendWeights::TrunkCard* best = nullptr;
			float bestDistance = FLT_MAX;
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					const auto it = a_block.grid.find(CellKey(cx + dx, cy + dy));
					if (it == a_block.grid.end())
						continue;
					for (const int i : it->second) {
						const auto& card = a_block.cards[i];
						const float distance = (a_x - card.x) * (a_x - card.x) + (a_y - card.y) * (a_y - card.y);
						if (distance < bestDistance) {
							bestDistance = distance;
							best = &card;
						}
					}
				}
			}
			return best;
		}

		// Nearest card within a_maxDistance model units, or nullptr: a piece far from any trunk card stays still
		const BendWeights::TrunkCard* NearestCardWithin(const BendWeights::Block& a_block, float a_x, float a_y, float a_maxDistance)
		{
			const int cx = CellOf(a_x), cy = CellOf(a_y);
			const int reach = static_cast<int>(std::ceil(a_maxDistance / kCellSize));
			const BendWeights::TrunkCard* best = nullptr;
			float bestDistance = a_maxDistance * a_maxDistance;
			for (int dy = -reach; dy <= reach; ++dy) {
				for (int dx = -reach; dx <= reach; ++dx) {
					const auto it = a_block.grid.find(CellKey(cx + dx, cy + dy));
					if (it == a_block.grid.end())
						continue;
					for (const int i : it->second) {
						const auto& card = a_block.cards[i];
						const float distance = (a_x - card.x) * (a_x - card.x) + (a_y - card.y) * (a_y - card.y);
						if (distance < bestDistance) {
							bestDistance = distance;
							best = &card;
						}
					}
				}
			}
			return best;
		}

		// Is a_vertices[a_first .. a_first + 3] a trunk card? Two corners at the bottom and two at the top, the top
		// centred over the bottom and no wider, taller than wide. Used where tree mods merge the cards with 3D trunks
		// and stumps into one shape, so the shape is not all quads.
		bool IsCardQuad(const VertexView& a_vertices, uint32_t a_first)
		{
			RE::NiPoint3 p[4];
			for (uint32_t k = 0; k < 4; ++k)
				p[k] = a_vertices(a_first + k);
			// Trees are placed tilted by up to ~11 degrees, so sort by height: the two lowest corners are the bottom
			std::sort(std::begin(p), std::end(p), [](const RE::NiPoint3& a, const RE::NiPoint3& b) { return a.z < b.z; });
			const RE::NiPoint3* low = p;
			const RE::NiPoint3* high = p + 2;
			const float height = (high[0].z + high[1].z - low[0].z - low[1].z) * 0.5f;
			if (height < 32.0f || low[1].z - low[0].z > 0.1f * height || high[1].z - high[0].z > 0.1f * height)
				return false;
			const float widthLow = std::hypot(low[0].x - low[1].x, low[0].y - low[1].y);
			const float widthHigh = std::hypot(high[0].x - high[1].x, high[0].y - high[1].y);
			const float offsetX = (low[0].x + low[1].x - high[0].x - high[1].x) * 0.5f;
			const float offsetY = (low[0].y + low[1].y - high[0].y - high[1].y) * 0.5f;
			return widthLow > 0.5f && widthLow < height && widthHigh <= widthLow * 1.5f + 1.0f && std::hypot(offsetX, offsetY) < 0.25f * height + 1.0f;
		}

		struct QuadBounds
		{
			float base = FLT_MAX, top = -FLT_MAX, cx = 0.0f, cy = 0.0f;
		};

		QuadBounds GetQuadBounds(const VertexView& a_vertices, uint32_t a_first)
		{
			QuadBounds q;
			for (uint32_t k = 0; k < 4; ++k) {
				const auto p = a_vertices(a_first + k);
				q.base = std::min(q.base, p.z);
				q.top = std::max(q.top, p.z);
				q.cx += p.x * 0.25f;
				q.cy += p.y * 0.25f;
			}
			return q;
		}
	}

	Kind Classify(const RE::BSRenderPass* a_pass, bool a_treeModSupport)
	{
		const auto property = a_pass ? a_pass->shaderProperty : nullptr;
		const auto geometry = a_pass ? a_pass->geometry : nullptr;
		if (!property || !geometry)
			return Kind::None;

		using Flag = RE::BSShaderProperty::EShaderPropertyFlag;
		if (!property->flags.all(Flag::kVertexColors) || !property->flags.none(Flag::kLODObjects, Flag::kHDLODObjects, Flag::kTreeAnim, Flag::kLODLandscape))
			return Kind::None;

		// DynDOLOD's merged object LOD geometry; loaded plants with the same flags are plain BSTriShapes
		static REL::Relocation<const RE::NiRTTI*> subIndexTriShapeRTTI{ RE::BSSubIndexTriShape::Ni_RTTI };
		if (geometry->GetRTTI() != subIndexTriShapeRTTI.get())
			return Kind::None;

		if (IsTreeShapeName(geometry))
			return Kind::Crown;

		// 3D LOD crowns
		if (property->flags.all(Flag::kTwoSided) && property->flags.any(Flag::kRimLighting, Flag::kSoftLighting))
			return Kind::Crown;

		const bool allQuads = IsAllQuads(geometry);
		if (property->flags.none(Flag::kTwoSided, Flag::kRimLighting, Flag::kSoftLighting)) {
			// 3D LOD trunks are flat crossed cards: quads only, which rocks and buildings with the same flags are not.
			// Tree mods merge the cards with 3D trunks and stumps; the bend data then looks for the cards.
			return allQuads || (a_treeModSupport && geometry->AsTriShape()) ? Kind::Trunk : Kind::None;
		}

		// Tree-mod crowns: two-sided without rim or soft lighting, never all quads, and only in the LOD4 ring;
		// at LOD8 and LOD16 the tree-mod trees are billboards and these shapes are not trees
		if (a_treeModSupport && geometry->AsTriShape() && !allQuads && property->flags.all(Flag::kTwoSided) && geometry->world.scale < 6.0f)
			return Kind::Crown;

		return Kind::None;
	}

	ID3D11ShaderResourceView* BendWeights::Get(RE::BSGeometry* a_geometry, Kind a_kind, uint a_frame, bool a_treeModSupport)
	{
		VertexView vertices;
		if (!a_geometry || a_kind == Kind::None || !GetVertices(a_geometry, vertices))
			return nullptr;

		const auto firstVertex = vertices(0);
		if (auto it = entries.find(vertices.vertexBuffer); it != entries.end()) {
			auto& entry = it->second;
			// Tree-mod crowns rebuild when their block gains trunk cards
			const bool stale = a_treeModSupport && entry.crown && [&] {
				const auto blockIt = blocks.find(entry.blockKey);
				return blockIt != blocks.end() && blockIt->second.generation != entry.generation;
			}();
			if (!stale && entry.vertexCount == vertices.count && entry.firstVertex[0] == firstVertex.x && entry.firstVertex[1] == firstVertex.y && entry.firstVertex[2] == firstVertex.z) {
				entry.lastFrame = a_frame;
				return entry.srv.get();
			}
			// Stale, or the vertex buffer address was reused by another shape
			entries.erase(it);
		}

		ZoneScopedN("TreeWind::BuildBendWeights");
		const uint64_t blockKey = BlockKey(a_geometry);
		auto& block = blocks[blockKey];
		const float worldScale = a_geometry->world.scale;
		const bool allQuads = IsAllQuads(a_geometry);
		const auto indices = a_geometry->GetGeometryRuntimeData().rendererData->rawIndexData;
		const uint32_t triangleCount = a_geometry->AsTriShape()->GetTrishapeRuntimeData().triangleCount;

		Entry entry;
		entry.vertexCount = vertices.count;
		entry.firstVertex[0] = firstVertex.x;
		entry.firstVertex[1] = firstVertex.y;
		entry.firstVertex[2] = firstVertex.z;
		entry.lastFrame = a_frame;
		entry.blockKey = blockKey;
		entry.crown = a_kind == Kind::Crown;

		// Remembers a shape with nothing to sway, so it is not scanned again
		const auto keepStill = [&]() -> ID3D11ShaderResourceView* {
			entry.generation = block.generation;
			entries.insert_or_assign(vertices.vertexBuffer, std::move(entry));
			return nullptr;
		};

		// Per vertex: bend weight (0 base, 1 top), tree random, tree sway scale, distance from the trunk (0 trunk, 1 crown edge)
		std::vector<uint16_t> data(static_cast<size_t>(vertices.count) * 4, 0);
		const auto put = [&](uint32_t a_index, float a_bend, float a_random, float a_scale, float a_radial) {
			data[a_index * 4 + 0] = DirectX::PackedVector::XMConvertFloatToHalf(std::clamp(a_bend, 0.0f, 1.0f));
			data[a_index * 4 + 1] = DirectX::PackedVector::XMConvertFloatToHalf(a_random);
			data[a_index * 4 + 2] = DirectX::PackedVector::XMConvertFloatToHalf(a_scale);
			data[a_index * 4 + 3] = DirectX::PackedVector::XMConvertFloatToHalf(std::clamp(a_radial, 0.0f, 1.0f));
		};

		if (a_kind == Kind::Trunk) {
			// First vertex of each trunk card
			std::vector<uint32_t> cardStarts;
			if (allQuads) {
				for (uint32_t c = 0; c + 3 < vertices.count; c += 4)
					cardStarts.push_back(c);
			} else {
				// Tree mods: find the cards among 3D trunks and stumps. A tree's cards cross at one centre, so a lone
				// tall quad (a pillar face, a post) without a partner is skipped.
				std::vector<uint32_t> candidates;
				for (uint32_t i = 0; i + 3 < vertices.count;) {
					if (IsCardQuad(vertices, i)) {
						candidates.push_back(i);
						i += 4;
					} else {
						++i;
					}
				}
				std::vector<QuadBounds> bounds;
				for (const auto c : candidates)
					bounds.push_back(GetQuadBounds(vertices, c));
				for (size_t a = 0; a < candidates.size(); ++a) {
					for (size_t b = 0; b < candidates.size(); ++b) {
						if (a != b && std::hypot(bounds[a].cx - bounds[b].cx, bounds[a].cy - bounds[b].cy) < 8.0f) {
							cardStarts.push_back(candidates[a]);
							break;
						}
					}
				}
			}

			// Tree mods: trees with a real 3D trunk instead of cards. A tall, narrow piece of a mixed shape is such a
			// trunk: it registers as its tree's card, so the crown follows it, and bends from its foot.
			std::vector<BendWeights::TrunkCard> trunks;
			std::vector<uint32_t> roots;
			std::unordered_map<uint32_t, size_t> trunkOfPiece;
			if (!allQuads && indices) {
				const float crownRadius = kCrownRadius * 4.0f / std::max(worldScale, 0.01f);
				std::vector<bool> onCard(vertices.count, false);
				for (const auto c : cardStarts)
					for (uint32_t k = 0; k < 4; ++k)
						onCard[c + k] = true;
				roots = PieceRoots(indices, triangleCount, vertices.count);
				struct Box
				{
					float sumX = 0.0f, sumY = 0.0f, minX = FLT_MAX, maxX = -FLT_MAX, minY = FLT_MAX, maxY = -FLT_MAX, minZ = FLT_MAX, maxZ = -FLT_MAX;
					int count = 0;
					bool card = false;
				};
				std::unordered_map<uint32_t, Box> boxes;
				for (uint32_t i = 0; i < vertices.count; ++i) {
					const auto p = vertices(i);
					auto& box = boxes[roots[i]];
					box.sumX += p.x;
					box.sumY += p.y;
					box.minX = std::min(box.minX, p.x);
					box.maxX = std::max(box.maxX, p.x);
					box.minY = std::min(box.minY, p.y);
					box.maxY = std::max(box.maxY, p.y);
					box.minZ = std::min(box.minZ, p.z);
					box.maxZ = std::max(box.maxZ, p.z);
					box.card = box.card || onCard[i];
					++box.count;
				}
				for (const auto& [root, box] : boxes) {
					const float width = std::max(box.maxX - box.minX, box.maxY - box.minY);
					const float height = box.maxZ - box.minZ;
					if (!box.card && box.count >= 6 && width < 0.6f * crownRadius && height > 64.0f && height > 3.0f * width) {
						trunkOfPiece[root] = trunks.size();
						trunks.push_back({ box.sumX / box.count, box.sumY / box.count, box.minZ, box.maxZ });
					}
				}
			}

			// Each trunk registers its tree for the crowns of this block, once per shape
			if (block.trunkShapes.insert(vertices.vertexBuffer).second && (!cardStarts.empty() || !trunks.empty())) {
				++block.generation;
				for (const auto& trunk : trunks)
					AddCard(block, trunk, worldScale);
				for (const auto c : cardStarts) {
					const auto q = GetQuadBounds(vertices, c);
					AddCard(block, { q.cx, q.cy, q.base, q.top }, worldScale);
				}
			}
			if (cardStarts.empty() && trunks.empty())
				return keepStill();

			for (uint32_t i = 0; i < vertices.count && !trunks.empty(); ++i) {
				const auto it = trunkOfPiece.find(roots[i]);
				if (it == trunkOfPiece.end())
					continue;
				const auto& trunk = trunks[it->second];
				const auto* card = NearestCard(block, trunk.x, trunk.y);
				put(i, (vertices(i).z - trunk.base) / std::max(trunk.top - trunk.base, 1.0f), card ? card->random : 0.5f, card ? card->scale : 1.0f, 0.0f);
			}
			// Each card bends from its own foot, with its tree's random and scale
			for (const auto c : cardStarts) {
				const auto q = GetQuadBounds(vertices, c);
				const auto* card = NearestCard(block, q.cx, q.cy);
				const float height = std::max(q.top - q.base, 1.0f);
				for (uint32_t k = 0; k < 4; ++k)
					put(c + k, (vertices(c + k).z - q.base) / height, card ? card->random : 0.5f, card ? card->scale : 1.0f, 0.0f);
			}
		} else if (allQuads) {
			// Billboard crowns (far LOD rings, billboard-only trees): each card bends from its bottom edge,
			// and crossed cards share the random through their rounded centre
			for (uint32_t c = 0; c + 3 < vertices.count; c += 4) {
				const auto q = GetQuadBounds(vertices, c);
				const float height = std::max(q.top - q.base, 1.0f);
				const float rx = std::round(q.cx / 8.0f), ry = std::round(q.cy / 8.0f);
				const float random = Hash01(rx, ry);
				const float scale = TreeScale(height, worldScale, Hash01(ry * 1.37f + 11.0f, rx * 0.71f - 5.0f));
				for (uint32_t k = 0; k < 4; ++k)
					put(c + k, (vertices(c + k).z - q.base) / height, random, scale, 0.6f);
			}
		} else if (IsTreeShapeName(a_geometry)) {
			// A 'treepassthru' shape that is not all quads (external billboards, LOD tree models): each connected
			// piece bends from its own foot, and the pieces of one tree share the random through their rounded centre
			if (!indices)
				return keepStill();
			const auto roots = PieceRoots(indices, triangleCount, vertices.count);
			struct Piece
			{
				float sumX = 0.0f, sumY = 0.0f, base = FLT_MAX, top = -FLT_MAX, random = 0.5f, scale = 1.0f;
				int count = 0;
			};
			std::unordered_map<uint32_t, Piece> pieces;
			for (uint32_t i = 0; i < vertices.count; ++i) {
				const auto p = vertices(i);
				auto& piece = pieces[roots[i]];
				piece.sumX += p.x;
				piece.sumY += p.y;
				piece.base = std::min(piece.base, p.z);
				piece.top = std::max(piece.top, p.z);
				++piece.count;
			}
			for (auto& [root, piece] : pieces) {
				const float rx = std::round(piece.sumX / piece.count / 8.0f), ry = std::round(piece.sumY / piece.count / 8.0f);
				piece.random = Hash01(rx, ry);
				piece.scale = TreeScale(std::max(piece.top - piece.base, 1.0f), worldScale, Hash01(ry * 1.37f + 11.0f, rx * 0.71f - 5.0f));
			}
			for (uint32_t i = 0; i < vertices.count; ++i) {
				const auto& piece = pieces[roots[i]];
				const float height = piece.top - piece.base;
				// Flat bits stay still
				put(i, height > 8.0f ? (vertices(i).z - piece.base) / height : 0.0f, piece.random, piece.scale, 0.6f);
			}
		} else if (a_treeModSupport) {
			// Tree-mod crowns: each connected piece (a branch, a leaf cluster) follows the ONE trunk card nearest its
			// centre, so a piece never tears between two trees; a piece with no card near stays still (full 3D LOD trees
			// with real trunks). In the far rings tree-mod billboards are not plain quads and have no trunk cards, so
			// each piece there bends from its own foot.
			const bool farRing = worldScale >= 6.0f;
			if (block.cards.empty() && !farRing)
				return nullptr;
			const float crownRadius = kCrownRadius * 4.0f / std::max(worldScale, 0.01f);
			const float maxDistance = 1.2f * crownRadius;
			const auto roots = PieceRoots(indices, triangleCount, vertices.count);
			struct Piece
			{
				float sumX = 0.0f, sumY = 0.0f, minX = FLT_MAX, maxX = -FLT_MAX, minY = FLT_MAX, maxY = -FLT_MAX, minZ = FLT_MAX, maxZ = -FLT_MAX;
				int count = 0;
				bool ownFoot = false;
				bool perVertex = false;  // one piece spans several trees: nearest card per vertex
				const BendWeights::TrunkCard* card = nullptr;
			};
			std::unordered_map<uint32_t, Piece> pieces;
			for (uint32_t i = 0; i < vertices.count; ++i) {
				const auto p = vertices(i);
				auto& piece = pieces[roots[i]];
				piece.sumX += p.x;
				piece.sumY += p.y;
				piece.minX = std::min(piece.minX, p.x);
				piece.maxX = std::max(piece.maxX, p.x);
				piece.minY = std::min(piece.minY, p.y);
				piece.maxY = std::max(piece.maxY, p.y);
				piece.minZ = std::min(piece.minZ, p.z);
				piece.maxZ = std::max(piece.maxZ, p.z);
				++piece.count;
			}
			uint64_t matched = 0;
			for (auto& [root, piece] : pieces) {
				const float cx = piece.sumX / piece.count, cy = piece.sumY / piece.count;
				if (farRing) {
					piece.card = block.cards.empty() ? nullptr : NearestCardWithin(block, cx, cy, maxDistance);
					piece.ownFoot = !piece.card && piece.maxZ - piece.minZ > 8.0f;
					matched += piece.card || piece.ownFoot;
					continue;
				}
				if (piece.maxX - piece.minX > 4.0f * crownRadius || piece.maxY - piece.minY > 4.0f * crownRadius) {
					piece.perVertex = true;
					continue;
				}
				piece.card = NearestCardWithin(block, cx, cy, maxDistance);
				matched += piece.card != nullptr;
			}
			for (uint32_t i = 0; i < vertices.count; ++i) {
				const auto p = vertices(i);
				const auto& piece = pieces[roots[i]];
				if (piece.ownFoot) {
					const float height = std::max(piece.maxZ - piece.minZ, 1.0f);
					const float rx = std::round(piece.sumX / piece.count / 8.0f), ry = std::round(piece.sumY / piece.count / 8.0f);
					put(i, (p.z - piece.minZ) / height, Hash01(rx, ry), TreeScale(height, worldScale, Hash01(ry * 1.37f + 11.0f, rx * 0.71f - 5.0f)), 0.6f);
					continue;
				}
				const auto* card = piece.perVertex ? NearestCardWithin(block, p.x, p.y, maxDistance) : piece.card;
				if (piece.perVertex && card)
					++matched;
				if (card) {
					const float height = std::max(card->top - card->base, 1.0f);
					const float radial = std::sqrt((p.x - card->x) * (p.x - card->x) + (p.y - card->y) * (p.y - card->y)) / crownRadius;
					put(i, (p.z - card->base) / height, card->random, card->scale, radial);
				}
			}
			if (matched == 0)
				return keepStill();
		} else {
			// 3D crowns follow the nearest trunk card of their block; until those trunks are drawn, the crown stays still
			if (block.cards.empty())
				return nullptr;
			const float crownRadius = kCrownRadius * 4.0f / std::max(worldScale, 0.01f);
			for (uint32_t i = 0; i < vertices.count; ++i) {
				const auto p = vertices(i);
				if (const auto* card = NearestCard(block, p.x, p.y)) {
					const float height = std::max(card->top - card->base, 1.0f);
					const float radial = std::sqrt((p.x - card->x) * (p.x - card->x) + (p.y - card->y) * (p.y - card->y)) / crownRadius;
					put(i, (p.z - card->base) / height, card->random, card->scale, radial);
				} else {
					put(i, 0.6f, Hash01(p.x * 0.01f, p.y * 0.01f), 1.0f, 0.5f);
				}
			}
		}

		D3D11_BUFFER_DESC bufferDesc{};
		bufferDesc.ByteWidth = static_cast<UINT>(data.size() * sizeof(uint16_t));
		bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
		bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		const D3D11_SUBRESOURCE_DATA initData{ data.data(), 0, 0 };
		if (FAILED(globals::d3d::device->CreateBuffer(&bufferDesc, &initData, entry.buffer.put())))
			return nullptr;
		Util::SetResourceName(entry.buffer.get(), "TreeWind::BendWeights");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.NumElements = vertices.count;
		if (FAILED(globals::d3d::device->CreateShaderResourceView(entry.buffer.get(), &srvDesc, entry.srv.put())))
			return nullptr;
		Util::SetResourceName(entry.srv.get(), "TreeWind::BendWeights SRV");

		entry.generation = block.generation;
		return entries.insert_or_assign(vertices.vertexBuffer, std::move(entry)).first->second.srv.get();
	}

	void BendWeights::Clear()
	{
		entries.clear();
		blocks.clear();
	}

	void BendWeights::Evict(uint a_frame)
	{
		std::erase_if(entries, [a_frame](const auto& a_entry) { return a_frame - a_entry.second.lastFrame > kEvictFrames; });
		// A block lives only while one of its shapes does, so its cards do not pile up
		std::unordered_set<uint64_t> liveBlocks;
		for (const auto& [vertexBuffer, entry] : entries)
			liveBlocks.insert(entry.blockKey);
		std::erase_if(blocks, [&liveBlocks](const auto& a_block) { return !liveBlocks.contains(a_block.first); });
	}
}
