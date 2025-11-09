#include "UnifiedWater.h"

#include "Menu.h"
#include "Menu/ThemeManager.h"
#include "PCH.h"
#include "State.h"
#include "ShaderCache.h"
#include "RE/C/Calendar.h"
#include "Globals.h"
#include "RE/M/MemoryManager.h"
#include "RE/N/NiGeometry.h"
#include "RE/N/NiGeometryData.h"
#include "RE/N/NiSmartPointer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <vector>

#include <d3d11.h>

namespace
{
struct EdgeKey
{
	std::uint32_t v0;
	std::uint32_t v1;

	bool operator==(const EdgeKey& rhs) const noexcept
	{
		return v0 == rhs.v0 && v1 == rhs.v1;
	}
};

struct EdgeKeyHash
{
	std::size_t operator()(const EdgeKey& key) const noexcept
	{
		return (static_cast<std::size_t>(key.v0) << 32) ^ key.v1;
	}
};

struct EdgeInfo
{
	std::uint32_t oppositeA = std::numeric_limits<std::uint32_t>::max();
	std::uint32_t oppositeB = std::numeric_limits<std::uint32_t>::max();
	std::uint32_t newIndex = std::numeric_limits<std::uint32_t>::max();

	[[nodiscard]] bool IsBoundary() const noexcept
	{
		return oppositeB == std::numeric_limits<std::uint32_t>::max();
	}
};

static std::uint16_t FloatToHalf(float value) noexcept
{
	if (!std::isfinite(value)) {
		return value > 0.0f ? 0x7C00 : 0xFC00;
	}

	std::uint32_t bits = 0;
	std::memcpy(&bits, &value, sizeof(bits));

	std::uint32_t sign = (bits >> 31) & 0x1;
	int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xFF) - 127;
	std::uint32_t mantissa = bits & 0x7FFFFF;

	std::uint16_t result = static_cast<std::uint16_t>(sign << 15);

	if (exponent > 15) {
		return static_cast<std::uint16_t>(result | 0x7C00);
	}

	if (exponent < -14) {
		if (exponent < -24) {
			return result;
		}

		signed int shift = (-14 - exponent);
		mantissa |= 0x800000;
		std::uint32_t sub = mantissa >> (shift + 13);
		if (((mantissa >> (shift + 12)) & 0x1) && ((sub & 0x1) || (mantissa & ((1u << (shift + 12)) - 1u)))) {
			++sub;
		}
		return static_cast<std::uint16_t>(result | (sub & 0x03FF));
	}

	std::uint16_t halfExp = static_cast<std::uint16_t>(exponent + 15);
	std::uint16_t halfMant = static_cast<std::uint16_t>(mantissa >> 13);
	if ((mantissa & 0x1FFF) > 0x1000 || ((mantissa & 0x3FFF) == 0x3000)) {
		++halfMant;
		if (halfMant == 0x0400) {
			halfMant = 0;
			++halfExp;
			if (halfExp >= 31) {
				return static_cast<std::uint16_t>(result | 0x7C00);
			}
		}
	}
	return static_cast<std::uint16_t>(result | (halfExp << 10) | (halfMant & 0x03FF));
}

static float HalfToFloat(std::uint16_t value) noexcept
{
	const std::uint32_t sign = (value >> 15) & 0x1;
	std::uint32_t exponent = (value >> 10) & 0x1F;
	std::uint32_t mantissa = value & 0x3FF;

	std::uint32_t bits;
	if (exponent == 0) {
		if (mantissa == 0) {
			bits = sign << 31;
		} else {
			exponent = 1;
			while ((mantissa & 0x400) == 0) {
				mantissa <<= 1;
				--exponent;
			}
			mantissa &= 0x3FF;
			bits = (sign << 31) | ((exponent + 127 - 15) << 23) | (mantissa << 13);
		}
	} else if (exponent == 0x1F) {
		bits = (sign << 31) | 0x7F800000 | (mantissa << 13);
	} else {
		bits = (sign << 31) | ((exponent + 127 - 15) << 23) | (mantissa << 13);
	}

	float result;
	std::memcpy(&result, &bits, sizeof(result));
	return result;
}

static void UpdateBounds(const std::vector<RE::NiPoint3>& positions, RE::NiBound& bound) noexcept
{
	if (positions.empty()) {
		bound.center = RE::NiPoint3();
		bound.radius = 0.0f;
		return;
	}

	RE::NiPoint3 minP = positions.front();
	RE::NiPoint3 maxP = positions.front();
	for (const auto& pos : positions) {
		minP.x = std::min(minP.x, pos.x);
		minP.y = std::min(minP.y, pos.y);
		minP.z = std::min(minP.z, pos.z);
		maxP.x = std::max(maxP.x, pos.x);
		maxP.y = std::max(maxP.y, pos.y);
		maxP.z = std::max(maxP.z, pos.z);
	}

	RE::NiPoint3 center{
		(minP.x + maxP.x) * 0.5f,
		(minP.y + maxP.y) * 0.5f,
		(minP.z + maxP.z) * 0.5f
	};

	float maxRadiusSq = 0.0f;
	for (const auto& pos : positions) {
		const float dx = pos.x - center.x;
		const float dy = pos.y - center.y;
		const float dz = pos.z - center.z;
		maxRadiusSq = std::max(maxRadiusSq, dx * dx + dy * dy + dz * dz);
	}

	bound.center = center;
	bound.radius = std::sqrt(maxRadiusSq);
}


static void ReleaseBuffer(RE::ID3D11Buffer*& buffer) noexcept
{
	if (buffer) {
		reinterpret_cast<ID3D11Buffer*>(buffer)->Release();
		buffer = nullptr;
	}
}

static bool EnsureRendererData(RE::BSTriShape* target, const RE::BSTriShape* source) noexcept
{
	if (!target)
		return false;

	const RE::BSGraphics::TriShape* sourceRendererData = nullptr;
	if (source) {
		sourceRendererData = source->GetGeometryRuntimeData().rendererData;
	}

	auto& targetRuntime = target->GetGeometryRuntimeData();
	auto* targetRendererData = targetRuntime.rendererData;
	if (!sourceRendererData || !sourceRendererData->rawVertexData || !sourceRendererData->rawIndexData) {
		sourceRendererData = targetRendererData;
	}

	if (!sourceRendererData || !sourceRendererData->rawVertexData || !sourceRendererData->rawIndexData) {
		logger::warn("[Unified Water] Missing renderer buffers while preparing mesh instance");
		return false;
	}

	const auto& runtime = target->GetTrishapeRuntimeData();
	const auto vertexCount = runtime.vertexCount;
	const auto triangleCount = runtime.triangleCount;
	if (vertexCount == 0 || triangleCount == 0) {
		logger::warn("[Unified Water] Mesh has no geometry to initialise renderer data");
		return false;
	}

	const std::size_t vertexStride = const_cast<RE::BSGraphics::VertexDesc&>(sourceRendererData->vertexDesc).GetSize();
	if (vertexStride == 0) {
		logger::warn("[Unified Water] Water mesh has an unexpected vertex stride");
		return false;
	}

	const std::size_t vertexBytes = static_cast<std::size_t>(vertexCount) * vertexStride;
	const std::size_t indexCount = static_cast<std::size_t>(triangleCount) * 3;
	const std::size_t indexBytes = indexCount * sizeof(std::uint16_t);
	if (vertexBytes == 0 || indexBytes == 0) {
		logger::warn("[Unified Water] Calculated buffer sizes are zero for mesh instance");
		return false;
	}

	auto* newVertexData = static_cast<std::uint8_t*>(RE::malloc(vertexBytes));
	if (!newVertexData) {
		logger::warn("[Unified Water] Failed to allocate CPU vertex data for mesh instance");
		return false;
	}
	std::memcpy(newVertexData, sourceRendererData->rawVertexData, vertexBytes);

	auto* newIndexData = static_cast<std::uint16_t*>(RE::malloc(indexBytes));
	if (!newIndexData) {
		logger::warn("[Unified Water] Failed to allocate CPU index data for mesh instance");
		RE::free(newVertexData);
		return false;
	}
	std::memcpy(newIndexData, sourceRendererData->rawIndexData, indexBytes);

	const bool allocateStruct = !targetRendererData || targetRendererData == sourceRendererData;
	if (allocateStruct) {
		auto* replacement = static_cast<RE::BSGraphics::TriShape*>(RE::malloc(sizeof(RE::BSGraphics::TriShape)));
		if (!replacement) {
			logger::warn("[Unified Water] Failed to allocate renderer data for mesh instance");
			RE::free(newIndexData);
			RE::free(newVertexData);
			return false;
		}

		std::memcpy(replacement, sourceRendererData, sizeof(RE::BSGraphics::TriShape));
		replacement->vertexBuffer = nullptr;
		replacement->indexBuffer = nullptr;
		replacement->rawVertexData = nullptr;
		replacement->rawIndexData = nullptr;
		replacement->refCount = 1;
		targetRuntime.rendererData = replacement;
		targetRendererData = replacement;
	} else {
		targetRendererData->vertexDesc = sourceRendererData->vertexDesc;
	}

	if (targetRendererData->rawVertexData)
		RE::free(targetRendererData->rawVertexData);
	if (targetRendererData->rawIndexData)
		RE::free(targetRendererData->rawIndexData);

	if (targetRendererData->vertexBuffer && targetRendererData->vertexBuffer != sourceRendererData->vertexBuffer)
		ReleaseBuffer(targetRendererData->vertexBuffer);
	else
		targetRendererData->vertexBuffer = nullptr;

	if (targetRendererData->indexBuffer && targetRendererData->indexBuffer != sourceRendererData->indexBuffer)
		ReleaseBuffer(targetRendererData->indexBuffer);
	else
		targetRendererData->indexBuffer = nullptr;

	targetRendererData->rawVertexData = newVertexData;
	targetRendererData->rawIndexData = newIndexData;
	targetRendererData->refCount = 1;

	return true;
}
}

static bool ApplyLoopSubdivision(RE::BSTriShape* shape, std::uint32_t iterations, bool verbose = true)
{
	if (!shape || iterations == 0) {
		if (verbose) {
			logger::info(
				"[Unified Water] Subdivision skipped (shape = {}, iterations = {})",
				static_cast<const void*>(shape),
				iterations);
		}
		return true;
	}

	auto* rendererData = shape->GetGeometryRuntimeData().rendererData;
	if (!rendererData) {
		logger::warn("[Unified Water] Missing renderer data for subdivision");
		return false;
	}

	const auto* rawVertexData = rendererData->rawVertexData;
	if (!rawVertexData) {
		logger::warn("[Unified Water] Missing CPU vertex data for subdivision");
		return false;
	}

	RE::NiGeometryData* geometryData = nullptr;
	if (auto& geometryRuntime = shape->GetGeometryRuntimeData(); geometryRuntime.unk20) {
		auto* geometryHandle = reinterpret_cast<RE::NiPointer<RE::NiGeometryData>*>(&geometryRuntime.unk20);
		geometryData = geometryHandle ? geometryHandle->get() : nullptr;
	}

	auto& runtime = shape->GetTrishapeRuntimeData();
	const std::uint32_t originalVertexCount = runtime.vertexCount;
	const std::uint32_t originalTriangleCount = runtime.triangleCount;

	if (originalVertexCount == 0 || originalTriangleCount == 0 || !rendererData->rawIndexData) {
		logger::warn("[Unified Water] Invalid mesh data for subdivision");
		return false;
	}

	const std::size_t vertexStride = rendererData->vertexDesc.GetSize();
	if (vertexStride < sizeof(float) * 4) {
		logger::warn("[Unified Water] Unexpected vertex stride for water mesh");
		return false;
	}

	std::vector<RE::NiPoint3> positions(originalVertexCount);
	for (std::uint32_t i = 0; i < originalVertexCount; ++i) {
		const auto* base = rawVertexData + i * vertexStride;
		const auto* pos = reinterpret_cast<const float*>(base);
		positions[i] = { pos[0], pos[1], pos[2] };
	}

	const bool hasPrimaryUV = rendererData->vertexDesc.HasFlag(RE::BSGraphics::Vertex::VF_UV);
	std::vector<std::array<float, 2>> primaryUVs;
	if (hasPrimaryUV) {
		primaryUVs.resize(originalVertexCount);
		const std::uint32_t offset0 = rendererData->vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_TEXCOORD0);
		for (std::uint32_t i = 0; i < originalVertexCount; ++i) {
			const auto* base = rawVertexData + i * vertexStride;
			if (offset0 + sizeof(std::uint16_t) * 2 <= vertexStride) {
				const auto* uv = reinterpret_cast<const std::uint16_t*>(base + offset0);
				primaryUVs[i][0] = HalfToFloat(uv[0]);
				primaryUVs[i][1] = HalfToFloat(uv[1]);
			} else {
				primaryUVs[i][0] = 0.0f;
				primaryUVs[i][1] = 0.0f;
			}
		}
	}

	const std::size_t indexCount = static_cast<std::size_t>(originalTriangleCount) * 3;
	std::vector<std::uint32_t> indices;
	indices.reserve(indexCount);
	std::size_t invalidTriangles = 0;
	for (std::size_t i = 0; i + 2 < indexCount; i += 3) {
		std::uint32_t tri[3];
		bool isValid = true;
		for (std::size_t j = 0; j < 3; ++j) {
			const auto value = static_cast<std::uint32_t>(rendererData->rawIndexData[i + j]);
			if (value >= originalVertexCount) {
				isValid = false;
				break;
			}
			tri[j] = value;
		}
		if (!isValid) {
			++invalidTriangles;
			continue;
		}
		indices.push_back(tri[0]);
		indices.push_back(tri[1]);
		indices.push_back(tri[2]);
	}

	if (indices.empty()) {
		logger::warn("[Unified Water] Subdivision aborted due to invalid triangle data");
		return false;
	}

	if (invalidTriangles > 0) {
		logger::warn("[Unified Water] Skipped {} triangle(s) with invalid indices during subdivision", invalidTriangles);
	}

	if (verbose) {
		logger::info(
			"[Unified Water] Starting subdivision: iterations = {}, initial verts = {}, initial tris = {}",
			iterations,
			originalVertexCount,
			originalTriangleCount);
	}

	for (std::uint32_t iteration = 0; iteration < iterations; ++iteration) {
		const std::size_t vertexCount = positions.size();
		std::unordered_map<EdgeKey, EdgeInfo, EdgeKeyHash> edges;
		edges.reserve(indices.size());

		std::vector<std::vector<std::uint32_t>> neighborLists(vertexCount);
		std::vector<std::vector<std::uint32_t>> boundaryNeighbors(vertexCount);

		auto registerNeighbor = [&neighborLists](std::uint32_t from, std::uint32_t to) {
			neighborLists[from].push_back(to);
		};

		auto registerEdge = [&edges](std::uint32_t a, std::uint32_t b, std::uint32_t opposite) {
			EdgeKey key{ std::min(a, b), std::max(a, b) };
			auto [it, inserted] = edges.try_emplace(key);
			auto& info = it->second;
			if (inserted) {
				info.oppositeA = opposite;
			} else if (info.oppositeA != opposite && info.oppositeB == std::numeric_limits<std::uint32_t>::max()) {
				info.oppositeB = opposite;
			}
		};

		for (std::size_t idx = 0; idx < indices.size(); idx += 3) {
			const std::uint32_t a = indices[idx + 0];
			const std::uint32_t b = indices[idx + 1];
			const std::uint32_t c = indices[idx + 2];

			registerNeighbor(a, b);
			registerNeighbor(a, c);
			registerNeighbor(b, a);
			registerNeighbor(b, c);
			registerNeighbor(c, a);
			registerNeighbor(c, b);

			registerEdge(a, b, c);
			registerEdge(b, c, a);
			registerEdge(c, a, b);
		}

		std::vector<bool> isBoundary(vertexCount, false);
		auto deduplicate = [](std::vector<std::uint32_t>& list) {
			std::sort(list.begin(), list.end());
			list.erase(std::unique(list.begin(), list.end()), list.end());
		};

		for (auto& [key, edge] : edges) {
			if (edge.IsBoundary()) {
				isBoundary[key.v0] = true;
				isBoundary[key.v1] = true;
				boundaryNeighbors[key.v0].push_back(key.v1);
				boundaryNeighbors[key.v1].push_back(key.v0);
			}
		}

		for (std::size_t i = 0; i < vertexCount; ++i) {
			deduplicate(neighborLists[i]);
			deduplicate(boundaryNeighbors[i]);
		}

		const auto previousPositions = positions;
		std::vector<std::array<float, 2>> previousUVs;
		if (hasPrimaryUV)
			previousUVs = primaryUVs;

		std::vector<RE::NiPoint3> updatedPositions(vertexCount);
		std::vector<std::array<float, 2>> updatedUVs;
		if (hasPrimaryUV)
			updatedUVs.resize(vertexCount);

		for (std::size_t i = 0; i < vertexCount; ++i) {
			const auto& current = previousPositions[i];
			if (isBoundary[i]) {
				// Keep boundary vertices fixed to preserve straight cell edges
				updatedPositions[i] = current;
				if (hasPrimaryUV)
					updatedUVs[i] = previousUVs[i];
			} else {
				const auto& neighbors = neighborLists[i];
				if (neighbors.size() >= 3) {
					const float neighborCount = static_cast<float>(neighbors.size());
					const float beta = (neighborCount == 3.0f) ? (3.0f / 16.0f) : (3.0f / (8.0f * neighborCount));
					RE::NiPoint3 neighborSum{};
					std::array<float, 2> uvSum{ 0.0f, 0.0f };
					for (auto index : neighbors) {
						neighborSum += previousPositions[index];
						if (hasPrimaryUV) {
							uvSum[0] += previousUVs[index][0];
							uvSum[1] += previousUVs[index][1];
						}
					}
					updatedPositions[i] = current * (1.0f - neighborCount * beta) + neighborSum * beta;
					if (hasPrimaryUV) {
						const auto& uvSelf = previousUVs[i];
						updatedUVs[i][0] = uvSelf[0] * (1.0f - neighborCount * beta) + uvSum[0] * beta;
						updatedUVs[i][1] = uvSelf[1] * (1.0f - neighborCount * beta) + uvSum[1] * beta;
					}
				} else {
					updatedPositions[i] = current;
					if (hasPrimaryUV)
						updatedUVs[i] = previousUVs[i];
				}
			}
		}

		std::vector<RE::NiPoint3> nextPositions = updatedPositions;
		std::vector<std::array<float, 2>> nextUVs;
		if (hasPrimaryUV)
			nextUVs = updatedUVs;

		nextPositions.reserve(updatedPositions.size() + edges.size());
		if (hasPrimaryUV)
			nextUVs.reserve(updatedUVs.size() + edges.size());

		auto appendUV = [&](const std::array<float, 2>& uv) {
			if (hasPrimaryUV)
				nextUVs.push_back(uv);
		};

		for (auto& [key, edge] : edges) {
			const auto& p0 = previousPositions[key.v0];
			const auto& p1 = previousPositions[key.v1];
			RE::NiPoint3 newPos;
			std::array<float, 2> newUV{ 0.0f, 0.0f };

			if (edge.IsBoundary()) {
				newPos = (p0 + p1) * 0.5f;
				if (hasPrimaryUV) {
					const auto& uv0 = previousUVs[key.v0];
					const auto& uv1 = previousUVs[key.v1];
					newUV[0] = 0.5f * (uv0[0] + uv1[0]);
					newUV[1] = 0.5f * (uv0[1] + uv1[1]);
				}
			} else {
				const auto& pa = previousPositions[edge.oppositeA];
				const auto& pb = previousPositions[edge.oppositeB];
				newPos = (p0 + p1) * 0.375f + (pa + pb) * 0.125f;
				if (hasPrimaryUV) {
					const auto& uv0 = previousUVs[key.v0];
					const auto& uv1 = previousUVs[key.v1];
					const auto& uvA = previousUVs[edge.oppositeA];
					const auto& uvB = previousUVs[edge.oppositeB];
					newUV[0] = 0.375f * (uv0[0] + uv1[0]) + 0.125f * (uvA[0] + uvB[0]);
					newUV[1] = 0.375f * (uv0[1] + uv1[1]) + 0.125f * (uvA[1] + uvB[1]);
				}
			}

			edge.newIndex = static_cast<std::uint32_t>(nextPositions.size());
			nextPositions.push_back(newPos);
			appendUV(newUV);
		}

		std::vector<std::uint32_t> nextIndices;
		nextIndices.reserve(indices.size() * 4);

		auto edgeIndex = [&edges](std::uint32_t u, std::uint32_t v) -> std::uint32_t {
			EdgeKey key{ std::min(u, v), std::max(u, v) };
			auto it = edges.find(key);
			return it != edges.end() ? it->second.newIndex : std::numeric_limits<std::uint32_t>::max();
		};

		for (std::size_t idx = 0; idx < indices.size(); idx += 3) {
			const std::uint32_t a = indices[idx + 0];
			const std::uint32_t b = indices[idx + 1];
			const std::uint32_t c = indices[idx + 2];
			const std::uint32_t ab = edgeIndex(a, b);
			const std::uint32_t bc = edgeIndex(b, c);
			const std::uint32_t ca = edgeIndex(c, a);

			if (ab == std::numeric_limits<std::uint32_t>::max() ||
				bc == std::numeric_limits<std::uint32_t>::max() ||
				ca == std::numeric_limits<std::uint32_t>::max()) {
				continue;
			}

			nextIndices.push_back(a);
			nextIndices.push_back(ab);
			nextIndices.push_back(ca);

			nextIndices.push_back(ab);
			nextIndices.push_back(b);
			nextIndices.push_back(bc);

			nextIndices.push_back(ca);
			nextIndices.push_back(bc);
			nextIndices.push_back(c);

			nextIndices.push_back(ab);
			nextIndices.push_back(bc);
			nextIndices.push_back(ca);
		}

		positions = std::move(nextPositions);
		indices = std::move(nextIndices);
		if (hasPrimaryUV)
			primaryUVs = std::move(nextUVs);

		if (positions.size() > std::numeric_limits<std::uint16_t>::max() || indices.size() > static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) * 3) {
			logger::warn("[Unified Water] Subdivision exceeded 16-bit index limits");
			return false;
		}

		logger::debug(
			"[Unified Water] Subdivision iteration {}/{} -> {} verts, {} tris",
			iteration + 1,
			iterations,
			positions.size(),
			indices.size() / 3);
	}

	const std::size_t finalVertexCount = positions.size();
	const std::size_t finalIndexCount = indices.size();
	if (finalVertexCount == 0 || finalIndexCount == 0) {
		logger::warn("[Unified Water] Subdivision produced empty mesh");
		return false;
	}

	if (verbose) {
		logger::info(
			"[Unified Water] Subdivision finished: final verts = {}, final tris = {}",
			finalVertexCount,
			finalIndexCount / 3);
	}

	std::vector<std::uint16_t> indexBufferData(finalIndexCount);
	for (std::size_t i = 0; i < finalIndexCount; ++i)
		indexBufferData[i] = static_cast<std::uint16_t>(indices[i]);

	std::vector<std::uint8_t> vertexBufferData(vertexStride * finalVertexCount, 0);
	for (std::size_t i = 0; i < finalVertexCount; ++i) {
		std::uint8_t* base = vertexBufferData.data() + i * vertexStride;
		auto* positionOut = reinterpret_cast<float*>(base);
		positionOut[0] = positions[i].x;
		positionOut[1] = positions[i].y;
		positionOut[2] = positions[i].z;
		positionOut[3] = 1.0f;

		if (hasPrimaryUV) {
			const std::uint32_t offset0 = rendererData->vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_TEXCOORD0);
			if (offset0 + sizeof(std::uint16_t) * 2 <= vertexStride) {
				auto* uvOut = reinterpret_cast<std::uint16_t*>(base + offset0);
				uvOut[0] = FloatToHalf(primaryUVs[i][0]);
				uvOut[1] = FloatToHalf(primaryUVs[i][1]);
			}
		}

		if (rendererData->vertexDesc.HasFlag(RE::BSGraphics::Vertex::VF_UV_2)) {
			const std::uint32_t offset1 = rendererData->vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_TEXCOORD1);
			if (offset1 + sizeof(std::uint16_t) * 2 <= vertexStride) {
				auto* uvOut1 = reinterpret_cast<std::uint16_t*>(base + offset1);
				const float u = hasPrimaryUV ? primaryUVs[i][0] : 0.0f;
				const float v = hasPrimaryUV ? primaryUVs[i][1] : 0.0f;
				uvOut1[0] = FloatToHalf(u);
				uvOut1[1] = FloatToHalf(v);
			}
		}
	}

	auto* oldRawVertex = rendererData->rawVertexData;
	auto* newRawVertex = static_cast<std::uint8_t*>(RE::malloc(vertexBufferData.size()));
	if (!newRawVertex) {
		logger::warn("[Unified Water] Failed to allocate CPU vertex buffer");
		return false;
	}
	std::memcpy(newRawVertex, vertexBufferData.data(), vertexBufferData.size());

	auto* oldRawIndex = rendererData->rawIndexData;
	auto* newRawIndex = static_cast<std::uint16_t*>(RE::malloc(indexBufferData.size() * sizeof(std::uint16_t)));
	if (!newRawIndex) {
		logger::warn("[Unified Water] Failed to allocate CPU index buffer");
		RE::free(newRawVertex);
		return false;
	}
	std::memcpy(newRawIndex, indexBufferData.data(), indexBufferData.size() * sizeof(std::uint16_t));

	auto* device = globals::d3d::device;
	if (!device) {
		logger::warn("[Unified Water] Missing D3D device during subdivision");
		RE::free(newRawVertex);
		RE::free(newRawIndex);
		return false;
	}

	ReleaseBuffer(rendererData->vertexBuffer);
	ReleaseBuffer(rendererData->indexBuffer);

	D3D11_BUFFER_DESC vbDesc{};
	vbDesc.ByteWidth = static_cast<UINT>(vertexBufferData.size());
	vbDesc.Usage = D3D11_USAGE_DEFAULT;
	vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

	D3D11_SUBRESOURCE_DATA vbInit{};
	vbInit.pSysMem = vertexBufferData.data();

	if (FAILED(device->CreateBuffer(&vbDesc, &vbInit, reinterpret_cast<ID3D11Buffer**>(&rendererData->vertexBuffer)))) {
		logger::warn("[Unified Water] Failed to create vertex buffer for subdivided mesh");
		RE::free(newRawVertex);
		RE::free(newRawIndex);
		return false;
	}

	D3D11_BUFFER_DESC ibDesc{};
	ibDesc.ByteWidth = static_cast<UINT>(indexBufferData.size() * sizeof(std::uint16_t));
	ibDesc.Usage = D3D11_USAGE_DEFAULT;
	ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

	D3D11_SUBRESOURCE_DATA ibInit{};
	ibInit.pSysMem = indexBufferData.data();

	if (FAILED(device->CreateBuffer(&ibDesc, &ibInit, reinterpret_cast<ID3D11Buffer**>(&rendererData->indexBuffer)))) {
		logger::warn("[Unified Water] Failed to create index buffer for subdivided mesh");
		RE::free(newRawVertex);
		RE::free(newRawIndex);
		ReleaseBuffer(rendererData->vertexBuffer);
		return false;
	}

	rendererData->rawVertexData = newRawVertex;
	rendererData->rawIndexData = newRawIndex;
	if (oldRawVertex)
		RE::free(oldRawVertex);
	if (oldRawIndex)
		RE::free(oldRawIndex);

	runtime.vertexCount = static_cast<std::uint16_t>(finalVertexCount);
	runtime.triangleCount = static_cast<std::uint16_t>(finalIndexCount / 3);

	if (geometryData) {
		const auto previousVertexCount = geometryData->vertices;
		RE::NiPoint3* const previousVertexArray = geometryData->vertex;
		RE::NiPoint3* const replacementVertexArray = static_cast<RE::NiPoint3*>(RE::malloc(finalVertexCount * sizeof(RE::NiPoint3)));
		if (replacementVertexArray) {
			std::memcpy(replacementVertexArray, positions.data(), finalVertexCount * sizeof(RE::NiPoint3));
			geometryData->vertex = replacementVertexArray;
			if (previousVertexArray)
				RE::free(previousVertexArray);
			geometryData->vertices = static_cast<std::uint16_t>(finalVertexCount);
		} else {
			geometryData->vertex = previousVertexArray;
			geometryData->vertices = previousVertexCount;
		}

		if (hasPrimaryUV) {
			RE::NiPoint2* const previousUVArray = geometryData->texture;
			RE::NiPoint2* const replacementUVArray = static_cast<RE::NiPoint2*>(RE::malloc(finalVertexCount * sizeof(RE::NiPoint2)));
			if (replacementUVArray) {
				for (std::size_t i = 0; i < finalVertexCount; ++i)
					replacementUVArray[i] = RE::NiPoint2(primaryUVs[i][0], primaryUVs[i][1]);
				geometryData->texture = replacementUVArray;
				if (previousUVArray)
					RE::free(previousUVArray);
			} else {
				geometryData->texture = previousUVArray;
			}
		}

		UpdateBounds(positions, geometryData->bound);
	}

	UpdateBounds(positions, shape->GetModelData().modelBound);

	return true;
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	UnifiedWater::Settings,
	UseOptimisedMeshes,
	EnableMeshSubdivision,
	ShowSubdivisionVisualizer,
	WaveIntensity,
	WaveAmplitude,
	WaveSpeed,
	WaveSteepness,
	FoamIntensity,
	FoamShoreStrength,
	FoamCrestStrength,
	FoamTurbulenceStrength,
	FoamFlowSpeedBase,
	FoamFlowSpeedRange,
	FoamShoreBoost,
	FoamSwirlStrength,
	FoamSwirlEnergyScale,
	WavePrimaryContribution,
	WaveSecondaryContribution,
	WaveDetailContribution,
	WavePrimarySpeed,
	WaveSecondarySpeed,
	WaveDetailSpeed,
	WaveDirectionBlend)

void UnifiedWater::LoadSettings(json& o_json)
{
	settings = o_json;
}

void UnifiedWater::SaveSettings(json& o_json)
{
	o_json = settings;
}

void UnifiedWater::RestoreDefaultSettings()
{
	settings = {};
}

void UnifiedWater::DrawSettings()
{
	ImGui::Checkbox("Use Optimised Meshes", &settings.UseOptimisedMeshes);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Uses meshes with significantly lower tri-count for improved performance with no visual quality loss.\n"
			"Will only affect newly created water - requires a change of location or game restart to take effect.");
	}

	ImGui::Checkbox("Enable Mesh Subdivision", &settings.EnableMeshSubdivision);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Enables 2x mesh subdivision for wave displacement near the camera.\n"
			"Increases detail and wave fidelity at the cost of performance.\n"
			"Requires a change of location or game restart to take effect.");
	}

	ImGui::Spacing();

	if (ImGui::TreeNodeEx("Enhanced Water Effects", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text("Enhanced Wave System");
		ImGui::SliderFloat("Wave Enhancement", &settings.WaveIntensity, 0.0f, 1.0f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Controls Gerstner wave enhancement strength.\n"
				"Adds realistic directional waves for more dynamic water surface.\n"
				"Set to 0 to disable enhanced waves.");
		}

		ImGui::SliderFloat("Wave Height", &settings.WaveAmplitude, 0.1f, 10.0f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Controls the amplitude (height) of water waves.");
		}

		ImGui::SliderFloat("Wave Speed", &settings.WaveSpeed, 0.1f, 10.0f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Controls how fast waves move across the water surface.");
		}

		ImGui::SliderFloat("Wave Steepness", &settings.WaveSteepness, 0.1f, 10.0f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Controls how peaked or sharp wave crests are.");
		}

		ImGui::Text("Wave Composition");
		ImGui::SliderFloat("Primary Wave Contribution", &settings.WavePrimaryContribution, 0.0f, 1.5f, "%.2f");
		ImGui::SliderFloat("Secondary Wave Contribution", &settings.WaveSecondaryContribution, 0.0f, 1.5f, "%.2f");
		ImGui::SliderFloat("Detail Wave Contribution", &settings.WaveDetailContribution, 0.0f, 1.5f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Controls weighting of the three Gerstner wave sets.");
		}

		ImGui::SliderFloat("Primary Wave Speed Mult", &settings.WavePrimarySpeed, 0.0f, 2.0f, "%.2f");
		ImGui::SliderFloat("Secondary Wave Speed Mult", &settings.WaveSecondarySpeed, 0.0f, 2.0f, "%.2f");
		ImGui::SliderFloat("Detail Wave Speed Mult", &settings.WaveDetailSpeed, 0.0f, 3.0f, "%.2f");
		ImGui::SliderFloat("Wave Direction Blend", &settings.WaveDirectionBlend, 0.0f, 3.0f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Adjust temporal speeds and wave alignment strength for the wave sets.");
		}

		ImGui::Spacing();
		
		ImGui::Text("Advanced Foam System");
		ImGui::SliderFloat("Foam Intensity", &settings.FoamIntensity, 0.0f, 2.0f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Controls overall foam visibility and coverage.\n"
				"Higher values = more foam, appears on lower wave peaks\n"
				"Lower values = less foam, only on highest wave peaks");
		}

		ImGui::SliderFloat("Foam Crest Strength", &settings.FoamCrestStrength, 0.0f, 2.0f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Scales whitecaps that sit on wave peaks.\n"
				"Higher values emphasise choppy whitecaps, lower values keep peaks cleaner.");
		}

		ImGui::SliderFloat("Foam Shore Strength", &settings.FoamShoreStrength, 0.0f, 2.0f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Controls shallow-water foam.\n"
				"Increase for more beach/river edge foam, decrease for calmer banks.");
		}

		ImGui::SliderFloat("Foam Turbulence", &settings.FoamTurbulenceStrength, 0.0f, 2.0f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Adjusts small-scale froth created by cross currents.\n"
				"Useful for tuning rapids or reducing shimmer on still water.");
		}

		ImGui::SliderFloat("Foam Flow Speed Base", &settings.FoamFlowSpeedBase, 0.0f, 0.2f, "%.3f");
		ImGui::SliderFloat("Foam Flow Speed Range", &settings.FoamFlowSpeedRange, 0.0f, 0.3f, "%.3f");
		ImGui::SliderFloat("Foam Shore Boost", &settings.FoamShoreBoost, 0.0f, 0.2f, "%.3f");
		ImGui::SliderFloat("Foam Swirl Base", &settings.FoamSwirlStrength, 0.0f, 20.0f, "%.2f");
		ImGui::SliderFloat("Foam Swirl Energy Scale", &settings.FoamSwirlEnergyScale, 0.0f, 25.0f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Tune foam advection speed and swirl strength to taste.");
		}

		ImGui::TreePop();
	}

	ImGui::Spacing();

	if (ImGui::TreeNodeEx("Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Show Tri Visualizer", &settings.ShowSubdivisionVisualizer);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Overlays triangle edges in the water shader to inspect subdivision levels.\nUseful for validating mesh LOD blends and subdivision multiplier tweaks.");
		}

		if (ImGui::Button("Regenerate Flowmap") && flowmap) {
			if (flowmap->RegenerateAndLoadFlowmap(waterCache))
				SetFlowmapTex();
		}

		if (ImGui::Button("Regenerate Caches") && waterCache)
			waterCache->RegenerateCaches();

		if (ImGui::Button("Quick Test - Guardian Stones")) {
			if (auto ui = RE::UI::GetSingleton(); ui && !ui->menuStack.empty() && RE::PlayerCharacter::GetSingleton()) {
				RE::Console::ExecuteCommand("player.setav speedmult 1000");
				RE::Console::ExecuteCommand("tgm");
				RE::Console::ExecuteCommand("tcl");
				RE::Console::ExecuteCommand("set timescale to 0");
				RE::Console::ExecuteCommand("set gamehour to 12");
				RE::Console::ExecuteCommand("coc guardianstones");
				RE::Console::ExecuteCommand("fw 81a");
			}
		}

		if (ImGui::Button("Quick Test - Solitude Exterior")) {
			if (auto ui = RE::UI::GetSingleton(); ui && !ui->menuStack.empty() && RE::PlayerCharacter::GetSingleton()) {
				RE::Console::ExecuteCommand("player.setav speedmult 1000");
				RE::Console::ExecuteCommand("tgm");
				RE::Console::ExecuteCommand("tcl");
				RE::Console::ExecuteCommand("set timescale to 0");
				RE::Console::ExecuteCommand("set gamehour to 12");
				RE::Console::ExecuteCommand("coc solitudeexterior01");
				RE::Console::ExecuteCommand("fw 81a");
			}
		}
	}
}

void UnifiedWater::DrawOverlay()
{
	if (!waterCache || !waterCache->IsBuildRunning() && !waterCache->HasBuildFailed())
		return;

	const auto shaderCache = globals::shaderCache;
	const float vOffset = shaderCache->IsCompiling() || shaderCache->GetFailedTasks() > 0 && !shaderCache->IsHideErrors() ? 120.0f : 0.0f;

	const auto snapshot = waterCache->GetBuildProgressSnapshot();

	auto& themeSettings = Menu::GetSingleton()->GetTheme();

	if (waterCache->IsBuildRunning()) {
		auto progressTitle = fmt::format("Generating Water Cache:");
		auto percent = static_cast<float>(snapshot.completed) / static_cast<float>(snapshot.total);
		auto progressOverlay = fmt::format("{}/{} ({:2.1f}%)", snapshot.completed, snapshot.total, 100 * percent);

		ImGui::SetNextWindowPos(ImVec2(ThemeManager::Constants::OVERLAY_WINDOW_POSITION, ThemeManager::Constants::OVERLAY_WINDOW_POSITION + vOffset));
		if (!ImGui::Begin("UWCacheCreationInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::End();
			return;
		}
		ImGui::TextUnformatted(progressTitle.c_str());
		ImGui::ProgressBar(percent, ImVec2(0.0f, 0.0f), progressOverlay.c_str());

		ImGui::End();
	} else if (waterCache->HasBuildFailed()) {
		ImGui::SetNextWindowPos(ImVec2(ThemeManager::Constants::OVERLAY_WINDOW_POSITION, ThemeManager::Constants::OVERLAY_WINDOW_POSITION + vOffset));
		if (!ImGui::Begin("UWCacheCreationInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::End();
			return;
		}

		ImGui::TextColored(themeSettings.StatusPalette.Error, "ERROR: Water cache generation failed for %d WorldSpaces. Check installation and CommunityShaders.log", snapshot.failed);

		ImGui::End();
	}
}

bool UnifiedWater::IsOverlayVisible() const
{
	return true;
}

void UnifiedWater::DataLoaded()
{
	auto args = RE::BSModelDB::DBTraits::ArgsType();
	args.unk8 = false;
	args.unkA = false;
	args.postProcess = false;
	RE::NiPointer<RE::NiNode> nif;

	if (const auto error = RE::BSModelDB::Demand("meshes\\water\\watermesh.nif", nif, args); error != RE::BSResource::ErrorCode::kNone) {
		logger::error("[Unified Water] Failed to load water mesh");
		return;
	}
	// TODO error check this properly
	const auto waterShape = nif->GetChildren().front()->AsNode()->GetChildren().front()->AsTriShape();
	waterMesh = RE::NiPointer(waterShape);
	logger::debug("[Unified Water] Water mesh loaded");
	if (waterMesh) {
		auto& baseRuntime = waterMesh->GetTrishapeRuntimeData();
		baseVertexCount = baseRuntime.vertexCount;
		baseTriangleCount = baseRuntime.triangleCount;
	}

	if (const auto error = RE::BSModelDB::Demand("meshes\\water\\optimisedwatermesh.nif", nif, args); error != RE::BSResource::ErrorCode::kNone) {
		logger::error("[Unified Water] Failed to load optimised water mesh");
		return;
	}
	// TODO error check this properly
	const auto optimisedWaterShape = nif->GetChildren().front()->AsNode()->GetChildren().front()->AsTriShape();
	optimisedWaterMesh = RE::NiPointer(optimisedWaterShape);
	logger::debug("[Unified Water] Optimised water mesh loaded");
	if (optimisedWaterMesh) {
		auto& optRuntime = optimisedWaterMesh->GetTrishapeRuntimeData();
		optimisedVertexCount = optRuntime.vertexCount;
		optimisedTriangleCount = optRuntime.triangleCount;
	}

	subdividedWaterMeshVariants.fill(nullptr);
	subdividedWaterMeshVariants[2] = waterMesh;  // Index 2 = base mesh (lowest detail for distance)

	if (waterMesh) {
		auto buildVariant = [&](std::uint32_t iterations) -> RE::NiPointer<RE::BSTriShape> {
			if (!iterations)
				return nullptr;
			RE::NiCloningProcess process;
			auto clone = RE::NiPointer(waterMesh->CreateClone(process)->AsTriShape());
			if (!clone)
				return nullptr;
			if (!EnsureRendererData(clone.get(), waterMesh.get())) {
				logger::warn("[Unified Water] Failed to prepare renderer data for subdivided mesh clone");
				return nullptr;
			}
			logger::info("[Unified Water] Building subdivided mesh variant with {} iteration(s)", iterations);
			if (!ApplyLoopSubdivision(clone.get(), iterations, true)) {
				logger::warn("[Unified Water] Failed to build subdivided mesh variant {}", iterations);
				return nullptr;
			}
			logger::info("[Unified Water] Subdivided mesh variant {} ready", iterations);
			return clone;
		};

		// Build LOD variants for distance-based selection
		if (auto variant = buildVariant(2); variant) {
			subdividedWaterMeshVariants[0] = variant;  // Highest detail (2x subdivision) - close water
		}
		if (auto variant = buildVariant(1); variant) {
			subdividedWaterMeshVariants[1] = variant;  // Medium detail (1x subdivision) - medium distance
		}
	}

	flowmap = new Flowmap();
	waterCache = new WaterCache();

	const bool rebuildAssets = LoadOrderChanged();
	if (rebuildAssets) {
		logger::info("[Unified Water] Load order changed, regenerating caches and dependent textures");
		waterCache->RegenerateCaches();
	} else {
		waterCache->LoadOrGenerateCaches();
	}

	while (waterCache->IsBuildRunning()) {
		std::this_thread::sleep_for(100ms);
	}

	if (waterCache->HasBuildFailed()) {
		logger::error("[Unified Water] Water cache build failed - systems will be degraded");
	}

	const bool flowmapReady = rebuildAssets ? flowmap->RegenerateAndLoadFlowmap(waterCache) : flowmap->LoadOrGenerateFlowmap(waterCache);
	if (flowmapReady)
		SetFlowmapTex();
}

bool UnifiedWater::LoadOrderChanged()
{
	auto* dataHandler = RE::TESDataHandler::GetSingleton();
	if (!dataHandler)
		return false;

	uint64_t hash = 14695981039346656037ull;

	auto addToHash = [&](const RE::TESFile* file) {
		if (!file || !file->fileName)
			return;
		for (auto p = reinterpret_cast<const unsigned char*>(file->fileName); *p; ++p) {
			hash ^= *p;
			hash *= 1099511628211ull;
		}
	};

	if (const auto mods = dataHandler->GetLoadedMods()) {
		const uint32_t count = dataHandler->GetLoadedModCount();
		for (uint32_t i = 0, n = count; i < n; ++i)
			addToHash(mods[i]);
	}

	if (const auto lightMods = dataHandler->GetLoadedLightMods()) {
		const uint32_t count = dataHandler->GetLoadedLightModCount();
		for (uint32_t i = 0, n = count; i < n; ++i)
			addToHash(lightMods[i]);
	}

	namespace fs = std::filesystem;
	const fs::path path = Util::PathHelpers::GetDataPath() / "UWLoadOrder.hash";

	uint64_t existingHash = 0;
	if (fs::exists(path)) {
		std::ifstream file(path, std::ios::binary);
		if (file.is_open()) {
			file.read(reinterpret_cast<char*>(&existingHash), sizeof(existingHash));
			file.close();
		}
	}

	if (hash != existingHash) {
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (file.is_open()) {
			file.write(reinterpret_cast<const char*>(&hash), sizeof(hash));
		}
	}

	return hash != existingHash;
}

void UnifiedWater::SetFlowmapTex() const
{
	RE::NiPointer<RE::NiSourceTexture> tex;
	if (!flowmap->TryGetFlowmap(tex))
		return;

	*gFlowMapSourceTex = tex;
	*gFlowMapSize = flowmap->GetWidth();

	logger::debug("[Unified Water] [Flowmap] Texture set");
}

void UnifiedWater::SetupResources()
{
	perFrame = new ConstantBuffer(ConstantBufferDesc<PerFrame>());
	perTile = new ConstantBuffer(ConstantBufferDesc<PerTile>());
}

void UnifiedWater::Reset()
{
	// Update the constant buffer when settings change
	hasLastTimingSample = false;
	lastTimingFrameIndex = std::numeric_limits<std::uint32_t>::max();
	lastGameTimeHours = 0.0f;
	lastRealTimeSeconds = 0.0f;
	lastTimeScale = 1.0f;
	currentGameTimeHours = 0.0f;
	currentRealTimeSeconds = 0.0f;
	currentTimeScale = 1.0f;
	prevTileData.clear();
}

void UnifiedWater::PostPostLoad()
{
	stl::detour_thunk<TES_SetWorldSpace>(REL::RelocationID(13170, 13315));
	stl::detour_thunk<TES_DestroySkyCell>(REL::RelocationID(20029, 20463));

	stl::detour_thunk<TESWaterSystem_InitializeWater>(REL::RelocationID(31388, 32179));
	stl::write_thunk_call<TESWaterSystem_InitializeWater_SetWaterShaderMaterialParams>(REL::RelocationID(31388, 32179).address() + REL::Relocate(0x360, 0x3BC, 0x35B));
	stl::write_vfunc<0x4, BSWaterShaderMaterial_ComputeCRC32>(RE::VTABLE_BSWaterShaderMaterial[0]);

	stl::detour_thunk<BGSTerrainBlock_Attach>(REL::RelocationID(30934, 31737));
	// Skip iterating attached meshes and calling TESWaterSystem::AddLODWater, this is handled in Attach now
	const auto addLoopOffset = REL::RelocationID(30934, 31737).address() + REL::Relocate(0x109, 0x109);
	if (REL::Module::IsAE())
		REL::safe_write(addLoopOffset, &REL::JMP8, 1);
	else {
		constexpr std::uint8_t patch[2] = { REL::NOP, REL::JMP32 };
		REL::safe_write(addLoopOffset, patch, 2);
	}

	stl::detour_thunk<BGSTerrainBlock_Detach>(REL::RelocationID(30936, 31739));

	stl::detour_thunk<BGSTerrainNode_UpdateWaterMeshSubVisibility>(REL::RelocationID(31059, 31846));

	stl::detour_thunk<TESWaterSystem_UpdateDisplacementMeshPosition>(REL::RelocationID(31384, 32175));

	stl::write_vfunc<0x6, BSWaterShader_SetupGeometry>(RE::VTABLE_BSWaterShader[0]);

	// Patch out the code compute shader calls that write to the flow map in Main::RenderWaterEffects
	REL::safe_fill(REL::RelocationID(35561, 36560).address() + REL::Relocate(0x1B7, 0x1F7), REL::NOP, 5);
	REL::safe_fill(REL::RelocationID(35561, 36560).address() + REL::Relocate(0x1EA, 0x22A), REL::NOP, 5);
	REL::safe_fill(REL::RelocationID(35561, 36560).address() + REL::Relocate(0x202, 0x242), REL::NOP, 5);

	gWaterLOD = reinterpret_cast<RE::NiNode**>(REL::RelocationID(516171, 402322).address());
	gFlowMapSize = reinterpret_cast<int32_t*>(REL::RelocationID(527644, 414596).address());
	gFlowMapSourceTex = reinterpret_cast<RE::NiPointer<RE::NiSourceTexture>*>(REL::RelocationID(527694, 414616).address());
	gDisplacementCellTexCoordOffset = reinterpret_cast<float4*>(REL::RelocationID(528184, 415129).address());
	gDisplacementMeshPos = reinterpret_cast<RE::NiPoint2*>(REL::RelocationID(516235, 402400).address());
	gDisplacementMeshFlowCellOffset = reinterpret_cast<RE::NiPoint2*>(REL::RelocationID(528164, 415109).address());

	logger::info("[Unified Water] Installed hooks");
}

void UnifiedWater::TESWaterSystem_InitializeWater_SetWaterShaderMaterialParams::thunk(RE::TESWaterForm* form, RE::BSWaterShaderMaterial* material)
{
	// The game prefills the material and hashes its contents, it uses this hash to check if there is an existing identical material and swaps
	// to using that material if so.
	// Problem is it does not include all data from the form, especially normal textures which can cause problems with existing materials
	// having their textures swapped out.
	// This func hash the texture names and temporarily stashes them in a ptr slot, this is added to the hash in ComputeCRC and zeroed back out again
	func(form, material);

	uint32_t hash = 2166136261u;
	auto addStrToHash = [&](const char* str) {
		for (auto p = reinterpret_cast<const unsigned char*>(str); *p; ++p) {
			hash ^= *p;
			hash *= 16777619u;
		}
	};

	addStrToHash(form->noiseTextures[0].textureName.c_str());
	addStrToHash(form->noiseTextures[1].textureName.c_str());
	addStrToHash(form->noiseTextures[2].textureName.c_str());
	addStrToHash(form->noiseTextures[3].textureName.c_str());
	uintptr_t bits = hash;
	std::memcpy(&material->normalTexture1, &bits, sizeof(uintptr_t));
}

void UnifiedWater::TESWaterSystem_InitializeWater::thunk(RE::TESWaterSystem* waterSystem, RE::BSTriShape* waterTri, RE::TESWaterForm* form, float waterHeight, void* unk4, bool noDisplacement, bool isProcedural)
{
	func(waterSystem, waterTri, form, waterHeight, unk4, noDisplacement, isProcedural);

	auto& singleton = globals::features::unifiedWater;
	
	// Log every water mesh initialization
	if (waterTri) {
		const char* name = waterTri->name.c_str();
		logger::info("[Unified Water] InitializeWater called: name='{}', noDisplacement={}, isProcedural={}", 
			name ? name : "NULL", noDisplacement, isProcedural);
	}
	
	if (!singleton.settings.EnableMeshSubdivision || noDisplacement || isProcedural) {
		logger::info("[Unified Water] Skipping mesh replacement: EnableMeshSubdivision={}, noDisplacement={}, isProcedural={}", 
			singleton.settings.EnableMeshSubdivision, noDisplacement, isProcedural);
		return;
	}

	if (!waterTri || !singleton.waterMesh || singleton.baseTriangleCount == 0) {
		logger::info("[Unified Water] Skipping: waterTri={}, waterMesh={}, baseTriangleCount={}", 
			(void*)waterTri, (void*)singleton.waterMesh.get(), singleton.baseTriangleCount);
		return;
	}

	// Skip if this is LOD water (handled by BGSTerrainBlock_Attach)
	const char* name = waterTri->name.c_str();
	if (name && std::strncmp(name, "WaterLOD_", 9) == 0) {
		logger::info("[Unified Water] Skipping LOD water: {}", name);
		return;
	}

	auto& runtime = waterTri->GetTrishapeRuntimeData();
	if (runtime.triangleCount == 0 || runtime.vertexCount == 0) {
		logger::info("[Unified Water] Skipping: zero geometry (tris={}, verts={})", runtime.triangleCount, runtime.vertexCount);
		return;
	}

	// Skip if already subdivided
	if (runtime.triangleCount > singleton.baseTriangleCount) {
		logger::info("[Unified Water] Already subdivided: tris={} > base={}", runtime.triangleCount, singleton.baseTriangleCount);
		return;
	}

	const float scale = waterTri->local.scale;
	if (!std::isfinite(scale) || scale > 1.5f) {
		logger::info("[Unified Water] Invalid scale: {}", scale);
		return;
	}

	// Calculate distance to camera for LOD selection
	float distanceToCamera = FLT_MAX;
	if (auto player = RE::PlayerCharacter::GetSingleton()) {
		RE::NiPoint3 playerPos = player->GetPosition();
		RE::NiPoint3 waterPos = waterTri->world.translate;
		
		float dx = playerPos.x - waterPos.x;
		float dy = playerPos.y - waterPos.y;
		distanceToCamera = std::sqrt(dx * dx + dy * dy);
	}
	
	logger::info("[Unified Water] Close water distance: {:.1f} units from player", distanceToCamera);

	// Determine LOD level based on distance
	std::uint32_t meshLODIndex = 2;  // Default to base mesh
	constexpr float LOD0_DISTANCE = 8192.0f;   // ~2 cells - highest detail
	constexpr float LOD1_DISTANCE = 16384.0f;  // ~4 cells - medium detail
	
	if (distanceToCamera < LOD0_DISTANCE) {
		meshLODIndex = 0;  // 2x subdivision
	} else if (distanceToCamera < LOD1_DISTANCE) {
		meshLODIndex = 1;  // 1x subdivision
	} else {
		// Too far for subdivision, use base mesh
		return;
	}

	// Use pre-subdivided mesh variant
	auto& variant = singleton.subdividedWaterMeshVariants[meshLODIndex];
	if (!variant) {
		logger::info("[Unified Water] Subdivided mesh variant {} not available", meshLODIndex);
		return;
	}

	// Get source geometry data from variant
	RE::NiGeometryData* variantGeomData = nullptr;
	if (auto& variantGeomRuntime = variant->GetGeometryRuntimeData(); variantGeomRuntime.unk20) {
		auto* geometryHandle = reinterpret_cast<RE::NiPointer<RE::NiGeometryData>*>(&variantGeomRuntime.unk20);
		variantGeomData = geometryHandle ? geometryHandle->get() : nullptr;
	}

	if (!variantGeomData) {
		logger::info("[Unified Water] Failed to get geometry data from variant mesh");
		return;
	}

	// Get target geometry data
	RE::NiGeometryData* targetGeomData = nullptr;
	if (auto& targetGeomRuntime = waterTri->GetGeometryRuntimeData(); targetGeomRuntime.unk20) {
		auto* geometryHandle = reinterpret_cast<RE::NiPointer<RE::NiGeometryData>*>(&targetGeomRuntime.unk20);
		targetGeomData = geometryHandle ? geometryHandle->get() : nullptr;
	}

	if (!targetGeomData) {
		logger::info("[Unified Water] Failed to get geometry data from water mesh");
		return;
	}

	// Copy the subdivided geometry data pointers (shares the data, doesn't deep copy)
	targetGeomData->vertices = variantGeomData->vertices;
	targetGeomData->vertex = variantGeomData->vertex;
	targetGeomData->normal = variantGeomData->normal;
	targetGeomData->texture = variantGeomData->texture;
	targetGeomData->bound = variantGeomData->bound;
	
	// Update runtime counts
	auto& variantRuntime = variant->GetTrishapeRuntimeData();
	runtime.vertexCount = variantRuntime.vertexCount;
	runtime.triangleCount = variantRuntime.triangleCount;
	
	// Mark renderer data as needing update
	if (!EnsureRendererData(waterTri, variant.get())) {
		logger::info("[Unified Water] Failed to update renderer data for close water");
		return;
	}
	
	logger::info(
		"[Unified Water] Applied pre-subdivided mesh LOD {} (distance: {:.0f}) to close water: {} verts, {} tris",
		meshLODIndex,
		distanceToCamera,
		runtime.vertexCount,
		runtime.triangleCount);
}

int32_t UnifiedWater::BSWaterShaderMaterial_ComputeCRC32::thunk(RE::BSWaterShaderMaterial* material, uint32_t srcHash)
{
	srcHash ^= static_cast<uint32_t>(reinterpret_cast<uint64_t>(material->normalTexture1.get())) + (srcHash << 6) + (srcHash >> 2);
	constexpr auto zero = static_cast<uintptr_t>(0);
	std::memcpy(&material->normalTexture1, &zero, sizeof(uintptr_t));
	return func(material, srcHash);
}

void UnifiedWater::TES_SetWorldSpace::thunk(RE::TES* tes, RE::TESWorldSpace* worldSpace, bool isExterior)
{
	func(tes, worldSpace, isExterior);

	auto& singleton = globals::features::unifiedWater;
	singleton.prevTileData.clear();
	singleton.hasLastTimingSample = false;
	singleton.lastTimingFrameIndex = std::numeric_limits<std::uint32_t>::max();
	singleton.lastGameTimeHours = 0.0f;
	singleton.lastRealTimeSeconds = 0.0f;
	singleton.lastTimeScale = 1.0f;
	singleton.currentGameTimeHours = 0.0f;
	singleton.currentRealTimeSeconds = 0.0f;
	singleton.currentTimeScale = 1.0f;
	singleton.waterCache->SetCurrentWorldSpace(worldSpace);
}

void UnifiedWater::TES_DestroySkyCell::thunk(RE::TES* tes)
{
	func(tes);

	auto& singleton = globals::features::unifiedWater;
	singleton.prevTileData.clear();
	singleton.hasLastTimingSample = false;
	singleton.lastTimingFrameIndex = std::numeric_limits<std::uint32_t>::max();
	singleton.lastGameTimeHours = 0.0f;
	singleton.lastRealTimeSeconds = 0.0f;
	singleton.lastTimeScale = 1.0f;
	singleton.currentGameTimeHours = 0.0f;
	singleton.currentRealTimeSeconds = 0.0f;
	singleton.currentTimeScale = 1.0f;
	singleton.waterCache->SetCurrentWorldSpace(nullptr);
}

void UnifiedWater::BGSTerrainNode_UpdateWaterMeshSubVisibility::thunk(const RE::BGSTerrainNode* node, RE::BSMultiBoundNode* waterParent)
{
	if (!node || !waterParent)
		return;

	if (node->GetLODLevel() != 4)
		return;

	const auto tes = globals::game::tes;
	if (!tes || !tes->gridCells)
		return;
	
	const auto& gridCells = tes->gridCells;

	const int32_t offsetX = tes->currentGridX - static_cast<int32_t>(gridCells->length >> 1);
	const int32_t offsetY = tes->currentGridY - static_cast<int32_t>(gridCells->length >> 1);
	const int32_t length = static_cast<int32_t>(gridCells->length);

	for (const auto& child : waterParent->GetChildren()) {
		if (!child)
			continue;

		int32_t x, y;
		Util::WorldToCell(child->world.translate, x, y);

		x -= offsetX;
		y -= offsetY;

		bool cull = false;
		if (x >= 0 && y >= 0 && x < length && y < length) {
			if (const auto cell = gridCells->GetCell(x, y); cell && cell->cellState.any(RE::TESObjectCELL::CellState::kAttached, static_cast<RE::TESObjectCELL::CellState>(6)))
				cull = true;
		}

		child->SetAppCulled(cull);
	}
}

void UnifiedWater::BGSTerrainBlock_Attach::thunk(RE::BGSTerrainBlock* block)
{
	const auto waterSystem = RE::TESWaterSystem::GetSingleton();
	if (!waterSystem) {
		return;
	}

	auto& singleton = globals::features::unifiedWater;

	std::vector<std::pair<RE::BSTriShape*, const WaterCache::Instruction*>> built;
	bool attaching = false;

	if (block && block->loaded && !block->attached && block->chunk && block->water) {
		block->chunk->DetachChild2(block->water);
		block->water->local.translate = block->chunk->local.translate;

		RE::NiUpdateData updateData;
		block->water->UpdateUpwardPass(updateData);

		const auto water = block->water;
		for (auto& child : water->GetChildren()) {
			if (child) {
				waterSystem->RemoveGeometry(child->AsGeometry());
				water->DetachChild(child.get());
			}
		}

		attaching = true;

		const auto node = block->node;
		const auto lodLevel = node->GetLODLevel();
		const auto worldSpace = block->node->manager->worldSpace;

		const auto instructions = singleton.waterCache->GetInstructions(worldSpace, lodLevel, node->x, node->y);
		if (!instructions) {
			logger::warn("[Unified Water] No instructions found for {} chunk at {}, {}", worldSpace->GetFormEditorID(), node->x, node->y);
			func(block);
			return;
		}

		// Calculate distance to camera for LOD selection
		float distanceToCamera = FLT_MAX;
		if (auto player = RE::PlayerCharacter::GetSingleton()) {
			RE::NiPoint3 playerPos = player->GetPosition();
			// Calculate block center position in world space
			float blockCenterX = (node->x * 4096.0f) + 2048.0f;
			float blockCenterY = (node->y * 4096.0f) + 2048.0f;
			RE::NiPoint3 blockCenter(blockCenterX, blockCenterY, 0.0f);
			
			float dx = playerPos.x - blockCenter.x;
			float dy = playerPos.y - blockCenter.y;
			distanceToCamera = std::sqrt(dx * dx + dy * dy);
		}

		for (auto& instruction : *instructions) {
			if (!instruction.form.ptr)
				continue;

			RE::NiCloningProcess cloningProcess;

			const bool farLOD = lodLevel > 4;
			const bool subdivisionEnabled = singleton.settings.EnableMeshSubdivision;
			const bool forceSubdivision = subdivisionEnabled && !farLOD;
			bool useOptimised = singleton.settings.UseOptimisedMeshes && !forceSubdivision;
			if (farLOD)
				useOptimised = false;  // Always keep far LOD water at normal vertex counts
			if (singleton.settings.UseOptimisedMeshes && !useOptimised) {
				logger::debug("[Unified Water] Subdivision or LOD requirements overriding optimised mesh usage for LOD {}", lodLevel);
			}

			// LOD selection based on camera distance (only for small tiles and when subdivision enabled)
			std::uint32_t meshLODIndex = 2;  // Default to base mesh (lowest detail)
			bool useSubdividedMesh = false;
			
			if (subdivisionEnabled && !farLOD && instruction.size <= 1) {
				// Distance thresholds in game units (1 cell = 4096 units)
				constexpr float LOD0_DISTANCE = 8192.0f;   // ~2 cells - highest detail (2x subdivision)
				constexpr float LOD1_DISTANCE = 16384.0f;  // ~4 cells - medium detail (1x subdivision)
				// Beyond LOD1_DISTANCE uses base mesh
				
				if (distanceToCamera < LOD0_DISTANCE) {
					meshLODIndex = 0;  // Highest detail (2x subdivision)
					useSubdividedMesh = true;
				} else if (distanceToCamera < LOD1_DISTANCE) {
					meshLODIndex = 1;  // Medium detail (1x subdivision)
					useSubdividedMesh = true;
				}
				// else meshLODIndex = 2 (base mesh)
			}

			RE::BSTriShape* templateShape = nullptr;
			if (useOptimised) {
				templateShape = singleton.optimisedWaterMesh.get();
			} else if (useSubdividedMesh) {
				auto& variant = singleton.subdividedWaterMeshVariants[meshLODIndex];
				if (variant) {
					templateShape = variant.get();
				}
			}

			if (!templateShape) {
				templateShape = singleton.waterMesh.get();
			}

			// Only log each unique cell once
			const std::uint64_t cellKey = (static_cast<std::uint64_t>(instruction.x) << 32) | static_cast<std::uint64_t>(instruction.y);
			if (singleton.loggedCells.find(cellKey) == singleton.loggedCells.end()) {
				singleton.loggedCells.insert(cellKey);
				logger::info(
					"[Unified Water] Mesh selection: LOD index = {}, distance = {:.0f}, terrain LOD = {}, optimised = {}, subdivided = {}, tileSize = {}",
					meshLODIndex,
					distanceToCamera,
					lodLevel,
					useOptimised,
					useSubdividedMesh,
					instruction.size);
			}

			if (!templateShape)
				continue;

			RE::BSTriShape* shape = templateShape->CreateClone(cloningProcess)->AsTriShape();

			const auto posX = (instruction.x - node->x) * 4096.0f + instruction.size * 2048.0f;
			const auto posY = (instruction.y - node->y) * 4096.0f + instruction.size * 2048.0f;
			shape->local.scale = static_cast<float>(instruction.size);
			shape->local.translate = { posX, posY, instruction.waterHeight };
			
			// Store LOD level in the shape name for later retrieval during rendering
			// Format: "WaterLOD_<level>" (e.g., "WaterLOD_8")
			char nameBuf[32];
			sprintf_s(nameBuf, "WaterLOD_%d", lodLevel);
			shape->name = nameBuf;

			water->AttachChild(shape, true);
			built.emplace_back(shape, &instruction);

			block->waterAttached = true;
		}
	}

	func(block);

	if (!attaching || !block->waterAttached)
		return;

	for (auto& [shape, instruction] : built) {
		waterSystem->InitializeWater(shape, instruction->form.ptr, instruction->waterHeight, nullptr, false, false);

		if (const auto prop = shape->GetGeometryRuntimeData().properties[1].get(); prop && prop->GetRTTI() == globals::rtti::BSWaterShaderPropertyRTTI.get()) {
			const auto waterShaderProp = static_cast<RE::BSWaterShaderProperty*>(prop);
			REX::EnumSet waterFlags = static_cast<RE::BSWaterShaderProperty::WaterFlag>(0b10000100);
			waterFlags |= RE::BSWaterShaderProperty::WaterFlag::kUseCubemapReflections;
			waterFlags |= RE::BSWaterShaderProperty::WaterFlag::kUseReflections;
			if (instruction->form.ptr->flags.any(RE::TESWaterForm::Flag::kEnableFlowmap))
				waterFlags |= RE::BSWaterShaderProperty::WaterFlag::kEnableFlowmap;
			if (instruction->form.ptr->flags.any(RE::TESWaterForm::Flag::kBlendNormals))
				waterFlags |= RE::BSWaterShaderProperty::WaterFlag::kBlendNormals;
			waterShaderProp->waterFlags = waterFlags;
		}

		// Remove from WaterSystem, will manage it ourselves
		waterSystem->waterObjects.pop_back();
	}

	(*singleton.gWaterLOD)->AttachChild(block->water, true);
	waterSystem->Enable();
}

void UnifiedWater::BGSTerrainBlock_Detach::thunk(RE::BGSTerrainBlock* block)
{
	const auto water = block->water;
	block->water = nullptr;

	func(block);

	block->water = water;

	if (water) {
		auto count = water->GetChildren().size();
		while (count > 0) {
			water->DetachChildAt(--count);
		}

		(*globals::features::unifiedWater.gWaterLOD)->DetachChild(water);
		block->waterAttached = false;
	}
}

void UnifiedWater::BSWaterShader_SetupGeometry::thunk(RE::BSShader* waterShader, RE::BSRenderPass* pass)
{
	auto& singleton = globals::features::unifiedWater;

	// Update and bind the per-frame constant buffer for vertex shader access
	if (singleton.perFrame) {
		PerFrame perFrameData{};
		perFrameData.WaveIntensity = singleton.settings.WaveIntensity;
		perFrameData.WaveAmplitude = singleton.settings.WaveAmplitude;
		perFrameData.WaveSpeed = singleton.settings.WaveSpeed;
		perFrameData.WaveSteepness = singleton.settings.WaveSteepness;
		perFrameData.FoamIntensity = singleton.settings.FoamIntensity;
		perFrameData.FoamShoreStrength = singleton.settings.FoamShoreStrength;
		perFrameData.FoamCrestStrength = singleton.settings.FoamCrestStrength;
		perFrameData.FoamTurbulenceStrength = singleton.settings.FoamTurbulenceStrength;
		perFrameData.FoamFlowSpeedBase = singleton.settings.FoamFlowSpeedBase;
		perFrameData.FoamFlowSpeedRange = singleton.settings.FoamFlowSpeedRange;
		perFrameData.FoamShoreBoost = singleton.settings.FoamShoreBoost;
		perFrameData.FoamSwirlStrength = singleton.settings.FoamSwirlStrength;
		perFrameData.FoamSwirlEnergyScale = singleton.settings.FoamSwirlEnergyScale;
		perFrameData.WavePrimaryContribution = singleton.settings.WavePrimaryContribution;
		perFrameData.WaveSecondaryContribution = singleton.settings.WaveSecondaryContribution;
		perFrameData.WaveDetailContribution = singleton.settings.WaveDetailContribution;
		perFrameData.WavePrimarySpeed = singleton.settings.WavePrimarySpeed;
		perFrameData.WaveSecondarySpeed = singleton.settings.WaveSecondarySpeed;
		perFrameData.WaveDetailSpeed = singleton.settings.WaveDetailSpeed;
		perFrameData.WaveDirectionBlend = singleton.settings.WaveDirectionBlend;
		perFrameData.TriVisualizerEnabled = singleton.settings.ShowSubdivisionVisualizer ? 1.0f : 0.0f;

		const auto* state = globals::state;
		const std::uint32_t frameIndex = state ? state->frameCount : singleton.lastTimingFrameIndex;
		if (singleton.lastTimingFrameIndex != frameIndex) {
			if (singleton.hasLastTimingSample) {
				singleton.lastGameTimeHours = singleton.currentGameTimeHours;
				singleton.lastRealTimeSeconds = singleton.currentRealTimeSeconds;
				singleton.lastTimeScale = singleton.currentTimeScale;
			}
			singleton.lastTimingFrameIndex = frameIndex;
		}

		float gameTimeHours = 0.0f;
		float realTimeSeconds = 0.0f;
		float timeScale = 1.0f;

		if (const auto calendar = RE::Calendar::GetSingleton()) {
			gameTimeHours = calendar->GetHoursPassed();
			timeScale = calendar->GetTimescale();
		}

		if (globals::state) {
			realTimeSeconds = globals::state->timer;
		}

		perFrameData.GameTimeHours = gameTimeHours;
		perFrameData.RealTimeSeconds = realTimeSeconds;
		perFrameData.TimeScale = timeScale;
		perFrameData.CellWorldSize = 4096.0f;
		perFrameData.PrevGameTimeHours = singleton.hasLastTimingSample ? singleton.lastGameTimeHours : gameTimeHours;
		perFrameData.PrevRealTimeSeconds = singleton.hasLastTimingSample ? singleton.lastRealTimeSeconds : realTimeSeconds;
		perFrameData.PrevTimeScale = singleton.hasLastTimingSample ? singleton.lastTimeScale : timeScale;
		
		singleton.perFrame->Update(perFrameData);
		
		auto context = globals::d3d::context;
		ID3D11Buffer* buffers[1] = { singleton.perFrame->CB() };
		context->VSSetConstantBuffers(7, 1, buffers);
		context->PSSetConstantBuffers(7, 1, buffers); // Bind to pixel shader too for foam

		singleton.currentGameTimeHours = gameTimeHours;
		singleton.currentRealTimeSeconds = realTimeSeconds;
		singleton.currentTimeScale = timeScale;
		singleton.hasLastTimingSample = true;
	}

	// Get water tile position and LOD level for per-tile data
	int32_t x, y;
	Util::WorldToCell(pass->geometry->world.translate, x, y);

	// Determine LOD level from the shape name if available
	// LOD water shapes created by BGSTerrainBlock_Attach are named "WaterLOD_<level>"
	// Regular water cells managed by TESWaterSystem have no special name - use LOD1 for single-cell precision
	int32_t lodLevel = 1; // Default to LOD1 for regular water cells (single-cell resolution)

	if (pass->geometry->name.c_str()) {
		const char* name = pass->geometry->name.c_str();
		if (strncmp(name, "WaterLOD_", 9) == 0) {
			lodLevel = atoi(name + 9);
			// Validate it's a power of 2 in the expected range
			if (lodLevel != 1 && lodLevel != 4 && lodLevel != 8 && lodLevel != 16 && lodLevel != 32) {
				logger::warn("[Unified Water] Invalid LOD level {} parsed from name '{}', using LOD1", lodLevel, name);
				lodLevel = 1; // Fallback to LOD1 if invalid
			}
		}
	}

	// Update per-tile data for temporal blending
	if (singleton.perTile) {
		PerTile perTileData{};

		RE::TESWorldSpace* activeWorldSpace = nullptr;
		std::uint32_t worldSpaceId = 0;
		if (const auto tes = RE::TES::GetSingleton()) {
			activeWorldSpace = tes->GetRuntimeData2().worldSpace;
			if (activeWorldSpace) {
				worldSpaceId = activeWorldSpace->GetFormID();
			}
		}

		auto mixKey = [](std::uint64_t seed, std::uint64_t value) noexcept {
			seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
			return seed;
		};

		std::uint64_t tileKeySeed = 0;
		tileKeySeed = mixKey(tileKeySeed, static_cast<std::uint64_t>(worldSpaceId));
		tileKeySeed = mixKey(tileKeySeed, static_cast<std::uint64_t>(static_cast<std::uint32_t>(lodLevel)));
		tileKeySeed = mixKey(tileKeySeed, static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)));
		tileKeySeed = mixKey(tileKeySeed, static_cast<std::uint64_t>(static_cast<std::uint32_t>(y)));
		const std::uint64_t tileKey = tileKeySeed;

		float prevNormalX = 0.0f;
		float prevNormalY = 0.0f;
		float prevDistance = 10000.0f;
		float prevSegments = 32.0f;
		const auto prevTileIt = singleton.prevTileData.find(tileKey);
		if (prevTileIt != singleton.prevTileData.end()) {
			prevNormalX = prevTileIt->second.normalX;
			prevNormalY = prevTileIt->second.normalY;
			prevDistance = prevTileIt->second.distance;
			prevSegments = prevTileIt->second.segmentsPerAxis;
		}

		perTileData.PrevData[0] = prevNormalX;
		perTileData.PrevData[1] = prevNormalY;
		perTileData.PrevData[2] = prevDistance;
		perTileData.PrevData[3] = prevSegments;

		perTileData.TileData[0] = static_cast<float>(x);
		perTileData.TileData[1] = static_cast<float>(y);
		perTileData.TileData[2] = static_cast<float>(lodLevel);
		perTileData.TileData[3] = 1.0f;

		float currentSegmentsPerAxis = prevSegments;
		if (const auto triShape = pass->geometry->AsTriShape()) {
			auto& runtimeData = triShape->GetTrishapeRuntimeData();
			const float triangleCount = static_cast<float>(runtimeData.triangleCount);
			if (triangleCount > 0.0f) {
				currentSegmentsPerAxis = std::max(1.0f, std::sqrt(triangleCount * 0.5f));
			}
		}
		perTileData.PrevData[3] = currentSegmentsPerAxis;

		float storedNormalX = prevNormalX;
		float storedNormalY = prevNormalY;
		float storedDistance = prevDistance;

		singleton.prevTileData[tileKey] = UnifiedWater::PrevTileData{ storedNormalX, storedNormalY, storedDistance, currentSegmentsPerAxis };

		singleton.perTile->Update(perTileData);

		auto context = globals::d3d::context;
		ID3D11Buffer* buffers[1] = { singleton.perTile->CB() };
		context->VSSetConstantBuffers(8, 1, buffers);
		context->PSSetConstantBuffers(8, 1, buffers); // Also bind to pixel shader for foam/normals
	}

	if (singleton.flowmap) {
		// ObjectUV.xyz below, xy contains width and height, z contains mesh scale
		// Previously flowmap size was in x, yz contained flowmap offset for water displacement mesh
		*singleton.gFlowMapSize = singleton.flowmap->GetWidth();                                            // ObjectUV.x
		singleton.gDisplacementMeshFlowCellOffset->x = static_cast<float>(singleton.flowmap->GetHeight());  // ObjectUV.y
		singleton.gDisplacementMeshFlowCellOffset->y = 1.0f - pass->geometry->local.scale;                  // ObjectUV.z (counters 1 - x in SetupGeometry)

		if (const auto prop = pass->geometry->GetGeometryRuntimeData().properties[1].get(); prop && prop->GetRTTI() == globals::rtti::BSWaterShaderPropertyRTTI.get()) {
			const auto waterShaderProp = static_cast<RE::BSWaterShaderProperty*>(prop);

			// CellTexCoordOffset.xyzw below - applies to non-displacement water only
			// xy is world cell flowmap based (0,0 is corner of flow map), zw is world cell
			// Funky maths here to counter what's being done in SetupGeometry
			// Previously these values were relative to the 5x5 flow grid centered on the player
			waterShaderProp->flowX = x + singleton.flowmap->GetOffsetX();                                                                   // CellTexCoordOffset.x
			waterShaderProp->flowY = y + singleton.flowmap->GetOffsetY() + singleton.flowmap->GetWidth() - singleton.flowmap->GetHeight();  // CellTexCoordOffset.y
			waterShaderProp->cellX = x;                                                                                                     // CellTexCoordOffset.z
			waterShaderProp->cellY = y;                                                                                                     // CellTexCoordOffset.w
		}
	}
	func(waterShader, pass);
}

void UnifiedWater::TESWaterSystem_UpdateDisplacementMeshPosition::thunk(RE::TESWaterSystem* waterSystem)
{
	func(waterSystem);

	const auto& singleton = globals::features::unifiedWater;
	if (!singleton.flowmap)
		return;

	const float posX = singleton.gDisplacementMeshPos->x / 4096.0f;
	const float posY = singleton.gDisplacementMeshPos->y / 4096.0f;
	const float offsetX = static_cast<float>(singleton.flowmap->GetOffsetX());
	const float offsetY = static_cast<float>(singleton.flowmap->GetOffsetY());
	const float height = static_cast<float>(singleton.flowmap->GetHeight());

	// CellTexCoordOffset.xyzw below - applies to displacement water only
	// Previously the values were calculated relative to the 5x5 flow grid
	*singleton.gDisplacementCellTexCoordOffset = float4(posX + offsetX, height - (posY + offsetY), posX, 1 - posY);
}
