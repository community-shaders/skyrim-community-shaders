#include "GrassBucketStore.h"


void GrassBucketStore::SetupResources()
{
	{
		D3D11_BUFFER_DESC bd{};
		bd.ByteWidth = sizeof(uint32_t);
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bd.StructureByteStride = sizeof(uint32_t);
		detectResult = std::make_unique<Buffer>(bd, nullptr, "GrassOptimizations::DetectResult");

		D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
		uav.Format = DXGI_FORMAT_UNKNOWN;
		uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uav.Buffer.FirstElement = 0;
		uav.Buffer.NumElements = 1;
		detectResult->CreateUAV(uav);
	}

	{
		D3D11_BUFFER_DESC bd{};
		bd.ByteWidth = sizeof(uint32_t);
		bd.Usage = D3D11_USAGE_STAGING;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		// Match the source buffer's structure so the copy works.
		bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bd.StructureByteStride = sizeof(uint32_t);
		detectStaging = std::make_unique<Buffer>(bd, nullptr, "GrassOptimizations::DetectStaging");
	}
}

void GrassBucketStore::ClearShaderCache()
{
	if (detectCS)
		detectCS->Release();
	detectCS = nullptr;
}

ID3D11ComputeShader* GrassBucketStore::GetDetectCS()
{
	if (!detectCS) {
		detectCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\GrassOptimizations\\DetectComplexCS.hlsl", {}, "cs_5_0"));
		if (!detectCS)
			logger::error("[GRASS OPTIMIZATIONS] detect CS load failed — complex detection disabled");
	}
	return detectCS;
}

void GrassBucketStore::ApplyPending(ID3D11Device* device, ID3D11DeviceContext* ctx)
{
	std::vector<PendingCapture> caps;
	std::vector<RE::BSMultiStreamInstanceTriShape*> rems;
	{
		std::scoped_lock lk(pendingMutex);
		caps.swap(pendingCaptures);
		rems.swap(pendingRemoves);
	}

	ApplyRemovals(rems);
	ApplyCaptures(caps);
	UploadDirtyBuckets(device, ctx);
}

void GrassBucketStore::RefreshComplexGrass(float threshold, ID3D11DeviceContext* ctx)
{
	if (threshold == cachedComplexThreshold)
		return;

	cachedComplexThreshold = threshold;

	for (auto& [key, b] : buckets)
		b.isComplex = DetectComplexGrass(b.diffuseTexture.get(), ctx);
}

void GrassBucketStore::StageRemoval(RE::BSMultiStreamInstanceTriShape* shape)
{
	std::scoped_lock lk(pendingMutex);
	pendingRemoves.push_back(shape);
}

bool GrassBucketStore::ClaimQueueSlot(RE::BSMultiStreamInstanceTriShape* shape, uint32_t frame)
{
	std::shared_lock lk(shapeBucketMutex);

	auto it = shapeBucketId.find(shape);
	// A bucket with no instance buffer falls back to vanilla per-shape drawing, so all shapes must still be queued. Checked here so the map does not need to track buffer state.
	// Deliberately read without bucketMutex, which UpdateGrass holds whole and would serialise every culling job.
	if (it == shapeBucketId.end() || !it->second->instanceBuf)
		return true;

	auto& lastQueued = it->second->lastQueuedFrame;
	uint32_t prev = lastQueued.load(std::memory_order_relaxed);
	if (prev == frame)
		return false; 
	return lastQueued.compare_exchange_strong(prev, frame, std::memory_order_relaxed);
}

void GrassBucketStore::DiscardPending()
{
	std::scoped_lock lk(pendingMutex);
	pendingCaptures.clear();
	pendingCaptures.shrink_to_fit();
	pendingRemoves.clear();
}

void GrassBucketStore::ApplyRemovals(const std::vector<RE::BSMultiStreamInstanceTriShape*>& removes)
{
	if (removes.empty())
		return;

	// A sorted, reused buffer to avoid reallocating during repeated cell loads.
	static thread_local std::vector<RE::BSMultiStreamInstanceTriShape*> dead;
	dead.assign(removes.begin(), removes.end());
	std::sort(dead.begin(), dead.end());

	const auto isDead = [](RE::BSMultiStreamInstanceTriShape* p) {
		return std::binary_search(dead.begin(), dead.end(), p);
	};

	{
		std::unique_lock lk(shapeBucketMutex);
		for (auto* s : removes)
			shapeBucketId.erase(s);
	}

	for (auto* s : removes)
		meshLibrary.ForgetShape(s);

	for (auto it = buckets.begin(); it != buckets.end();) {
		auto& b = it->second;
		const size_t before = b.slices.size();

		// Compact in place, record the first removed so the rebuild re-uploads only from there.
		uint32_t firstRemoved = UINT32_MAX;
		size_t write = 0;
		uint32_t removedInstances = 0;
		// firstNewSlice indexes b.slices, so compaction shifts it. Left stale it points past the pending
		// slices and AppendNewSlices would upload none of them, leaving their records unwritten.
		uint32_t remappedFirstNew = UINT32_MAX;

		for (size_t read = 0; read < before; ++read) {
			if ((uint32_t)read == b.firstNewSlice)
				remappedFirstNew = (uint32_t)write;
			if (isDead(b.slices[read].shape)) {
				if (firstRemoved == UINT32_MAX)
					firstRemoved = (uint32_t)write;
				removedInstances += b.slices[read].count;
				continue;
			}
			if (write != read) {
				b.slices[write] = std::move(b.slices[read]);
				b.sliceBounds[write] = b.sliceBounds[read];
			}
			++write;
		}

		b.slices.resize(write);
		b.sliceBounds.resize(write);

		if (b.firstNewSlice != UINT32_MAX)
			b.firstNewSlice = (remappedFirstNew != UINT32_MAX) ? remappedFirstNew : (uint32_t)write;

		if (write != before) {
			b.clustersValid = false;
			b.totalInstances -= std::min(removedInstances, b.totalInstances);
		}

		if (b.slices.empty()) {
			// Culling jobs read this bucket through shapeBucketId while holding the lock shared. Taking it
			// exclusively blocks until those reads finish, and erasing the entry stops any later job from
			// reaching it, so destroying the bucket here is safe.
			std::unique_lock lk(shapeBucketMutex);
			std::erase_if(shapeBucketId, [dying = &b](const auto& entry) { return entry.second == dying; });
			it = buckets.erase(it);
			continue;
		}

		if (b.slices.size() != before) {
			b.needsCompact = true;
			b.coarseValid = false;
			b.rebuildFromSlice = (b.rebuildFromSlice == UINT32_MAX) ? firstRemoved : std::min(b.rebuildFromSlice, firstRemoved);
		}

		++it;
	}
}

void GrassBucketStore::ApplyCaptures(std::vector<PendingCapture>& captures)
{
	// Queue pending adds so additions can be processed under one lock
	static thread_local std::vector<std::pair<RE::BSMultiStreamInstanceTriShape*, GrassBucket*>> pendingMapAdds;
	pendingMapAdds.clear();

	auto* ctx = globals::d3d::context;

	for (auto& pc : captures) {
		const uint32_t meshId = meshLibrary.ResolveMeshId(pc.shape);
		const uint32_t triCount = meshId ? 0u : (uint32_t)pc.shape->GetTrishapeRuntimeData().triangleCount;
		const BucketKey bk{ meshId, meshId ? nullptr : pc.diffuseTexture, triCount, meshId ? 0u : pc.descVal };
		auto& b = buckets[bk];
		b.meshId = meshId;
		b.diffuseTexture = RE::NiPointer<RE::NiSourceTexture>(pc.diffuseTexture);

		if (b.firstNewSlice == UINT32_MAX)
			b.firstNewSlice = (uint32_t)b.slices.size();

		if (!b.typeParamsValid) {
			CacheBucketTypeParams(b, pc.shape);
			b.isComplex = DetectComplexGrass(pc.diffuseTexture, ctx);
		}
		if (frameParams.enableMeshLOD)
			meshLibrary.EnsureLODMeshes(meshId);

		BucketSlice s;
		s.shape = pc.shape;
		s.count = pc.count;
		s.fadeStart = frameParams.fadeStart;
		s.origin = pc.origin;
		s.localMin = pc.localMin;
		s.localMax = pc.localMax;
		s.data = std::move(pc.bytes);

		SliceBounds sb;
		sb.lo[0] = pc.origin.x + pc.localMin.x;
		sb.lo[1] = pc.origin.y + pc.localMin.y;
		sb.lo[2] = pc.origin.z + pc.localMin.z;
		sb.hi[0] = pc.origin.x + pc.localMax.x;
		sb.hi[1] = pc.origin.y + pc.localMax.y;
		sb.hi[2] = pc.origin.z + pc.localMax.z;

		b.totalInstances += pc.count;
		b.slices.push_back(std::move(s));
		if (pc.shape)
			pendingMapAdds.emplace_back(pc.shape, &b);
		b.sliceBounds.push_back(sb);
		b.clustersValid = false;
		b.coarseValid = false;
	}

	if (!pendingMapAdds.empty()) {
		std::unique_lock lk(shapeBucketMutex);
		for (const auto& [shape, bucket] : pendingMapAdds)
			shapeBucketId[shape] = bucket;
	}
}

bool GrassBucketStore::CompactBucket(GrassBucket& b, ID3D11Device* device, ID3D11DeviceContext* ctx)
{
	if (!b.instanceBuf || !b.originBuf || !b.capacityInstances)
		return false;

	const uint32_t surviving = (b.firstNewSlice == UINT32_MAX) ? (uint32_t)b.slices.size() : b.firstNewSlice;

	// Split the survivors which have fragmented by removals into runs of still contiguous offsets, so they can be copied back into a new contiguous buffer.
	struct Run
	{
		uint32_t oldFirst;
		uint32_t newFirst;
		uint32_t instances;
	};
	static thread_local std::vector<Run> runs;
	runs.clear();

	uint32_t newOffset = 0;
	for (uint32_t i = 0; i < surviving; ++i) {
		const BucketSlice& s = b.slices[i];
		if (s.bufferOffset == UINT32_MAX || !s.count)
			return false;  // never uploaded, so there is nothing resident to shift

		if (!runs.empty() && s.bufferOffset == runs.back().oldFirst + runs.back().instances)
			runs.back().instances += s.count;
		else
			runs.push_back({ s.bufferOffset, newOffset, s.count });

		newOffset += s.count;
	}

	if (runs.empty())
		return false;

	// Only trailing slices went, so everything still resident is already where it belongs.
	if (runs.size() == 1 && runs[0].oldFirst == 0)
		return true;

	// D3D11 doesn't allow overlapping copies within one resource, so shift the survivors into a new buffer and swap it in.
	ID3D11Buffer* oldInstance = b.instanceBuf;
	ID3D11Buffer* oldOrigin = b.originBuf;
	ID3D11ShaderResourceView* oldInstanceSRV = b.instanceSRV;
	ID3D11ShaderResourceView* oldOriginSRV = b.originSRV;
	b.instanceBuf = nullptr;
	b.originBuf = nullptr;
	b.instanceSRV = nullptr;
	b.originSRV = nullptr;

	if (!CreateBucketSourceBuffers(b, b.capacityInstances, device)) {
		auto rel = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };
		rel(b.instanceBuf);
		rel(b.originBuf);
		rel(b.instanceSRV);
		rel(b.originSRV);
		b.instanceBuf = oldInstance;
		b.originBuf = oldOrigin;
		b.instanceSRV = oldInstanceSRV;
		b.originSRV = oldOriginSRV;
		return false;  // offsets are untouched, so a CPU rebuild is still valid
	}

	constexpr UINT kRecordBytes = kGrassStride;
	constexpr UINT kOriginBytes = 4 * (UINT)sizeof(float);
	for (const Run& run : runs) {
		const D3D11_BOX ibox{ run.oldFirst * kRecordBytes, 0, 0, (run.oldFirst + run.instances) * kRecordBytes, 1, 1 };
		ctx->CopySubresourceRegion(b.instanceBuf, 0, run.newFirst * kRecordBytes, 0, 0, oldInstance, 0, &ibox);

		const D3D11_BOX obox{ run.oldFirst * kOriginBytes, 0, 0, (run.oldFirst + run.instances) * kOriginBytes, 1, 1 };
		ctx->CopySubresourceRegion(b.originBuf, 0, run.newFirst * kOriginBytes, 0, 0, oldOrigin, 0, &obox);
	}

	newOffset = 0;
	for (uint32_t i = 0; i < surviving; ++i) {
		b.slices[i].bufferOffset = newOffset;
		newOffset += b.slices[i].count;
	}

	oldInstanceSRV->Release();
	oldOriginSRV->Release();
	oldInstance->Release();
	oldOrigin->Release();
	return true;
}

void GrassBucketStore::UploadDirtyBuckets(ID3D11Device* device, ID3D11DeviceContext* ctx)
{
	for (auto& [key, b] : buckets) {
		if (b.needsCompact) {
			b.needsCompact = false;
			if (CompactBucket(b, device, ctx)) {
				b.rebuildFromSlice = UINT32_MAX;  // survivors sit contiguously again
			} else {
				b.dirty = true;
			}
		}

		if (b.dirty) {
			RebuildBucket(b, device, ctx);
		} else if (b.firstNewSlice != UINT32_MAX) {
			if (b.totalInstances > b.capacityInstances)
				RebuildBucket(b, device, ctx);
			else
				AppendNewSlices(b, ctx);
		}
	}
}

void GrassBucketStore::CaptureGIDGroup(RE::BSMultiStreamInstanceTriShape* shape, RE::BSMultiStreamInstanceTriShape::GroupHeader* header, const uint16_t* instanceData, size_t dataBytes)
{
	if (!shape || !header || !instanceData)
		return;

	auto prop = shape->GetGeometryRuntimeData().shaderProperty;
	if (!prop || prop->GetRTTI() != globals::rtti::BSGrassShaderPropertyRTTI.get())
		return;

	RE::NiSourceTexture* tex = prop->GetBaseTexture();
	if (!tex)
		return;

	const uint64_t descVal = *reinterpret_cast<const uint64_t*>(&shape->GetGeometryRuntimeData().vertexDesc);
	const uint32_t stride = kGrassStride;

	uint32_t count = header->groupInstanceCount;
	if (dataBytes / stride < count)
		count = (uint32_t)(dataBytes / stride);

	StageCapture(shape, instanceData, count, stride, descVal, tex);
}

bool GrassBucketStore::StageCapture(RE::BSMultiStreamInstanceTriShape* shape, const void* src, uint32_t count, uint32_t stride, uint64_t descVal, RE::NiSourceTexture* tex)
{
	if (!shape || !src || !tex || !count || stride != kGrassStride) {
		logger::debug("[GRASS OPTIMIZATIONS] capture rejected: count={} stride={} desc={:016X} shape={:p}", count, stride, descVal, (void*)shape);
		return false;
	}

	PendingCapture pc;
	pc.shape = shape;
	pc.descVal = descVal;
	pc.diffuseTexture = tex;
	pc.count = count;
	pc.origin = shape->world.translate;
	pc.bytes.resize((size_t)count * kGrassStride);
	std::memcpy(pc.bytes.data(), src, pc.bytes.size());

	// Read the instance coordinates from the source allocation. Store the local min/max in the slice so the coarse bounds can be computed without re-reading the instance data.
	{
		using DirectX::PackedVector::XMConvertHalfToFloat;
		RE::NiPoint3 lmn{ FLT_MAX, FLT_MAX, FLT_MAX };
		RE::NiPoint3 lmx{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
		const uint8_t* data = pc.bytes.data();
		for (uint32_t i = 0; i < count; ++i) {
			const uint8_t* cord = data + (size_t)i * kGrassStride;
			const float lx = XMConvertHalfToFloat(*reinterpret_cast<const uint16_t*>(cord + 0));
			const float ly = XMConvertHalfToFloat(*reinterpret_cast<const uint16_t*>(cord + 2));
			const float lz = XMConvertHalfToFloat(*reinterpret_cast<const uint16_t*>(cord + 4));
			lmn.x = std::min(lmn.x, lx);
			lmn.y = std::min(lmn.y, ly);
			lmn.z = std::min(lmn.z, lz);
			lmx.x = std::max(lmx.x, lx);
			lmx.y = std::max(lmx.y, ly);
			lmx.z = std::max(lmx.z, lz);
		}
		pc.localMin = lmn;
		pc.localMax = lmx;
	}

	std::scoped_lock lk(pendingMutex);
	pendingCaptures.push_back(std::move(pc));
	return true;
}

void GrassBucketStore::CacheBucketTypeParams(GrassBucket& b, RE::BSMultiStreamInstanceTriShape* shape)
{
	if (b.typeParamsValid || !shape)
		return;

	if (auto* prop = static_cast<RE::BSGrassShaderProperty*>(shape->GetGeometryRuntimeData().shaderProperty.get()))
		b.wavePeriod = prop->wavePeriod;

	const auto& bound = shape->GetModelData().modelBound;
	b.boundCenter = bound.center;
	b.modelRadius = bound.radius;

	const float tris = (float)shape->GetTrishapeRuntimeData().triangleCount;
	const float cost = std::max(1.0f, tris / 8.0f);
	const float w = std::sqrt(cost);
	b.distScale = 1.0f / w;
	b.minPixelScale = w;
	b.typeParamsValid = true;
}

void GrassBucketStore::RebuildBucket(GrassBucket& bucket, ID3D11Device* device, ID3D11DeviceContext* ctx)
{
	// Rebuild only from the tail. Do a full rebuild when both are UINT32_MAX, or when the first stale slice is not contiguous with the previous slices.
	uint32_t from = std::min(bucket.rebuildFromSlice, bucket.firstNewSlice);
	if (from == UINT32_MAX || from > (uint32_t)bucket.slices.size())
		from = 0;

	// A partial rebuild is only valid if everything below `from` sits contiguously.
	uint32_t startInstance = 0;
	for (uint32_t i = 0; i < from; ++i) {
		const auto& s = bucket.slices[i];
		if (s.bufferOffset == UINT32_MAX || s.bufferOffset != startInstance) {
			from = 0;
			startInstance = 0;
			break;
		}
		startInstance += s.count;
	}

	// Copy from the old buffer starting with the given start instance to the new buffer on the GPU, so the rebuild below still only uploads the tail.
	if (!EnsureBucketCapacity(bucket, bucket.totalInstances, device, ctx, startInstance))
		return;

	if (!bucket.totalInstances) {
		bucket.dirty = false;
		bucket.rebuildFromSlice = UINT32_MAX;
		bucket.firstNewSlice = UINT32_MAX;
		return;
	}

	uint32_t tailInstances = 0;
	for (uint32_t i = from; i < (uint32_t)bucket.slices.size(); ++i)
		tailInstances += bucket.slices[i].count;

	if (tailInstances) {
		// Reused across cell loads, so flattening allocates nothing once grown.
		static thread_local std::vector<uint8_t> records;
		static thread_local std::vector<float> origins;

		records.clear();
		records.reserve((size_t)tailInstances * kGrassStride);
		origins.resize((size_t)tailInstances * 4);
		float* op = origins.data();

		uint32_t off = startInstance;
		for (uint32_t i = from; i < (uint32_t)bucket.slices.size(); ++i) {
			auto& s = bucket.slices[i];
			s.bufferOffset = off;
			records.insert(records.end(), s.data.begin(), s.data.end());
			for (uint32_t j = 0; j < s.count; ++j) {
				*op++ = s.origin.x;
				*op++ = s.origin.y;
				*op++ = s.origin.z;
				*op++ = s.fadeStart;
			}
			off += s.count;
		}

		// One upload per buffer covering the whole tail, not one per slice.
		const D3D11_BOX ibox{ startInstance * kGrassStride, 0, 0, (startInstance + tailInstances) * kGrassStride, 1, 1 };
		ctx->UpdateSubresource(bucket.instanceBuf, 0, &ibox, records.data(), 0, 0);

		const D3D11_BOX obox{ startInstance * 4 * (UINT)sizeof(float), 0, 0,
			(startInstance + tailInstances) * 4 * (UINT)sizeof(float), 1, 1 };
		ctx->UpdateSubresource(bucket.originBuf, 0, &obox, origins.data(), 0, 0);
	}

	bucket.dirty = false;
	bucket.rebuildFromSlice = UINT32_MAX;
	bucket.firstNewSlice = UINT32_MAX;
}

void GrassBucketStore::AppendNewSlices(GrassBucket& bucket, ID3D11DeviceContext* ctx)
{
	if (bucket.totalInstances > bucket.capacityInstances) {
		bucket.dirty = true;
		RebuildBucket(bucket, globals::d3d::device, ctx);
		return;
	}

	uint32_t prefix = 0;
	for (uint32_t i = 0; i < bucket.firstNewSlice; ++i) {
		const auto& s = bucket.slices[i];
		if (s.bufferOffset == UINT32_MAX || s.bufferOffset != prefix) {
			bucket.dirty = true;
			RebuildBucket(bucket, globals::d3d::device, ctx);
			return;
		}
		prefix += s.count;
	}

	uint32_t tailInstances = 0;
	for (uint32_t i = bucket.firstNewSlice; i < (uint32_t)bucket.slices.size(); ++i)
		tailInstances += bucket.slices[i].count;

	if (!tailInstances) {
		bucket.firstNewSlice = UINT32_MAX;
		return;
	}

	static thread_local std::vector<uint8_t> recTail;
	static thread_local std::vector<float> originTail;

	recTail.clear();
	recTail.reserve((size_t)tailInstances * kGrassStride);
	originTail.resize((size_t)tailInstances * 4);
	float* op = originTail.data();

	uint32_t off = prefix;
	for (uint32_t i = bucket.firstNewSlice; i < (uint32_t)bucket.slices.size(); ++i) {
		auto& s = bucket.slices[i];
		s.bufferOffset = off;
		recTail.insert(recTail.end(), s.data.begin(), s.data.end());
		for (uint32_t j = 0; j < s.count; ++j) {
			*op++ = s.origin.x;
			*op++ = s.origin.y;
			*op++ = s.origin.z;
			*op++ = s.fadeStart;
		}
		off += s.count;
	}

	const D3D11_BOX ibox{ prefix * kGrassStride, 0, 0, off * kGrassStride, 1, 1 };
	ctx->UpdateSubresource(bucket.instanceBuf, 0, &ibox, recTail.data(), 0, 0);

	const D3D11_BOX obox{ prefix * 4 * (UINT)sizeof(float), 0, 0,
		off * 4 * (UINT)sizeof(float), 1, 1 };
	ctx->UpdateSubresource(bucket.originBuf, 0, &obox, originTail.data(), 0, 0);

	bucket.firstNewSlice = UINT32_MAX;
}

// The widest allocation is kGrassStride bytes per instance, so a larger capacity wraps its ByteWidth
// and silently under-allocates. Bounding capacityInstances here also bounds EnsureLODBin's cap.
constexpr uint32_t kMaxBucketInstances = UINT32_MAX / kGrassStride;

bool GrassBucketStore::EnsureBucketCapacity(GrassBucket& b, uint32_t needed, ID3D11Device* device, ID3D11DeviceContext* ctx, uint32_t preserveInstances)
{
	if (b.capacityInstances >= needed && b.instanceBuf)
		return true;

	if (needed > kMaxBucketInstances) {
		logger::error("[GRASS OPTIMIZATIONS] bucket capacity rejected: needed={} max={}", needed, kMaxBucketInstances);
		return false;
	}

	uint32_t cap = b.capacityInstances ? b.capacityInstances : 4096;
	while (cap < needed)
		cap = std::min(cap * 2, kMaxBucketInstances);

	// Hold the old instance/origin buffers across the reallocation so the data up to preserve can be copied to the new buffers on the GPU.
	const uint32_t preserve = std::min(preserveInstances, b.capacityInstances);
	struct OldBuffers
	{
		ID3D11Buffer* instance;
		ID3D11Buffer* origin;
		~OldBuffers()
		{
			if (instance)
				instance->Release();
			if (origin)
				origin->Release();
		}
	} old{ b.instanceBuf, b.originBuf };

	b.instanceBuf = nullptr;
	b.originBuf = nullptr;

	b.ReleaseResources();

	if (!CreateBucketSourceBuffers(b, cap, device) ||
		!CreateBucketCullScratch(b, cap, device) ||
		!CreateBucketArgsBuffer(b, device)) {
		b.ReleaseResources();
		b.capacityInstances = 0;
		// Ensure buffer offset is reset, so the next rebuild will not start from a stale offset. The rebuild will re-upload the whole bucket, so the offsets will be set correctly.
		for (auto& s : b.slices)
			s.bufferOffset = UINT32_MAX;
		return false;
	}

	// Carry the unchanged head across on the GPU, so the caller still uploads only the tail.
	if (preserve && ctx) {
		if (old.instance && b.instanceBuf) {
			const D3D11_BOX box{ 0, 0, 0, preserve * kGrassStride, 1, 1 };
			ctx->CopySubresourceRegion(b.instanceBuf, 0, 0, 0, 0, old.instance, 0, &box);
		}
		if (old.origin && b.originBuf) {
			const D3D11_BOX box{ 0, 0, 0, preserve * 4 * (UINT)sizeof(float), 1, 1 };
			ctx->CopySubresourceRegion(b.originBuf, 0, 0, 0, 0, old.origin, 0, &box);
		}
	}

	b.capacityInstances = cap;
	b.dirty = true;
	return true;
}

bool GrassBucketStore::CreateBucketSourceBuffers(GrassBucket& b, uint32_t capacity, ID3D11Device* device)
{
	{
		D3D11_BUFFER_DESC bd{};
		bd.ByteWidth = capacity * kGrassStride;
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
		if (FAILED(device->CreateBuffer(&bd, nullptr, &b.instanceBuf)) || !b.instanceBuf) {
			logger::error("[GRASS OPTIMIZATIONS] instance buffer create failed bytes={}", bd.ByteWidth);
			return false;
		}
		Util::SetResourceName(b.instanceBuf, "GrassOptimizations::InstanceBuf");

		D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
		sv.Format = DXGI_FORMAT_R32_TYPELESS;
		sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
		sv.BufferEx.FirstElement = 0;
		sv.BufferEx.NumElements = capacity * 8;
		sv.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
		if (FAILED(device->CreateShaderResourceView(b.instanceBuf, &sv, &b.instanceSRV)) || !b.instanceSRV) {
			logger::error("[GRASS OPTIMIZATIONS] instance raw SRV create failed");
			return false;
		}
		Util::SetResourceName(b.instanceSRV, "GrassOptimizations::InstanceBuf SRV");
	}

	// Origin + spawn time.
	{
		D3D11_BUFFER_DESC bd{};
		bd.ByteWidth = capacity * 4 * sizeof(float);
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bd.StructureByteStride = 4 * sizeof(float);
		if (FAILED(device->CreateBuffer(&bd, nullptr, &b.originBuf)) || !b.originBuf) {
			logger::error("[GRASS OPTIMIZATIONS] origin buffer create failed");
			return false;
		}
		Util::SetResourceName(b.originBuf, "GrassOptimizations::OriginBuf");

		D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
		sv.Format = DXGI_FORMAT_UNKNOWN;
		sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		sv.Buffer.NumElements = capacity;
		if (FAILED(device->CreateShaderResourceView(b.originBuf, &sv, &b.originSRV)) || !b.originSRV) {
			logger::error("[GRASS OPTIMIZATIONS] origin SRV create failed");
			return false;
		}
		Util::SetResourceName(b.originSRV, "GrassOptimizations::OriginBuf SRV");
	}

	return true;
}

bool GrassBucketStore::CreateBucketCullScratch(GrassBucket& b, uint32_t capacity, ID3D11Device* device)
{
	// Compacted survivors, consumed as an instanced vertex stream by the draw.
	{
		D3D11_BUFFER_DESC bd{};
		bd.ByteWidth = capacity * kGrassStride;
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_UNORDERED_ACCESS;
		bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
		if (FAILED(device->CreateBuffer(&bd, nullptr, &b.compactedBuf)) || !b.compactedBuf) {
			logger::error("[GRASS OPTIMIZATIONS] compacted buffer create failed");
			return false;
		}
		Util::SetResourceName(b.compactedBuf, "GrassOptimizations::CompactedBuf");

		D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
		uav.Format = DXGI_FORMAT_R32_TYPELESS;
		uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uav.Buffer.FirstElement = 0;
		uav.Buffer.NumElements = capacity * 8;
		uav.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
		if (FAILED(device->CreateUnorderedAccessView(b.compactedBuf, &uav, &b.compactedUAV))) {
			logger::error("[GRASS OPTIMIZATIONS] compacted UAV create failed");
			return false;
		}
		Util::SetResourceName(b.compactedUAV, "GrassOptimizations::CompactedBuf UAV");
	}

	// Extras: [i*2+0] = {origin.xyz, isComplex}, [i*2+1] = {windCur, windPrev, fade, collision}.
	{
		D3D11_BUFFER_DESC bd{};
		bd.ByteWidth = capacity * 2 * 4 * sizeof(float);
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
		bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bd.StructureByteStride = 4 * sizeof(float);
		if (FAILED(device->CreateBuffer(&bd, nullptr, &b.extrasBuf)) || !b.extrasBuf) {
			logger::error("[GRASS OPTIMIZATIONS] extras buffer create failed");
			return false;
		}
		Util::SetResourceName(b.extrasBuf, "GrassOptimizations::ExtrasBuf");

		D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
		uav.Format = DXGI_FORMAT_UNKNOWN;
		uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uav.Buffer.FirstElement = 0;
		uav.Buffer.NumElements = capacity * 2;
		if (FAILED(device->CreateUnorderedAccessView(b.extrasBuf, &uav, &b.extrasUAV))) {
			logger::error("[GRASS OPTIMIZATIONS] extras UAV create failed");
			return false;
		}
		Util::SetResourceName(b.extrasUAV, "GrassOptimizations::ExtrasBuf UAV");

		D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
		sv.Format = DXGI_FORMAT_UNKNOWN;
		sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		sv.Buffer.NumElements = capacity * 2;
		if (FAILED(device->CreateShaderResourceView(b.extrasBuf, &sv, &b.extrasSRV))) {
			logger::error("[GRASS OPTIMIZATIONS] extras SRV create failed");
			return false;
		}
		Util::SetResourceName(b.extrasSRV, "GrassOptimizations::ExtrasBuf SRV");
	}

	return true;
}

bool GrassBucketStore::CreateBucketArgsBuffer(GrassBucket& b, ID3D11Device* device)
{
	// 3 uints of padding, so that the instanceCount is UAV accessible, so the cull CS can write to it directly.
	// Then the 5-uint args block at instanceCountOffset.
	const uint32_t initArgs[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	D3D11_SUBRESOURCE_DATA init{ initArgs, 0, 0 };

	D3D11_BUFFER_DESC bd{};
	bd.ByteWidth = 8 * sizeof(uint32_t);
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	bd.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;

	if (FAILED(device->CreateBuffer(&bd, &init, &b.argsBuf)) || !b.argsBuf) {
		logger::error("[GRASS OPTIMIZATIONS] args buffer create failed");
		return false;
	}
	Util::SetResourceName(b.argsBuf, "GrassOptimizations::ArgsBuf");

	// Windows on to the instance count, with the shader's address 0 mapping to it, so clearing the view resets the count without disturbing indexCount. 
	D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
	uav.Format = DXGI_FORMAT_R32_TYPELESS;
	uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uav.Buffer.FirstElement = instanceCountOffset / sizeof(uint32_t);
	uav.Buffer.NumElements = 1;
	uav.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
	if (FAILED(device->CreateUnorderedAccessView(b.argsBuf, &uav, &b.argsUAV)) || !b.argsUAV) {
		logger::error("[GRASS OPTIMIZATIONS] args UAV create failed");
		return false;
	}
	Util::SetResourceName(b.argsUAV, "GrassOptimizations::ArgsBuf UAV");

	return true;
}

void GrassBucketStore::UpdateCoarseBounds(GrassBucket& b)
{
	RE::NiPoint3 mn{ FLT_MAX, FLT_MAX, FLT_MAX };
	RE::NiPoint3 mx{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
	for (const auto& s : b.slices) {
		mn.x = std::min(mn.x, s.origin.x + s.localMin.x);
		mn.y = std::min(mn.y, s.origin.y + s.localMin.y);
		mn.z = std::min(mn.z, s.origin.z + s.localMin.z);
		mx.x = std::max(mx.x, s.origin.x + s.localMax.x);
		mx.y = std::max(mx.y, s.origin.y + s.localMax.y);
		mx.z = std::max(mx.z, s.origin.z + s.localMax.z);
	}
	if (mn.x > mx.x) {  // no slices / no instances — leave an empty box
		mn = { 0.0f, 0.0f, 0.0f };
		mx = { 0.0f, 0.0f, 0.0f };
	}

	// Generously pad to prevent instances on the screen edge from being visibly culled. 
	const float pad = b.modelRadius + 128.0f;
	mn.x -= pad;
	mn.y -= pad;
	mn.z -= pad;
	mx.x += pad;
	mx.y += pad;
	mx.z += pad;

	b.coarseMin = mn;
	b.coarseMax = mx;
	b.coarseValid = true;
}

bool GrassBucketStore::DetectComplexGrass(RE::NiSourceTexture* tex, ID3D11DeviceContext* ctx)
{
	auto* rt = tex ? tex->rendererTexture : nullptr;
	auto* resourceView = rt ? rt->resourceView : nullptr;
	if (auto it = complexCache.find(tex); it != complexCache.end() && it->second.resourceView == resourceView) {
		it->second.complex = std::abs(it->second.normalLength - 1.0f) < cachedComplexThreshold;
		return it->second.complex;
	}

	bool complex = false;
	float normalLength = 0.0f;

	if (GetDetectCS() && detectResult && detectStaging && resourceView) {
		UINT initialCount = 0;
		ID3D11UnorderedAccessView* resultUAV = detectResult->uav.get();
		ctx->CSSetUnorderedAccessViews(0, 1, &resultUAV, &initialCount);
		ctx->CSSetShader(detectCS, nullptr, 0);
		ctx->CSSetShaderResources(0, 1, &resourceView);
		ctx->Dispatch(1, 1, 1);

		ID3D11UnorderedAccessView* nullUAV = nullptr;
		ctx->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
		ID3D11ShaderResourceView* nullSRV = nullptr;
		ctx->CSSetShaderResources(0, 1, &nullSRV);

		// Stalls once per unique texture on load rather than every frame or requiring a CPU readback of the full texture.
		ctx->CopyResource(detectStaging->resource.get(), detectResult->resource.get());
		D3D11_MAPPED_SUBRESOURCE m{};
		if (SUCCEEDED(ctx->Map(detectStaging->resource.get(), 0, D3D11_MAP_READ, 0, &m))) {
			// Compare using the decoded length from the shader to avoid requring a constant buffer
			std::memcpy(&normalLength, m.pData, sizeof(float));
			complex = std::abs(normalLength - 1.0f) < cachedComplexThreshold;
			ctx->Unmap(detectStaging->resource.get(), 0);
		}
	}

	complexCache.insert_or_assign(tex, ComplexEntry{ RE::NiPointer<RE::NiSourceTexture>(tex), resourceView, normalLength, complex });
	return complex;
}

bool GrassBucketStore::EnsureLODBin(GrassBucket& b, GrassMeshLibrary::LODTier tier, ID3D11Device* device)
{
	GrassBucket::LODBin& bin = b.lodBins[(size_t)tier];
	const bool tierEnabled = (tier == GrassMeshLibrary::LODTier::kFar) ? frameParams.enableFarLOD : frameParams.enableMidLOD;

	if (!frameParams.enableMeshLOD || !tierEnabled || b.meshId == 0) {
		if (bin.capacityInstances)
			bin.Release();
		return false;
	}

	meshLibrary.EnsureLODMeshes(b.meshId);
	if (!meshLibrary.GetLODMesh(b.meshId, tier)) {
		if (bin.capacityInstances)
			bin.Release();
		return false;
	}

	const uint32_t cap = b.capacityInstances;
	if (!cap)
		return false;
	if (bin.capacityInstances >= cap && bin.compactedBuf)
		return true;

	// Worst case every survivor takes this tier's mesh, so the bin is sized like the full-detail one.
	bin.Release();

	const char* tierName = (tier == GrassMeshLibrary::LODTier::kFar) ? "far" : "middle";

	{
		D3D11_BUFFER_DESC bd{};
		bd.ByteWidth = cap * kGrassStride;
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_UNORDERED_ACCESS;
		bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
		if (FAILED(device->CreateBuffer(&bd, nullptr, &bin.compactedBuf)) || !bin.compactedBuf) {
			logger::error("[GRASS OPTIMIZATIONS] {} LOD compacted buffer create failed", tierName);
			bin.Release();
			return false;
		}
		Util::SetResourceName(bin.compactedBuf, "GrassOptimizations::LODCompactedBuf");

		D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
		uav.Format = DXGI_FORMAT_R32_TYPELESS;
		uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uav.Buffer.FirstElement = 0;
		uav.Buffer.NumElements = cap * 8;
		uav.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
		if (FAILED(device->CreateUnorderedAccessView(bin.compactedBuf, &uav, &bin.compactedUAV))) {
			logger::error("[GRASS OPTIMIZATIONS] {} LOD compacted UAV create failed", tierName);
			bin.Release();
			return false;
		}
	}

	{
		D3D11_BUFFER_DESC bd{};
		bd.ByteWidth = cap * 2 * 4 * sizeof(float);
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
		bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bd.StructureByteStride = 4 * sizeof(float);
		if (FAILED(device->CreateBuffer(&bd, nullptr, &bin.extrasBuf)) || !bin.extrasBuf) {
			logger::error("[GRASS OPTIMIZATIONS] {} LOD extras buffer create failed", tierName);
			bin.Release();
			return false;
		}
		Util::SetResourceName(bin.extrasBuf, "GrassOptimizations::LODExtrasBuf");

		D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
		uav.Format = DXGI_FORMAT_UNKNOWN;
		uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uav.Buffer.FirstElement = 0;
		uav.Buffer.NumElements = cap * 2;
		uav.Buffer.Flags = 0;
		if (FAILED(device->CreateUnorderedAccessView(bin.extrasBuf, &uav, &bin.extrasUAV))) {
			logger::error("[GRASS OPTIMIZATIONS] {} LOD extras UAV create failed", tierName);
			bin.Release();
			return false;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
		sv.Format = DXGI_FORMAT_UNKNOWN;
		sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		sv.Buffer.NumElements = cap * 2;
		if (FAILED(device->CreateShaderResourceView(bin.extrasBuf, &sv, &bin.extrasSRV))) {
			logger::error("[GRASS OPTIMIZATIONS] {} LOD extras SRV create failed", tierName);
			bin.Release();
			return false;
		}
	}

	{
		const uint32_t initArgs[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
		D3D11_SUBRESOURCE_DATA init{ initArgs, 0, 0 };

		D3D11_BUFFER_DESC bd{};
		bd.ByteWidth = 8 * sizeof(uint32_t);
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		bd.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;

		if (FAILED(device->CreateBuffer(&bd, &init, &bin.argsBuf)) || !bin.argsBuf) {
			logger::error("[GRASS OPTIMIZATIONS] {} LOD args buffer create failed", tierName);
			bin.Release();
			return false;
		}
		Util::SetResourceName(bin.argsBuf, "GrassOptimizations::LODArgsBuf");

		D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
		uav.Format = DXGI_FORMAT_R32_TYPELESS;
		uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uav.Buffer.FirstElement = instanceCountOffset / sizeof(uint32_t);
		uav.Buffer.NumElements = 1;
		uav.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
		if (FAILED(device->CreateUnorderedAccessView(bin.argsBuf, &uav, &bin.argsUAV)) || !bin.argsUAV) {
			logger::error("[GRASS OPTIMIZATIONS] {} LOD args UAV create failed", tierName);
			bin.Release();
			return false;
		}
	}

	bin.capacityInstances = cap;
	return true;
}
