#include "Features/GrassLighting.h"
#include "Features/GrassOptimizations.h"
#include "Features/LinearLighting.h"

#include <bit>

void GrassOptimizations::SetupResources()
{
	cullParamsCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<CullParamsCB>(), "GrassOptimizations::CullParamsCB");
	{
		// Select the engine instance stream in the optimized shader.
		std::array<uint32_t, sizeof(CullBucketCB) / sizeof(uint32_t)> marker{};
		marker[0] = 0xFFFFFFFFu;
		vanillaDrawCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc(sizeof(marker), false), "GrassOptimizations::VanillaDrawCB");
		vanillaDrawCB->Update(marker);
	}

	if (FAILED(globals::d3d::context->QueryInterface(__uuidof(ID3D11DeviceContext1), ctx1.put_void())) || !ctx1) {
		logger::error("[GRASS OPTIMIZATIONS] ID3D11DeviceContext1 unavailable — feature disabled");
		ctx1 = nullptr;
	}

	ownsGrass.store(ctx1 && GetCullCS(), std::memory_order_release);
}

bool GrassOptimizations::EnsureCullBucketCapacity(uint32_t slots)
{
	if (cullBucketCB && cullBucketCBSlots >= slots)
		return true;

	const uint32_t capacity = std::max(64u, std::bit_ceil(slots));
	cullBucketCB.reset();
	cullBucketCBSlots = 0;

	try {
		cullBucketCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc(capacity * kSlotBytes), "GrassOptimizations::CullBucketCB");
	} catch (...) {
		logger::error("[GRASS OPTIMIZATIONS] cull bucket CB create failed slots={}", capacity);
		return false;
	}

	cullBucketCBSlots = capacity;

	return true;
}

bool GrassOptimizations::UploadCullBucketConstants(ID3D11DeviceContext* ctx)
{
	if (!EnsureCullBucketCapacity((uint32_t)visibleBuckets.size()))
		return false;
	cullEntries.clear();
	cullEntries.reserve(visibleBuckets.size());

	D3D11_MAPPED_SUBRESOURCE mapped{};
	if (FAILED(ctx->Map(cullBucketCB->CB(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		return false;

	// SetupTechnique populates the fog color for PerGeometry register c16.
	static const auto* fogNearColor = reinterpret_cast<const float*>(REL::RelocationID(527805, 414756).address() + 0x100);
	static const auto* grassWindAmount = reinterpret_cast<const float*>(REL::RelocationID(527802, 414753).address());
	static const auto* windGrassMultiplier = reinterpret_cast<const float*>(REL::RelocationID(513337, 391112).address());

	GrassPerGeometry geometry{};
	std::memcpy(geometry.fogNearColor, fogNearColor, sizeof(geometry.fogNearColor));
	geometry.windVector[2] = std::min(*grassWindAmount, 60.0f) * *windGrassMultiplier;
	geometry.scaleMask[2] = 1.0f;

	if (auto* sunLight = GetSunLight()) {
		const auto& sunDirection = sunLight->GetWorldDirection();
		const auto& sun = sunLight->GetLightRuntimeData();

		geometry.dirLightDirection[0] = -sunDirection.x;
		geometry.dirLightDirection[1] = -sunDirection.y;
		geometry.dirLightDirection[2] = -sunDirection.z;
		geometry.dirLightColor[0] = sun.diffuse.red;
		geometry.dirLightColor[1] = sun.diffuse.green;
		geometry.dirLightColor[2] = sun.diffuse.blue;
		geometry.ambientColor[0] = sun.ambient.red;
		geometry.ambientColor[1] = sun.ambient.green;
		geometry.ambientColor[2] = sun.ambient.blue;
	}

	auto* slots = static_cast<CullBucketSlot*>(mapped.pData);
	uint32_t slot = 0;
	for (GrassBucket* bucket : visibleBuckets) {
		bucket->cullSlot = slot;
		auto& data = slots[slot];
		const auto& draw = bucket->drawResources.key.draw;

		data.geometry = geometry;
		data.geometry.windVector[0] = draw.windDirection[0];
		data.geometry.windVector[1] = draw.windDirection[1];
		data.geometry.scaleMask[0] = data.geometry.scaleMask[1] = draw.uniformScale ? 1.0f : 0.0f;
		data.material = bucket->drawResources.key.material.constants;

		const auto& linearLighting = globals::features::linearLighting;
		data.material.params1[2] = linearLighting.IsLinearLightingActive() ? linearLighting.settings.emitColorMult : 1.0f;

		const float canopyHeight = draw.boundCenter[2] > 1.0f ? 2.0f * draw.boundCenter[2] : std::max(draw.boundCenter[2] + draw.modelRadius, 1.0f);
		float occlusionRadius = draw.modelRadius + std::hypot(draw.boundCenter[0], draw.boundCenter[1], draw.boundCenter[2] - 0.5f * canopyHeight);

		for (const auto* lod : bucket->lodMeshes) {
			if (lod)
				occlusionRadius = std::max(occlusionRadius, lod->occlusionRadius);
		}

		const auto lodMeshCost = [bucket](size_t tier) {
			const auto* lod = bucket->lodMeshes[tier];
			return lod ? lod->GetMeshCost() : bucket->minPixelScale;
		};

		const auto lodCanopyHeight = [bucket, canopyHeight](size_t tier) {
			const auto* lod = bucket->lodMeshes[tier];
			return lod ? lod->canopyHeight : canopyHeight;
		};

		const CullBucketCB cull{
			.groupCount = bucket->groupCount,
			.sliceTableOffset = bucket->sliceTableOffset,
			.sliceCount = bucket->sliceTableCount,
			.groupBase = bucket->groupBase,
			.tierStride = bucketStore.cullOutput.capacity,
			.outputOffset = bucket->outputOffset,
			.argsOffset = slot * kTierCount * kArgsStride,
			.occlusionRadius = occlusionRadius,
			.boundExtent = bucket->boundExtent,
			.modelRadius = draw.modelRadius,
			.distScale = bucket->distScale,
			.minPixelScale = bucket->minPixelScale,
			.midLODEnabled = bucket->lodMeshes[0] ? 1.0f : 0.0f,
			.farLODEnabled = bucket->lodMeshes[1] ? 1.0f : 0.0f,
			.complexThreshold = globals::features::grassLighting.settings.ComplexGrassThreshold,
			.midLODMeshCost = lodMeshCost(0),
			.wavePeriod = draw.wavePeriod,
			.timeBase = timeBase,
			.prevTimeBase = prevTimeBase,
			.farLODMeshCost = lodMeshCost(1),
			.canopyHeights = { canopyHeight, lodCanopyHeight(0), lodCanopyHeight(1) },
			.alphaTestRef = bucket->alphaTestRefKnown ? bucket->alphaTestRef : 0.0f
		};

		data.cull = cull;
		cullEntries.push_back({ .cull = cull });
		++slot;
	}

	ctx->Unmap(cullBucketCB->CB(), 0);

	return true;
}

bool GrassOptimizations::UploadSliceTable(ID3D11DeviceContext* ctx)
{
	if (sliceTableCPU.size() > sliceTableCapacity) {
		sliceTable.reset();
		sliceTableCapacity = 0;

		const uint32_t capacity = std::max(256u, std::bit_ceil((uint32_t)sliceTableCPU.size()));
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = capacity * 2 * sizeof(uint32_t);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = 2 * sizeof(uint32_t);

		try {
			sliceTable = std::make_unique<Buffer>(desc, nullptr, "GrassOptimizations::SliceTable");
			D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
			srv.Format = DXGI_FORMAT_UNKNOWN;
			srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			srv.Buffer.NumElements = capacity;
			sliceTable->CreateSRV(srv);
			sliceTableCapacity = capacity;
		} catch (...) {
			logger::error("[GRASS OPTIMIZATIONS] slice table create failed elements={}", capacity);
			sliceTable.reset();
			return false;
		}
	}

	D3D11_MAPPED_SUBRESOURCE mapped{};
	if (!sliceTable || !sliceTable->srv || FAILED(ctx->Map(sliceTable->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		return false;

	std::memcpy(mapped.pData, sliceTableCPU.data(), sliceTableCPU.size() * 2 * sizeof(uint32_t));
	ctx->Unmap(sliceTable->resource.get(), 0);

	return true;
}

void GrassRangeAllocator::Insert(uint32_t offset, uint32_t count)
{
	freeRanges.emplace(offset, count);
	rangesBySize.emplace(count, offset);
}

void GrassRangeAllocator::Erase(std::map<uint32_t, uint32_t>::iterator range)
{
	rangesBySize.erase({ range->second, range->first });
	freeRanges.erase(range);
}

uint32_t GrassRangeAllocator::Allocate(uint32_t count)
{
	const auto fit = rangesBySize.lower_bound({ count, 0 });
	if (fit == rangesBySize.end())
		return UINT32_MAX;

	const auto [available, offset] = *fit;
	Erase(freeRanges.find(offset));
	if (available > count)
		Insert(offset + count, available - count);
	return offset;
}

bool GrassRangeAllocator::TryExtend(uint32_t offset, uint32_t count, uint32_t newCount)
{
	const auto next = freeRanges.find(offset + count);
	if (next == freeRanges.end() || next->second < newCount - count)
		return false;

	const uint32_t remaining = next->second - (newCount - count);
	Erase(next);
	if (remaining)
		Insert(offset + newCount, remaining);
	return true;
}

void GrassRangeAllocator::Free(uint32_t offset, uint32_t count)
{
	if (!count)
		return;

	auto next = freeRanges.lower_bound(offset);
	if (next != freeRanges.begin()) {
		if (const auto previous = std::prev(next); previous->first + previous->second == offset) {
			offset = previous->first;
			count += previous->second;
			Erase(previous);
		}
	}

	if (next != freeRanges.end() && offset + count == next->first) {
		count += next->second;
		Erase(next);
	}

	Insert(offset, count);
}

void GrassRangeAllocator::Grow(uint32_t newCapacity)
{
	if (newCapacity <= capacity)
		return;

	const uint32_t previous = std::exchange(capacity, newCapacity);
	Free(previous, newCapacity - previous);
}

uint32_t GrassBucketStore::ReservedInstanceCapacity(uint32_t needed)
{
	// Leave headroom and round to 4096 instances to reduce reallocations.
	const uint64_t reserved = (uint64_t)needed + ((uint64_t)needed + 3) / 4;
	return (uint32_t)std::min<uint64_t>(kMaxPoolInstances, std::max<uint64_t>(4096, (reserved + 4095) & ~uint64_t{ 4095 }));
}

bool GrassBucketStore::CanReduceInstanceCapacity(uint32_t capacity, uint32_t needed)
{
	// Avoid reallocating for small reductions in usage.
	return capacity > 4096 && ReservedInstanceCapacity(needed) <= capacity - capacity / 8;
}

void GrassBucketStore::RequestPoolGrowth(const GrassPool& pool, std::shared_ptr<GrassPoolGrowth>& growth, uint32_t extra, uint32_t minCapacity, uint32_t maxCapacity,
	std::unique_ptr<Buffer> (*create)(uint32_t))
{
	// Grow geometrically to limit pool copies on the render thread.
	const uint64_t current = pool.ranges.Capacity();
	const uint64_t capacity = std::min<uint64_t>(maxCapacity, std::max<uint64_t>({ minCapacity, 2 * current, current + extra + extra / 4 }));
	if (growth || capacity < current + extra)
		return;

	auto job = std::make_shared<GrassPoolGrowth>();
	job->capacity = (uint32_t)capacity;
	growth = job;

	if (!allocator)
		allocator = std::make_unique<BS::thread_pool<>>(1);

	// Creating a large buffer can stall, so it happens on the allocation thread.
	allocator->detach_task([job = std::move(job), create] {
		// A sole reference means the store no longer needs this buffer.
		if (job.use_count() > 1)
			job->buffer = create(job->capacity);
		job->done.store(true, std::memory_order_release);
	});
}

void GrassBucketStore::AdoptPoolGrowth(GrassPool& pool, std::shared_ptr<GrassPoolGrowth>& growth, uint32_t stride, ID3D11DeviceContext* ctx)
{
	if (!growth || !growth->done.load(std::memory_order_acquire))
		return;

	const auto job = std::exchange(growth, nullptr);
	const uint32_t current = pool.ranges.Capacity();
	if (!job->buffer || job->capacity <= current)
		return;

	if (current) {
		const D3D11_BOX all{ 0, 0, 0, current * stride, 1, 1 };
		ctx->CopySubresourceRegion(job->buffer->resource.get(), 0, 0, 0, 0, pool.buffer->resource.get(), 0, &all);
	}

	pool.buffer = std::move(job->buffer);
	pool.ranges.Grow(job->capacity);
}

void GrassBucketStore::RetireSlices(std::vector<std::unique_ptr<GrassInstanceSlice>>& slices)
{
	if (slices.empty())
		return;

	if (!allocator)
		allocator = std::make_unique<BS::thread_pool<>>(1);
	allocator->detach_task([retired = std::move(slices)] {});
}

std::unique_ptr<Buffer> GrassBucketStore::CreateStructuredBuffer(uint32_t count, uint32_t stride, const char* name)
{
	D3D11_BUFFER_DESC desc{};
	desc.ByteWidth = count * stride;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	desc.StructureByteStride = stride;
	auto buffer = std::make_unique<Buffer>(desc, nullptr, name);

	D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.Format = DXGI_FORMAT_UNKNOWN;
	srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srv.Buffer.NumElements = count;
	buffer->CreateSRV(srv);
	return buffer;
}

std::unique_ptr<Buffer> GrassBucketStore::CreateRawBuffer(uint64_t bytes, uint32_t bindings, uint32_t misc, const char* name)
{
	if (bytes > UINT32_MAX)
		throw std::length_error("Grass buffer exceeds D3D11 ByteWidth");

	D3D11_BUFFER_DESC desc{};
	desc.ByteWidth = (UINT)bytes;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = bindings;
	desc.MiscFlags = misc | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;

	auto buffer = std::make_unique<Buffer>(desc, nullptr, name);
	if (bindings & D3D11_BIND_UNORDERED_ACCESS) {
		D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
		uav.Format = DXGI_FORMAT_R32_TYPELESS;
		uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uav.Buffer.NumElements = desc.ByteWidth / 4;
		uav.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
		buffer->CreateUAV(uav);
	}

	return buffer;
}

std::unique_ptr<Buffer> GrassBucketStore::CreateInstancePoolBuffer(uint32_t capacity)
{
	try {
		auto buffer = CreateRawBuffer((uint64_t)capacity * kSourceStride, D3D11_BIND_SHADER_RESOURCE, 0, "GrassOptimizations::InstancePool");
		D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Format = DXGI_FORMAT_R32_TYPELESS;
		srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
		srv.BufferEx.NumElements = capacity * kSourceStride / 4;
		srv.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;

		buffer->CreateSRV(srv);
		return buffer;
	} catch (...) {
		logger::error("[GRASS OPTIMIZATIONS] instance pool create failed instances={}", capacity);
		return nullptr;
	}
}

std::unique_ptr<Buffer> GrassBucketStore::CreateGroupPoolBuffer(uint32_t capacity)
{
	try {
		return CreateStructuredBuffer(capacity, sizeof(CullGroup), "GrassOptimizations::GroupPool");
	} catch (...) {
		logger::error("[GRASS OPTIMIZATIONS] group pool create failed groups={}", capacity);
		return nullptr;
	}
}

bool GrassBucketStore::CreateCullOutput(GrassCullBuffers& buffers, uint32_t capacity)
{
	try {
		buffers.records = CreateRawBuffer((uint64_t)capacity * kTierCount * kCullRecordStride, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE, 0, "GrassOptimizations::CompactedBuf");
		for (uint32_t tier = 0; tier < kTierCount; ++tier) {
			D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
			srv.Format = DXGI_FORMAT_R32_TYPELESS;
			srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
			srv.BufferEx.FirstElement = tier * capacity * kCullRecordStride / 4;
			srv.BufferEx.NumElements = capacity * kCullRecordStride / 4;
			srv.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;

			buffers.records->CreateSRV(srv);
			buffers.compactedSRVs[tier] = std::move(buffers.records->srv);
			Util::SetResourceName(buffers.compactedSRVs[tier].get(), "GrassOptimizations::CompactedBuf Tier%u SRV", tier);
		}

		buffers.capacity = capacity;

		return true;
	} catch (...) {
		logger::error("[GRASS OPTIMIZATIONS] cull output create failed instances={}", capacity);
		return false;
	}
}

bool GrassBucketStore::EnsureArgsCapacity(uint32_t count)
{
	if (count <= argsCapacity)
		return true;

	const uint64_t capacity = std::max<uint64_t>(count, 2ull * argsCapacity);

	try {
		auto buffer = CreateRawBuffer(capacity * kTierCount * kArgsStride, D3D11_BIND_UNORDERED_ACCESS, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS, "GrassOptimizations::ArgsBuf");
		arguments = std::move(buffer);
		argsCapacity = (uint32_t)capacity;

		return true;
	} catch (...) {
		logger::error("[GRASS OPTIMIZATIONS] args buffer create failed buckets={}", capacity);
		return false;
	}
}

bool GrassBucketStore::PrepareCullOutputs(std::span<GrassBucket*> visible)
{
	constexpr uint32_t kRetentionFrames = 600;

	if (!EnsureArgsCapacity((uint32_t)visible.size()))
		return false;

	uint64_t used = 0;
	for (auto* bucket : visible) {
		bucket->outputOffset = (uint32_t)used;
		used += std::min((uint64_t)bucket->residentInstances, (uint64_t)bucket->groupCount * kCullGroupSize);
	}

	const uint32_t maxCapacity = UINT32_MAX / (kTierCount * kCullRecordStride);
	if (used > maxCapacity)
		return false;

	// Delay shrinking so camera turns do not cause repeated reallocations.
	if (CanReduceInstanceCapacity(cullOutput.capacity, (uint32_t)used))
		++cullOutput.underutilizedFrames;
	else
		cullOutput.underutilizedFrames = 0;

	if (used > cullOutput.capacity || cullOutput.underutilizedFrames >= kRetentionFrames) {
		cullOutput = {};
		if (!CreateCullOutput(cullOutput, std::min(maxCapacity, std::max(65536u, ReservedInstanceCapacity((uint32_t)used)))))
			return false;
	}

	return true;
}

void GrassBucketStore::UploadPending(ID3D11DeviceContext* ctx)
{
	// Failed growth is retried when an allocation next runs out of space.
	AdoptPoolGrowth(instancePool, instancePoolGrowth, kSourceStride, ctx);
	AdoptPoolGrowth(groupPool, groupPoolGrowth, sizeof(CullGroup), ctx);

	if (frameParams.enableMeshLOD) {
		for (auto* bucket : pendingUploads) {
			if (bucket->HasPendingSlices())
				meshLibrary.EnsureLODMeshes(*bucket->drawResources.key.mesh);
		}
	}

	UploadNewSlices(ctx);

	std::erase_if(pendingUploads, [](const GrassBucket* bucket) { return !bucket->HasPendingSlices(); });
}

uint32_t GrassBucketStore::SelectUploadSlices(const GrassBucket& bucket, uint64_t& budgetBytes, uint64_t& uploadBytes)
{
	// Allow one oversized slice per frame so uploads always make progress.
	uint32_t endSlice = bucket.uploadedSlices;
	uploadBytes = 0;
	for (; endSlice < (uint32_t)bucket.slices.size(); ++endSlice) {
		const uint64_t sliceBytes = (uint64_t)bucket.slices[endSlice]->count * kSourceStride;
		if (uploadBytes + sliceBytes > budgetBytes && (uploadBytes || budgetBytes < kUploadBudgetBytes))
			break;
		uploadBytes += sliceBytes;
	}

	// End this frame's uploads rather than letting smaller slices overtake this one.
	if (endSlice == bucket.uploadedSlices)
		budgetBytes = 0;
	budgetBytes -= std::min(uploadBytes, budgetBytes);
	return endSlice;
}

void GrassBucketStore::UploadNewSlices(ID3D11DeviceContext* ctx)
{
	uploadBatches.clear();
	uint64_t budgetBytes = kUploadBudgetBytes;
	uint64_t stagingBytes = 0;

	for (auto* bucket : pendingUploads) {
		if (!budgetBytes)
			break;
		if (!bucket->HasPendingSlices())
			continue;

		uint64_t bytes = 0;
		const uint32_t endSlice = SelectUploadSlices(*bucket, budgetBytes, bytes);
		if (endSlice == bucket->uploadedSlices)
			continue;

		uploadBatches.push_back({ bucket, endSlice, (uint32_t)(bytes / kSourceStride) });
		stagingBytes += bytes;
	}

	if (uploadBatches.empty())
		return;

	uint64_t stagingOffset = 0;
	auto* staging = MapUploadStaging(ctx, stagingBytes, stagingOffset);
	if (!staging)
		return;

	// Map before allocating pool ranges so a failed map cannot strand allocations.
	uploadCopies.clear();
	uint64_t unplacedInstances = 0;

	for (const auto& batch : uploadBatches) {
		auto& bucket = *batch.bucket;
		uint32_t unplaced = batch.instances;
		// Prefer one contiguous range per batch to reduce copy calls.
		uint32_t batchOffset = instancePool.ranges.Allocate(unplaced);

		for (; bucket.uploadedSlices < batch.endSlice; ++bucket.uploadedSlices) {
			auto& slice = *bucket.slices[bucket.uploadedSlices];

			uint32_t offset;
			if (batchOffset != UINT32_MAX) {
				offset = batchOffset;
				batchOffset += slice.count;
			} else {
				offset = instancePool.ranges.Allocate(slice.count);
			}

			if (offset == UINT32_MAX) {
				// Stop at the first allocation failure to preserve the uploaded slice prefix.
				unplacedInstances += unplaced;
				break;
			}

			const uint64_t sliceBytes = (uint64_t)slice.count * kSourceStride;
			std::memcpy(staging + stagingOffset, slice.data.data(), sliceBytes);
			AddUploadCopy(stagingOffset, offset, slice.count);
			stagingOffset += sliceBytes;
			unplaced -= slice.count;

			slice.bufferOffset = offset;
			// Start fading after upload so delayed slices do not appear partly faded.
			slice.fadeStart = frameParams.fadeStart;
			bucket.residentInstances += slice.count;
		}
	}

	ctx->Unmap(uploadStaging->resource.get(), 0);

	if (unplacedInstances)
		RequestPoolGrowth(instancePool, instancePoolGrowth, (uint32_t)std::min<uint64_t>(unplacedInstances, kMaxPoolInstances), 1u << 20, kMaxPoolInstances, CreateInstancePoolBuffer);

	for (const auto& copy : uploadCopies) {
		const D3D11_BOX box{ (UINT)copy.stagingOffset, 0, 0, (UINT)(copy.stagingOffset + (uint64_t)copy.count * kSourceStride), 1, 1 };
		ctx->CopySubresourceRegion(instancePool.buffer->resource.get(), 0, copy.poolOffset * kSourceStride, 0, 0, uploadStaging->resource.get(), 0, &box);
	}
}

uint8_t* GrassBucketStore::MapUploadStaging(ID3D11DeviceContext* ctx, uint64_t bytes, uint64_t& offset)
{
	// Append without discarding until the staging buffer wraps.
	if (!uploadStaging || uploadStaging->desc.ByteWidth < bytes) {
		uploadStaging.reset();
		try {
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = (UINT)std::max<uint64_t>(4 * kUploadBudgetBytes, std::bit_ceil(bytes));
			desc.Usage = D3D11_USAGE_DYNAMIC;
			// The vertex-buffer binding permits NO_OVERWRITE on D3D11.0.
			desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			uploadStaging = std::make_unique<Buffer>(desc, nullptr, "GrassOptimizations::UploadStaging");
			uploadStagingOffset = desc.ByteWidth;  // The first map must discard.
		} catch (...) {
			logger::error("[GRASS OPTIMIZATIONS] upload staging create failed bytes={}", bytes);
			return nullptr;
		}
	}

	const bool wrap = uploadStagingOffset + bytes > uploadStaging->desc.ByteWidth;
	D3D11_MAPPED_SUBRESOURCE mapped{};
	if (FAILED(ctx->Map(uploadStaging->resource.get(), 0, wrap ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mapped)))
		return nullptr;
	if (wrap)
		uploadStagingOffset = 0;

	offset = uploadStagingOffset;
	uploadStagingOffset += bytes;
	return static_cast<uint8_t*>(mapped.pData);
}

void GrassBucketStore::AddUploadCopy(uint64_t stagingOffset, uint32_t poolOffset, uint32_t count)
{
	if (!uploadCopies.empty()) {
		auto& last = uploadCopies.back();
		if (last.poolOffset + last.count == poolOffset && last.stagingOffset + (uint64_t)last.count * kSourceStride == stagingOffset) {
			last.count += count;
			return;
		}
	}
	uploadCopies.push_back({ stagingOffset, poolOffset, count });
}

void GrassBucketStore::UploadCullGroups(GrassBucket& bucket, ID3D11DeviceContext* ctx)
{
	// Limit spare capacity because all buckets share this pool.
	const uint32_t groupCount = (uint32_t)bucket.cullGroups.size();
	const uint32_t reserved = std::max(256u, (groupCount + groupCount / 4 + 255) & ~255u);
	if (groupCount > bucket.groupCapacity) {
		if (bucket.groupCapacity && groupPool.ranges.TryExtend(bucket.groupBase, bucket.groupCapacity, reserved)) {
			bucket.groupCapacity = reserved;
		} else if (const uint32_t base = groupPool.ranges.Allocate(reserved); base != UINT32_MAX) {
			// The CPU copy supplies moved groups, so no GPU copy is needed.
			groupPool.ranges.Free(bucket.groupBase, bucket.groupCapacity);
			bucket.groupBase = base;
			bucket.groupCapacity = reserved;
			bucket.residentGroups = 0;
		} else {
			// Keep drawing resident groups while the pool grows.
			RequestPoolGrowth(groupPool, groupPoolGrowth, reserved, 1u << 16, kMaxPoolGroups, CreateGroupPoolBuffer);
			bucket.rebuildPending = bucket.rebuildPending || bucket.deadGroups;
		}
	} else if (bucket.groupCapacity > 2 * reserved) {
		// Live groups occupy a prefix, so the unused tail can be freed in place.
		groupPool.ranges.Free(bucket.groupBase + reserved, bucket.groupCapacity - reserved);
		bucket.groupCapacity = reserved;
	}

	// Upload new groups and dirty resident ranges, deferring groups that do not yet fit.
	const uint32_t uploadable = std::min({ groupCount, bucket.groupCapacity, kMaxRecordGroups });
	auto& ranges = bucket.dirtyGroupRanges;
	if (uploadable > bucket.residentGroups)
		ranges.emplace_back(bucket.residentGroups, uploadable);

	std::sort(ranges.begin(), ranges.end());
	constexpr uint32_t kMergeGap = 64;  // Neighbouring ranges share one update rather than many tiny ones.

	for (size_t i = 0; i < ranges.size();) {
		const uint32_t firstGroup = ranges[i].first;
		uint32_t endGroup = ranges[i].second;

		for (++i; i < ranges.size() && ranges[i].first <= endGroup + kMergeGap; ++i)
			endGroup = std::max(endGroup, ranges[i].second);
		endGroup = std::min(endGroup, uploadable);

		if (endGroup <= firstGroup)
			continue;

		const D3D11_BOX box{ (bucket.groupBase + firstGroup) * (UINT)sizeof(CullGroup), 0, 0, (bucket.groupBase + endGroup) * (UINT)sizeof(CullGroup), 1, 1 };
		ctx->UpdateSubresource(groupPool.buffer->resource.get(), 0, &box, bucket.cullGroups.data() + firstGroup, 0, 0);
	}

	ranges.clear();
	bucket.residentGroups = uploadable;
}
