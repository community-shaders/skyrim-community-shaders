#include "PGrassRenderer.h"

#include "Features/ProceduralGrass.h"

#include "Features/GrassCollision.h"
#include "Features/LightLimitFix.h"
#include "Features/Skylighting.h"
#include "Features/WetnessEffects.h"
#include "HiZPyramid.h"
#include "ShaderCache.h"
#include "State.h"
#include "Utils/D3D.h"

using namespace PGrassCommon;
using namespace PGrassRendererQuads;

namespace
{
	bool IsOccupiedGrassTile(const uint32_t* occupancyRows, const uint32_t patchStartX, const uint32_t patchEndX, const uint32_t patchStartY,
		const uint32_t patchEndY, const uint32_t density, const float edgeNoise)
	{
		if (!occupancyRows)
			return true;

		constexpr int32_t cellsPerAxis = QuadrantCellPitch;
		constexpr float cellWidth = 2048.0f / cellsPerAxis;
		const float patchWidth = 4096.0f / density;
		const float noise = std::max(edgeNoise, 0.0f);
		const int32_t minCellX = std::max(0, static_cast<int32_t>(std::floor((patchStartX * patchWidth - noise) / cellWidth)));
		const int32_t minCellY = std::max(0, static_cast<int32_t>(std::floor((patchStartY * patchWidth - noise) / cellWidth)));
		const int32_t maxCellX = std::min(cellsPerAxis - 1, static_cast<int32_t>(std::floor((patchEndX * patchWidth + noise) / cellWidth)));
		const int32_t maxCellY = std::min(cellsPerAxis - 1, static_cast<int32_t>(std::floor((patchEndY * patchWidth + noise) / cellWidth)));
		if (maxCellX < minCellX || maxCellY < minCellY)
			return false;

		const uint32_t width = static_cast<uint32_t>(maxCellX - minCellX + 1);
		const uint16_t mask = static_cast<uint16_t>(width >= 16u ? 0xFFFFu : ((1u << width) - 1u) << minCellX);
		for (int32_t y = minCellY; y <= maxCellY; ++y)
			if ((occupancyRows[y] & mask) != 0u)
				return true;

		return false;
	}

	/** @brief Squared distance from a point to the farthest corner of a rectangle. */
	float MaxDistanceSqToRect(const float2& origin, const float minX, const float minY, const float maxX, const float maxY)
	{
		const float dx = std::max(std::abs(minX - origin.x), std::abs(maxX - origin.x));
		const float dy = std::max(std::abs(minY - origin.y), std::abs(maxY - origin.y));
		return dx * dx + dy * dy;
	}

	/** @brief Offset from a point to the nearest point of a rectangle. */
	float2 OffsetToRect(const float2& origin, const float minX, const float minY, const float maxX, const float maxY)
	{
		return { std::clamp(origin.x, minX, maxX) - origin.x, std::clamp(origin.y, minY, maxY) - origin.y };
	}

	/** @brief Default-usage SRV structured buffer whose element ranges are updated in place. */
	template <class T>
	std::unique_ptr<StructuredBuffer> CreateRangeUpdatedBuffer(const uint32_t count, const char* name)
	{
		auto buffer = std::make_unique<StructuredBuffer>(StructuredBufferDesc<T>(count, false, false), count, name);
		buffer->CreateSRV();
		return buffer;
	}
}

namespace PGrassRendererQuads
{
	uint32_t QuadrantHash(uint32_t x, uint32_t y)
	{
		constexpr uint32_t multiplier = 1103515245u;
		const uint32_t qx = multiplier * ((x >> 1u) ^ y);
		const uint32_t qy = multiplier * ((y >> 1u) ^ x);
		return multiplier * (qx ^ (qy >> 3u));
	}

	SideFrustum BuildSideFrustum(const float4x4& viewProj)
	{
		SideFrustum frustum{};
		frustum.planes[0] = { viewProj._11 + viewProj._14, viewProj._21 + viewProj._24, viewProj._31 + viewProj._34, viewProj._41 + viewProj._44 };
		frustum.planes[1] = { -viewProj._11 + viewProj._14, -viewProj._21 + viewProj._24, -viewProj._31 + viewProj._34, -viewProj._41 + viewProj._44 };
		frustum.planes[2] = { viewProj._12 + viewProj._14, viewProj._22 + viewProj._24, viewProj._32 + viewProj._34, viewProj._42 + viewProj._44 };
		frustum.planes[3] = { -viewProj._12 + viewProj._14, -viewProj._22 + viewProj._24, -viewProj._32 + viewProj._34, -viewProj._42 + viewProj._44 };
		return frustum;
	}

	namespace
	{
		/** @brief Classifies a camera-relative box against the side planes. */
		QuadrantFrustumState ClassifyBox(const SideFrustum& frustum, const float3& center, const float3& extent)
		{
			bool fullyInside = true;
			for (const auto& plane : frustum.planes) {
				const float distance = plane.x * center.x + plane.y * center.y + plane.z * center.z + plane.w;
				const float radius = std::abs(plane.x) * extent.x + std::abs(plane.y) * extent.y + std::abs(plane.z) * extent.z;
				if (distance + radius < 0.0f)
					return QuadrantFrustumState::Outside;
				fullyInside &= distance - radius >= 0.0f;
			}
			return fullyInside ? QuadrantFrustumState::Inside : QuadrantFrustumState::Intersecting;
		}
	}

	QuadrantFrustumState ClassifyQuadrantFrustum(const Quadrant& quadrant, const SideFrustum& frustum, const float4& cameraPosAdjust, float geometryPadding)
	{
		if (!(quadrant.maxHeight > QuadrantNoHeight && quadrant.minHeight <= quadrant.maxHeight))
			return QuadrantFrustumState::Intersecting;

		const float minZ = quadrant.minHeight - 256.0f;
		const float maxZ = quadrant.maxHeight + 300.0f;
		const float3 center = { quadrant.worldPos.x + 1024.0f - cameraPosAdjust.x, quadrant.worldPos.y + 1024.0f - cameraPosAdjust.y, (minZ + maxZ) * 0.5f - cameraPosAdjust.z };

		// The top and bottom side planes have a Z component, so include the blade envelope on every axis.
		const float3 extent = { 1024.0f + geometryPadding, 1024.0f + geometryPadding, (maxZ - minZ) * 0.5f + geometryPadding };
		return ClassifyBox(frustum, center, extent);
	}
}

template <uint32_t QuadrantCount, uint32_t PatchBladeCount>
PGrassRenderer<QuadrantCount, PatchBladeCount>::PGrassRenderer(const Tier a_tier, const uint32_t grassDensity, Buffer* vertexIndicesBuf, Buffer* outerVertexIndicesBuf,
	const uint32_t bladeStride, const uint32_t slopeExtra) :
	tier(a_tier),
	bladeStrideBytes(bladeStride),
	slopeExtraBlades(slopeExtra),
	vertexIndicesBuffer(vertexIndicesBuf),
	outerVertexIndicesBuffer(outerVertexIndicesBuf)
{
	bladeBatchSizeString = std::to_string(tier == Tier::Mid ? MidBladeBatchSize : LowBladeBatchSize);

	CreateBuffers();
	SetDensity(grassDensity);

	GetBladeGeneratorCS();

	GetVertexShader(false, false);
	if (HasDepthPrepass())
		GetVertexShader(true, false);
	if (outerVertexIndicesBuffer) {
		GetVertexShader(false, true);
		if (HasDepthPrepass())
			GetVertexShader(true, true);
	}

	GetPS();
	if (tier == Tier::High)
		GetPS(false, false, true);
	if (!UsesSimpleLighting()) {
		const bool hasLocalLightVariant = globals::features::lightLimitFix.loaded;
		const bool hasWetnessVariant = globals::features::wetnessEffects.loaded;
		if (hasLocalLightVariant)
			GetPS(false, true);
		if (hasWetnessVariant)
			GetPS(true, false);
		if (hasLocalLightVariant && hasWetnessVariant)
			GetPS(true, true);
	}
}

template <uint32_t QuadrantCount, uint32_t PatchBladeCount>
bool PGrassRenderer<QuadrantCount, PatchBladeCount>::UsesGrassCollision() const
{
	return globals::features::grassCollision.loaded && (tier == Tier::High || tier == Tier::Mid);
}

template <uint32_t QuadrantCount, uint32_t PatchBladeCount>
const char* PGrassRenderer<QuadrantCount, PatchBladeCount>::LodDefine() const
{
	switch (tier) {
	case Tier::High:
		return "HIGH_LOD";
	case Tier::Mid:
		return "MID_LOD";
	default:
		return "LOW_LOD";
	}
}

template <uint32_t QuadrantCount, uint32_t PatchBladeCount>
const char* PGrassRenderer<QuadrantCount, PatchBladeCount>::VertexDefine() const
{
	switch (tier) {
	case Tier::High:
		return "HIGH_VERTEX";
	case Tier::Mid:
		return "MID_VERTEX";
	default:
		return "LOW_VERTEX";
	}
}

template <uint32_t QuadrantCount, uint32_t PatchBladeCount>
void PGrassRenderer<QuadrantCount, PatchBladeCount>::CreateBuffers()
{
	quadrantsCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<QuadrantDataArray<QuadrantCount>>(), "PGrass::QuadrantsCB");

	constexpr uint32_t grassSampleCount = QuadrantCount * QuadrantGrassSamples;
	constexpr uint32_t grassCellCount = QuadrantCount * OccupancyTileCount;
	constexpr uint32_t occupancyRowCount = QuadrantCount * OccupancyTilesPerAxis;
	quadrantGrassCellsSB = CreateRangeUpdatedBuffer<uint32_t>(grassCellCount, "PGrass::QuadrantGrassCells");
	quadrantGrassCellsStaging.assign(grassCellCount, 0u);
	quadrantOccupancySB = CreateRangeUpdatedBuffer<uint32_t>(occupancyRowCount, "PGrass::OccupancyRows");
	quadrantOccupancyStaging.assign(occupancyRowCount, 0xFFFFu);
	quadrantOccupancyVersions.resize(QuadrantCount);
	quadrantHeightSB = CreateRangeUpdatedBuffer<float>(grassSampleCount, "PGrass::QuadrantHeights");
	quadrantHeightStaging.assign(grassSampleCount, QuadrantNoHeight);
	tileHeightBoundsSB = CreateRangeUpdatedBuffer<float2>(grassCellCount, "PGrass::TileHeightBounds");
	tileHeightBoundsStaging.resize(grassCellCount);
	stagedQuadrants.resize(QuadrantCount);
	slotDirty.assign(QuadrantCount, 0u);
	slotByWorldQuadrant.reserve(QuadrantCount);

	constexpr uint32_t workItemCapacity = 2u * QuadrantCount * PatchBladeCount * OccupancyTileCount;
	visibleWorkSB = std::make_unique<StructuredBuffer>(StructuredBufferDesc<uint32_t>(workItemCapacity, true), workItemCapacity, "PGrass::VisibleWork");
	visibleWorkSB->CreateSRV();
	workRangeCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<std::array<uint32_t, 4>>(), "PGrass::WorkRange");
	visibleWorkStaging.reserve(workItemCapacity);
	visibleWorkCandidates.reserve(QuadrantCount);
	visibleTilesStaging.reserve(QuadrantCount * OccupancyTileCount);
	occupancyCache.reserve(QuadrantCount * 2u);

	D3D11_BUFFER_DESC argsBufferDesc{};
	argsBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	argsBufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	argsBufferDesc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
	argsBufferDesc.ByteWidth = 10 * sizeof(uint32_t);

	const uint32_t initialArgs[10] = {
		vertexIndicesBuffer->desc.ByteWidth / sizeof(uint16_t), 0, 0, 0, 0,
		outerVertexIndicesBuffer ? outerVertexIndicesBuffer->desc.ByteWidth / sizeof(uint16_t) : 0, 0, 0, 0, 0
	};
	D3D11_SUBRESOURCE_DATA argsInit{ initialArgs, 0, 0 };
	argsBuffer = std::make_unique<Buffer>(argsBufferDesc, &argsInit, "PGrass::IndirectArgs");

	D3D11_UNORDERED_ACCESS_VIEW_DESC argsUAVDesc{};
	argsUAVDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	argsUAVDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	argsUAVDesc.Buffer.NumElements = 10;
	argsUAVDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
	argsBuffer->CreateUAV(argsUAVDesc);
	D3D11_SHADER_RESOURCE_VIEW_DESC argsSRVDesc{};
	argsSRVDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	argsSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
	argsSRVDesc.BufferEx.NumElements = 10;
	argsSRVDesc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
	argsBuffer->CreateSRV(argsSRVDesc);

	if (UsesBatchedDraws()) {
		// Batched tiers draw fixed groups of blades and discard the final batch's unused tail in the VS.
		const uint32_t batchArgs[10]{};
		D3D11_SUBRESOURCE_DATA batchArgsInit{ batchArgs, 0, 0 };
		batchArgsBuffer = std::make_unique<Buffer>(argsBufferDesc, &batchArgsInit, "PGrass::BatchArgs");
		batchArgsBuffer->CreateUAV(argsUAVDesc);
	}

	D3D11_BUFFER_DESC stagingDesc{};
	stagingDesc.Usage = D3D11_USAGE_STAGING;
	stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	stagingDesc.ByteWidth = 10 * sizeof(uint32_t);
	if (SUCCEEDED(globals::d3d::device->CreateBuffer(&stagingDesc, nullptr, argsStaging.put())))
		Util::SetResourceName(argsStaging.get(), "PGrass::IndirectArgsStaging");
}

template <uint32_t QuadrantCount, uint32_t PatchBladeCount>
void PGrassRenderer<QuadrantCount, PatchBladeCount>::SetDensity(const uint32_t grassDensity)
{
	if (density == grassDensity)
		return;

	density = grassDensity;
	patchesPerQuadrant = grassDensity * grassDensity / 4;
	densityString = std::to_string(grassDensity);

	hasCachedWorkList = false;
	patchesPerRow = density / 2u;
	patchRows = (patchesPerQuadrant + patchesPerRow - 1u) / patchesPerRow;
	const float patchWorldSize = 4096.0f / static_cast<float>(density);

	for (uint32_t tileY = 0; tileY < OccupancyTilesPerAxis; ++tileY) {
		for (uint32_t tileX = 0; tileX < OccupancyTilesPerAxis; ++tileX) {
			const uint32_t tile = tileY * OccupancyTilesPerAxis + tileX;
			const uint32_t startX = tileX * patchesPerRow / OccupancyTilesPerAxis;
			const uint32_t startY = tileY * patchRows / OccupancyTilesPerAxis;
			const uint32_t endX = (tileX + 1u) * patchesPerRow / OccupancyTilesPerAxis;
			const uint32_t endY = (tileY + 1u) * patchRows / OccupancyTilesPerAxis;
			tilePatchCounts[tile] = (endX - startX) * (endY - startY);
			tileLocalBounds[tile] = {
				static_cast<float>(startX) * patchWorldSize,
				static_cast<float>(startY) * patchWorldSize,
				static_cast<float>(endX) * patchWorldSize,
				static_cast<float>(endY) * patchWorldSize
			};
		}
	}

	occupancyCache.clear();
	hasUploadedQuadrants = false;  // Tile reach follows the patch size.

	ClearGeneratorCache();
}

template <uint32_t QuadrantCount, uint32_t PatchBladeCount>
void PGrassRenderer<QuadrantCount, PatchBladeCount>::EnsureBladeCapacity(uint64_t requiredBladeCount)
{
	if (bladesSB && requiredBladeCount <= bladeBufferCapacity)
		return;

	const uint64_t allocationQuantum = std::max<uint64_t>(patchesPerQuadrant, 1u);
	const uint64_t maximumCandidateCount = 2u * allocationQuantum * QuadrantCount * (PatchBladeCount + slopeExtraBlades);
	const uint64_t maximumBufferElements = std::numeric_limits<UINT>::max() / bladeStrideBytes;
	const uint64_t maximumCapacity = std::min(maximumCandidateCount, maximumBufferElements);

	if (requiredBladeCount > maximumCapacity) {
		// The generator drops UAV writes past the end, so clamping loses blades rather than corrupting memory.
		if (!bladeCapacityClampLogged) {
			logger::error("[Procedural Grass] {} blade demand {} exceeds the D3D11 buffer limit of {}; excess blades are dropped",
				LodDefine(), requiredBladeCount, maximumCapacity);
			bladeCapacityClampLogged = true;
		}
		requiredBladeCount = maximumCapacity;
		if (bladesSB && requiredBladeCount <= bladeBufferCapacity)
			return;
	}

	uint64_t targetCapacity = std::max(requiredBladeCount, allocationQuantum);
	if (requiredBladeCount > allocationQuantum) {
		// Reserve 12.5% for nearby work entering the frustum.
		const uint64_t demandMargin = std::max(requiredBladeCount / 8u, allocationQuantum);
		targetCapacity = requiredBladeCount + demandMargin;
	}
	if (bladeBufferCapacity != 0) {
		const uint64_t growthMargin = std::max<uint64_t>(bladeBufferCapacity / 2u, allocationQuantum);
		targetCapacity = std::max(targetCapacity, static_cast<uint64_t>(bladeBufferCapacity) + growthMargin);
	}

	targetCapacity = ((targetCapacity + allocationQuantum - 1u) / allocationQuantum) * allocationQuantum;
	targetCapacity = std::clamp(targetCapacity, requiredBladeCount, maximumCapacity);

	const auto newCapacity = static_cast<uint32_t>(targetCapacity);
	auto bladesDesc = StructuredBufferDesc<Blade>(newCapacity, true, false);
	bladesDesc.StructureByteStride = bladeStrideBytes;
	bladesDesc.ByteWidth = bladeStrideBytes * newCapacity;

	bladesSB = std::make_unique<StructuredBuffer>(bladesDesc, newCapacity, "PGrass::Blades");
	bladesSB->CreateUAV();
	bladesSB->CreateSRV();

	logger::info("[Procedural Grass] {} blade buffer high-water mark: {} blades ({:.1f} MiB), {} required",
		LodDefine(), newCapacity, static_cast<double>(bladesDesc.ByteWidth) / (1024.0 * 1024.0), requiredBladeCount);
	bladeBufferCapacity = newCapacity;
}

template <uint32_t QuadrantCount, uint32_t PatchBladeCount>
void PGrassRenderer<QuadrantCount, PatchBladeCount>::ClearGeneratorCache()
{
	generatorCS = nullptr;
	generatorCompileAttempted = false;
}

template <uint32_t QuadrantCount, uint32_t PatchBladeCount>
void PGrassRenderer<QuadrantCount, PatchBladeCount>::ClearShaderCache()
{
	ClearGeneratorCache();
	generatorVariants.clear();
	generatorVariantUse = 0;
	batchArgsCS = nullptr;
	vertexShaders = {};
	pixelShaders = {};
}

template <uint32_t QuadrantCount, uint32_t PatchBladeCount>
void PGrassRenderer<QuadrantCount, PatchBladeCount>::ResetIndirectArgs(ID3D11DeviceContext* ctx, const bool generating)
{
	// The outer list grows down from the end of the blade buffer, so its start instance is the capacity.
	const uint32_t outerIndexCount = outerVertexIndicesBuffer ? outerVertexIndicesBuffer->desc.ByteWidth / sizeof(uint16_t) : 0u;
	const uint32_t args[10] = {
		vertexIndicesBuffer->desc.ByteWidth / sizeof(uint16_t), 0, 0, 0, 0,
		outerIndexCount, 0, 0, 0, generating && outerVertexIndicesBuffer ? bladeBufferCapacity : 0u
	};
	ctx->UpdateSubresource(argsBuffer->resource.get(), 0, nullptr, args, 0, 0);

	if (!generating && batchArgsBuffer) {
		const uint32_t emptyBatchArgs[10]{};
		ctx->UpdateSubresource(batchArgsBuffer->resource.get(), 0, nullptr, emptyBatchArgs, 0, 0);
	}
}

template <uint32_t QuadrantCount, uint32_t PatchBladeCount>
void PGrassRenderer<QuadrantCount, PatchBladeCount>::GenerateBlades(ID3D11DeviceContext* ctx, const std::vector<Quadrant>& quadrants, const GenerateParams& params)
{
	auto* bladeGenerator = GetBladeGeneratorCS();
	auto* batchArgsGenerator = batchArgsBuffer ? GetBatchArgsCS() : nullptr;

	if (!bladeGenerator || (batchArgsBuffer && !batchArgsGenerator) || quadrants.size() > QuadrantCount) {
		ResetIndirectArgs(ctx, false);
		return;
	}

	const float tileReach = params.frustumPadding + 4096.0f / density;
	UploadQuadrantInputs(quadrants, params, tileReach);

	const auto quadrantsBuffer = quadrantsCB->CB();
	ctx->CSSetConstantBuffers(7, 1, &quadrantsBuffer);

	WorkListState workListState{ params, density, static_cast<uint32_t>(quadrants.size()),
		globals::game::frameBufferCached.GetCameraViewProjUnjittered().Transpose(), globals::game::frameBufferCached.GetCameraPosAdjust() };

	// CPU visibility depends only on these inputs. Wind and Hi-Z still run in the generator every frame.
	if (!hasCachedWorkList || !(workListState == lastWorkListState))
		BuildVisibleWorkList(quadrants, workListState);

	EnsureBladeCapacity(cachedRequiredBladeCount);
	DispatchGeneration(ctx, bladeGenerator, batchArgsGenerator);
}

template <uint32_t QuadrantCount, uint32_t PatchBladeCount>
void PGrassRenderer<QuadrantCount, PatchBladeCount>::UploadQuadrantInputs(const std::vector<Quadrant>& quadrants, const GenerateParams& params, const float tileReach)
{
	const bool fadesChanged = !hasUploadedQuadrants || params.lodFadeIn != quadrantDataStaging.lodFadeIn || params.lodFadeOut != quadrantDataStaging.lodFadeOut;
	const bool contentChanged = !hasUploadedQuadrants || params.contentVersion != lastUploadVersion ||
	                            tileReach != lastTileReach || quadrants.size() != lastQuadrantCount;
	if (!contentChanged && !fadesChanged)
		return;

	quadrantDataStaging.lodFadeIn = params.lodFadeIn;
	quadrantDataStaging.lodFadeOut = params.lodFadeOut;

	if (contentChanged) {
		const bool restageAll = !hasUploadedQuadrants || tileReach != lastTileReach;

		slotByWorldQuadrant.clear();
		for (uint32_t i = 0; i < quadrants.size(); ++i)
			if (quadrants[i].inUse)
				slotByWorldQuadrant.emplace(GrassQuadrantKey(quadrants[i].WorldX(), quadrants[i].WorldY()), i);

		// A changed slot also changes the border samples it shares with up to eight neighbours.
		const auto markNeighbours = [&](const Quadrant& quadrant) {
			for (int32_t dy = -1; dy <= 1; ++dy)
				for (int32_t dx = -1; dx <= 1; ++dx)
					if (const auto it = slotByWorldQuadrant.find(GrassQuadrantKey(quadrant.WorldX() + dx, quadrant.WorldY() + dy)); it != slotByWorldQuadrant.end())
						slotDirty[it->second] = 1u;
		};

		for (uint32_t i = static_cast<uint32_t>(quadrants.size()); i < lastQuadrantCount; ++i) {
			if (stagedQuadrants[i].inUse)
				markNeighbours(stagedQuadrants[i]);
			stagedQuadrants[i] = {};
		}

		for (uint32_t i = 0; i < quadrants.size(); ++i) {
			const bool changed = restageAll || !(quadrants[i] == stagedQuadrants[i]);
			if (!changed)
				continue;
			slotDirty[i] = 1u;
			markNeighbours(quadrants[i]);
			if (stagedQuadrants[i].inUse)
				markNeighbours(stagedQuadrants[i]);
		}

		for (uint32_t i = 0; i < quadrants.size(); ++i)
			if (slotDirty[i])
				StageQuadrant(i, quadrants[i], tileReach);
		for (uint32_t i = 0; i < quadrants.size(); ++i)
			if (slotDirty[i])
				PackQuadrantGrassCells(i, quadrants);

		// Upload contiguous runs of dirty slots.
		for (uint32_t first = 0; first < quadrants.size();) {
			if (!slotDirty[first]) {
				++first;
				continue;
			}
			uint32_t end = first;
			while (end < quadrants.size() && slotDirty[end])
				slotDirty[end++] = 0u;
			const uint32_t count = end - first;
			quadrantGrassCellsSB->UpdateRange(quadrantGrassCellsStaging.data() + first * OccupancyTileCount, first * OccupancyTileCount * sizeof(uint32_t), count * OccupancyTileCount * sizeof(uint32_t));
			quadrantOccupancySB->UpdateRange(quadrantOccupancyStaging.data() + first * OccupancyTilesPerAxis, first * OccupancyTilesPerAxis * sizeof(uint32_t), count * OccupancyTilesPerAxis * sizeof(uint32_t));
			quadrantHeightSB->UpdateRange(quadrantHeightStaging.data() + first * QuadrantGrassSamples, first * QuadrantGrassSamples * sizeof(float), count * QuadrantGrassSamples * sizeof(float));
			tileHeightBoundsSB->UpdateRange(tileHeightBoundsStaging.data() + first * OccupancyTileCount, first * OccupancyTileCount * sizeof(float2), count * OccupancyTileCount * sizeof(float2));
			first = end;
		}

		std::copy(quadrants.begin(), quadrants.end(), stagedQuadrants.begin());
		lastUploadVersion = params.contentVersion;
		lastTileReach = tileReach;
		lastQuadrantCount = quadrants.size();
	}

	quadrantsCB->Update(&quadrantDataStaging, offsetof(QuadrantDataArray<QuadrantCount>, data) + quadrants.size() * sizeof(QuadrantData));
	hasUploadedQuadrants = true;
}

template <uint32_t QuadrantCount, uint32_t PatchBladeCount>
void PGrassRenderer<QuadrantCount, PatchBladeCount>::StageQuadrant(const uint32_t index, const Quadrant& quadrant, const float tileReach)
{
	auto& quadrantData = quadrantDataStaging.data[index];
	quadrantData.quadWorldPos = quadrant.worldPos;
	const uint32_t hashX = static_cast<uint32_t>((quadrant.cellX + QuadrantHashCellOffsetX) * 32 + quadrant.x * 16);
	const uint32_t hashY = static_cast<uint32_t>((quadrant.cellY + QuadrantHashCellOffsetY) * 32 + quadrant.y * 16);
	quadrantData.quadrantHash = QuadrantHash(hashX, hashY);
	quadrantData.flags = quadrant.HasLand() ? WorkHasLand : 0u;

	auto* heightDst = quadrantHeightStaging.data() + index * QuadrantGrassSamples;
	if (quadrant.heights)
		std::copy_n(quadrant.heights, QuadrantGrassSamples, heightDst);
	else
		std::fill_n(heightDst, QuadrantGrassSamples, QuadrantNoHeight);
	StageTileHeightBounds(index, quadrant, tileReach);
}

template <uint32_t QuadrantCount, uint32_t PatchBladeCount>
void PGrassRenderer<QuadrantCount, PatchBladeCount>::PackQuadrantGrassCells(const uint32_t index, const std::vector<Quadrant>& quadrants)
{
	const auto& quadrant = quadrants[index];
	auto* cellDst = quadrantGrassCellsStaging.data() + index * OccupancyTileCount;
	auto* rows = quadrantOccupancyStaging.data() + index * OccupancyTilesPerAxis;
	if (!quadrant.grassIds) {
		std::fill_n(cellDst, OccupancyTileCount, 0u);
		std::fill_n(rows, OccupancyTilesPerAxis, 0u);
		quadrantOccupancyVersions[index] = 0;
		return;
	}

	// A locally filled border can be bare in the adjacent quadrant. Unite every sample with the quadrants sharing it so
	// both sides fade from the same values; the union is order-independent, so each slot packs on its own.
	std::array<const uint8_t*, 9> neighbourIds{};
	for (int32_t dy = -1; dy <= 1; ++dy) {
		for (int32_t dx = -1; dx <= 1; ++dx) {
			const auto it = slotByWorldQuadrant.find(GrassQuadrantKey(quadrant.WorldX() + dx, quadrant.WorldY() + dy));
			neighbourIds[(dy + 1) * 3 + dx + 1] = it != slotByWorldQuadrant.end() ? quadrants[it->second].grassIds : nullptr;
		}
	}

	constexpr uint32_t lastSample = QuadrantCellPitch;
	const auto sharedId = [&](const uint32_t x, const uint32_t y) {
		uint8_t id = quadrant.grassIds[y * QuadrantGrassPitch + x];
		const int32_t xSides[2] = { x == 0 ? -1 : (x == lastSample ? 1 : 0), 0 };
		const int32_t ySides[2] = { y == 0 ? -1 : (y == lastSample ? 1 : 0), 0 };
		for (const int32_t sx : xSides) {
			for (const int32_t sy : ySides) {
				if (sx == 0 && sy == 0)
					continue;
				if (const auto* ids = neighbourIds[(sy + 1) * 3 + sx + 1]) {
					const uint32_t nx = sx < 0 ? lastSample : (sx > 0 ? 0 : x);
					const uint32_t ny = sy < 0 ? lastSample : (sy > 0 ? 0 : y);
					id = MergeGrassIds(id, ids[ny * QuadrantGrassPitch + nx]);
				}
			}
		}
		return id;
	};

	std::array<uint8_t, QuadrantGrassSamples> ids;
	for (uint32_t y = 0; y < QuadrantGrassPitch; ++y)
		for (uint32_t x = 0; x < QuadrantGrassPitch; ++x)
			ids[y * QuadrantGrassPitch + x] = (x == 0 || y == 0 || x == lastSample || y == lastSample) ? sharedId(x, y) : quadrant.grassIds[y * QuadrantGrassPitch + x];

	// Pack each 2x2 LAND cell into one uint so bilinear sampling needs one structured-buffer load.
	uint64_t version = GrassHashOffsetBasis;
	for (uint32_t y = 0; y < QuadrantCellPitch; ++y) {
		uint32_t row = 0u;
		for (uint32_t x = 0; x < QuadrantCellPitch; ++x) {
			const uint32_t base = y * QuadrantGrassPitch + x;
			const uint32_t packed = uint32_t(ids[base]) | uint32_t(ids[base + 1]) << 8 |
			                        uint32_t(ids[base + QuadrantGrassPitch]) << 16 | uint32_t(ids[base + QuadrantGrassPitch + 1]) << 24;
			cellDst[y * QuadrantCellPitch + x] = packed;
			if (packed != 0u)
				row |= 1u << x;
		}
		rows[y] = row;
		GrassHashValue(version, row);
	}
	quadrantOccupancyVersions[index] = version;
}

template <uint32_t QuadrantCount, uint32_t PatchBladeCount>
void PGrassRenderer<QuadrantCount, PatchBladeCount>::StageTileHeightBounds(const uint32_t index, const Quadrant& quadrant, const float tileReach)
{
	// Include neighboring LAND samples reached by candidate jitter and clump displacement.
	const uint32_t sampleReach = static_cast<uint32_t>(std::ceil(tileReach / 128.0f));

	for (uint32_t tileY = 0; tileY < OccupancyTilesPerAxis; ++tileY) {
		for (uint32_t tileX = 0; tileX < OccupancyTilesPerAxis; ++tileX) {
			float2 bounds = float2(QuadrantNoHeight, QuadrantNoHeight);

			if (quadrant.heights) {
				const uint32_t minX = tileX > sampleReach ? tileX - sampleReach : 0u;
				const uint32_t minY = tileY > sampleReach ? tileY - sampleReach : 0u;
				const uint32_t maxX = std::min(tileX + sampleReach + 1u, QuadrantCellPitch);
				const uint32_t maxY = std::min(tileY + sampleReach + 1u, QuadrantCellPitch);

				float maxDeltaX = 0.0f;
				float maxDeltaY = 0.0f;
				bounds = float2(std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());

				for (uint32_t y = minY; y <= maxY; ++y) {
					for (uint32_t x = minX; x <= maxX; ++x) {
						const float height = quadrant.heights[y * QuadrantGrassPitch + x];
						bounds.x = std::min(bounds.x, height);
						bounds.y = std::max(bounds.y, height);

						if (x > minX)
							maxDeltaX = std::max(maxDeltaX, std::abs(height - quadrant.heights[y * QuadrantGrassPitch + x - 1u]));
						if (y > minY)
							maxDeltaY = std::max(maxDeltaY, std::abs(height - quadrant.heights[(y - 1u) * QuadrantGrassPitch + x]));
					}
				}

				// Roots can extend their sampled terrain plane after jitter and clump displacement.
				const float extension = (maxDeltaX + maxDeltaY) * (tileReach / 128.0f);
				bounds.x -= extension;
				bounds.y += extension;
			}

			tileHeightBoundsStaging[index * OccupancyTileCount + tileY * OccupancyTilesPerAxis + tileX] = bounds;
		}
	}
}

template <uint32_t QuadrantCount, uint32_t PatchBladeCount>
const typename PGrassRenderer<QuadrantCount, PatchBladeCount>::OccupancyCacheEntry& PGrassRenderer<QuadrantCount, PatchBladeCount>::GetOccupiedTiles(const Quadrant& quadrant, const uint32_t index, const float edgeNoise)
{
	auto& occupancy = occupancyCache[GrassQuadrantKey(quadrant.WorldX(), quadrant.WorldY())];
	const auto* rows = quadrantOccupancyStaging.data() + index * OccupancyTilesPerAxis;
	if (occupancy.cacheVersion == quadrantOccupancyVersions[index] && occupancy.density == density && occupancy.edgeNoise == edgeNoise)
		return occupancy;

	occupancy.occupiedTileCount = 0;
	for (uint32_t tileY = 0; tileY < OccupancyTilesPerAxis; ++tileY) {
		const uint32_t patchStartY = tileY * patchRows / OccupancyTilesPerAxis;
		const uint32_t patchEndY = (tileY + 1u) * patchRows / OccupancyTilesPerAxis;
		for (uint32_t tileX = 0; tileX < OccupancyTilesPerAxis; ++tileX) {
			const uint32_t tile = tileY * OccupancyTilesPerAxis + tileX;
			if (tilePatchCounts[tile] == 0u)
				continue;
			const uint32_t patchStartX = tileX * patchesPerRow / OccupancyTilesPerAxis;
			const uint32_t patchEndX = (tileX + 1u) * patchesPerRow / OccupancyTilesPerAxis;
			if (IsOccupiedGrassTile(rows, patchStartX, patchEndX, patchStartY, patchEndY, density, edgeNoise))
				occupancy.occupiedTiles[occupancy.occupiedTileCount++] = { static_cast<uint16_t>(tile), static_cast<uint16_t>(tilePatchCounts[tile]) };
		}
	}
	occupancy.cacheVersion = quadrantOccupancyVersions[index];
	occupancy.density = density;
	occupancy.edgeNoise = edgeNoise;
	return occupancy;
}

template <uint32_t QuadrantCount, uint32_t PatchBladeCount>
void PGrassRenderer<QuadrantCount, PatchBladeCount>::BuildVisibleWorkList(const std::vector<Quadrant>& quadrants, const WorkListState& state)
{
	visibleWorkStaging.clear();
	visibleWorkCandidates.clear();
	visibleTilesStaging.clear();

	const auto& params = state.params;
	const auto& lodOrigin = params.lodOrigin;
	const auto& lodFadeIn = params.lodFadeIn;
	const auto& lodFadeOut = params.lodFadeOut;
	const auto& cameraPosAdjust = state.cameraPosAdjust;
	const float frustumPadding = params.frustumPadding;
	const bool disableCulls = params.disableCulls;

	const uint32_t maxTilePatchWidth = (patchesPerRow + OccupancyTilesPerAxis - 1u) / OccupancyTilesPerAxis;
	const uint32_t maxTilePatchHeight = (patchRows + OccupancyTilesPerAxis - 1u) / OccupancyTilesPerAxis;
	const uint32_t fullGX = (patchesPerQuadrant + ThreadGroupSize - 1u) / ThreadGroupSize;
	const uint32_t tileGX = (maxTilePatchWidth * maxTilePatchHeight + ThreadGroupSize - 1u) / ThreadGroupSize;
	const bool isLowTier = tier == Tier::Low;
	const bool cullInnerFade = !disableCulls && isLowTier && lodFadeIn.y > 0.0f;
	const bool cullLowOuter = !disableCulls && isLowTier && lodFadeIn.w > 0.0f;
	// High and Mid retain all lanes through their dithered tier transition, then stop.
	const bool cullNearOuter = !disableCulls && (tier == Tier::High || tier == Tier::Mid);
	const float nearOuterRadius = lodFadeOut.y > 0.0f ? lodFadeOut.x + 1.0f / lodFadeOut.y : lodFadeOut.x;
	const float nearOuterRadiusSq = nearOuterRadius * nearOuterRadius;
	const float innerFadeRadius = std::max(lodFadeIn.x - params.fadeInPositionPadding, 0.0f);
	const float innerFadeRadiusSq = innerFadeRadius * innerFadeRadius;
	const float lowOuterRadius = lodFadeIn.w + params.fadeInPositionPadding;
	const auto frustum = BuildSideFrustum(state.viewProj);

	uint64_t requiredBladeCount = 0;
	uint64_t tiledGroupCount = 0;
	uint64_t legacyGroupCount = 0;

	// Distance-band rejection of a world rectangle, shared by quadrant and tile tests.
	const auto rectRejectedByDistance = [&](const float minX, const float minY, const float maxX, const float maxY) {
		const float2 nearest = OffsetToRect(lodOrigin, minX, minY, maxX, maxY);
		const float nearestSq = nearest.x * nearest.x + nearest.y * nearest.y;
		if (cullInnerFade && MaxDistanceSqToRect(lodOrigin, minX, minY, maxX, maxY) < innerFadeRadiusSq)
			return true;
		if (cullLowOuter && std::max(std::abs(nearest.x), std::abs(nearest.y)) > lowOuterRadius)
			return true;
		return cullNearOuter && nearestSq >= nearOuterRadiusSq;
	};

	const auto tileRejected = [&](const uint32_t quadrantIndex, const uint32_t tile, const uint32_t workFlags) {
		const auto& quadrant = quadrants[quadrantIndex];
		const auto& bounds = tileLocalBounds[tile];
		const float minX = quadrant.worldPos.x + bounds.x;
		const float minY = quadrant.worldPos.y + bounds.y;
		const float maxX = quadrant.worldPos.x + bounds.z;
		const float maxY = quadrant.worldPos.y + bounds.w;

		if ((workFlags & WorkInsideFrustum) == 0u) {
			const auto& heightBounds = tileHeightBoundsStaging[quadrantIndex * OccupancyTileCount + tile];
			if (heightBounds.x > QuadrantNoHeight && heightBounds.y >= heightBounds.x) {
				const float minZ = heightBounds.x - 256.0f;
				const float maxZ = heightBounds.y + 300.0f;
				const float3 center = { (minX + maxX) * 0.5f - cameraPosAdjust.x, (minY + maxY) * 0.5f - cameraPosAdjust.y, (minZ + maxZ) * 0.5f - cameraPosAdjust.z };
				const float3 extent = { (maxX - minX) * 0.5f + frustumPadding, (maxY - minY) * 0.5f + frustumPadding, (maxZ - minZ) * 0.5f + frustumPadding };
				if (PGrassRendererQuads::ClassifyBox(frustum, center, extent) == QuadrantFrustumState::Outside)
					return true;
			}
		}

		return rectRejectedByDistance(minX, minY, maxX, maxY);
	};
	if (occupancyCache.size() > static_cast<size_t>(QuadrantCount) * 4u)
		occupancyCache.clear();

	const auto appendWork = [&](const uint32_t patchCount, const uint32_t quadrantIndex, const uint32_t workFlags) {
		for (uint32_t lane = 0; lane < PatchBladeCount; ++lane) {
			visibleWorkStaging.push_back((quadrantIndex & WorkQuadrantMask) | lane << WorkLaneShift | workFlags);

			const uint32_t ownedSlopeExtras = slopeExtraBlades > lane ? 1u + (slopeExtraBlades - 1u - lane) / PatchBladeCount : 0u;
			requiredBladeCount += static_cast<uint64_t>(patchCount) * (1u + ownedSlopeExtras);
		}
	};

	for (uint32_t i = 0; i < quadrants.size(); ++i) {
		const auto& quadrant = quadrants[i];
		if (!quadrant.grassIds)
			continue;
		const bool hasLand = quadrant.HasLand();
		auto frustumState = QuadrantFrustumState::Inside;
		if (!disableCulls) {
			frustumState = ClassifyQuadrantFrustum(quadrant, frustum, cameraPosAdjust, frustumPadding);
			if (frustumState == QuadrantFrustumState::Outside)
				continue;
		}

		const float worldX = quadrant.worldPos.x;
		const float worldY = quadrant.worldPos.y;
		const float quadrantMaxX = worldX + 2048.0f;
		const float quadrantMaxY = worldY + 2048.0f;
		if (!disableCulls && rectRejectedByDistance(worldX, worldY, quadrantMaxX, quadrantMaxY))
			continue;

		uint32_t flags = (hasLand ? WorkHasLand : 0u) | (frustumState == QuadrantFrustumState::Inside ? WorkInsideFrustum : 0u);
		const auto* occupancyRows = quadrantOccupancyStaging.data() + i * OccupancyTilesPerAxis;
		if (std::all_of(occupancyRows, occupancyRows + OccupancyTilesPerAxis, [](const uint32_t row) { return row == 0xFFFFu; }))
			flags |= WorkFullGrass;

		if (disableCulls) {
			legacyGroupCount += static_cast<uint64_t>(PatchBladeCount) * fullGX;
			visibleWorkCandidates.push_back({ i, flags, 0, 0, nullptr });
			continue;
		}

		const auto& occupancy = GetOccupiedTiles(quadrant, i, params.edgeNoise);
		if (occupancy.occupiedTileCount == 0u)
			continue;

		// Odd densities can place the final patch row beyond the nominal quadrant edge.
		const float tileMaxX = worldX + tileLocalBounds.back().z;
		const float tileMaxY = worldY + tileLocalBounds.back().w;
		const bool needsTileTests = (frustumState != QuadrantFrustumState::Inside && hasLand) ||
		                            (cullInnerFade && OffsetToRect(lodOrigin, worldX, worldY, tileMaxX, tileMaxY).LengthSquared() < innerFadeRadiusSq) ||
		                            (cullLowOuter && std::max({ std::abs(worldX - lodOrigin.x), std::abs(tileMaxX - lodOrigin.x), std::abs(worldY - lodOrigin.y), std::abs(tileMaxY - lodOrigin.y) }) > lowOuterRadius) ||
		                            (cullNearOuter && MaxDistanceSqToRect(lodOrigin, worldX, worldY, tileMaxX, tileMaxY) >= nearOuterRadiusSq);
		if (!needsTileTests) {
			legacyGroupCount += static_cast<uint64_t>(PatchBladeCount) * fullGX;
			tiledGroupCount += static_cast<uint64_t>(PatchBladeCount) * occupancy.occupiedTileCount * tileGX;
			visibleWorkCandidates.push_back({ i, flags, 0, occupancy.occupiedTileCount, occupancy.occupiedTiles.data() });
			continue;
		}

		// Reuse these culling results when emitting the selected work layout.
		const uint32_t tileOffset = static_cast<uint32_t>(visibleTilesStaging.size());
		for (uint32_t tileIndex = 0; tileIndex < occupancy.occupiedTileCount; ++tileIndex) {
			const auto& tile = occupancy.occupiedTiles[tileIndex];
			if (!tileRejected(i, tile.tile, flags))
				visibleTilesStaging.push_back(tile);
		}
		const uint32_t visibleTileCount = static_cast<uint32_t>(visibleTilesStaging.size()) - tileOffset;
		if (visibleTileCount == 0u)
			continue;
		legacyGroupCount += static_cast<uint64_t>(PatchBladeCount) * fullGX;
		tiledGroupCount += static_cast<uint64_t>(PatchBladeCount) * visibleTileCount * tileGX;
		visibleWorkCandidates.push_back({ i, flags, tileOffset, visibleTileCount, nullptr });
	}

	const bool useOccupiedTiles = !disableCulls &&
	                              ((cullInnerFade || cullLowOuter) ? tiledGroupCount * 8u <= legacyGroupCount * 7u : tiledGroupCount * 4u <= legacyGroupCount * 3u);
	for (const auto& candidate : visibleWorkCandidates) {
		if (useOccupiedTiles) {
			for (uint32_t tileIndex = candidate.tileOffset; tileIndex < candidate.tileOffset + candidate.tileCount; ++tileIndex) {
				const auto& tile = candidate.cachedTiles ? candidate.cachedTiles[tileIndex] : visibleTilesStaging[tileIndex];
				appendWork(tile.patchCount, candidate.quadrantIndex, candidate.flags | WorkOccupiedTile | static_cast<uint32_t>(tile.tile) << WorkTileShift);
			}
		} else {
			appendWork(patchesPerQuadrant, candidate.quadrantIndex, candidate.flags);
		}
	}

	if (!visibleWorkStaging.empty())
		visibleWorkSB->UpdatePartial(visibleWorkStaging.data(), visibleWorkStaging.size() * sizeof(uint32_t));
	cachedWorkGX = useOccupiedTiles ? tileGX : fullGX;
	cachedRequiredBladeCount = requiredBladeCount;
	lastWorkListState = state;
	hasCachedWorkList = true;
}

template <uint32_t QuadrantCount, uint32_t PatchBladeCount>
void PGrassRenderer<QuadrantCount, PatchBladeCount>::DispatchGeneration(ID3D11DeviceContext* ctx, ID3D11ComputeShader* bladeGenerator, ID3D11ComputeShader* batchArgsGenerator)
{
	ResetIndirectArgs(ctx, true);

	ID3D11UnorderedAccessView* outputUAVs[2] = { bladesSB->UAV(), argsBuffer->uav.get() };
	ctx->CSSetUnorderedAccessViews(0, 2, outputUAVs, nullptr);

	ID3D11ShaderResourceView* heightSRV = quadrantHeightSB->SRV();
	ctx->CSSetShaderResources(3, 1, &heightSRV);
	ID3D11ShaderResourceView* workSRVs[2] = { visibleWorkSB->SRV(), quadrantGrassCellsSB->SRV() };
	ctx->CSSetShaderResources(5, 2, workSRVs);
	ID3D11ShaderResourceView* hiZSRV = globals::hiZPyramid->GetSRV();
	ctx->CSSetShaderResources(8, 1, &hiZSRV);
	ID3D11ShaderResourceView* boundsSRVs[2] = { tileHeightBoundsSB->SRV(), quadrantOccupancySB->SRV() };
	ctx->CSSetShaderResources(11, 2, boundsSRVs);

	if (tier == Tier::High) {
		auto& skylighting = globals::features::skylighting;
		ID3D11ShaderResourceView* skylightingSRV = skylighting.loaded && skylighting.texProbeArray ? skylighting.texProbeArray->srv.get() : nullptr;
		ctx->CSSetShaderResources(50, 1, &skylightingSRV);
	}

	const auto rangeBuffer = workRangeCB->CB();
	ctx->CSSetConstantBuffers(0, 1, &rangeBuffer);

	// The generator indexes work items by group Z, which D3D11 caps per dispatch.
	const auto dispatchRange = [&](uint32_t offset, uint32_t count) {
		ctx->CSSetShader(bladeGenerator, nullptr, 0);
		while (count) {
			const uint32_t slice = std::min<uint32_t>(count, D3D11_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION);
			workRangeCB->Update(std::array<uint32_t, 4>{ offset, 0u, 0u, 0u });
			ctx->Dispatch(cachedWorkGX, 1, slice);
			offset += slice;
			count -= slice;
		}
	};
	if (UsesGrassCollision())
		globals::features::grassCollision.BindProceduralGrassGenerationResources(ctx);

	dispatchRange(0u, static_cast<uint32_t>(visibleWorkStaging.size()));
	ID3D11Buffer* nullRangeBuffer = nullptr;
	ctx->CSSetConstantBuffers(0, 1, &nullRangeBuffer);

	ID3D11ShaderResourceView* nullSRV = nullptr;
	ID3D11UnorderedAccessView* nullOutputUAVs[2] = {};
	ctx->CSSetUnorderedAccessViews(0, 2, nullOutputUAVs, nullptr);
	ctx->CSSetShaderResources(100, 1, &nullSRV);
	if (batchArgsBuffer) {
		ID3D11ShaderResourceView* bladeArgsSRV = argsBuffer->srv.get();
		ctx->CSSetShaderResources(13, 1, &bladeArgsSRV);
		ID3D11UnorderedAccessView* batchArgsUAV = batchArgsBuffer->uav.get();
		ctx->CSSetUnorderedAccessViews(0, 1, &batchArgsUAV, nullptr);
		ctx->CSSetShader(batchArgsGenerator, nullptr, 0);
		ctx->Dispatch(1, 1, 1);
		ID3D11UnorderedAccessView* nullBatchArgsUAV = nullptr;
		ctx->CSSetUnorderedAccessViews(0, 1, &nullBatchArgsUAV, nullptr);
		ctx->CSSetShaderResources(13, 1, &nullSRV);
	}
	ID3D11ShaderResourceView* nullBoundsSRVs[2] = {};
	ctx->CSSetShaderResources(11, 2, nullBoundsSRVs);
}

template <uint32_t QuadrantCount, uint32_t PatchBladeCount>
uint32_t PGrassRenderer<QuadrantCount, PatchBladeCount>::ReadBladeCount()
{
	if (!argsStaging)
		return 0;

	auto ctx = globals::d3d::context;

	if (argsReadbackPending) {
		D3D11_MAPPED_SUBRESOURCE mapped{};
		const HRESULT result = ctx->Map(argsStaging.get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
		if (result == DXGI_ERROR_WAS_STILL_DRAWING)
			return lastBladeCount;

		argsReadbackPending = false;
		if (FAILED(result))
			return lastBladeCount;

		const auto* args = static_cast<const uint32_t*>(mapped.pData);
		lastBladeCount = args[1] + args[6];
		ctx->Unmap(argsStaging.get(), 0);
	}

	ctx->CopyResource(argsStaging.get(), argsBuffer->resource.get());
	argsReadbackPending = true;
	return lastBladeCount;
}

template <uint32_t QuadrantCount, uint32_t PatchBladeCount>
void PGrassRenderer<QuadrantCount, PatchBladeCount>::DrawBlades(ID3D11DeviceContext* ctx, const bool depth,
	ID3D11PixelShader* innerPS, ID3D11PixelShader* outerPS)
{
	if (!bladesSB)
		return;

	auto* innerVS = GetVertexShader(depth, false);
	auto* outerVS = outerVertexIndicesBuffer ? GetVertexShader(depth, true) : nullptr;
	if (!innerVS || (outerVertexIndicesBuffer && !outerVS))
		return;
	// Mid writes opaque depth without a PS; High needs its alpha clip shader.
	if ((!depth || tier == Tier::High) && (!innerPS || (outerVertexIndicesBuffer && !outerPS)))
		return;

	ID3D11ShaderResourceView* srvs[2] = { bladesSB->SRV(), argsBuffer->srv.get() };
	ctx->VSSetShaderResources(0, 2, srvs);

	ID3D11Buffer* drawArgs = batchArgsBuffer ? batchArgsBuffer->resource.get() : argsBuffer->resource.get();
	ctx->IASetIndexBuffer(vertexIndicesBuffer->resource.get(), DXGI_FORMAT_R16_UINT, 0);
	ctx->VSSetShader(innerVS, nullptr, 0);
	ctx->PSSetShader(innerPS, nullptr, 0);
	ctx->DrawIndexedInstancedIndirect(drawArgs, 0);
	if (outerVertexIndicesBuffer) {
		ctx->IASetIndexBuffer(outerVertexIndicesBuffer->resource.get(), DXGI_FORMAT_R16_UINT, 0);
		ctx->VSSetShader(outerVS, nullptr, 0);
		if (outerPS != innerPS)
			ctx->PSSetShader(outerPS, nullptr, 0);
		ctx->DrawIndexedInstancedIndirect(drawArgs, 5 * sizeof(uint32_t));
	}

	ID3D11ShaderResourceView* nullArgsSRV = nullptr;
	ctx->VSSetShaderResources(1, 1, &nullArgsSRV);
}

template <uint32_t QuadrantCount, uint32_t PatchBladeCount>
void PGrassRenderer<QuadrantCount, PatchBladeCount>::RenderDepth(ID3D11DeviceContext* ctx, ID3D11PixelShader* depthClipPS)
{
	DrawBlades(ctx, true, depthClipPS, depthClipPS);
}

template <uint32_t QuadrantCount, uint32_t PatchBladeCount>
void PGrassRenderer<QuadrantCount, PatchBladeCount>::RenderGrass(ID3D11DeviceContext* ctx)
{
	if (!bladesSB)
		return;

	// Blades stay wet while Wetness Effects reports wet surfaces, including as they dry after the rain stops.
	auto& wetnessEffects = globals::features::wetnessEffects;
	bool hasRainWetness = false;
	if (wetnessEffects.loaded && wetnessEffects.settings.EnableWetnessEffects) {
		const auto wetnessData = wetnessEffects.GetCommonBufferData();
		hasRainWetness = wetnessData.Wetness > 0.0f || wetnessData.Raining > 0.0f;
	}

	const bool fullLighting = !UsesSimpleLighting();
	const bool noWetness = fullLighting && wetnessEffects.loaded && !hasRainWetness;
	auto& lightLimitFix = globals::features::lightLimitFix;
	const bool noLocalLights = fullLighting && lightLimitFix.loaded && lightLimitFix.lightCount == 0 && lightLimitFix.strictLightDataTemp.NumStrictLights == 0;

	auto* outerPS = GetPS(noWetness, noLocalLights);
	auto* innerPS = tier == Tier::High && outerVertexIndicesBuffer ? GetPS(noWetness, noLocalLights, true) : outerPS;
	DrawBlades(ctx, false, innerPS, outerPS);
}

template <uint32_t QuadrantCount, uint32_t PatchBladeCount>
void PGrassRenderer<QuadrantCount, PatchBladeCount>::AppendVertexShaderDefines(ShaderDefines& defines) const
{
	defines.push_back({ VertexDefine(), nullptr });
	defines.push_back({ LodDefine(), nullptr });
	if (UsesBatchedDraws())
		defines.push_back({ "BLADE_BATCH_SIZE", bladeBatchSizeString.c_str() });
	if (UsesGrassCollision())
		defines.push_back({ "PGRASS_CACHED_COLLISION", nullptr });
}

template <uint32_t QuadrantCount, uint32_t PatchBladeCount>
ID3D11ComputeShader* PGrassRenderer<QuadrantCount, PatchBladeCount>::GetBladeGeneratorCS()
{
	if (generatorCS || generatorCompileAttempted)
		return generatorCS.get();

	generatorCompileAttempted = true;
	static const std::string threadGroupSizeString = std::to_string(ThreadGroupSize);
	ShaderDefines defines;
	defines.push_back({ LodDefine(), nullptr });
	defines.push_back({ "THREADGROUP_SIZE", threadGroupSizeString.c_str() });
	defines.push_back({ "DENSITY", densityString.c_str() });
	defines.push_back({ "QUADRANT_DATA_SIZE", quadrantCountString.c_str() });
	defines.push_back({ "PATCH_BLADE_COUNT", patchBladeCountString.c_str() });
	const std::string extraSlots = std::to_string(slopeExtraBlades);
	defines.push_back({ "SLOPE_EXTRA_BLADES", extraSlots.c_str() });
	if (outerVertexIndicesBuffer && tier == Tier::Low)
		defines.push_back({ "LOW_OUTER_GEOMETRY", nullptr });
	else if (outerVertexIndicesBuffer && tier == Tier::Mid)
		defines.push_back({ "MID_OUTER_GEOMETRY", nullptr });

	if (tier == Tier::High) {
		if (globals::features::skylighting.loaded && globals::features::skylighting.texProbeArray)
			defines.push_back({ "SKYLIGHTING", nullptr });
		defines.push_back({ "HIGH_GEOMETRY_LOD", nullptr });
		for (auto* feature : Feature::GetFeatureList()) {
			const auto featureName = feature->GetShaderDefineName();
			if (feature->loaded && (featureName == "TERRAIN_SHADOWS" || featureName == "CLOUD_SHADOWS"))
				defines.push_back({ featureName.data(), nullptr });
		}
	}

	if (UsesGrassCollision())
		defines.push_back({ "PGRASS_CACHED_COLLISION", nullptr });

	std::string key = BuildDefineList(defines);
	for (const auto& [name, value] : *globals::state->GetDefines()) {
		key += ';';
		key += name;
		key += '=';
		key += value;
	}
	key += std::format(";{}{}{}{}", globals::state->IsDeveloperMode(), globals::shaderCache->IsDiskCache(),
		globals::state->enablePartialPrecision.load(std::memory_order_relaxed), globals::state->enableAvoidFlowControl.load(std::memory_order_relaxed));

	if (auto it = generatorVariants.find(key); it != generatorVariants.end()) {
		it->second.lastUse = ++generatorVariantUse;
		generatorCS = it->second.shader;
		return generatorCS.get();
	}

	generatorCS.attach(CompileShader<ID3D11ComputeShader>(L"Data\\Shaders\\ProceduralGrass\\PGrassBladeGeneratorCS.hlsl", defines, "cs_5_0"));
	if (generatorCS) {
		// Retain recent configurations without growing the cache for every slider value.
		constexpr size_t MaxGeneratorVariants = 32;
		if (generatorVariants.size() >= MaxGeneratorVariants) {
			const auto oldest = std::min_element(generatorVariants.begin(), generatorVariants.end(), [](const auto& a, const auto& b) {
				return a.second.lastUse < b.second.lastUse;
			});
			generatorVariants.erase(oldest);
		}
		auto& cached = generatorVariants[key];
		cached.shader = generatorCS;
		cached.lastUse = ++generatorVariantUse;
	}
	return generatorCS.get();
}

template <uint32_t QuadrantCount, uint32_t PatchBladeCount>
ID3D11ComputeShader* PGrassRenderer<QuadrantCount, PatchBladeCount>::GetBatchArgsCS()
{
	if (!batchArgsCS) {
		ShaderDefines defines{ { "BLADE_BATCH_SIZE", bladeBatchSizeString.c_str() } };
		if (outerVertexIndicesBuffer)
			defines.push_back({ "PGRASS_OUTER_LIST", nullptr });
		batchArgsCS.attach(CompileShader<ID3D11ComputeShader>(L"Data\\Shaders\\ProceduralGrass\\PGrassBatchArgsCS.hlsl", defines, "cs_5_0"));
	}
	return batchArgsCS.get();
}

template <uint32_t QuadrantCount, uint32_t PatchBladeCount>
void PGrassRenderer<QuadrantCount, PatchBladeCount>::AppendFeatureDefines(ShaderDefines& defines, const bool simpleLighting) const
{
	for (auto* feature : Feature::GetFeatureList()) {
		const auto featureName = feature->GetShaderDefineName();
		if (!feature->loaded || !feature->HasShaderDefine(RE::BSShader::Type::Lighting))
			continue;
		if (featureName == "SKYLIGHTING" && !globals::features::skylighting.texProbeArray)
			continue;

		// Simple lighting keeps only the colour-space and shadowing features it evaluates.
		if (simpleLighting && featureName != "ENABLE_LL" && featureName != "TERRAIN_SHADOWS" && featureName != "CLOUD_SHADOWS" &&
			featureName != "SCREEN_SPACE_SHADOWS" && featureName != "SKYLIGHTING")
			continue;
		defines.push_back({ featureName.data(), nullptr });
	}
}

template <uint32_t QuadrantCount, uint32_t PatchBladeCount>
ID3D11VertexShader* PGrassRenderer<QuadrantCount, PatchBladeCount>::GetVertexShader(const bool depth, const bool outer)
{
	auto& shader = vertexShaders[(depth ? 2 : 0) + (outer ? 1 : 0)];
	if (shader)
		return shader.get();

	ShaderDefines defines;
	if (depth) {
		defines.push_back({ "DEPTH", nullptr });
		if (tier == Tier::High)
			defines.push_back({ "DEPTH_CLIP", nullptr });
	} else {
		AppendFeatureDefines(defines, false);
	}

	if (outer && tier == Tier::High) {
		defines.push_back({ "HIGH_OUTER_VERTEX", nullptr });
		defines.push_back({ LodDefine(), nullptr });
		if (UsesGrassCollision())
			defines.push_back({ "PGRASS_CACHED_COLLISION", nullptr });
	} else {
		AppendVertexShaderDefines(defines);
		if (outer)
			defines.push_back({ tier == Tier::Low ? "LOW_OUTER_VERTEX" : "MID_OUTER_VERTEX", nullptr });
		else if (tier == Tier::High && !depth)
			defines.push_back({ "HIGH_INNER", nullptr });
	}

	shader.attach(CompileShader<ID3D11VertexShader>(L"Data\\Shaders\\ProceduralGrass\\PGrassVS.hlsl", defines, "vs_5_0"));
	return shader.get();
}

template <uint32_t QuadrantCount, uint32_t PatchBladeCount>
ID3D11PixelShader* PGrassRenderer<QuadrantCount, PatchBladeCount>::GetPS(bool noWetness, bool noLocalLights, bool innerHigh)
{
	const bool simpleLighting = UsesSimpleLighting();
	if (simpleLighting) {
		noWetness = false;
		noLocalLights = false;
	}
	innerHigh = innerHigh && tier == Tier::High;

	const size_t variant = (noWetness ? 1 : 0) + (noLocalLights ? 2 : 0) + (innerHigh ? 4 : 0);
	auto& selectedPS = pixelShaders[variant];
	if (!selectedPS) {
		ShaderDefines defines;
		AppendFeatureDefines(defines, simpleLighting);

		defines.push_back({ LodDefine(), nullptr });
		defines.push_back({ VertexDefine(), nullptr });
		if (noWetness)
			defines.push_back({ "PGRASS_DRY_WETNESS", nullptr });
		if (noLocalLights)
			defines.push_back({ "PGRASS_NO_LOCAL_LIGHTS", nullptr });
		if (innerHigh)
			defines.push_back({ "HIGH_INNER", nullptr });

		selectedPS.attach(CompileShader<ID3D11PixelShader>(L"Data\\Shaders\\ProceduralGrass\\PGrassPS.hlsl", defines, "ps_5_0"));
	}

	return selectedPS.get();
}

template <uint32_t QuadrantCount, uint32_t PatchBladeCount>
template <typename ShaderT>
ShaderT* PGrassRenderer<QuadrantCount, PatchBladeCount>::CompileShader(const wchar_t* path, std::vector<std::pair<const char*, const char*>>& defines, const char* programType)
{
	logger::info("[Procedural Grass] Compiling {} - {}", std::filesystem::path(path).string(), BuildDefineList(defines));
	return static_cast<ShaderT*>(Util::CompileShader(path, defines, programType));
}

template <uint32_t QuadrantCount, uint32_t PatchBladeCount>
std::string PGrassRenderer<QuadrantCount, PatchBladeCount>::BuildDefineList(std::span<const std::pair<const char*, const char*>> defines)
{
	std::string out;
	out.reserve(defines.size() * 16);
	for (const auto& [name, value] : defines) {
		if (!out.empty())
			out += ", ";
		out += name;
		if (value) {
			out += ' ';
			out += value;
		}
	}
	return out;
}

template class PGrassRenderer<HighTierQuadrantCap, 4>;
template class PGrassRenderer<MidTierQuadrantCap, MidPatchBladeCount>;
template class PGrassRenderer<LowTierQuadrantCap, 1>;
