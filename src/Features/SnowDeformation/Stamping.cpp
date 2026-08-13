#include "Features/SnowDeformation.h"

#include "Utils/ActorUtils.h"
#include "Utils/Game.h"

// Shapes whose bottom is more than this far above the actor's ground position
// do not carve: feet, calves, a sneaking torso and ragdoll limbs pass, heads
// walking by do not.
static constexpr float kStampSurfaceBand = 40.0f;
// Sanity clamp on extracted shape radii (rejects degenerate and room-sized
// collision shapes).
static constexpr float kMinStampShapeRadius = 4.0f;
static constexpr float kMaxStampShapeRadius = 128.0f;
// StampRadius setting value that leaves shape radii unscaled.
static constexpr float kStampRadiusNeutral = 20.0f;
// A shape moving further than this in one frame is teleporting (fast travel,
// cell load): the capsule collapses to a point instead of carving a line
// across the window.
static constexpr float kTrailBreakDistance = 256.0f;
// Movement below this (in units, per gate check) counts as standing still.
static constexpr float kStampMovementGate = 3.0f;
// Settled-latch tuning: accumulated displacement that wakes a settled corpse
// (real dragging/explosions), and how long a corpse must be still to settle.
static constexpr float kCorpseWakeDistance = 50.0f;
static constexpr uint16_t kCorpseSettleFrames = 90;

void SnowDeformation::GatherStamps(PerFrame& perFrameData)
{
	uint stampCount = 0;
	RE::NiPoint3 cameraPosition = Util::GetEyePosition();
	std::unordered_map<uint64_t, float2> currentPositions;

	// Stamps come from the actors' actual Havok collision shapes — the same
	// per-shape extraction Grass Collision uses (Util::GetShapeBound over
	// TraverseScenegraphCollision) instead of one scaled circle at the actor
	// center. Feet and lower-leg capsules carve individually (trails gain
	// real footfall structure), ragdolls carve where their limbs lie, and
	// horses or giants get wide tracks from their genuinely larger shapes
	// with no per-race tuning.
	auto addStamps = [&](RE::ActorHandle a_handle) {
		if (stampCount >= kMaxStamps)
			return;
		auto actor = a_handle.get();
		if (!actor || !actor->Is3DLoaded())
			return;
		auto position = actor->GetPosition();
		// Outside the deformation window nothing can be recorded anyway.
		if (cameraPosition.GetSquaredDistance(position) > 0.25f * kWorldSize * kWorldSize)
			return;
		auto root = actor->Get3D(false);
		if (!root)
			return;

		const uint32_t formID = actor->formID;
		// The living keep their trenches open just by being there; the dead
		// carve only WHILE MOVING (the ragdoll fall stamps its imprint),
		// then go quiet at rest — and the refill slowly buries them. A
		// corpse already at rest when first seen never stamps at all. Do not
		// waive first-sight for fresh kills to cover decapitation's 3D swap:
		// the waiver re-trenches under already-buried corpses.
		const bool isDead = actor->IsDead();

		CorpseRest* rest = nullptr;
		if (isDead) {
			if (corpseRestStates.size() > 512 && !corpseRestStates.contains(formID))
				corpseRestStates.clear();
			rest = &corpseRestStates[formID];
		} else {
			// Seen alive (including reanimation): back to living rules.
			corpseRestStates.erase(formID);
		}
		bool anyShapeMoved = false;
		bool anyShapeWoken = false;

		// Airborne LIVING actors do not touch the snow: jumping, levitating
		// or falling carves nothing until contact. Dead ragdolls are exempt:
		// their controllers freeze in stale states (often kInAir), which
		// would suppress normal corpse imprints; their movement gate
		// governs them instead.
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

				// Capsule stamping: the segment runs from this shape's
				// previous position, so fast movers carve continuous trails
				// instead of chains of spaced circles.
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
				// Measured against the frozen resting anchor, so dragging
				// accumulates past the wake distance within a few frames
				// while jitter oscillating around a point never does.
				const bool woken = !firstSight && sqDelta > kCorpseWakeDistance * kCorpseWakeDistance;
				if (isDead) {
					anyShapeMoved |= !firstSight && moved;
					anyShapeWoken |= woken;
				}
				if (isDead && (firstSight || !moved || (rest->settled && !woken))) {
					// Corpse at rest: no stamp. Keep the OLD anchor so
					// ragdoll micro-jitter cannot hold the trench open, but
					// real movement (dragging, explosions) re-triggers
					// against it. First-seen corpses store a baseline.
					currentPositions[key] = firstSight ? current : it->second;
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				currentPositions[key] = current;

				float4 stamp{};
				stamp.x = current.x;
				stamp.y = current.y;
				stamp.z = 1.0f;
				// StampRadius acts as a scale on the shape's own radius.
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

	stampPrevPositions = std::move(currentPositions);
	perFrameData.StampCount = stampCount;
}
