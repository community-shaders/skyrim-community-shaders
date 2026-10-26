#include "TreeShapes.h"

#include <DirectXPackedVector.h>
#include <cfloat>
#include <numeric>

#include "Globals.h"
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

		// Connected pieces of a shape from its CPU index copy: the root vertex of each vertex's piece
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
			for (uint32_t t = 0; t < a_triangleCount; ++t) {
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

	Kind Classify(const RE::BSRenderPass* a_pass)
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

		// 3D LOD trunks are flat crossed cards: quads only, which rocks and buildings with the same flags are not
		if (property->flags.none(Flag::kTwoSided, Flag::kRimLighting, Flag::kSoftLighting) && IsAllQuads(geometry))
			return Kind::Trunk;

		return Kind::None;
	}

	ID3D11ShaderResourceView* BendWeights::Get(RE::BSGeometry* a_geometry, Kind a_kind, uint a_frame)
	{
		VertexView vertices;
		if (!a_geometry || a_kind == Kind::None || !GetVertices(a_geometry, vertices))
			return nullptr;

		const auto firstVertex = vertices(0);
		if (auto it = entries.find(vertices.vertexBuffer); it != entries.end()) {
			auto& entry = it->second;
			if (entry.vertexCount == vertices.count && entry.firstVertex[0] == firstVertex.x && entry.firstVertex[1] == firstVertex.y && entry.firstVertex[2] == firstVertex.z) {
				entry.lastFrame = a_frame;
				return entry.srv.get();
			}
			// The vertex buffer address was reused by another shape
			entries.erase(it);
		}

		auto& block = blocks[BlockKey(a_geometry)];
		const float worldScale = a_geometry->world.scale;
		const bool allQuads = IsAllQuads(a_geometry);

		// Per vertex: bend weight (0 base, 1 top), tree random, tree sway scale, distance from the trunk (0 trunk, 1 crown edge)
		std::vector<uint16_t> data(static_cast<size_t>(vertices.count) * 4, 0);
		const auto put = [&](uint32_t a_index, float a_bend, float a_random, float a_scale, float a_radial) {
			data[a_index * 4 + 0] = DirectX::PackedVector::XMConvertFloatToHalf(std::clamp(a_bend, 0.0f, 1.0f));
			data[a_index * 4 + 1] = DirectX::PackedVector::XMConvertFloatToHalf(a_random);
			data[a_index * 4 + 2] = DirectX::PackedVector::XMConvertFloatToHalf(a_scale);
			data[a_index * 4 + 3] = DirectX::PackedVector::XMConvertFloatToHalf(std::clamp(a_radial, 0.0f, 1.0f));
		};

		if (a_kind == Kind::Trunk) {
			// Each card bends from its own foot and registers its tree for the crowns of this block
			const bool newShape = block.trunkShapes.insert(vertices.vertexBuffer).second;
			for (uint32_t c = 0; c + 3 < vertices.count; c += 4) {
				const auto q = GetQuadBounds(vertices, c);
				if (newShape)
					AddCard(block, { q.cx, q.cy, q.base, q.top }, worldScale);
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
			const auto indices = a_geometry->GetGeometryRuntimeData().rendererData->rawIndexData;
			if (!indices)
				return nullptr;
			const auto roots = PieceRoots(indices, a_geometry->AsTriShape()->GetTrishapeRuntimeData().triangleCount, vertices.count);
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

		Entry entry;
		D3D11_BUFFER_DESC bufferDesc{};
		bufferDesc.ByteWidth = static_cast<UINT>(data.size() * sizeof(uint16_t));
		bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
		bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		const D3D11_SUBRESOURCE_DATA initData{ data.data(), 0, 0 };
		if (FAILED(globals::d3d::device->CreateBuffer(&bufferDesc, &initData, entry.buffer.put())))
			return nullptr;
		Util::SetResourceName(entry.buffer.get(), "TreeLODWind::BendWeights");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.NumElements = vertices.count;
		if (FAILED(globals::d3d::device->CreateShaderResourceView(entry.buffer.get(), &srvDesc, entry.srv.put())))
			return nullptr;
		Util::SetResourceName(entry.srv.get(), "TreeLODWind::BendWeights SRV");

		entry.vertexCount = vertices.count;
		entry.firstVertex[0] = firstVertex.x;
		entry.firstVertex[1] = firstVertex.y;
		entry.firstVertex[2] = firstVertex.z;
		entry.lastFrame = a_frame;
		return entries.insert_or_assign(vertices.vertexBuffer, std::move(entry)).first->second.srv.get();
	}

	void BendWeights::Evict(uint a_frame)
	{
		std::erase_if(entries, [a_frame](const auto& a_entry) { return a_frame - a_entry.second.lastFrame > kEvictFrames; });
		// Nothing in view any more: forget the trunk cards too
		if (entries.empty())
			blocks.clear();
	}
}
