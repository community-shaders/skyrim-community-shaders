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

void SnowDeformation::GatherStamps(PerFrame& perFrameData)
{
	uint stampCount = 0;
	RE::NiPoint3 cameraPosition = Util::GetEyePosition();
	std::unordered_map<uint64_t, float2> currentPositions;

	// Stamps come from actors' Havok collision shapes (Util::GetShapeBound
	// over TraverseScenegraphCollision), so feet, legs and ragdoll limbs
	// carve individually.
	auto addStamps = [&](RE::ActorHandle a_handle) {
		if (stampCount >= kMaxStamps)
			return;
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
		uint32_t shapeIndex = 0;
		RE::BSVisit::TraverseScenegraphCollision(root, [&](RE::bhkNiCollisionObject* a_object) -> RE::BSVisit::BSVisitControl {
			RE::NiPoint3 centerPos;
			float radius;
			if (Util::GetShapeBound(a_object, centerPos, radius)) {
				// Stable per-skeleton traversal order keys the trail history.
				const uint32_t thisIndex = shapeIndex++;
				if (stampCount >= kMaxStamps)
					return RE::BSVisit::BSVisitControl::kStop;
				if (centerPos.z - radius > groundZ + kStampSurfaceBand)
					return RE::BSVisit::BSVisitControl::kContinue;
				if (radius < kMinStampShapeRadius || radius > kMaxStampShapeRadius)
					return RE::BSVisit::BSVisitControl::kContinue;

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
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				currentPositions[key] = current;

				float4 stamp{};
				stamp.x = current.x;
				stamp.y = current.y;
				stamp.z = 1.0f;
				// StampRadius scales the shape's own radius.
				stamp.w = radius * settings.StampRadius / kStampRadiusNeutral;
				perFrameData.Stamps[stampCount] = stamp;
				perFrameData.StampEnds[stampCount] = { previous.x, previous.y, 0.0f, 0.0f };
				stampCount++;
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});

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

	// Loose props carve while moving. The cheap root-position gate runs
	// before any collision traversal.
	std::unordered_map<uint32_t, RE::NiPoint3> currentPropPositions;
	const auto tes = RE::TES::GetSingleton();
	auto* playerRef = RE::PlayerCharacter::GetSingleton();
	if (tes && playerRef) {
		tes->ForEachReferenceInRange(playerRef, 0.5f * deformWorldSize, [&](RE::TESObjectREFR* a_ref) {
			if (!a_ref || a_ref->As<RE::Actor>())
				return RE::BSContainer::ForEachResult::kContinue;
			auto* base = a_ref->GetBaseObject();
			if (!base)
				return RE::BSContainer::ForEachResult::kContinue;
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
				return RE::BSContainer::ForEachResult::kContinue;
			}
			if (!a_ref->Is3DLoaded())
				return RE::BSContainer::ForEachResult::kContinue;
			auto root = a_ref->Get3D(false);
			if (!root)
				return RE::BSContainer::ForEachResult::kContinue;

			// Gate on the 3D root's world transform, not the reference
			// position: Havok moves the scene graph every frame while the
			// reference position lags until the body settles.
			const auto position = root->world.translate;
			const uint32_t formID = a_ref->formID;
			auto prevIt = propPrevPositions.find(formID);
			if (prevIt == propPrevPositions.end()) {
				currentPropPositions[formID] = position;
				return RE::BSContainer::ForEachResult::kContinue;  // first sight: baseline only
			}
			// Frozen anchor: slow motion accumulates toward the gate instead
			// of resetting every frame.
			const bool propMoved = position.GetSquaredDistance(prevIt->second) >= kStampMovementGate * kStampMovementGate;
			currentPropPositions[formID] = propMoved ? position : prevIt->second;
			if (!propMoved)
				return RE::BSContainer::ForEachResult::kContinue;  // at rest: the refill buries it
			if (stampCount >= kMaxStamps)
				return RE::BSContainer::ForEachResult::kContinue;  // keep collecting anchors

			// Ground = land height, so mid-air flight paths do not carve.
			float groundZ = position.z;
			tes->GetLandHeight(position, groundZ);

			uint32_t shapeIndex = 0;
			RE::BSVisit::TraverseScenegraphCollision(root, [&](RE::bhkNiCollisionObject* a_object) -> RE::BSVisit::BSVisitControl {
				RE::NiPoint3 centerPos;
				float radius;
				if (Util::GetShapeBound(a_object, centerPos, radius)) {
					const uint32_t thisIndex = shapeIndex++;
					if (stampCount >= kMaxStamps)
						return RE::BSVisit::BSVisitControl::kStop;
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
					stamp.w = radius * settings.StampRadius / kStampRadiusNeutral;
					perFrameData.Stamps[stampCount] = stamp;
					perFrameData.StampEnds[stampCount] = { previous.x, previous.y, 0.0f, 0.0f };
					stampCount++;
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});

			// Shape types with no bound extractor (MOPP/list): one stamp from
			// the root's bound sphere.
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
					stamp.w = radius * settings.StampRadius / kStampRadiusNeutral;
					perFrameData.Stamps[stampCount] = stamp;
					perFrameData.StampEnds[stampCount] = { previous.x, previous.y, 0.0f, 0.0f };
					stampCount++;
				}
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
	}
	propPrevPositions = std::move(currentPropPositions);

	stampPrevPositions = std::move(currentPositions);
	perFrameData.StampCount = stampCount;
}
