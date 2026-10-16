#include "GrassBucketStore.h"

#include <numbers>
#include <thread>

GrassBucketStore::~GrassBucketStore()
{
	decltype(shapes) retiredShapes;
	{
		std::scoped_lock lock(pendingMutex);
		stopping = true;

		for (const auto& [native, record] : shapes)
			record->registered.store(false, std::memory_order_release);

		retiredShapes.swap(shapes);
	}

	preparation.reset();
	allocator.reset();
	cullSnapshot.store(nullptr, std::memory_order_release);
	retiredProxies.clear();
	buckets.clear();
	activeShapes.clear();
	readyEvents.clear();
	pendingEvents.clear();
}

void GrassBucketStore::RegisterModel(RE::BSMultiStreamInstanceTriShape* shape, const char* modelPath)
{
	if (!shape || !modelPath || !*modelPath)
		return;

	auto& mesh = meshLibrary.RegisterMesh(modelPath);
	if (auto previousRegistration = FindShape(shape); previousRegistration && previousRegistration->mesh == &mesh)
		return;

	auto registration = std::make_shared<GrassShape>();
	registration->native = shape;
	registration->mesh = &mesh;
	registration->geometry.reset(static_cast<RE::BSMultiStreamInstanceTriShape*>(shape->Clone()));

	if (!registration->geometry)
		return;

	std::shared_ptr<GrassShape> previousRegistration;
	{
		std::scoped_lock lock(pendingMutex);
		if (stopping)
			return;

		auto& entry = shapes[shape];
		if (entry && entry->mesh == &mesh)
			return;

		previousRegistration = std::exchange(entry, registration);
		if (previousRegistration) {
			previousRegistration->registered.store(false, std::memory_order_release);
			QueueEvent(Removal{ previousRegistration });
		}
	}

	std::vector<uint32_t> groupIndices;
	{
		EngineGroupLock lock;
		const auto& instances = shape->GetMultiStreamTrishapeRuntimeData().instanceGroups;

		for (uint32_t i = 0; i < instances.size(); ++i) {
			if (instances[i])
				groupIndices.push_back(i);
		}
	}

	for (uint32_t group : groupIndices)
		CaptureEngineGroup(shape, group);
}

void GrassBucketStore::CaptureEngineGroup(RE::BSMultiStreamInstanceTriShape* shape, uint32_t engineGroup)
{
	if (!shape || engineGroup == UINT32_MAX)
		return;

	std::scoped_lock lifecycleLock(pendingMutex);
	const auto entry = shapes.find(shape);
	if (stopping || entry == shapes.end())
		return;

	auto property = shape->GetGeometryRuntimeData().shaderProperty;
	if (!property || property->GetRTTI() != globals::rtti::BSGrassShaderPropertyRTTI.get())
		return;

	EngineGroupLock engineLock;
	const auto& groups = shape->GetMultiStreamTrishapeRuntimeData().instanceGroups;
	const auto* group = engineGroup < groups.size() ? groups[engineGroup] : nullptr;
	const auto* vertexBuffer = group ? group->vertexBuffer : nullptr;
	if (!vertexBuffer || !vertexBuffer->m_data || !group->instanceCount)
		return;

	auto capture = CaptureMetadata(shape, group->instanceCount, (uint32_t)(vertexBuffer->byteWidth / group->instanceCount));
	if (!capture)
		return;

	capture->shape = entry->second;
	capture->slice.owner = entry->second.get();
	capture->slice.engineGroup = engineGroup;
	capture->asset.key.mesh = entry->second->mesh;
	const auto* data = static_cast<const uint8_t*>(vertexBuffer->m_data);
	capture->sourceData.assign(data, data + (size_t)group->instanceCount * kGrassStride);

	QueueEvent(std::move(*capture));
}

void GrassBucketStore::RemoveEngineGroup(RE::BSMultiStreamInstanceTriShape* shape, uint32_t engineGroup,
	void (*remove)(RE::BSMultiStreamInstanceTriShape*, uint32_t))
{
	std::scoped_lock lock(pendingMutex);
	remove(shape, engineGroup);

	if (engineGroup == UINT32_MAX)
		return;

	if (auto entry = shapes.find(shape); entry != shapes.end())
		QueueEvent(Removal{ entry->second, engineGroup });
}

void GrassBucketStore::StageRemoval(RE::BSMultiStreamInstanceTriShape* shape)
{
	decltype(shapes)::node_type retired;
	{
		std::scoped_lock lock(pendingMutex);
		retired = shapes.extract(shape);

		if (retired) {
			retired.mapped()->registered.store(false, std::memory_order_release);
			QueueEvent(Removal{ retired.mapped() });
		}
	}
}

GrassBucketStore::EngineGroupLock::EngineGroupLock()
{
	while (_InterlockedCompareExchange(word.get(), 1, 0))
		std::this_thread::yield();
}

GrassBucketStore::EngineGroupLock::~EngineGroupLock()
{
	_InterlockedExchange(word.get(), 0);
}

std::optional<PendingCapture> GrassBucketStore::CaptureMetadata(RE::BSMultiStreamInstanceTriShape* shape, uint32_t count, uint32_t stride)
{
	auto& geometry = shape->GetGeometryRuntimeData();
	auto* grassProperty = static_cast<RE::BSGrassShaderProperty*>(geometry.shaderProperty.get());
	auto* texture = grassProperty->GetBaseTexture();
	auto* material = grassProperty->material;
	auto* rendererData = geometry.rendererData;

	if (!count || stride != kGrassStride || !texture || !material || material->GetType() != RE::BSShaderMaterial::Type::kLighting ||
		!rendererData || !rendererData->vertexBuffer || !rendererData->indexBuffer)
		return std::nullopt;

	const auto& bound = shape->GetModelData().modelBound;

	PendingCapture capture;
	auto& asset = capture.asset;
	auto& draw = asset.key.draw;
	asset.vertexBuffer.copy_from(reinterpret_cast<ID3D11Buffer*>(rendererData->vertexBuffer));
	asset.indexBuffer.copy_from(reinterpret_cast<ID3D11Buffer*>(rendererData->indexBuffer));

	asset.material.reset(material);
	// TruePBR marks converted grass properties with kMenuScreen.
	draw.pbrMaterial = grassProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kMenuScreen);
	draw.sphereNormal = grassProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kEffectLighting);
	asset.key.material = GrassMeshLibrary::CaptureMaterial(material, draw.pbrMaterial);
	draw.uniformScale = grassProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kUniformScale);
	asset.key.descVal = *reinterpret_cast<const uint64_t*>(&geometry.vertexDesc);

	asset.diffuseTexture = RE::NiPointer<RE::NiSourceTexture>(texture);
	capture.slice.count = count;

	asset.key.triCount = shape->GetTrishapeRuntimeData().triangleCount;
	draw.boundCenter = { bound.center.x, bound.center.y, bound.center.z };
	draw.modelRadius = bound.radius;
	draw.wavePeriod = grassProperty->wavePeriod;

	capture.slice.origin = shape->world.translate;

	// The wind blows along the model's Y axis.
	RE::NiPoint3 windDirection{ shape->world.rotate.entry[1][0], shape->world.rotate.entry[1][1], shape->world.rotate.entry[1][2] };
	windDirection.Unitize();
	draw.windDirection[0] = windDirection.x;
	draw.windDirection[1] = windDirection.y;

	return capture;
}

// The engine stores position and brightness in halves 0-3, rotation rows at (4,5,6), (8,9,10), (12,7,11), and size variance at 13.
// Pack the orthonormal rotation as a quaternion with its largest component omitted; Common/GrassInstance.hlsli decodes it.
void GrassBucketStore::PackInstances(GrassChunk& slice, const void* sourceData)
{
	using DirectX::PackedVector::XMConvertHalfToFloat;
	slice.data.resize((size_t)slice.count * kSourceStride);
	slice.maxScale = 1.0f;
	RE::NiPoint3 localMin{ FLT_MAX, FLT_MAX, FLT_MAX };
	RE::NiPoint3 localMax{ -FLT_MAX, -FLT_MAX, -FLT_MAX };

	const auto* sourceBytes = static_cast<const uint8_t*>(sourceData);
	const auto packComponent = [](float component) {
		const float normalized = std::clamp(component * std::numbers::sqrt2_v<float>, -1.0f, 1.0f);
		return (uint32_t)(std::lround(normalized * 16383.0f) + 16384);
	};

	for (uint32_t instanceIndex = 0; instanceIndex < slice.count; ++instanceIndex) {
		uint16_t halfValues[16];
		std::memcpy(halfValues, sourceBytes + (size_t)instanceIndex * kGrassStride, sizeof(halfValues));
		float values[16];

		for (int componentIndex = 0; componentIndex < 13; ++componentIndex)
			values[componentIndex] = XMConvertHalfToFloat(halfValues[componentIndex]);

		slice.maxScale = std::max(slice.maxScale, std::abs(1.0f + XMConvertHalfToFloat(halfValues[13])));
		localMin.x = std::min(localMin.x, values[0]);
		localMin.y = std::min(localMin.y, values[1]);
		localMin.z = std::min(localMin.z, values[2]);
		localMax.x = std::max(localMax.x, values[0]);
		localMax.y = std::max(localMax.y, values[1]);
		localMax.z = std::max(localMax.z, values[2]);

		const DirectX::XMFLOAT3X3 rotation{ values[4], values[5], values[6], values[8], values[9], values[10], values[12], values[7], values[11] };
		DirectX::XMFLOAT4 quaternion;
		// Engine rotation rows act on column vectors, while DirectXMath uses row vectors.
		DirectX::XMStoreFloat4(&quaternion, DirectX::XMQuaternionRotationMatrix(DirectX::XMMatrixTranspose(DirectX::XMLoadFloat3x3(&rotation))));
		const float components[]{ quaternion.x, quaternion.y, quaternion.z, quaternion.w };
		uint32_t omittedComponent = 0;
		for (uint32_t componentIndex = 1; componentIndex < 4; ++componentIndex) {
			if (std::abs(components[componentIndex]) > std::abs(components[omittedComponent]))
				omittedComponent = componentIndex;
		}

		const float rotationSign = components[omittedComponent] < 0.0f ? -1.0f : 1.0f;
		const float rotationLength = std::sqrt(components[0] * components[0] + components[1] * components[1] + components[2] * components[2] + components[3] * components[3]);
		uint32_t packedComponents[3];

		for (uint32_t componentIndex = 0, packedIndex = 0; componentIndex < 4; ++componentIndex) {
			if (componentIndex != omittedComponent)
				packedComponents[packedIndex++] = packComponent(rotationSign * components[componentIndex] / rotationLength);
		}

		const uint32_t brightness = (uint32_t)std::lround(std::clamp(values[3], 0.0f, 1.0f) * 65535.0f);
		const uint32_t packed[4] = {
			(uint32_t)halfValues[0] | ((uint32_t)halfValues[1] << 16),
			(uint32_t)halfValues[2] | (brightness << 16),
			packedComponents[0] | (packedComponents[1] << 15) | (omittedComponent << 30),
			packedComponents[2] | ((uint32_t)halfValues[13] << 16),
		};

		std::memcpy(slice.data.data() + (size_t)instanceIndex * kSourceStride, packed, sizeof(packed));
	}

	slice.bounds.lo[0] = slice.origin.x + localMin.x;
	slice.bounds.lo[1] = slice.origin.y + localMin.y;
	slice.bounds.lo[2] = slice.origin.z + localMin.z;
	slice.bounds.hi[0] = slice.origin.x + localMax.x;
	slice.bounds.hi[1] = slice.origin.y + localMax.y;
	slice.bounds.hi[2] = slice.origin.z + localMax.z;
}

void GrassBucketStore::QueueEvent(PendingEvent event)
{
	if (stopping)
		return;

	std::visit([](auto& command) { command.shape->pendingChanges.fetch_add(1, std::memory_order_release); }, event);
	if (auto* removal = std::get_if<Removal>(&event)) {
		auto& groups = removal->shape->groups;

		if (removal->engineGroup == UINT32_MAX) {
			for (auto& [index, group] : groups)
				group->current.store(false, std::memory_order_release);
			groups.clear();
		} else if (auto group = groups.extract(removal->engineGroup)) {
			removal->lifetime = std::move(group.mapped());
			removal->lifetime->current.store(false, std::memory_order_release);
		}

		readyEvents.push_back(std::move(event));
		return;
	}

	auto& capture = std::get<PendingCapture>(event);
	auto& group = capture.shape->groups[capture.slice.engineGroup];
	if (group) {
		group->current.store(false, std::memory_order_release);
		capture.shape->pendingChanges.fetch_add(1, std::memory_order_release);
		readyEvents.emplace_back(Removal{ capture.shape, capture.slice.engineGroup, group });
	}

	capture.slice.lifetime = group = std::make_shared<GrassGroup>();
	pendingEvents.push_back(std::move(event));

	if (preparing)
		return;

	preparing = true;

	if (!preparation)
		preparation = std::make_unique<BS::thread_pool<>>(1);

	preparation->detach_task([this] { PreparePending(); });
}

void GrassBucketStore::PreparePending()
{
	std::vector<PendingEvent> events;
	for (;;) {
		{
			std::scoped_lock lock(pendingMutex);
			if (pendingEvents.empty()) {
				preparing = false;
				return;
			}
			events.swap(pendingEvents);
		}

		for (auto& event : events) {
			if (auto* capture = std::get_if<PendingCapture>(&event)) {
				if (capture->shape->registered.load(std::memory_order_acquire) && capture->slice.lifetime->current.load(std::memory_order_acquire))
					PackInstances(capture->slice, capture->sourceData.data());
				capture->sourceData = std::vector<uint8_t>{};
			}
		}

		{
			std::scoped_lock lock(pendingMutex);
			readyEvents.insert(readyEvents.end(), std::make_move_iterator(events.begin()), std::make_move_iterator(events.end()));
		}
		events.clear();
	}
}

void GrassBucketStore::EndFrame()
{
	retiredProxies.clear();
	DrainPending();

	if (buckets.empty())
		compactionScratch.reset();

	PublishCullSnapshot();
}

void GrassBucketStore::DrainPending()
{
	std::vector<PendingEvent> events;
	{
		std::scoped_lock lock(pendingMutex);
		events.swap(readyEvents);
	}

	if (events.empty())
		return;

	std::unordered_set<GrassBucket*> affected;
	std::vector<std::unique_ptr<GrassChunk>> retired;

	for (auto& event : events) {
		if (auto* capture = std::get_if<PendingCapture>(&event))
			ApplyCapture(*capture, affected, retired);
		else
			ApplyRemoval(std::get<Removal>(event), affected, retired);
	}

	for (auto* bucket : affected) {
		PruneSlices(*bucket);

		if (bucket->slices.empty())
			EraseBucket(*bucket);
	}

	std::erase_if(activeShapes, [](const auto& shape) {
		if (!shape->chunks.empty())
			return false;

		shape->admitted = false;
		shape->current = nullptr;

		return true;
	});

	RetireSlices(retired);
}

void GrassBucketStore::ApplyCapture(PendingCapture& capture, std::unordered_set<GrassBucket*>& affected, std::vector<std::unique_ptr<GrassChunk>>& retired)
{
	auto& shape = *capture.shape;
	shape.pendingChanges.fetch_sub(1, std::memory_order_release);
	if (!shape.registered.load(std::memory_order_acquire) || !capture.slice.lifetime->current.load(std::memory_order_acquire)) {
		if (!capture.slice.data.empty())
			retired.push_back(std::make_unique<GrassChunk>(std::move(capture.slice)));
		return;
	}

	auto [bucketEntry, inserted] = buckets.try_emplace(capture.asset.key);
	auto& bucket = bucketEntry->second;

	if (inserted) {
		bucket.asset = std::move(capture.asset);
		if (!meshLibrary.CreateAsset(bucket.asset, capture.shape->geometry.get())) {
			buckets.erase(bucketEntry);
			return;
		}

		proxyBuckets.emplace(bucket.asset.proxy.get(), &bucket);
		const float meshCost = std::sqrt(std::max(1.0f, (float)bucket.asset.key.triCount / 8.0f));
		bucket.distScale = 1.0f / meshCost;
		bucket.minPixelScale = meshCost;
	}

	if (!shape.admitted) {
		activeShapes.push_back(capture.shape);
		shape.admitted = true;
	}

	if (affected.contains(&bucket))
		PruneSlices(bucket);

	if (shape.current && shape.current != &bucket) {
		affected.insert(shape.current);
		MigrateShapeSlices(*shape.current, bucket, shape);
	}

	shape.current = &bucket;
	if (auto previous = shape.chunks.find(capture.slice.engineGroup); previous != shape.chunks.end()) {
		previous->second->bucket = nullptr;
		PruneSlices(bucket);
		retired.push_back(std::move(previous->second));
		shape.chunks.erase(previous);
	}

	capture.slice.fadeStart = frameParams.fadeStart;

	auto chunk = std::make_unique<GrassChunk>(std::move(capture.slice));
	AppendSlice(bucket, *chunk);
	shape.chunks.emplace(chunk->engineGroup, std::move(chunk));
	snapshotDirty = true;
}

void GrassBucketStore::ApplyRemoval(const Removal& removal, std::unordered_set<GrassBucket*>& affected, std::vector<std::unique_ptr<GrassChunk>>& retired)
{
	auto& shape = *removal.shape;
	shape.pendingChanges.fetch_sub(1, std::memory_order_release);
	const auto detach = [&](std::unique_ptr<GrassChunk>& chunk) {
		affected.insert(chunk->bucket);
		chunk->bucket = nullptr;
		retired.push_back(std::move(chunk));
	};

	if (removal.engineGroup == UINT32_MAX) {
		for (auto& [group, chunk] : shape.chunks)
			detach(chunk);
		shape.chunks.clear();
	} else if (auto chunk = shape.chunks.find(removal.engineGroup); chunk != shape.chunks.end() && chunk->second->lifetime == removal.lifetime) {
		detach(chunk->second);
		shape.chunks.erase(chunk);
	}

	if (shape.chunks.empty())
		shape.current = nullptr;

	snapshotDirty = true;
}

void GrassBucketStore::AppendSlice(GrassBucket& bucket, GrassChunk& slice)
{
	maintenance.insert(&bucket);

	slice.bucket = &bucket;
	slice.bufferOffset = UINT32_MAX;
	slice.firstCullGroup = UINT32_MAX;

	if (slice.maxScale > bucket.maxScale) {
		bucket.maxScale = slice.maxScale;
		bucket.coarseValid = false;
		bucket.clustersValid = false;
	}

	const auto& bounds = slice.bounds;

	// Extend valid bounds here and recompute invalidated bounds after removals.
	if (bucket.coarseValid || bucket.slices.empty()) {
		const float padding = bucket.boundsPadding;
		const RE::NiPoint3 lo{ bounds.lo[0] - padding, bounds.lo[1] - padding, bounds.lo[2] - padding };
		const RE::NiPoint3 hi{ bounds.hi[0] + padding, bounds.hi[1] + padding, bounds.hi[2] + padding };

		if (bucket.slices.empty()) {
			bucket.coarseMin = lo;
			bucket.coarseMax = hi;
		} else {
			bucket.coarseMin.x = std::min(bucket.coarseMin.x, lo.x);
			bucket.coarseMin.y = std::min(bucket.coarseMin.y, lo.y);
			bucket.coarseMin.z = std::min(bucket.coarseMin.z, lo.z);
			bucket.coarseMax.x = std::max(bucket.coarseMax.x, hi.x);
			bucket.coarseMax.y = std::max(bucket.coarseMax.y, hi.y);
			bucket.coarseMax.z = std::max(bucket.coarseMax.z, hi.z);
		}

		bucket.coarseValid = true;
	}

	bucket.totalInstances += slice.count;
	bucket.slices.push_back(&slice);
}

void GrassBucketStore::PruneSlices(GrassBucket& bucket)
{
	const size_t originalCount = bucket.slices.size();
	size_t retainedCount = 0;
	uint32_t removedInstances = 0;
	uint32_t removedClusteredSlices = 0;
	uint32_t removedUploadedSlices = 0;

	for (size_t index = 0; index < originalCount; ++index) {
		auto& slice = *bucket.slices[index];
		if (slice.bucket == &bucket) {
			if (retainedCount != index)
				bucket.slices[retainedCount] = &slice;
			++retainedCount;
			continue;
		}

		removedInstances += slice.count;
		if (index < bucket.uploadedSlices) {
			++removedUploadedSlices;
			UpdateShapeResidency(slice, false);
		}

		if (index >= bucket.clusteredSlices)
			continue;

		++removedClusteredSlices;
		// Keep removed groups in place with zero counts until the cached runs are rebuilt.
		if (bucket.clustersValid && slice.firstCullGroup != UINT32_MAX) {
			const uint32_t groupCount = (slice.count + kCullGroupSize - 1) / kCullGroupSize;
			for (uint32_t groupIndex = 0; groupIndex < groupCount; ++groupIndex)
				bucket.cullGroups[slice.firstCullGroup + groupIndex].count = 0;
			bucket.deadGroups += groupCount;
			bucket.dirtyGroupRanges.emplace_back(slice.firstCullGroup, slice.firstCullGroup + groupCount);
		}
	}

	if (retainedCount == originalCount)
		return;

	bucket.slices.resize(retainedCount);

	bucket.uploadedSlices -= removedUploadedSlices;

	bucket.clusteredSlices -= removedClusteredSlices;
	if (bucket.deadGroups > bucket.cullGroups.size() / 2)
		bucket.clustersValid = false;

	bucket.totalInstances -= removedInstances;

	if (bucket.slices.empty()) {
		bucket.residentInstances = 0;
		bucket.clusteredSlices = 0;
		bucket.clustersValid = false;
		bucket.coarseValid = false;
		bucket.fragmented = false;
		return;
	}

	bucket.coarseValid = false;
	if (removedUploadedSlices) {
		bucket.fragmented = true;
		maintenance.insert(&bucket);
	}
}

void GrassBucketStore::MigrateShapeSlices(GrassBucket& source, GrassBucket& destination, GrassShape& shape)
{
	std::vector<GrassChunk*> migratingSlices;
	for (auto* chunk : source.slices) {
		if (chunk->bucket == &source && chunk->owner == &shape) {
			chunk->bucket = nullptr;
			migratingSlices.push_back(chunk);
		}
	}

	PruneSlices(source);

	for (auto* chunk : migratingSlices)
		AppendSlice(destination, *chunk);
}

void GrassBucketStore::EraseBucket(GrassBucket& bucket)
{
	maintenance.erase(&bucket);
	meshLibrary.ReleaseAsset(bucket.asset);
	retiredProxies.push_back(bucket.asset.proxy);
	proxyBuckets.erase(bucket.asset.proxy.get());
	buckets.erase(bucket.asset.key);
}

GrassBucket* GrassBucketStore::FindBucket(const RE::BSGeometry* shape)
{
	const auto entry = proxyBuckets.find(shape);
	return entry == proxyBuckets.end() ? nullptr : entry->second;
}

std::shared_ptr<GrassShape> GrassBucketStore::FindShape(RE::BSMultiStreamInstanceTriShape* shape)
{
	std::scoped_lock lock(pendingMutex);
	const auto entry = shapes.find(shape);
	return entry == shapes.end() ? nullptr : entry->second;
}

std::shared_ptr<GrassShape> GrassBucketStore::FindRenderedShape(RE::BSMultiStreamInstanceTriShape* shape) const
{
	const auto snapshot = AcquireCullSnapshot();
	if (!snapshot)
		return nullptr;

	const auto entry = snapshot->nativeShapes.find(shape);
	if (entry != snapshot->nativeShapes.end() && entry->second.registration->registered.load(std::memory_order_acquire))
		return entry->second.registration;

	return nullptr;
}

bool GrassBucketStore::IsShapeOptimized(const GrassShape& shape)
{
	const auto& native = shape.native->GetMultiStreamTrishapeRuntimeData();
	return !shape.pendingChanges.load(std::memory_order_acquire) && shape.current && shape.current->optimizedReady && shape.residentInstances == native.instanceCount &&
	       shape.chunks.size() == native.activeGroupCount;
}

bool GrassBucketStore::IsGroupOptimized(const GrassShape& shape, uint32_t group)
{
	if (!shape.current || !shape.current->optimizedReady)
		return false;

	const auto entry = shape.chunks.find(group);
	if (entry == shape.chunks.end())
		return false;

	const auto& chunk = *entry->second;
	return chunk.lifetime->current.load(std::memory_order_acquire) && chunk.bufferOffset != UINT32_MAX;
}

void GrassBucketStore::UpdateShapeResidency(const GrassChunk& slice, bool resident)
{
	auto& residentCount = slice.owner->residentInstances;
	const bool wasResident = residentCount != 0;

	if (resident)
		residentCount += slice.count;
	else
		residentCount -= slice.count;

	if (wasResident != (residentCount != 0))
		snapshotDirty = true;
}

void GrassBucketStore::PublishCullSnapshot()
{
	if (!snapshotDirty)
		return;

	auto snapshot = std::make_shared<CullSnapshot>();
	for (const auto& shape : activeShapes) {
		const bool direct = shape->residentInstances && shape->current && shape->current->optimizedReady;
		snapshot->nativeShapes.emplace(shape->native, CullSnapshot::NativeShape{ shape, direct });
	}

	for (const auto& [key, bucket] : buckets) {
		if (!bucket.slices.empty())
			snapshot->proxies.push_back(bucket.asset.proxy);
	}

	cullSnapshot.store(std::move(snapshot), std::memory_order_release);
	snapshotDirty = false;
}

void GrassBucketStore::SetOptimizedReady(GrassBucket& bucket, bool ready)
{
	if (bucket.optimizedReady != ready) {
		bucket.optimizedReady = ready;
		snapshotDirty = true;
	}
}
