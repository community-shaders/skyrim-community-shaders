#include "LODBlendRingFix.h"

#include "Globals.h"

namespace
{
	// Band radii mirror the landscape vertex shader in Lighting.hlsl.
	constexpr float BandOuterRadius = 9625.59961f;
	constexpr float BandInnerRadius = BandOuterRadius - 1.0f / 0.000375600968f;
	constexpr float HalfCellSize = 2048.0f;
	constexpr float SlideSeconds = 0.25f;

	/** @brief Per-axis distance the ring center can move off the grid center while the band stays inside the loaded cells. */
	constexpr float LeanSlack(uint32_t a_gridsToLoad)
	{
		return std::max(0.0f, static_cast<float>(a_gridsToLoad) * HalfCellSize - BandOuterRadius);
	}

	// On 5x5 the engine skips LOD blending on the plus-shaped inner cells, so the leaning band must stay clear of them.
	static_assert(BandInnerRadius - LeanSlack(5) > 3.0f * HalfCellSize);

	float2 ToFloat2(const RE::NiPoint2& a_point)
	{
		return { a_point.x, a_point.y };
	}
}

LODBlendRingFix& LODBlendRingFix::GetInstance()
{
	static LODBlendRingFix instance;
	return instance;
}

void LODBlendRingFix::Install()
{
	// Raised by TES on every grid shift; TES defers its per-cell LOD blend reflag until it clears.
	blendInProgress = reinterpret_cast<bool*>(REL::RelocationID(527641, 414593).address());
}

void LODBlendRingFix::EndSlide()
{
	assert(blendInProgress);
	*blendInProgress = false;
	sliding = false;
}

void LODBlendRingFix::Update()
{
	auto* tes = globals::game::tes;
	auto* player = globals::game::player;
	if (!tes || !player || tes->interiorCell || !tes->GetRuntimeData2().worldSpace) {
		if (sliding)
			EndSlide();
		hasCenter = false;
		return;
	}

	auto& state = *globals::game::smState;
	const float2 engineCenter = ToFloat2(state.gridArrayCenter);
	if (!hasCenter || engineCenter != center) {
		gridCenter = engineCenter;
		// The engine writes old == new on loads, teleports and multi-cell jumps.
		sliding = hasCenter && ToFloat2(state.oldGridArrayCenter) != engineCenter;
		if (sliding) {
			slideOrigin = center;
			slideProgress = 0.0f;
		} else
			EndSlide();
		hasCenter = true;
	}

	if (!gridsToLoad)
		gridsToLoad = RE::GetINISetting("uGridsToLoad:General");
	const float slack = gridsToLoad ? LeanSlack(gridsToLoad->data.u) : 0.0f;
	const auto position = player->GetPosition();
	float2 lean = float2(position.x, position.y) - gridCenter;
	lean.Clamp(float2(-slack), float2(slack));
	const float2 desired = gridCenter + lean;

	if (sliding) {
		slideProgress = std::min(1.0f, slideProgress + RE::GetSecondsSinceLastFrame() / SlideSeconds);
		center = float2::SmoothStep(slideOrigin, desired, slideProgress);
		if (slideProgress >= 1.0f)
			EndSlide();
	} else
		center = desired;

	state.oldGridArrayCenter = state.gridArrayCenter = RE::NiPoint2(center.x, center.y);
	// Pins the renderer's own game-time lerp at t = 0 so it never releases blendInProgress early.
	state.gridArrayLerpStart = std::numeric_limits<float>::max();
}
