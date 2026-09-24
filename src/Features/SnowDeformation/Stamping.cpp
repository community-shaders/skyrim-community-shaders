#include "Features/SnowDeformation.h"

#include "Utils/ActorUtils.h"
#include "Utils/Game.h"

// Shapes whose bottom is further than this above ground level do not carve.
static constexpr float kStampSurfaceBand = 40.0f;
// Sanity clamp on extracted shape radii.
static constexpr float kMinStampShapeRadius = 4.0f;
static constexpr float kMaxStampShapeRadius = 128.0f;
// StampRadius setting value at which shape radii are unscaled.
static constexpr float kStampRadiusNeutral = 20.0f;
// Per-frame movement beyond this (teleport, cell load) breaks the capsule trail.
static constexpr float kTrailBreakDistance = 256.0f;
// Movement below this counts as standing still.
static constexpr float kStampMovementGate = 3.0f;
// Corpse settled-latch: wake displacement and frames-still until settled.
static constexpr float kCorpseWakeDistance = 50.0f;
static constexpr uint16_t kCorpseSettleFrames = 90;

// World centre of a collision object's rigid body; the centre half of
// Util::GetShapeBound, for shapes whose radius is already cached.
static bool ShapeCenter(RE::bhkNiCollisionObject* a_object, RE::NiPoint3& a_center)
{
	auto* body = a_object ? a_object->body.get() : nullptr;
	auto* rigid = body ? body->AsBhkRigidBody() : nullptr;
	if (!rigid)
		return false;
	RE::hkVector4 massCenter;
	rigid->GetCenterOfMassWorld(massCenter);
	float massTrans[4];
	_mm_storeu_ps(massTrans, massCenter.quad);
	a_center = RE::NiPoint3(massTrans[0], massTrans[1], massTrans[2]) * RE::bhkWorld::GetWorldScaleInverse();
	return true;
}

// Stamps carry 1 / radius^2 so the update shader's per-texel test needs no divide.
static float StampInvRadiusSq(float a_radius)
{
	return 1.0f / std::max(a_radius * a_radius, 1e-4f);
}

void SnowDeformation::GatherStamps(PerFrame& perFrameData)
{
	uint stampCount = 0;
	RE::NiPoint3 cameraPosition = Util::GetEyePosition();
	// Per-frame containers are members, cleared rather than rebuilt, so their
	// buckets are not reallocated every frame.
	auto& currentPositions = stampAnchorScratch;
	currentPositions.clear();
	// Refs skipped this frame (stamp budget, props at rest) whose anchors carry over.
	auto& anchoredRefs = anchoredRefScratch;
	anchoredRefs.clear();

	globals::profiler->BeginPass("SnowDeformation::GatherActors");

	// Stamps come from actors' Havok collision shapes (Util::GetShapeBound
	// over TraverseScenegraphCollision), so feet, legs and ragdoll limbs
	// carve individually.
	auto addStamps = [&](RE::ActorHandle a_handle) {
		auto actor = a_handle.get();
		if (!actor || !actor->Is3DLoaded())
			return;
		auto position = actor->GetPosition();
		// Cull to the deformation window.
		if (cameraPosition.GetSquaredDistance(position) > 0.25f * deformWorldSize * deformWorldSize)
			return;
		auto root = actor->Get3D(false);
		if (!root)
			return;

		const uint32_t formID = actor->formID;
		if (stampCount >= kMaxStamps) {
			anchoredRefs.insert(formID);
			return;
		}
		// The dead carve only while moving; at rest the refill buries them.
		// No first-sight waiver: decapitation swaps the 3D, and a waiver
		// would re-trench under already-buried corpses.
		const bool isDead = actor->IsDead();

		CorpseRest* rest = nullptr;
		if (isDead) {
			if (corpseRestStates.size() > 512 && !corpseRestStates.contains(formID))
				corpseRestStates.clear();
			rest = &corpseRestStates[formID];
		} else {
			// Reanimated: back to living rules.
			corpseRestStates.erase(formID);
		}
		bool anyShapeMoved = false;
		bool anyShapeWoken = false;

		// Airborne living actors do not carve. Dead ragdolls are exempt:
		// their controllers freeze in stale states (often kInAir).
		if (!isDead)
			if (auto* charController = actor->GetCharController(); charController && charController->context.currentState == RE::hkpCharacterStateType::kInAir)
				return;

		const float groundZ = position.z;
		// The skeleton walk and the shape radii only change with the 3D, so
		// they are cached per actor; each frame reads the shapes' centres.
		auto& shapes = actorShapeCache[formID];
		shapes.seenFrame = gatherFrame;
		if (shapes.root.get() != root || gatherFrame - shapes.builtFrame >= kActorShapeCacheFrames) {
			// A new 3D starts its cycle offset by formID, so actors' periodic
			// walks spread over the frames instead of landing on one.
			shapes.builtFrame = shapes.root.get() != root ? gatherFrame - formID % kActorShapeCacheFrames : gatherFrame;
			shapes.root.reset(root);
			shapes.shapes.clear();
			RE::BSVisit::TraverseScenegraphCollision(root, [&](RE::bhkNiCollisionObject* a_object) -> RE::BSVisit::BSVisitControl {
				RE::NiPoint3 centerPos;
				float radius;
				if (Util::GetShapeBound(a_object, centerPos, radius))
					shapes.shapes.push_back({ RE::NiPointer<RE::bhkNiCollisionObject>(a_object), radius });
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}

		for (uint32_t thisIndex = 0; thisIndex < shapes.shapes.size(); thisIndex++) {
			// Stable per-skeleton traversal order keys the trail history.
			const auto& shape = shapes.shapes[thisIndex];
			const float radius = shape.radius;
			RE::NiPoint3 centerPos;
			{
				if (stampCount >= kMaxStamps) {
					anchoredRefs.insert(formID);
					break;
				}
				if (!ShapeCenter(shape.object.get(), centerPos))
					continue;
				if (centerPos.z - radius > groundZ + kStampSurfaceBand)
					continue;
				if (radius < kMinStampShapeRadius || radius > kMaxStampShapeRadius)
					continue;

				// Capsule stamp from the shape's previous position keeps
				// fast movers' trails continuous.
				float2 current = { centerPos.x, centerPos.y };
				float2 previous = current;
				const uint64_t key = (uint64_t(formID) << 16) | uint64_t(thisIndex & 0xFFFF);
				auto it = stampPrevPositions.find(key);
				bool moved = true;
				float sqDelta = 0.0f;
				if (it != stampPrevPositions.end()) {
					float2 delta = { current.x - it->second.x, current.y - it->second.y };
					sqDelta = delta.x * delta.x + delta.y * delta.y;
					if (sqDelta < kTrailBreakDistance * kTrailBreakDistance)
						previous = it->second;
					moved = sqDelta > kStampMovementGate * kStampMovementGate;
				}
				const bool firstSight = (it == stampPrevPositions.end());
				// Against the frozen resting anchor: dragging accumulates
				// past the gate, ragdoll jitter does not.
				const bool woken = !firstSight && sqDelta > kCorpseWakeDistance * kCorpseWakeDistance;
				if (isDead) {
					anyShapeMoved |= !firstSight && moved;
					anyShapeWoken |= woken;
				}
				if (isDead && (firstSight || !moved || (rest->settled && !woken))) {
					// At rest: no stamp. Keep the old anchor so micro-jitter
					// cannot hold the trench open.
					currentPositions[key] = firstSight ? current : it->second;
					continue;
				}
				currentPositions[key] = current;

				float4 stamp{};
				stamp.x = current.x;
				stamp.y = current.y;
				stamp.z = 1.0f;
				// StampRadius scales the shape's own radius.
				const float stampRadius = radius * settings.StampRadius / kStampRadiusNeutral;
				stamp.w = StampInvRadiusSq(stampRadius);
				perFrameData.Stamps[stampCount] = stamp;
				perFrameData.StampEnds[stampCount] = { previous.x, previous.y, stampRadius, 0.0f };
				stampCount++;
			}
		}

		if (rest) {
			if (anyShapeWoken) {
				rest->settled = false;
				rest->stillFrames = 0;
			} else if (anyShapeMoved) {
				rest->stillFrames = 0;
			} else if (!rest->settled && ++rest->stillFrames >= kCorpseSettleFrames) {
				rest->settled = true;
			}
		}
	};

	if (auto player = RE::PlayerCharacter::GetSingleton())
		addStamps(player->GetHandle());

	if (const auto processLists = RE::ProcessLists::GetSingleton()) {
		for (auto& actorHandle : processLists->highActorHandles)
			addStamps(actorHandle);
	}

	// Actors unseen for a cache period drop their shapes (and the 3D they hold).
	if (gatherFrame % kActorShapeCacheFrames == 0)
		std::erase_if(actorShapeCache, [&](const auto& a_kv) { return gatherFrame - a_kv.second.seenFrame >= kActorShapeCacheFrames; });
	gatherFrame++;

	globals::profiler->EndPass();

	// Loose props carve while moving. Visiting every reference in range is
	// the cost, not the movers, so each frame walks one kPropScanInterval-th
	// of the cells; props found moving are revisited every frame until their
	// cell is walked again. Anchors update in place.
	globals::profiler->BeginPass("SnowDeformation::GatherProps");
	const uint32_t phase = propScanFrame;
	propScanFrame = (propScanFrame + 1) % kPropScanInterval;
	if (phase == 0)
		propScanCycle++;
	bool scanning = false;
	auto considerProp = [&](RE::TESObjectREFR* a_ref) {
		auto* base = a_ref->GetBaseObject();
		if (!base)
			return;
		// Havok-movable base types only; projectiles must not carve
		// under their flight path.
		switch (base->GetFormType()) {
		case RE::FormType::Misc:
		case RE::FormType::Weapon:
		case RE::FormType::Armor:
		case RE::FormType::Ammo:
		case RE::FormType::Book:
		case RE::FormType::Ingredient:
		case RE::FormType::AlchemyItem:
		case RE::FormType::SoulGem:
		case RE::FormType::KeyMaster:
		case RE::FormType::Light:
		case RE::FormType::MovableStatic:
			break;
		default:
			return;
		}
		if (!a_ref->Is3DLoaded())
			return;
		auto root = a_ref->Get3D(false);
		if (!root)
			return;

		// Gate on the 3D root's world transform, not the reference
		// position: Havok moves the scene graph every frame while the
		// reference position lags until the body settles.
		const auto position = root->world.translate;
		const uint32_t formID = a_ref->formID;
		auto [anchorIt, firstSight] = propPrevPositions.try_emplace(formID, PropAnchor{ position, propScanCycle });
		anchorIt->second.cycle = propScanCycle;
		if (firstSight)
			return;  // baseline only
		// Frozen anchor: slow motion accumulates toward the gate instead
		// of resetting every frame. At rest the refill buries the prop.
		if (position.GetSquaredDistance(anchorIt->second.pos) < kStampMovementGate * kStampMovementGate)
			return;
		anchorIt->second.pos = position;
		if (scanning)
			propScanMovers[phase].push_back(a_ref->CreateRefHandle());
		if (stampCount >= kMaxStamps) {
			anchoredRefs.insert(formID);
			return;
		}

		// Ground = land height, so mid-air flight paths do not carve.
		float groundZ = position.z;
		RE::TES::GetSingleton()->GetLandHeight(position, groundZ);

		uint32_t shapeIndex = 0;
		RE::BSVisit::TraverseScenegraphCollision(root, [&](RE::bhkNiCollisionObject* a_object) -> RE::BSVisit::BSVisitControl {
			RE::NiPoint3 centerPos;
			float radius;
			if (Util::GetShapeBound(a_object, centerPos, radius)) {
				const uint32_t thisIndex = shapeIndex++;
				if (stampCount >= kMaxStamps) {
					anchoredRefs.insert(formID);
					return RE::BSVisit::BSVisitControl::kStop;
				}
				if (centerPos.z - radius > groundZ + kStampSurfaceBand)
					return RE::BSVisit::BSVisitControl::kContinue;
				if (radius < kMinStampShapeRadius || radius > kMaxStampShapeRadius)
					return RE::BSVisit::BSVisitControl::kContinue;

				// Props share the (formID << 16 | shape) keyspace with
				// actors; formIDs are unique.
				float2 current = { centerPos.x, centerPos.y };
				float2 previous = current;
				const uint64_t key = (uint64_t(formID) << 16) | uint64_t(thisIndex & 0xFFFF);
				auto it = stampPrevPositions.find(key);
				if (it != stampPrevPositions.end()) {
					float2 delta = { current.x - it->second.x, current.y - it->second.y };
					if (delta.x * delta.x + delta.y * delta.y < kTrailBreakDistance * kTrailBreakDistance)
						previous = it->second;
				}
				currentPositions[key] = current;

				float4 stamp{};
				stamp.x = current.x;
				stamp.y = current.y;
				stamp.z = 1.0f;
				const float stampRadius = radius * settings.StampRadius / kStampRadiusNeutral;
				stamp.w = StampInvRadiusSq(stampRadius);
				perFrameData.Stamps[stampCount] = stamp;
				perFrameData.StampEnds[stampCount] = { previous.x, previous.y, stampRadius, 0.0f };
				stampCount++;
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});

		// Shape types with no bound extractor (MOPP/list): one stamp from
		// the root's bound sphere.
		if (shapeIndex == 0 && stampCount >= kMaxStamps)
			anchoredRefs.insert(formID);
		if (shapeIndex == 0 && stampCount < kMaxStamps) {
			const auto& bound = root->worldBound;
			float radius = std::clamp(bound.radius, kMinStampShapeRadius, kMaxStampShapeRadius);
			if (bound.center.z - radius <= groundZ + kStampSurfaceBand) {
				float2 current = { bound.center.x, bound.center.y };
				float2 previous = current;
				const uint64_t key = (uint64_t(formID) << 16) | 0xFFFFull;
				auto it = stampPrevPositions.find(key);
				if (it != stampPrevPositions.end()) {
					float2 delta = { current.x - it->second.x, current.y - it->second.y };
					if (delta.x * delta.x + delta.y * delta.y < kTrailBreakDistance * kTrailBreakDistance)
						previous = it->second;
				}
				currentPositions[key] = current;

				float4 stamp{};
				stamp.x = current.x;
				stamp.y = current.y;
				stamp.z = 1.0f;
				const float stampRadius = radius * settings.StampRadius / kStampRadiusNeutral;
				stamp.w = StampInvRadiusSq(stampRadius);
				perFrameData.Stamps[stampCount] = stamp;
				perFrameData.StampEnds[stampCount] = { previous.x, previous.y, stampRadius, 0.0f };
				stampCount++;
			}
		}
	};

	const auto tes = RE::TES::GetSingleton();
	auto* playerRef = RE::PlayerCharacter::GetSingleton();
	if (tes && playerRef) {
		// The cells TES::ForEachReferenceInRange would walk: the interior
		// cell, else every attached grid cell the radius reaches, plus the
		// sky cell.
		const auto originPos = playerRef->GetPosition();
		const float radius = 0.5f * deformWorldSize;
		auto& cells = propScanCells;
		cells.clear();
		if (auto* parentCell = playerRef->GetParentCell(); parentCell && parentCell->IsInteriorCell()) {
			cells.push_back(parentCell);
		} else {
			if (auto* grid = tes->gridCells) {
				for (uint32_t x = 0; x < grid->length; x++) {
					for (uint32_t y = 0; y < grid->length; y++) {
						auto* cell = grid->GetCell(x, y);
						if (!cell || !cell->IsAttached())
							continue;
						if (auto* coords = cell->GetCoordinates()) {
							constexpr float kCellSize = 4096.0f;
							const float minX = coords->cellX * kCellSize;
							const float minY = coords->cellY * kCellSize;
							if (originPos.x + radius < minX || originPos.x - radius > minX + kCellSize ||
								originPos.y + radius < minY || originPos.y - radius > minY + kCellSize)
								continue;
						}
						cells.push_back(cell);
					}
				}
			}
			if (auto* worldspace = playerRef->GetWorldspace())
				if (auto* sky = worldspace->GetSkyCell())
					cells.push_back(sky);
		}

		auto scanCell = [&](RE::TESObjectCELL* a_cell) {
			a_cell->ForEachReferenceInRange(originPos, radius, [&](RE::TESObjectREFR* a_ref) {
				if (a_ref && !a_ref->As<RE::Actor>())
					considerProp(a_ref);
				return RE::BSContainer::ForEachResult::kContinue;
			});
		};
		// The first frames walk everything so there is a baseline to move from.
		const bool scanAll = propPrevPositions.empty();
		scanning = true;
		if (scanAll) {
			for (auto& slice : propScanMovers)
				slice.clear();
			for (auto* cell : cells)
				scanCell(cell);
		} else {
			propScanMovers[phase].clear();
			for (size_t i = phase; i < cells.size(); i += kPropScanInterval)
				scanCell(cells[i]);
		}
		scanning = false;
		for (uint32_t slice = 0; slice < kPropScanInterval; slice++) {
			if (scanAll || slice == phase)
				continue;
			for (const auto& handle : propScanMovers[slice])
				if (auto ref = handle.get(); ref)
					considerProp(ref.get());
		}
		// A whole cycle unseen: the reference left the range or unloaded.
		if (phase == 0 && propScanCycle >= 2)
			std::erase_if(propPrevPositions, [&](const auto& a_kv) { return a_kv.second.cycle + 1 < propScanCycle; });
	}
	globals::profiler->EndPass();

	// Skipped refs, and props still tracked (at rest, or in a cell not walked
	// this frame), keep their anchors, so their next stamp bridges the gap
	// instead of restarting as a point.
	for (const auto& [key, anchor] : stampPrevPositions) {
		const uint32_t formID = uint32_t(key >> 16);
		if (anchoredRefs.contains(formID) || propPrevPositions.contains(formID))
			currentPositions.try_emplace(key, anchor);
	}
	std::swap(stampPrevPositions, currentPositions);
	perFrameData.StampCount = stampCount;
}
