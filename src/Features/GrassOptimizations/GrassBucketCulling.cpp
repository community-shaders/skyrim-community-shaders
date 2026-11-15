#include "Features/GrassOptimizations.h"
#include "Profiler.h"

void GrassOptimizations::CullGrass(const RE::NiCamera* camera)
{
	auto* ctx = globals::d3d::context;

	RE::NiFrustumPlanes frustum{};
	constexpr float frustumEdgePadding = 128.0f;
	Util::ComputeFrustumPlanes(frustum, camera->GetRuntimeData2().viewFrustum, camera->world, frustumEdgePadding);

	const RE::NiPoint3 cameraTranslation = camera->world.translate;
	const __m128 cameraPosition = _mm_setr_ps(cameraTranslation.x, cameraTranslation.y, cameraTranslation.z, 0.0f);

	FrustumSoA frustumSoA;
	BuildFrustumSoA(frustumSoA, frustum);

	// Setup runs with the main camera's PerFrame data.
	const auto& frameBuffer = globals::game::frameBufferCached;
	// Match the projection jitter used by the depth buffer.
	const auto& mainCameraViewProj = frameBuffer.GetCameraViewProj();
	const auto& mainCameraPos = frameBuffer.GetCameraPosAdjust();

	CullParamsCB params{};

	for (int i = 0; i < 6; ++i) {
		params.frustumPlanes[i][0] = frustum.cullingPlanes[i].normal.x;
		params.frustumPlanes[i][1] = frustum.cullingPlanes[i].normal.y;
		params.frustumPlanes[i][2] = frustum.cullingPlanes[i].normal.z;
		params.frustumPlanes[i][3] = frustum.cullingPlanes[i].constant;
	}

	std::memcpy(params.cameraViewProj, &mainCameraViewProj, sizeof(params.cameraViewProj));
	params.cameraPos[0] = mainCameraPos.x;
	params.cameraPos[1] = mainCameraPos.y;
	params.cameraPos[2] = mainCameraPos.z;

	params.minPixelSize = settings.MinPixelSize;
	params.fullDetailPixelSize = settings.FullDetailPixelSize;
	params.lodMinKeep = settings.MinDensity;
	params.lodFadeBand = 0.15f;

	const auto& viewFrustum = camera->GetRuntimeData2().viewFrustum;
	const auto [screenWidth, screenHeight] = globals::game::renderer->GetScreenSize();
	params.meshCostBias = settings.MeshCostBias;
	params.projScale = screenHeight / (2.0f * std::abs(viewFrustum.fTop));
	params.maxDistSq = maxDistSq;
	params.edgeFadeStart = std::clamp(settings.EdgeFadeStart, 0.0f, 1.0f);

	params.alphaParam1 = grassStartFadeDistance;
	params.alphaParam2 = maxGrassDistance;
	params.fadeNow = timeAccum;
	params.fadeInTimeRcp = fadeInTimeRcp;

	const float collisionDistance = std::max(0.0f, settings.CollisionDistance);
	params.invisibleFadeCull = settings.InvisibleFadeCull;
	params.simpleShadingPixelSize = std::max(0.0f, settings.SimpleShadingPixelSize);
	params.collisionDistSq = collisionDistance * collisionDistance;
	params.midLODPixelSize = settings.MidLODPixelSize;
	params.farLODPixelSize = settings.EnableMidLOD ? std::min(settings.FarLODPixelSize, settings.MidLODPixelSize) : settings.FarLODPixelSize;
	params.densityDistanceRcp = 1.0f / std::max(vanillaMaxDistance, 1e-4f);

	params.meshLODBandPx = std::max(0.0f, settings.MeshLODBandPixels);
	const auto* rowW = params.cameraViewProj[3];
	params.projectionWLength = std::sqrt(rowW[0] * rowW[0] + rowW[1] * rowW[1] + rowW[2] * rowW[2]);
	params.occlusionBias = std::max(0.0f, settings.OcclusionBias);
	params.costBiasStartDist = std::max(0.0f, settings.CostBiasStartDistance);

	visibleBuckets.clear();
	sliceTableCPU.clear();

	rebuildBudget = 1;
	globals::profiler->BeginPass("GrassOptimizations::SliceCull");
	for (auto& bucket : bucketStore.buckets | std::views::values) {
		bucket.ResetCullState();
		const auto* texture = bucket.drawResources.diffuseTexture.get();
		if (!bucket.totalInstances || !bucket.uploadedSlices || !texture || !texture->rendererTexture)
			continue;

		bucketStore.PrepareBucket(bucket);
		CullBucketSlices(bucket, frustumSoA, cameraPosition);
		if (bucket.sliceTableCount)
			visibleBuckets.push_back(&bucket);
	}

	std::erase_if(visibleBuckets, [&](GrassBucket* bucket) {
		auto* texture = bucket->drawResources.diffuseTexture.get();
		auto* textureView = texture->rendererTexture->resourceView;

		if (bucket->resolvedTextureView != textureView || !textureView) {
			const bool pbr = bucket->drawResources.key.draw.pbrMaterial;
			bucket->complexSRV = pbr ? nullptr : bucketStore.meshLibrary.GetTextureClassification(texture, ctx);
			bucket->resolvedTextureView = pbr || bucket->complexSRV ? textureView : nullptr;
		}

		bucketStore.UploadCullGroups(*bucket, ctx);

		if (bucket->residentGroups < bucket->cullGroups.size())
			ClipVisibleGroups(*bucket, bucket->residentGroups);

		if (bucket->groupCount)
			return false;

		bucket->ResetCullState();
		return true;
	});

	globals::profiler->EndPass();

	UploadCullState(ctx, params);
}

ID3D11ComputeShader* GrassOptimizations::GetCullCS()
{
	if (!cullCS && !cullCSFailed) {
		cullCS.attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\GrassOptimizations\\GrassCullingCS.hlsl", {}, "cs_5_0")));
		if (!cullCS) {
			cullCSFailed = true;
			logger::error("[GRASS OPTIMIZATIONS] cull CS load failed; optimized drawing disabled");
		}
	}

	return cullCS.get();
}

bool GrassOptimizations::DispatchCull(ID3D11DeviceContext* ctx)
{
	static_assert((size_t)GrassMeshLibrary::LODTier::kMiddle == 0 && (size_t)GrassMeshLibrary::LODTier::kFar == 1 && kTierCount == 3,
		"GrassCullingCS tiers must match LODTier");

	try {
		if (!cullDispatchCB)
			cullDispatchCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<CullDispatchCB>(false), "GrassOptimizations::CullDispatchCB");

		if (cullEntries.size() > cullBucketBufferCapacity) {
			const uint32_t capacity = std::max<uint32_t>(64, std::bit_ceil((uint32_t)cullEntries.size()));
			cullBucketBuffer = GrassBucketStore::CreateStructuredBuffer(capacity, sizeof(CullBucketEntry), "GrassOptimizations::CullBuckets");
			cullBucketBufferCapacity = capacity;
		}
	} catch (...) {
		logger::error("[GRASS OPTIMIZATIONS] cull inputs create failed buckets={}", cullEntries.size());
		return false;
	}

	// Contiguous dispatch ranges let the shader find each bucket by binary search.
	CullDispatchCB params{ (uint32_t)cullEntries.size(), 0, 0, 0 };
	for (auto& entry : cullEntries) {
		entry.dispatchStart = params.groupCount;
		params.groupCount += entry.cull.groupCount;
	}

	if (!params.groupCount)
		return true;
	params.dispatchWidth = DispatchWidth(params.groupCount);

	const D3D11_BOX box{ 0, 0, 0, (UINT)(cullEntries.size() * sizeof(CullBucketEntry)), 1, 1 };
	ctx->UpdateSubresource(cullBucketBuffer->resource.get(), 0, &box, cullEntries.data(), 0, 0);
	cullDispatchCB->Update(params);

	// UploadCullState binds Hi-Z, the slice table, cull parameters and the argument UAV.
	ID3D11ShaderResourceView* pools[2] = { bucketStore.InstancePoolSRV(), bucketStore.GroupPoolSRV() };
	ctx->CSSetShaderResources(0, 2, pools);
	ID3D11ShaderResourceView* buckets = cullBucketBuffer->srv.get();
	ctx->CSSetShaderResources(4, 1, &buckets);
	ID3D11Buffer* paramsCB = cullDispatchCB->CB();
	ctx->CSSetConstantBuffers(1, 1, &paramsCB);
	ID3D11UnorderedAccessView* output = bucketStore.cullOutput.records->uav.get();
	ctx->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
	ctx->Dispatch(params.dispatchWidth, (params.groupCount + params.dispatchWidth - 1) / params.dispatchWidth, 1);

	return true;
}

bool GrassOptimizations::IsMainCameraPerFrame(const RE::NiPoint3& mainCameraPos)
{
	const auto& posAdjust = globals::game::frameBufferCached.GetCameraPosAdjust();
	const RE::NiPoint3 eyeOffset{ posAdjust.x - mainCameraPos.x, posAdjust.y - mainCameraPos.y, posAdjust.z - mainCameraPos.z };

	return eyeOffset.SqrLength() < 64.0f * 64.0f;
}

bool GrassOptimizations::HasCurrentCull() const
{
	return lastFrame == globals::game::graphicsState->frameCount;
}

void GrassOptimizations::BuildFrustumSoA(FrustumSoA& out, const RE::NiFrustumPlanes& frustum)
{
	constexpr RE::NiFrustumPlanes::ActivePlane planeBits[RE::NiFrustumPlanes::Planes::kTotal] = {
		RE::NiFrustumPlanes::ActivePlane::kNear, RE::NiFrustumPlanes::ActivePlane::kFar,
		RE::NiFrustumPlanes::ActivePlane::kLeft, RE::NiFrustumPlanes::ActivePlane::kRight,
		RE::NiFrustumPlanes::ActivePlane::kTop, RE::NiFrustumPlanes::ActivePlane::kBottom
	};

	// Inactive planes must pass every box test.
	alignas(16) float nx[8], ny[8], nz[8], d[8];
	for (uint32_t i = 0; i < 8; ++i) {
		nx[i] = ny[i] = nz[i] = 0.0f;
		d[i] = -1.0f;
	}

	for (uint32_t i = 0; i < 6; ++i) {
		if (!frustum.activePlanes.any(planeBits[i]))
			continue;
		const auto& plane = frustum.cullingPlanes[i];
		nx[i] = plane.normal.x;
		ny[i] = plane.normal.y;
		nz[i] = plane.normal.z;
		d[i] = plane.constant;
	}

	for (uint32_t planeBatch = 0; planeBatch < 2; ++planeBatch) {
		out.nx[planeBatch] = _mm_load_ps(nx + planeBatch * 4);
		out.ny[planeBatch] = _mm_load_ps(ny + planeBatch * 4);
		out.nz[planeBatch] = _mm_load_ps(nz + planeBatch * 4);
		out.d[planeBatch] = _mm_load_ps(d + planeBatch * 4);
	}

	for (uint32_t i = 0; i < 6; ++i) {
		out.planes[i] = { _mm_set1_ps(nx[i]), _mm_set1_ps(ny[i]), _mm_set1_ps(nz[i]), _mm_set1_ps(d[i]) };
	}
}

bool GrassOptimizations::ClassifyAabb(const FrustumSoA& frustum, __m128 minimum, __m128 maximum, uint32_t& intersectingPlanes)
{
	intersectingPlanes = 0;
	const __m128 minX = _mm_shuffle_ps(minimum, minimum, _MM_SHUFFLE(0, 0, 0, 0));
	const __m128 minY = _mm_shuffle_ps(minimum, minimum, _MM_SHUFFLE(1, 1, 1, 1));
	const __m128 minZ = _mm_shuffle_ps(minimum, minimum, _MM_SHUFFLE(2, 2, 2, 2));
	const __m128 maxX = _mm_shuffle_ps(maximum, maximum, _MM_SHUFFLE(0, 0, 0, 0));
	const __m128 maxY = _mm_shuffle_ps(maximum, maximum, _MM_SHUFFLE(1, 1, 1, 1));
	const __m128 maxZ = _mm_shuffle_ps(maximum, maximum, _MM_SHUFFLE(2, 2, 2, 2));

	for (uint32_t planeBatch = 0; planeBatch < 2; ++planeBatch) {
		// Choose the farthest corner using the plane normal's sign bits.
		const __m128 positiveX = _mm_blendv_ps(maxX, minX, frustum.nx[planeBatch]);
		const __m128 positiveY = _mm_blendv_ps(maxY, minY, frustum.ny[planeBatch]);
		const __m128 positiveZ = _mm_blendv_ps(maxZ, minZ, frustum.nz[planeBatch]);

		const __m128 maxProjection = _mm_add_ps(
			_mm_add_ps(_mm_mul_ps(frustum.nx[planeBatch], positiveX), _mm_mul_ps(frustum.ny[planeBatch], positiveY)),
			_mm_mul_ps(frustum.nz[planeBatch], positiveZ));

		if (_mm_movemask_ps(_mm_cmplt_ps(_mm_sub_ps(maxProjection, frustum.d[planeBatch]), _mm_setzero_ps())))
			return false;

		// Skip child tests for planes that contain the whole box.
		const __m128 negativeX = _mm_blendv_ps(minX, maxX, frustum.nx[planeBatch]);
		const __m128 negativeY = _mm_blendv_ps(minY, maxY, frustum.ny[planeBatch]);
		const __m128 negativeZ = _mm_blendv_ps(minZ, maxZ, frustum.nz[planeBatch]);
		const __m128 minProjection = _mm_add_ps(_mm_add_ps(_mm_mul_ps(frustum.nx[planeBatch], negativeX), _mm_mul_ps(frustum.ny[planeBatch], negativeY)), _mm_mul_ps(frustum.nz[planeBatch], negativeZ));
		const uint32_t insideMask = _mm_movemask_ps(_mm_cmpge_ps(_mm_sub_ps(minProjection, frustum.d[planeBatch]), _mm_setzero_ps()));
		intersectingPlanes |= ((~insideMask) & 0xFu) << (planeBatch * 4);
	}

	intersectingPlanes &= 0x3Fu;

	return true;
}

uint32_t GrassOptimizations::LaneMask(uint32_t remaining)
{
	return (1u << std::min(4u, remaining)) - 1u;
}

void GrassOptimizations::CullBucketSlices(GrassBucket& bucket, const FrustumSoA& frustumSoA, __m128 cameraPosition)
{
	bucket.sliceTableOffset = (uint32_t)sliceTableCPU.size();
	if (bucket.rebuildPending && rebuildBudget) {
		--rebuildBudget;
		bucket.clustersValid = false;
	}
	if (!bucket.clustersValid || bucket.clusteredSlices != bucket.uploadedSlices)
		bucketStore.MergeSlicesIntoRuns(bucket);
	if (bucket.coarseDirty)
		GrassBucketStore::RecomputeCoarseBounds(bucket);
	const auto& bounds = bucket.coarseBounds;
	const float pad = bucket.boundsPadding;
	const __m128 bucketMin = _mm_setr_ps(bounds.lo[0] - pad, bounds.lo[1] - pad, bounds.lo[2] - pad, 0.0f);
	const __m128 bucketMax = _mm_setr_ps(bounds.hi[0] + pad, bounds.hi[1] + pad, bounds.hi[2] + pad, 0.0f);
	const __m128 nearest = _mm_max_ps(_mm_max_ps(_mm_sub_ps(bucketMin, cameraPosition), _mm_sub_ps(cameraPosition, bucketMax)), _mm_setzero_ps());
	if (_mm_cvtss_f32(_mm_dp_ps(nearest, nearest, 0x71)) > maxDistSq)
		return;
	uint32_t planeMask;
	if (!ClassifyAabb(frustumSoA, bucketMin, bucketMax, planeMask))
		return;
	const __m128 signBit = _mm_set1_ps(-0.0f);
	const __m128 farthest = _mm_max_ps(_mm_andnot_ps(signBit, _mm_sub_ps(bucketMin, cameraPosition)), _mm_andnot_ps(signBit, _mm_sub_ps(bucketMax, cameraPosition)));
	const bool allWithinDistance = _mm_cvtss_f32(_mm_dp_ps(farthest, farthest, 0x71)) <= maxDistSq;
	if (!planeMask && allWithinDistance) {
		if (!bucket.cullGroups.empty())
			AppendVisibleGroups(bucket, 0, (uint32_t)bucket.cullGroups.size());
	} else if (planeMask && bucket.sliceBlocksUseful) {
		CullBucketSliceBlocks(bucket, frustumSoA, cameraPosition, planeMask, allWithinDistance);
	} else {
		CullSliceRuns(bucket, frustumSoA, cameraPosition, 0, bucket.sliceRunCount, planeMask, allWithinDistance);
	}
}

void GrassOptimizations::CullBucketSliceBlocks(GrassBucket& bucket, const FrustumSoA& frustumSoA, __m128 cameraPosition, uint32_t planeMask, bool allWithinDistance)
{
	const __m128 zero = _mm_setzero_ps();
	const __m128 cameraX = _mm_shuffle_ps(cameraPosition, cameraPosition, _MM_SHUFFLE(0, 0, 0, 0));
	const __m128 cameraY = _mm_shuffle_ps(cameraPosition, cameraPosition, _MM_SHUFFLE(1, 1, 1, 1));
	const __m128 cameraZ = _mm_shuffle_ps(cameraPosition, cameraPosition, _MM_SHUFFLE(2, 2, 2, 2));
	const __m128 maxDistanceSq = _mm_set1_ps(maxDistSq);

	const uint32_t blockCount = (bucket.sliceRunCount + GrassBucket::kRunsPerCullBlock - 1) / GrassBucket::kRunsPerCullBlock;
	for (uint32_t firstBlock = 0; firstBlock < blockCount; firstBlock += 4) {
		const auto& batch = bucket.sliceBlockBatches[firstBlock / 4];
		const BoxBatch blocks(batch, _mm_set1_ps(bucket.boundsPadding));
		uint32_t visibleMask = LaneMask(blockCount - firstBlock);
		uint32_t withinDistanceMask = visibleMask;

		if (!allWithinDistance) {
			visibleMask &= _mm_movemask_ps(_mm_cmple_ps(blocks.NearestDistanceSq(cameraX, cameraY, cameraZ), maxDistanceSq));
			if (!visibleMask)
				continue;
			withinDistanceMask = _mm_movemask_ps(_mm_cmple_ps(blocks.FarthestDistanceSq(cameraX, cameraY, cameraZ), maxDistanceSq));
		}

		// A plane that contains a whole block needs no test on that block's runs.
		__m128i childPlaneMasksSIMD = _mm_set1_epi32(planeMask);
		for (uint32_t remainingPlanes = planeMask; visibleMask && remainingPlanes; remainingPlanes &= remainingPlanes - 1) {
			const uint32_t planeIndex = std::countr_zero(remainingPlanes);

			const auto& plane = frustumSoA.planes[planeIndex];
			visibleMask &= ~_mm_movemask_ps(_mm_cmplt_ps(blocks.MaxPlaneDistance(plane), zero));
			const __m128i insideMask = _mm_castps_si128(_mm_cmpge_ps(blocks.MinPlaneDistance(plane), zero));
			childPlaneMasksSIMD = _mm_andnot_si128(_mm_and_si128(insideMask, _mm_set1_epi32(1 << planeIndex)), childPlaneMasksSIMD);
		}

		alignas(16) uint32_t childPlaneMasks[4];
		_mm_store_si128((__m128i*)childPlaneMasks, childPlaneMasksSIMD);

		for (; visibleMask; visibleMask &= visibleMask - 1) {
			const uint32_t lane = std::countr_zero(visibleMask);
			const bool withinDistance = (withinDistanceMask >> lane) & 1u;

			if (!childPlaneMasks[lane] && withinDistance) {
				AppendVisibleGroups(bucket, batch.firstGroup[lane], batch.groupCount[lane]);
				continue;
			}

			const uint32_t firstRun = (firstBlock + lane) * GrassBucket::kRunsPerCullBlock;
			const uint32_t endRun = std::min(firstRun + GrassBucket::kRunsPerCullBlock, bucket.sliceRunCount);
			CullSliceRuns(bucket, frustumSoA, cameraPosition, firstRun, endRun, childPlaneMasks[lane], withinDistance);
		}
	}
}

void GrassOptimizations::CullSliceRuns(GrassBucket& bucket, const FrustumSoA& frustumSoA, __m128 cameraPosition, uint32_t firstRun, uint32_t endRun, uint32_t planeMask, bool withinDistance)
{
	const __m128 zero = _mm_setzero_ps();
	const __m128 cameraX = _mm_shuffle_ps(cameraPosition, cameraPosition, _MM_SHUFFLE(0, 0, 0, 0));
	const __m128 cameraY = _mm_shuffle_ps(cameraPosition, cameraPosition, _MM_SHUFFLE(1, 1, 1, 1));
	const __m128 cameraZ = _mm_shuffle_ps(cameraPosition, cameraPosition, _MM_SHUFFLE(2, 2, 2, 2));
	const __m128 maxDistanceSq = _mm_set1_ps(maxDistSq);

	for (; firstRun < endRun; firstRun += 4) {
		const auto& batch = bucket.sliceRunBatches[firstRun / 4];
		const BoxBatch runs(batch, _mm_set1_ps(bucket.boundsPadding));
		uint32_t visibleMask = LaneMask(endRun - firstRun);
		if (!withinDistance)
			visibleMask &= _mm_movemask_ps(_mm_cmple_ps(runs.NearestDistanceSq(cameraX, cameraY, cameraZ), maxDistanceSq));

		for (uint32_t remainingPlanes = planeMask; visibleMask && remainingPlanes; remainingPlanes &= remainingPlanes - 1)
			visibleMask &= ~_mm_movemask_ps(_mm_cmplt_ps(runs.MaxPlaneDistance(frustumSoA.planes[std::countr_zero(remainingPlanes)]), zero));

		// Consume lanes in order so adjacent visible groups still coalesce.
		for (; visibleMask; visibleMask &= visibleMask - 1) {
			const uint32_t lane = std::countr_zero(visibleMask);
			AppendVisibleGroups(bucket, batch.firstGroup[lane], batch.groupCount[lane]);
		}
	}
}

void GrassOptimizations::ClipVisibleGroups(GrassBucket& bucket, uint32_t groupLimit)
{
	// In-place compaction never overwrites an unread entry.
	const uint32_t begin = bucket.sliceTableOffset;
	const uint32_t end = begin + bucket.sliceTableCount;
	uint32_t kept = begin;
	uint32_t visible = 0;

	for (uint32_t entry = begin; entry < end; ++entry) {
		const auto [firstGroup, visibleStart] = sliceTableCPU[entry];
		const uint32_t count = (entry + 1 < end ? sliceTableCPU[entry + 1].second : bucket.groupCount) - visibleStart;
		const uint32_t clipped = firstGroup < groupLimit ? std::min(count, groupLimit - firstGroup) : 0;
		if (!clipped)
			continue;
		sliceTableCPU[kept++] = { firstGroup, visible };
		visible += clipped;
	}

	bucket.sliceTableCount = kept - begin;
	bucket.groupCount = visible;
}

void GrassOptimizations::AppendVisibleGroups(GrassBucket& bucket, uint32_t firstGroup, uint32_t groupCount)
{
	if (!groupCount)
		return;

	bool extendsLast = false;
	if (bucket.sliceTableCount) {
		const auto& [sourceStart, visibleStart] = sliceTableCPU.back();
		const uint32_t previousCount = bucket.groupCount - visibleStart;
		extendsLast = sourceStart + previousCount == firstGroup;
	}

	if (!extendsLast) {
		sliceTableCPU.emplace_back(firstGroup, bucket.groupCount);
		++bucket.sliceTableCount;
	}

	bucket.groupCount += groupCount;
}

void GrassOptimizations::DisableOptimizedDraws()
{
	for (auto& [key, bucket] : bucketStore.buckets)
		bucket.ResetCullState();

	visibleBuckets.clear();
}

void GrassOptimizations::UploadCullState(ID3D11DeviceContext* ctx, CullParamsCB& params)
{
	if (visibleBuckets.empty()) {
		bucketStore.PrepareCullOutputs({});
		return;
	}

	// End setup before Hi-Z opens its own profiler passes.
	globals::profiler->BeginPass("GrassOptimizations::CullSetup");

	ID3D11ShaderResourceView* nullInstanceView = nullptr;
	ctx->VSSetShaderResources(2, 1, &nullInstanceView);

	const bool prepared = bucketStore.PrepareCullOutputs(visibleBuckets) && UploadCullBucketConstants(ctx) && UploadSliceTable(ctx);
	globals::profiler->EndPass();
	if (!prepared) {
		DisableOptimizedDraws();
		return;
	}

	// Hi-Z changes compute bindings, so build it before binding the culling shader.
	const bool useHiZ = settings.EnableOcclusionCulling && globals::hiZPyramid->Build(globals::d3d::device, ctx);
	globals::profiler->BeginPass("GrassOptimizations::InstanceCull");
	params.hiZEnabled = useHiZ ? 1.0f : 0.0f;
	const auto hiZUVScale = globals::hiZPyramid->GetUVScale();
	params.hiZSizeX = hiZUVScale.x;
	params.hiZSizeY = hiZUVScale.y;
	params.hiZMipCount = (float)globals::hiZPyramid->GetMipCount();
	cullParamsCB->Update(params);

	ID3D11Buffer* paramsCB = cullParamsCB->CB();
	ctx->CSSetConstantBuffers(0, 1, &paramsCB);
	ctx->CSSetShader(cullCS.get(), nullptr, 0);

	const uint32_t argumentCount = (uint32_t)visibleBuckets.size() * kTierCount * kArgsStride / sizeof(uint32_t);
	cullArguments.assign(argumentCount, 0);

	for (const GrassBucket* bucket : visibleBuckets) {
		auto* arguments = cullArguments.data() + bucket->cullSlot * kTierCount * kArgsStride / sizeof(uint32_t);
		arguments[0] = 3u * bucket->drawResources.key.triCount;
		for (uint32_t tier = 1; tier < kTierCount; ++tier) {
			if (const auto* lod = bucket->lodMeshes[tier - 1])
				arguments[tier * kArgsStride / sizeof(uint32_t)] = lod->indexCount;
		}
	}

	const D3D11_BOX argumentBox{ 0, 0, 0, argumentCount * (UINT)sizeof(uint32_t), 1, 1 };
	ctx->UpdateSubresource(bucketStore.arguments->resource.get(), 0, &argumentBox, cullArguments.data(), 0, 0);

	ID3D11ShaderResourceView* sharedSRVs[2] = { useHiZ ? globals::hiZPyramid->GetSRV() : nullptr, sliceTable->srv.get() };
	ctx->CSSetShaderResources(2, 2, sharedSRVs);
	ID3D11UnorderedAccessView* argumentsUAV = bucketStore.arguments->uav.get();
	ctx->CSSetUnorderedAccessViews(1, 1, &argumentsUAV, nullptr);

	const bool dispatched = DispatchCull(ctx);

	ID3D11UnorderedAccessView* nullUAVs[2] = {};
	ctx->CSSetUnorderedAccessViews(0, (UINT)std::size(nullUAVs), nullUAVs, nullptr);
	ID3D11ShaderResourceView* nullSRVs[5] = {};
	ctx->CSSetShaderResources(0, (UINT)std::size(nullSRVs), nullSRVs);
	ctx->CSSetShader(nullptr, nullptr, 0);
	globals::profiler->EndPass();

	if (!dispatched) {
		DisableOptimizedDraws();
		return;
	}

	// Present can destroy buckets, so do not retain their pointers across frames.
	visibleBuckets.clear();
}

uint32_t GrassOptimizations::DispatchWidth(uint32_t groupCount)
{
	return std::min(groupCount, (uint32_t)D3D11_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION);
}

void GrassBucketStore::PrepareBucket(GrassBucket& bucket)
{
	const uint32_t lodState = (meshLibrary.GetLODGeneration() << 3) | (frameParams.enableMeshLOD ? 1u : 0u) | (frameParams.enableMidLOD ? 2u : 0u) |
	                          (frameParams.enableFarLOD ? 4u : 0u);

	if (bucket.preparedLODState != lodState) {
		bucket.preparedLODState = lodState;
		bucket.lodMeshes = {};
		const auto& draw = bucket.drawResources.key.draw;
		const auto center = DirectX::XMVectorSet(draw.boundCenter[0], draw.boundCenter[1], draw.boundCenter[2], 0.0f);
		bucket.boundExtent = DirectX::XMVectorGetX(DirectX::XMVector3Length(center)) + draw.modelRadius;

		if (frameParams.enableMeshLOD) {
			meshLibrary.EnsureLODMeshes(*bucket.drawResources.key.mesh);
			if (frameParams.enableMidLOD)
				bucket.lodMeshes[0] = meshLibrary.GetLODMesh(*bucket.drawResources.key.mesh, GrassMeshLibrary::LODTier::kMiddle);
			if (frameParams.enableFarLOD)
				bucket.lodMeshes[1] = meshLibrary.GetLODMesh(*bucket.drawResources.key.mesh, GrassMeshLibrary::LODTier::kFar);
			for (const auto* lod : bucket.lodMeshes) {
				if (lod)
					bucket.boundExtent = std::max(bucket.boundExtent, lod->boundExtent);
			}
		}
	}

	bucket.boundsPadding = bucket.boundExtent * bucket.maxScale + 128.0f;
}

void GrassBucketStore::MergeSlicesIntoRuns(GrassBucket& bucket)
{
	// Shape origins can span several cells, so cluster slices by their bounds.
	const auto cellOf = [](const SliceBounds& bounds) {
		constexpr float kCellSize = 4096.0f;
		const auto cellX = (int32_t)std::floor((bounds.lo[0] + bounds.hi[0]) * 0.5f / kCellSize);
		const auto cellY = (int32_t)std::floor((bounds.lo[1] + bounds.hi[1]) * 0.5f / kCellSize);
		return ((uint64_t)(uint32_t)cellX << 32) | (uint32_t)cellY;
	};

	// Reuse existing groups when rebuilding runs after removals.
	const bool rebuild = !bucket.clustersValid;
	const uint32_t reusedSlices = rebuild ? bucket.clusteredSlices : 0;
	std::vector<CullGroup> previousGroups;

	if (rebuild) {
		previousGroups.swap(bucket.cullGroups);
		bucket.cullGroups.reserve(previousGroups.size());
		bucket.sliceRunBatches.clear();
		bucket.sliceRunCount = 0;
		bucket.runLiveGroups.clear();
		bucket.sliceBlockBatches.clear();
		bucket.sliceBlocksUseful = false;
		bucket.clusteredSlices = 0;
		bucket.residentGroups = 0;
		bucket.deadGroups = 0;
		bucket.dirtyGroupRanges.clear();
		bucket.coarseBounds = SliceBounds::Empty();
		bucket.coarseDirty = false;
	}

	const uint32_t uploadedSliceCount = bucket.uploadedSlices;
	for (uint32_t sliceIndex = bucket.clusteredSlices; sliceIndex < uploadedSliceCount; ++sliceIndex) {
		auto& slice = *bucket.slices[sliceIndex];
		const auto& bounds = slice.bounds;
		bucket.coarseBounds.Include(bounds);

		// A run's slices share a cell, but their source records need not be contiguous.
		// Empty runs must stay closed because newer groups may follow them.
		const auto cell = cellOf(bounds);
		const bool sameCellAsPrevious = bucket.sliceRunCount && cell == bucket.lastRunCell && bucket.runLiveGroups.back();
		bucket.lastRunCell = cell;

		if (!sameCellAsPrevious) {
			if (bucket.sliceRunCount % 4 == 0)
				bucket.sliceRunBatches.emplace_back();
			++bucket.sliceRunCount;
			bucket.runLiveGroups.push_back(0);
		}

		auto& runBatch = bucket.sliceRunBatches.back();
		const uint32_t runLane = (bucket.sliceRunCount - 1) % 4;

		const uint32_t blockIndex = (bucket.sliceRunCount - 1) / GrassBucket::kRunsPerCullBlock;
		const uint32_t blockLane = blockIndex % 4;

		const bool startsBlock = !sameCellAsPrevious && (bucket.sliceRunCount - 1) % GrassBucket::kRunsPerCullBlock == 0;
		if (startsBlock && blockLane == 0)
			bucket.sliceBlockBatches.emplace_back();

		auto& block = bucket.sliceBlockBatches.back();
		block.IncludeBounds(blockLane, bounds, startsBlock);
		runBatch.IncludeBounds(runLane, bounds, !sameCellAsPrevious);

		if (startsBlock)
			block.firstGroup[blockLane] = (uint32_t)bucket.cullGroups.size();
		if (!sameCellAsPrevious)
			runBatch.firstGroup[runLane] = (uint32_t)bucket.cullGroups.size();

		// A group's instances must share the slice origin used by the shader.
		const uint32_t groupCount = (slice.count + kCullGroupSize - 1) / kCullGroupSize;
		const uint32_t firstGroup = (uint32_t)bucket.cullGroups.size();

		if (sliceIndex < reusedSlices) {
			bucket.cullGroups.insert(bucket.cullGroups.end(), previousGroups.begin() + slice.firstCullGroup, previousGroups.begin() + slice.firstCullGroup + groupCount);
		} else {
			for (uint32_t firstInstance = 0; firstInstance < slice.count; firstInstance += kCullGroupSize) {
				bucket.cullGroups.push_back({ { slice.origin.x, slice.origin.y, slice.origin.z }, slice.fadeStart,
					slice.bufferOffset + firstInstance, std::min(kCullGroupSize, slice.count - firstInstance) });
			}
		}

		slice.firstCullGroup = firstGroup;
		runBatch.groupCount[runLane] += groupCount;
		block.groupCount[blockLane] += groupCount;
		bucket.runLiveGroups.back() += groupCount;
	}

	// Skip block culling when scattered runs make parent bounds ineffective.
	bucket.sliceBlocksUseful = false;
	if (bucket.sliceRunCount >= GrassBucket::kRunsPerCullBlock * 2) {
		float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
		float totalBlockArea = 0.0f;
		const uint32_t blockCount = (bucket.sliceRunCount + GrassBucket::kRunsPerCullBlock - 1) / GrassBucket::kRunsPerCullBlock;
		const float padding = 2.0f * bucket.boundsPadding;

		for (uint32_t blockIndex = 0; blockIndex < blockCount; ++blockIndex) {
			const auto& block = bucket.sliceBlockBatches[blockIndex / 4];
			const uint32_t blockLane = blockIndex % 4;
			minX = std::min(minX, block.loX[blockLane]);
			minY = std::min(minY, block.loY[blockLane]);
			maxX = std::max(maxX, block.hiX[blockLane]);
			maxY = std::max(maxY, block.hiY[blockLane]);
			totalBlockArea += (block.hiX[blockLane] - block.loX[blockLane] + padding) * (block.hiY[blockLane] - block.loY[blockLane] + padding);
		}

		bucket.sliceBlocksUseful = totalBlockArea < (maxX - minX + padding) * (maxY - minY + padding) * (0.5f * blockCount);
	}

	bucket.clusteredSlices = uploadedSliceCount;
	bucket.clustersValid = true;
	bucket.rebuildPending = bucket.rebuildPending && !rebuild;
}

void GrassBucketStore::RetireRunGroups(GrassBucket& bucket, uint32_t firstGroup, uint32_t groupCount)
{
	if (!bucket.sliceRunCount)
		return;

	// The runs are ordered by first group, and each slice belongs to one run.
	const auto runFirstGroup = [&](uint32_t run) { return bucket.sliceRunBatches[run / 4].firstGroup[run % 4]; };
	uint32_t run = 0;
	for (uint32_t end = bucket.sliceRunCount; end - run > 1;) {
		const uint32_t middle = (run + end) / 2;
		(runFirstGroup(middle) <= firstGroup ? run : end) = middle;
	}

	auto& live = bucket.runLiveGroups[run];
	live -= std::min(live, groupCount);
	if (live)
		return;

	// Inverted bounds exclude empty runs when recomputing the coarse box.
	auto& batch = bucket.sliceRunBatches[run / 4];
	const uint32_t lane = run % 4;
	batch.loX[lane] = batch.loY[lane] = batch.loZ[lane] = FLT_MAX;
	batch.hiX[lane] = batch.hiY[lane] = batch.hiZ[lane] = -FLT_MAX;
	batch.groupCount[lane] = 0;
	bucket.coarseDirty = true;
}

void GrassBucketStore::RecomputeCoarseBounds(GrassBucket& bucket)
{
	bucket.coarseBounds = SliceBounds::Empty();
	auto& coarse = bucket.coarseBounds;

	for (uint32_t run = 0; run < bucket.sliceRunCount; ++run) {
		const auto& batch = bucket.sliceRunBatches[run / 4];
		const uint32_t lane = run % 4;

		coarse.lo[0] = std::min(coarse.lo[0], batch.loX[lane]);
		coarse.lo[1] = std::min(coarse.lo[1], batch.loY[lane]);
		coarse.lo[2] = std::min(coarse.lo[2], batch.loZ[lane]);
		coarse.hi[0] = std::max(coarse.hi[0], batch.hiX[lane]);
		coarse.hi[1] = std::max(coarse.hi[1], batch.hiY[lane]);
		coarse.hi[2] = std::max(coarse.hi[2], batch.hiZ[lane]);
	}

	bucket.coarseDirty = false;
}
