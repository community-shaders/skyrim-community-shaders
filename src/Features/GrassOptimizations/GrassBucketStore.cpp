#include "GrassBucketStore.h"

#include <numbers>
#include <thread>

GrassBucketStore::~GrassBucketStore()
{
	// Keep registrations alive until workers stop and buckets release their chunk pointers.
	decltype(shapes) retainedShapes;
	{
		std::scoped_lock lock(pendingMutex);
		stopping = true;

		for (const auto& [native, record] : shapes)
			record->registered.store(false, std::memory_order_release);

		retainedShapes.swap(shapes);
	}

	preparation.reset();
	allocator.reset();

	publishedSnapshot.store(nullptr, std::memory_order_release);
	cullSnapshot.reset();
	retiredSnapshot.reset();

	pendingCaptures.clear();
	readyEvents.clear();

	buckets.clear();
	retiredProxies.clear();
	activeShapes.clear();
}

void GrassBucketStore::RegisterModel(RE::BSMultiStreamInstanceTriShape* shape, const char* modelPath)
{
	if (!shape || !modelPath || !*modelPath)
		return;

	auto& mesh = meshLibrary.RegisterMesh(modelPath);
	if (auto previousRegistration = FindShape(shape); previousRegistration && previousRegistration->mesh == &mesh)
		return;

	auto registration = std::make_shared<GrassShapeRegistration>();
	registration->native = shape;
	registration->mesh = &mesh;
	registration->geometry.reset(static_cast<RE::BSMultiStreamInstanceTriShape*>(shape->Clone()));

	if (!registration->geometry)
		return;

	std::shared_ptr<GrassShapeRegistration> previousRegistration;
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
		const auto& engineGroups = shape->GetMultiStreamTrishapeRuntimeData().instanceGroups;

		for (uint32_t i = 0; i < engineGroups.size(); ++i) {
			if (engineGroups[i])
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

	EngineGroupLock engineLock;
	CaptureEngineGroup(entry->second, engineGroup);
}

void GrassBucketStore::CaptureQueuedGroups()
{
	std::scoped_lock lifecycleLock(pendingMutex);
	if (stopping)
		return;

	EngineGroupLock engineLock;
	for (const auto& [native, registration] : shapes) {
		const auto& data = native->GetMultiStreamTrishapeRuntimeData();
		if (registration->groups.size() == data.activeGroupCount)
			continue;

		for (uint32_t index = 0; index < data.instanceGroups.size(); ++index) {
			if (data.instanceGroups[index] && !registration->groups.contains(index))
				CaptureEngineGroup(registration, index);
		}
	}
}

void GrassBucketStore::CaptureEngineGroup(const std::shared_ptr<GrassShapeRegistration>& registration, uint32_t engineGroup)
{
	auto* shape = registration->native;
	auto property = shape->GetGeometryRuntimeData().shaderProperty;
	if (!property || property->GetRTTI() != globals::rtti::BSGrassShaderPropertyRTTI.get())
		return;

	const auto& engineGroups = shape->GetMultiStreamTrishapeRuntimeData().instanceGroups;
	const auto* group = engineGroup < engineGroups.size() ? engineGroups[engineGroup] : nullptr;
	const auto* vertexBuffer = group ? group->vertexBuffer : nullptr;
	if (!vertexBuffer || !vertexBuffer->m_data || !group->instanceCount)
		return;

	// The engine initializes only the low 32 bits of the vertex buffer's byte width.
	const uint32_t byteWidth = (uint32_t)vertexBuffer->byteWidth;
	auto capture = CaptureMetadata(shape, group->instanceCount, byteWidth / group->instanceCount);
	if (!capture)
		return;

	capture->shape = registration;
	capture->slice.owner = registration.get();
	capture->slice.engineGroup = engineGroup;
	capture->drawResources.key.mesh = registration->mesh;
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
	// Engine-reference destruction can re-enter the hooks, so release it outside pendingMutex.
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
	auto& drawResources = capture.drawResources;
	auto& draw = drawResources.key.draw;
	drawResources.vertexBuffer.copy_from(reinterpret_cast<ID3D11Buffer*>(rendererData->vertexBuffer));
	drawResources.indexBuffer.copy_from(reinterpret_cast<ID3D11Buffer*>(rendererData->indexBuffer));

	drawResources.material.reset(material);
	// TruePBR marks converted grass properties with kMenuScreen.
	draw.pbrMaterial = grassProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kMenuScreen);
	draw.sphereNormal = grassProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kEffectLighting);
	drawResources.key.material = GrassMeshLibrary::CaptureMaterial(material, draw.pbrMaterial);
	draw.uniformScale = grassProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kUniformScale);
	drawResources.key.vertexDesc = *reinterpret_cast<const uint64_t*>(&geometry.vertexDesc);

	drawResources.diffuseTexture = RE::NiPointer<RE::NiSourceTexture>(texture);
	capture.slice.count = count;

	drawResources.key.triCount = shape->GetTrishapeRuntimeData().triangleCount;
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
void GrassBucketStore::PackInstances(GrassInstanceSlice& slice, const void* sourceData)
{
	using DirectX::PackedVector::XMConvertHalfToFloat;
	slice.data.resize((size_t)slice.count * kSourceStride);
	slice.groupBounds.assign((slice.count + kCullGroupSize - 1) / kCullGroupSize, {});
	slice.maxScale = 1.0f;

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

		auto& bounds = slice.groupBounds[instanceIndex / kCullGroupSize];
		const float scale = std::abs(1.0f + XMConvertHalfToFloat(halfValues[13]));
		bounds.maxScale = std::max(bounds.maxScale, scale);
		bounds.valid &= std::isfinite(values[0]) && std::isfinite(values[1]) && std::isfinite(values[2]) && std::isfinite(scale);

		for (int axis = 0; axis < 3; ++axis) {
			bounds.lo[axis] = std::min(bounds.lo[axis], values[axis]);
			bounds.hi[axis] = std::max(bounds.hi[axis], values[axis]);
		}

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

	const float origin[]{ slice.origin.x, slice.origin.y, slice.origin.z };
	std::fill_n(slice.bounds.lo, 3, FLT_MAX);
	std::fill_n(slice.bounds.hi, 3, -FLT_MAX);

	for (auto& bounds : slice.groupBounds) {
		slice.maxScale = std::max(slice.maxScale, bounds.maxScale);
		for (int axis = 0; axis < 3; ++axis) {
			bounds.lo[axis] += origin[axis];
			bounds.hi[axis] += origin[axis];

			slice.bounds.lo[axis] = std::min(slice.bounds.lo[axis], bounds.lo[axis]);
			slice.bounds.hi[axis] = std::max(slice.bounds.hi[axis], bounds.hi[axis]);
		}
	}
}

void GrassBucketStore::QueueEvent(PendingEvent event)
{
	if (stopping)
		return;

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
		readyEvents.emplace_back(Removal{ capture.shape, capture.slice.engineGroup, group });
	}

	capture.slice.lifetime = group = std::make_shared<GrassGroupLifetime>();
	pendingCaptures.push_back(std::move(capture));

	if (preparing)
		return;

	preparing = true;

	if (!preparation)
		preparation = std::make_unique<BS::thread_pool<>>(1);

	preparation->detach_task([this] { PreparePending(); });
}

void GrassBucketStore::PreparePending()
{
	std::vector<PendingCapture> captures;
	for (;;) {
		{
			std::scoped_lock lock(pendingMutex);
			if (pendingCaptures.empty()) {
				preparing = false;
				return;
			}
			captures.swap(pendingCaptures);
		}

		for (auto& capture : captures) {
			if (capture.shape->registered.load(std::memory_order_acquire) && capture.slice.lifetime->current.load(std::memory_order_acquire))
				PackInstances(capture.slice, capture.sourceData.data());
			capture.sourceData = std::vector<uint8_t>{};
		}

		{
			std::scoped_lock lock(pendingMutex);
			readyEvents.insert(readyEvents.end(), std::make_move_iterator(captures.begin()), std::make_move_iterator(captures.end()));
		}
		captures.clear();
	}
}

void GrassBucketStore::EndFrame()
{
	retiredProxies.clear();
	DrainPending();

	// Release shared buffers once no buckets use them.
	if (buckets.empty()) {
		uploadStaging.reset();
		instancePool = {};
		groupPool = {};
		instancePoolGrowth.reset();
		groupPoolGrowth.reset();
		cullOutput = {};
	}

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
	std::vector<std::unique_ptr<GrassInstanceSlice>> retired;

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
		shape->currentBucket = nullptr;

		return true;
	});

	RetireSlices(retired);
}

void GrassBucketStore::ApplyCapture(PendingCapture& capture, std::unordered_set<GrassBucket*>& affected, std::vector<std::unique_ptr<GrassInstanceSlice>>& retired)
{
	auto& shape = *capture.shape;
	if (!shape.registered.load(std::memory_order_acquire) || !capture.slice.lifetime->current.load(std::memory_order_acquire)) {
		if (!capture.slice.data.empty())
			retired.push_back(std::make_unique<GrassInstanceSlice>(std::move(capture.slice)));
		return;
	}

	auto [bucketEntry, inserted] = buckets.try_emplace(capture.drawResources.key);
	auto& bucket = bucketEntry->second;

	if (inserted) {
		bucket.drawResources = std::move(capture.drawResources);
		if (!meshLibrary.CreateDrawResources(bucket.drawResources, capture.shape->geometry.get())) {
			buckets.erase(bucketEntry);
			return;
		}

		const float meshCost = GrassMeshLibrary::MeshCost(3 * bucket.drawResources.key.triCount);
		bucket.distScale = 1.0f / meshCost;
		bucket.minPixelScale = meshCost;
		snapshotDirty = true;
	}

	if (!shape.admitted) {
		activeShapes.push_back(capture.shape);
		shape.admitted = true;
	}

	if (affected.contains(&bucket))
		PruneSlices(bucket);

	if (shape.currentBucket && shape.currentBucket != &bucket) {
		affected.insert(shape.currentBucket);
		MigrateShapeSlices(*shape.currentBucket, bucket, shape);
	}

	shape.currentBucket = &bucket;
	if (auto previous = shape.chunks.find(capture.slice.engineGroup); previous != shape.chunks.end()) {
		previous->second->bucket = nullptr;
		PruneSlices(bucket);
		retired.push_back(std::move(previous->second));
		shape.chunks.erase(previous);
	}

	auto chunk = std::make_unique<GrassInstanceSlice>(std::move(capture.slice));
	AppendSlice(bucket, *chunk);
	shape.chunks.emplace(chunk->engineGroup, std::move(chunk));
}

void GrassBucketStore::ApplyRemoval(const Removal& removal, std::unordered_set<GrassBucket*>& affected, std::vector<std::unique_ptr<GrassInstanceSlice>>& retired)
{
	// Do not wait for older captures still being prepared; their lifetime checks reject them.
	auto& shape = *removal.shape;
	const auto detach = [&](std::unique_ptr<GrassInstanceSlice>& chunk) {
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
		shape.currentBucket = nullptr;
}

void GrassBucketStore::AppendSlice(GrassBucket& bucket, GrassInstanceSlice& slice)
{
	pendingUploads.insert(&bucket);

	slice.bucket = &bucket;
	slice.bufferOffset = UINT32_MAX;
	slice.firstCullGroup = UINT32_MAX;

	bucket.maxScale = std::max(bucket.maxScale, slice.maxScale);
	bucket.totalInstances += slice.count;
	bucket.slices.push_back(&slice);
}

void GrassBucketStore::PruneSlices(GrassBucket& bucket)
{
	const size_t originalCount = bucket.slices.size();
	size_t retainedCount = 0;
	uint32_t removedInstances = 0;
	uint32_t removedResidentInstances = 0;
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
			// Dead groups upload zero counts before any cull reads the reused range.
			instancePool.ranges.Free(slice.bufferOffset, slice.count);
			removedResidentInstances += slice.count;
			++removedUploadedSlices;
		}

		if (index >= bucket.clusteredSlices)
			continue;

		++removedClusteredSlices;
		// Preserve group indices until the budgeted rebuild compacts them.
		if (bucket.clustersValid && slice.firstCullGroup != UINT32_MAX) {
			const uint32_t groupCount = (slice.count + kCullGroupSize - 1) / kCullGroupSize;
			for (uint32_t groupIndex = 0; groupIndex < groupCount; ++groupIndex)
				bucket.cullGroups[slice.firstCullGroup + groupIndex].count = 0;
			bucket.deadGroups += groupCount;
			bucket.dirtyGroupRanges.emplace_back(slice.firstCullGroup, slice.firstCullGroup + groupCount);
			RetireRunGroups(bucket, slice.firstCullGroup, groupCount);
		}
	}

	if (retainedCount == originalCount)
		return;

	bucket.slices.resize(retainedCount);

	bucket.uploadedSlices -= removedUploadedSlices;

	bucket.clusteredSlices -= removedClusteredSlices;
	// Reclaim group-pool space when more than a quarter of the groups are dead.
	if (bucket.deadGroups > bucket.cullGroups.size() / 4)
		bucket.rebuildPending = true;

	bucket.totalInstances -= removedInstances;
	bucket.residentInstances -= removedResidentInstances;

	if (bucket.slices.empty()) {
		bucket.clusteredSlices = 0;
		bucket.clustersValid = false;
		bucket.coarseBounds = SliceBounds::Empty();
	}
}

void GrassBucketStore::MigrateShapeSlices(GrassBucket& source, GrassBucket& destination, GrassShapeRegistration& shape)
{
	std::vector<GrassInstanceSlice*> migratingSlices;
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
	pendingUploads.erase(&bucket);
	groupPool.ranges.Free(bucket.groupBase, std::exchange(bucket.groupCapacity, 0));
	meshLibrary.ReleaseDrawResources(bucket.drawResources);
	retiredProxies.push_back(bucket.drawResources.proxy);
	buckets.erase(bucket.drawResources.key);
	snapshotDirty = true;
}

std::shared_ptr<GrassShapeRegistration> GrassBucketStore::FindShape(RE::BSMultiStreamInstanceTriShape* shape)
{
	std::scoped_lock lock(pendingMutex);
	const auto entry = shapes.find(shape);
	return entry == shapes.end() ? nullptr : entry->second;
}

void GrassBucketStore::PublishCullSnapshot()
{
	if (!snapshotDirty)
		return;

	auto snapshot = std::make_unique<CullSnapshot>();

	// Buckets bind their own materials, but share a proxy only when engine pass state matches.
	struct PassKey
	{
		uint64_t shaderFlags = 0;
		uint64_t vertexDesc = 0;
		uint16_t alphaFlags = 0;
		uint8_t alphaThreshold = 0;
		bool hasAlpha = false;

		bool operator==(const PassKey&) const = default;
	};
	std::vector<PassKey> groupKeys;

	for (auto& [key, bucket] : buckets) {
		if (bucket.slices.empty())
			continue;

		const auto& geometry = bucket.drawResources.proxy->GetGeometryRuntimeData();
		const auto* alpha = geometry.alphaProperty.get();
		const PassKey passKey{
			.shaderFlags = geometry.shaderProperty->flags.underlying(),
			.vertexDesc = std::bit_cast<uint64_t>(geometry.vertexDesc),
			.alphaFlags = alpha ? alpha->alphaFlags : uint16_t{ 0 },
			.alphaThreshold = alpha ? alpha->alphaThreshold : uint8_t{ 0 },
			.hasAlpha = alpha != nullptr
		};

		const auto existingGroup = std::ranges::find(groupKeys, passKey);
		if (existingGroup == groupKeys.end()) {
			groupKeys.push_back(passKey);
			snapshot->proxyGroups.push_back({ bucket.drawResources.proxy, { &bucket } });
		} else {
			snapshot->proxyGroups[existingGroup - groupKeys.begin()].buckets.push_back(&bucket);
		}
	}

	retiredSnapshot = std::move(cullSnapshot);
	cullSnapshot = std::move(snapshot);
	publishedSnapshot.store(cullSnapshot.get(), std::memory_order_release);
	snapshotDirty = false;
}
