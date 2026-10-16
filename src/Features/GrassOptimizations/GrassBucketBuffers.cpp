#include "Features/GrassLighting.h"
#include "Features/GrassOptimizations.h"

#include <bit>

void GrassOptimizations::SetupResources()
{
	cullParamsCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<CullParamsCB>(), "GrassOptimizations::CullParamsCB");
	{
		// The marker selects the engine instance stream in the optimized shader permutation.
		std::array<uint32_t, sizeof(CullBucketCB) / sizeof(uint32_t)> marker{};
		marker[0] = 0xFFFFFFFFu;
		vanillaDrawCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc(sizeof(marker), false), "GrassOptimizations::VanillaDrawCB");
		vanillaDrawCB->Update(marker);
	}

	hiZ.SetupResources();

	if (FAILED(globals::d3d::context->QueryInterface(__uuidof(ID3D11DeviceContext1), ctx1.put_void())) || !ctx1) {
		logger::error("[GRASS OPTIMIZATIONS] ID3D11DeviceContext1 unavailable — feature disabled");
		ctx1 = nullptr;
	}
}

bool GrassOptimizations::EnsureCullBucketCapacity(uint32_t slots)
{
	if (cullBucketCB && cullBucketCBSlots >= slots)
		return true;

	uint32_t capacity = cullBucketCBSlots ? cullBucketCBSlots : 64;
	while (capacity < slots)
		capacity *= 2;

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

	D3D11_MAPPED_SUBRESOURCE mapped{};
	if (FAILED(ctx->Map(cullBucketCB->CB(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		return false;

	// SetupTechnique has already populated the fog color used at c16 in PerGeometry.
	static const auto* fogNearColor = reinterpret_cast<const float*>(REL::RelocationID(527805, 414756).address() + 0x100);
	static const auto* grassWindAmount = reinterpret_cast<const float*>(REL::RelocationID(527802, 414753).address());
	static const auto* grassWindAmountMax = reinterpret_cast<const float*>(REL::RelocationID(504665, 373550).address());
	static const auto* windGrassMultiplier = reinterpret_cast<const float*>(REL::RelocationID(513337, 391112).address());

	GrassPerGeometry geometry{};
	std::memcpy(geometry.fogNearColor, fogNearColor, sizeof(geometry.fogNearColor));
	geometry.windVector[2] = std::min(*grassWindAmount, *grassWindAmountMax) * *windGrassMultiplier;
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
		const auto& draw = bucket->asset.key.draw;
		data.geometry = geometry;
		data.geometry.windVector[0] = draw.windDirection[0];
		data.geometry.windVector[1] = draw.windDirection[1];
		data.geometry.scaleMask[0] = data.geometry.scaleMask[1] = draw.uniformScale ? 1.0f : 0.0f;
		data.material = bucket->asset.key.material.constants;
		data.cull = {
			.groupCount = bucket->groupCount,
			.sliceTableOffset = bucket->sliceTableOffset,
			.sliceCount = bucket->sliceTableCount,
			.dispatchWidth = DispatchWidth(bucket->groupCount),
			.tierStride = bucket->output->capacity,
			.outputOffset = bucket->outputOffset,
			.argsOffset = slot * kTierCount * kArgsStride,
			.boundExtent = bucket->boundExtent,
			.modelRadius = draw.modelRadius,
			.distScale = bucket->distScale,
			.minPixelScale = bucket->minPixelScale,
			.midLODEnabled = bucket->lodMeshes[0] ? 1.0f : 0.0f,
			.farLODEnabled = bucket->lodMeshes[1] ? 1.0f : 0.0f,
			.complexThreshold = globals::features::grassLighting.settings.ComplexGrassThreshold,
			.wavePeriod = draw.wavePeriod,
			.timeBase = timeBase,
			.prevTimeBase = prevTimeBase
		};
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

		uint32_t capacity = 256;
		while (capacity < sliceTableCPU.size())
			capacity *= 2;

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

uint32_t GrassBucketStore::ReservedInstanceCapacity(uint32_t needed)
{
	// Keep 25% headroom, rounded to 4096 records, without overflowing the D3D11 byte width.
	const uint64_t reserved = (uint64_t)needed + ((uint64_t)needed + 3) / 4;
	return (uint32_t)std::min<uint64_t>(kMaxBucketInstances, std::max<uint64_t>(4096, (reserved + 4095) & ~uint64_t{ 4095 }));
}

uint32_t GrassBucketStore::GrownInstanceCapacity(uint32_t current, uint32_t needed)
{
	// Small buckets start at a power of two instead of reserving 4096 records.
	if (!current)
		return std::clamp(std::bit_ceil(needed), 256u, kMaxBucketInstances);

	return (uint32_t)std::min<uint64_t>(kMaxBucketInstances, std::max<uint64_t>(ReservedInstanceCapacity(needed), 2ull * current));
}

bool GrassBucketStore::CanReduceInstanceCapacity(uint32_t capacity, uint32_t needed)
{
	// Require at least a 12.5% reduction before replacing an otherwise usable buffer.
	return capacity > 4096 && ReservedInstanceCapacity(needed) <= capacity - capacity / 8;
}

void GrassBucketStore::AdoptBucketBuffers(GrassBucket& bucket, GrassSourceBuffers& replacement, ID3D11DeviceContext* ctx)
{
	const uint32_t residentCount = bucket.residentInstances;
	if (residentCount) {
		const D3D11_BOX residentRange{ 0, 0, 0, residentCount * kSourceStride, 1, 1 };
		ctx->CopySubresourceRegion(replacement.instances->resource.get(), 0, 0, 0, 0, bucket.source.instances->resource.get(), 0, &residentRange);
	}

	// Source growth leaves the per-view outputs, arguments and cached cull groups intact.
	std::swap(bucket.source, replacement);
}

bool GrassBucketStore::ResizeBucketAsync(GrassBucket& bucket, uint32_t capacity, ID3D11DeviceContext* ctx)
{
	if (bucket.allocation) {
		if (!bucket.allocation->done.load(std::memory_order_acquire))
			return false;

		const auto completedAllocation = std::exchange(bucket.allocation, nullptr);
		// Adopt replacements that preserve resident data and request further growth next frame if needed.
		if (completedAllocation->ok && completedAllocation->capacity >= bucket.residentInstances) {
			AdoptBucketBuffers(bucket, completedAllocation->source, ctx);
			return true;
		}
	}

	auto job = std::make_shared<BucketAllocation>();
	job->capacity = capacity;
	bucket.allocation = job;
	QueueAllocation(std::move(job));

	return false;
}

void GrassBucketStore::QueueAllocation(std::shared_ptr<BucketAllocation> job)
{
	if (!allocator)
		allocator = std::make_unique<BS::thread_pool<>>(1);

	allocator->detach_task([job = std::move(job)] {
		if (job.use_count() > 1)
			job->ok = CreateBucketSourceBuffers(job->source, job->capacity);
		job->done.store(true, std::memory_order_release);
	});
}

void GrassBucketStore::RetireSlices(std::vector<std::unique_ptr<GrassChunk>>& slices)
{
	if (slices.empty())
		return;

	if (!allocator)
		allocator = std::make_unique<BS::thread_pool<>>(1);
	allocator->detach_task([retired = std::move(slices)] {});
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

bool GrassBucketStore::CreateBucketSourceBuffers(GrassSourceBuffers& buffers, uint32_t capacity)
{
	try {
		auto buffer = CreateRawBuffer((uint64_t)capacity * kSourceStride, D3D11_BIND_SHADER_RESOURCE, 0, "GrassOptimizations::InstanceBuf");
		D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Format = DXGI_FORMAT_R32_TYPELESS;
		srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
		srv.BufferEx.NumElements = capacity * kSourceStride / 4;
		srv.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;

		buffer->CreateSRV(srv);
		buffers.instances = std::move(buffer);
		buffers.capacity = capacity;

		return true;
	} catch (...) {
		logger::error("[GRASS OPTIMIZATIONS] source buffer create failed instances={}", capacity);
		return false;
	}
}

bool GrassBucketStore::CreateBucketCullScratch(GrassCullBuffers& buffers, uint32_t capacity, uint32_t tiers)
{
	try {
		buffers.records = CreateRawBuffer((uint64_t)capacity * tiers * kCullRecordStride, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE, 0, "GrassOptimizations::CompactedBuf");
		for (uint32_t tier = 0; tier < tiers; ++tier) {
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
		buffers.tiers = tiers;

		return true;
	} catch (...) {
		logger::error("[GRASS OPTIMIZATIONS] cull output create failed instances={} tiers={}", capacity, tiers);
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

	GrassCullBuffers* pageToShrink = nullptr;
	for (auto& page : outputPages) {
		if (!pageToShrink && page->underutilizedFrames >= kRetentionFrames)
			pageToShrink = page.get();
		page->used = 0;
	}

	// Place large buckets first to reduce unused space in shared output pages.
	const auto requiredInstances = [](const GrassBucket* bucket) {
		return (uint32_t)std::min({ (uint64_t)bucket->totalInstances, (uint64_t)bucket->source.capacity, (uint64_t)bucket->groupCount * kCullGroupSize });
	};

	std::sort(visible.begin(), visible.end(), [&](const GrassBucket* left, const GrassBucket* right) {
		const uint32_t leftCount = requiredInstances(left);
		const uint32_t rightCount = requiredInstances(right);
		return leftCount != rightCount ? leftCount > rightCount : std::less<const GrassBucket*>{}(left, right);
	});

	for (auto* bucket : visible) {
		const uint32_t instanceCount = requiredInstances(bucket);
		const uint32_t tiers = bucket->lodMeshes[0] || bucket->lodMeshes[1] ? kTierCount : 1;
		GrassCullBuffers* selectedPage = nullptr;

		for (auto& page : outputPages) {
			const uint32_t available = page->capacity - page->used;
			if (page.get() == pageToShrink || page->tiers != tiers || available < instanceCount)
				continue;
			if (!selectedPage || available < selectedPage->capacity - selectedPage->used)
				selectedPage = page.get();
		}

		if (!selectedPage) {
			const uint32_t maxCapacity = UINT32_MAX / (tiers * kCullRecordStride);
			if (instanceCount > maxCapacity)
				return false;

			auto page = std::make_unique<GrassCullBuffers>();
			const uint32_t capacity = std::min(maxCapacity, std::max(65536u, ReservedInstanceCapacity(instanceCount)));
			if (!CreateBucketCullScratch(*page, capacity, tiers))
				return false;

			selectedPage = page.get();
			outputPages.push_back(std::move(page));
		}

		bucket->output = selectedPage;
		bucket->outputOffset = selectedPage->used;
		selectedPage->used += instanceCount;
	}

	for (auto& page : outputPages) {
		page->idleFrames = page->used ? 0 : page->idleFrames + 1;
		if (CanReduceInstanceCapacity(page->capacity, page->used))
			++page->underutilizedFrames;
		else
			page->underutilizedFrames = 0;
	}

	uint64_t activeBytes = 0;
	for (const auto& page : outputPages) {
		if (page->used)
			activeBytes += (uint64_t)page->capacity * page->tiers * kCullRecordStride;
	}

	// Retain the recent working set through camera turns, then trim it after 600 smaller frames.
	if (activeBytes >= outputReserveBytes || ++outputReserveFrames >= kRetentionFrames) {
		outputReserveBytes = activeBytes;
		outputReserveFrames = 0;
	}

	const uint64_t spareBudgetBytes = std::max(activeBytes / 4 + (1ull << 20), outputReserveBytes - activeBytes);
	std::stable_sort(outputPages.begin(), outputPages.end(), [](const auto& a, const auto& b) { return a->idleFrames < b->idleFrames; });
	uint64_t retainedBytes = 0;
	std::erase_if(outputPages, [&](const auto& page) {
		if (page->used)
			return false;

		const uint64_t bytes = (uint64_t)page->capacity * page->tiers * kCullRecordStride;
		if (page.get() == pageToShrink || page->idleFrames >= kRetentionFrames || retainedBytes + bytes > spareBudgetBytes)
			return true;
		retainedBytes += bytes;

		return false;
	});

	return true;
}

void GrassBucketStore::UploadPending(ID3D11DeviceContext* ctx)
{
	for (auto* bucket : maintenance) {
		if (!bucket->HasPendingSlices())
			continue;

		bucket->underutilizedFrames = 0;
		uint64_t requiredInstances = bucket->residentInstances;
		for (uint32_t i = bucket->uploadedSlices; i < bucket->slices.size(); ++i)
			requiredInstances += bucket->slices[i]->count;

		if (bucket->allocation && bucket->allocation->capacity < bucket->source.capacity)
			bucket->allocation.reset();

		if (bucket->source.instances && bucket->source.capacity >= requiredInstances) {
			bucket->allocation.reset();
		} else if (requiredInstances <= kMaxBucketInstances) {
			ResizeBucketAsync(*bucket, GrownInstanceCapacity(bucket->source.capacity, (uint32_t)requiredInstances), ctx);
		}
	}

	uint64_t uploadBudget = kUploadBudgetBytes;
	uint64_t defragBudget = kDefragBudgetBytes;
	bool shrinkAttempted = false;

	for (auto it = maintenance.begin(); it != maintenance.end();) {
		auto& bucket = **it;

		if (frameParams.enableMeshLOD && bucket.HasPendingSlices())
			meshLibrary.EnsureLODMeshes(*bucket.asset.key.mesh);

		if (bucket.HasPendingSlices() && bucket.source.instances && uploadBudget)
			AppendNewSlices(bucket, uploadBudget, ctx);

		if (bucket.fragmented && defragBudget)
			bucket.fragmented = !DefragBucket(bucket, defragBudget, ctx);

		const bool canShrink = !bucket.HasPendingSlices() && !bucket.fragmented && CanReduceInstanceCapacity(bucket.source.capacity, bucket.totalInstances);
		if (canShrink) {
			if (++bucket.underutilizedFrames >= 600 && !shrinkAttempted) {
				shrinkAttempted = true;
				if (ResizeBucketAsync(bucket, ReservedInstanceCapacity(bucket.totalInstances), ctx))
					bucket.underutilizedFrames = 0;
			}
		} else {
			bucket.underutilizedFrames = 0;
			if (bucket.allocation && bucket.allocation->capacity < bucket.source.capacity)
				bucket.allocation.reset();
		}

		if (bucket.HasPendingSlices() || bucket.fragmented || canShrink)
			++it;
		else
			it = maintenance.erase(it);
	}
}

void GrassBucketStore::AppendNewSlices(GrassBucket& bucket, uint64_t& budgetBytes, ID3D11DeviceContext* ctx)
{
	// Allow one oversized slice per frame so uploads always make progress.
	const uint64_t availableBytes = (uint64_t)(bucket.source.capacity - bucket.residentInstances) * kSourceStride;
	uint32_t endSlice = bucket.uploadedSlices;
	uint64_t uploadBytes = 0;
	bool outOfBudget = false;
	for (; endSlice < (uint32_t)bucket.slices.size(); ++endSlice) {
		const uint64_t sliceBytes = (uint64_t)bucket.slices[endSlice]->count * kSourceStride;
		if (uploadBytes + sliceBytes > availableBytes)
			break;
		if (uploadBytes + sliceBytes > budgetBytes && (uploadBytes || budgetBytes < kUploadBudgetBytes)) {
			outOfBudget = true;
			break;
		}
		uploadBytes += sliceBytes;
	}

	if (endSlice == bucket.uploadedSlices) {
		// A full buffer waits for its replacement without taking the budget from other buckets.
		if (outOfBudget)
			budgetBytes = 0;
		return;
	}

	UploadSlices(bucket, bucket.uploadedSlices, endSlice, bucket.residentInstances, ctx);
	budgetBytes -= std::min(uploadBytes, budgetBytes);
	bucket.uploadedSlices = endSlice;
}

void GrassBucketStore::UploadSlices(GrassBucket& bucket, uint32_t firstSlice, uint32_t endSlice, uint32_t firstInstance, ID3D11DeviceContext* ctx)
{
	// Sum the slice counts because resident records may contain removal gaps.
	uint32_t instanceCount = 0;
	for (uint32_t i = firstSlice; i < endSlice; ++i)
		instanceCount += bucket.slices[i]->count;
	if (!instanceCount)
		return;

	// Reuse storage across cell loads and upload the flattened range in one call.
	static thread_local std::vector<uint8_t> records;
	records.clear();
	records.reserve((size_t)instanceCount * kSourceStride);

	uint32_t nextInstance = firstInstance;
	for (uint32_t i = firstSlice; i < endSlice; ++i) {
		auto& slice = *bucket.slices[i];
		slice.bufferOffset = nextInstance;
		records.insert(records.end(), slice.data.begin(), slice.data.end());
		nextInstance += slice.count;
	}

	const D3D11_BOX box{ firstInstance * kSourceStride, 0, 0, nextInstance * kSourceStride, 1, 1 };
	ctx->UpdateSubresource(bucket.source.instances->resource.get(), 0, &box, records.data(), 0, 0);
	bucket.residentInstances = nextInstance;

	for (uint32_t i = firstSlice; i < endSlice; ++i)
		UpdateShapeResidency(*bucket.slices[i], true);
}

bool GrassBucketStore::DefragBucket(GrassBucket& bucket, uint64_t& budgetBytes, ID3D11DeviceContext* ctx)
{
	if (!bucket.source.instances)
		return true;

	uint32_t destinationOffset = 0;
	for (uint32_t firstSlice = 0; firstSlice < bucket.uploadedSlices;) {
		auto& slice = *bucket.slices[firstSlice];
		if (slice.bufferOffset <= destinationOffset) {
			destinationOffset = slice.bufferOffset + slice.count;
			++firstSlice;
			continue;
		}

		uint32_t endSlice = firstSlice + 1;
		uint32_t instanceCount = slice.count;
		uint64_t copyBytes = (uint64_t)instanceCount * kSourceStride * 2;

		// Always allow one move per frame so a slice larger than the budget still makes progress.
		if (copyBytes > budgetBytes && budgetBytes < kDefragBudgetBytes) {
			budgetBytes = 0;
			return false;
		}

		// Move adjacent slices together to reduce GPU copy calls.
		while (endSlice < bucket.uploadedSlices) {
			const auto& following = *bucket.slices[endSlice];
			const uint64_t combinedBytes = copyBytes + (uint64_t)following.count * kSourceStride * 2;
			if (following.bufferOffset != slice.bufferOffset + instanceCount || combinedBytes > budgetBytes)
				break;
			instanceCount += following.count;
			copyBytes = combinedBytes;
			++endSlice;
		}

		if (!MoveInstances(bucket, slice.bufferOffset, destinationOffset, instanceCount, ctx)) {
			// Preserve resident records and retry next frame if scratch allocation fails.
			budgetBytes = 0;
			return false;
		}

		budgetBytes -= std::min(copyBytes, budgetBytes);

		for (uint32_t sliceIndex = firstSlice; sliceIndex < endSlice; ++sliceIndex) {
			auto& movedSlice = *bucket.slices[sliceIndex];
			movedSlice.bufferOffset = destinationOffset;

			if (bucket.clustersValid && movedSlice.firstCullGroup != UINT32_MAX && sliceIndex < bucket.clusteredSlices) {
				const uint32_t groupCount = (movedSlice.count + kCullGroupSize - 1) / kCullGroupSize;
				for (uint32_t groupIndex = 0; groupIndex < groupCount; ++groupIndex)
					bucket.cullGroups[movedSlice.firstCullGroup + groupIndex].sourceStart = destinationOffset + groupIndex * kCullGroupSize;
				bucket.dirtyGroupRanges.emplace_back(movedSlice.firstCullGroup, movedSlice.firstCullGroup + groupCount);
			}

			destinationOffset += movedSlice.count;
		}
		firstSlice = endSlice;
	}

	bucket.residentInstances = destinationOffset;
	return true;
}

bool GrassBucketStore::MoveInstances(GrassBucket& bucket, uint32_t sourceOffset, uint32_t destinationOffset, uint32_t count, ID3D11DeviceContext* ctx)
{
	// Copy toward lower offsets through bounded scratch storage so unread source records remain intact.
	constexpr uint32_t kChunkInstances = 16384;
	if (!compactionScratch) {
		try {
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = kChunkInstances * kSourceStride;
			desc.Usage = D3D11_USAGE_DEFAULT;
			compactionScratch = std::make_unique<Buffer>(desc, nullptr, "GrassOptimizations::CompactionScratch");
		} catch (...) {
			return false;
		}
	}
	for (uint32_t offset = 0; offset < count;) {
		const uint32_t chunkCount = std::min(kChunkInstances, count - offset);
		const UINT recordBytes = chunkCount * kSourceStride;
		const uint32_t sourceInstance = sourceOffset + offset;
		const uint32_t destinationInstance = destinationOffset + offset;

		const D3D11_BOX sourceRange{ sourceInstance * kSourceStride, 0, 0, sourceInstance * kSourceStride + recordBytes, 1, 1 };
		ctx->CopySubresourceRegion(compactionScratch->resource.get(), 0, 0, 0, 0, bucket.source.instances->resource.get(), 0, &sourceRange);
		const D3D11_BOX scratchRange{ 0, 0, 0, recordBytes, 1, 1 };
		ctx->CopySubresourceRegion(bucket.source.instances->resource.get(), 0, destinationInstance * kSourceStride, 0, 0, compactionScratch->resource.get(), 0, &scratchRange);
		offset += chunkCount;
	}

	return true;
}

bool GrassBucketStore::UploadCullGroups(GrassBucket& bucket, ID3D11DeviceContext* ctx)
{
	const uint32_t groupCount = (uint32_t)bucket.cullGroups.size();
	if (groupCount > bucket.groupCapacity || !bucket.groups) {
		uint32_t capacity = bucket.groupCapacity ? bucket.groupCapacity : 256;
		while (capacity < groupCount)
			capacity *= 2;

		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = capacity * sizeof(CullGroup);
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(CullGroup);

		try {
			auto groups = std::make_unique<Buffer>(desc, nullptr, "GrassOptimizations::GroupBuf");
			D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
			srv.Format = DXGI_FORMAT_UNKNOWN;
			srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			srv.Buffer.NumElements = capacity;
			groups->CreateSRV(srv);
			bucket.groups = std::move(groups);
		} catch (...) {
			logger::error("[GRASS OPTIMIZATIONS] group buffer create failed groups={}", capacity);
			return false;
		}

		bucket.groupCapacity = capacity;
		bucket.residentGroups = 0;
	}

	// Upload groups appended at the tail plus each resident range emptied or moved since the last upload.
	auto& ranges = bucket.dirtyGroupRanges;
	if (groupCount > bucket.residentGroups)
		ranges.emplace_back(bucket.residentGroups, groupCount);

	std::sort(ranges.begin(), ranges.end());
	constexpr uint32_t kMergeGap = 64;  // Neighbouring ranges share one update rather than many tiny ones.

	for (size_t i = 0; i < ranges.size();) {
		const uint32_t firstGroup = ranges[i].first;
		uint32_t endGroup = ranges[i].second;

		for (++i; i < ranges.size() && ranges[i].first <= endGroup + kMergeGap; ++i)
			endGroup = std::max(endGroup, ranges[i].second);
		endGroup = std::min(endGroup, groupCount);

		if (endGroup <= firstGroup)
			continue;

		const D3D11_BOX box{ firstGroup * (UINT)sizeof(CullGroup), 0, 0, endGroup * (UINT)sizeof(CullGroup), 1, 1 };
		ctx->UpdateSubresource(bucket.groups->resource.get(), 0, &box, bucket.cullGroups.data() + firstGroup, 0, 0);
	}

	ranges.clear();
	bucket.residentGroups = groupCount;

	return true;
}
