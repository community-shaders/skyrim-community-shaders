#pragma once

#include "EngineFix.h"

/** @brief Moves the terrain LOD blend ring on the frame clock instead of game time, eased and leaning toward the player. */
struct LODBlendRingFix : EngineFix
{
	std::string GetName() override { return "LOD Blend Ring Fix"; }

	void Install() override;

	static LODBlendRingFix& GetInstance();

	/** @brief Owns the engine's ring center for this frame. Main thread, once per frame. */
	void Update();

private:
	/** @brief Ends any slide and lets the engine run its deferred per-cell LOD blend reflag. */
	void EndSlide();

	bool* blendInProgress = nullptr;
	RE::Setting* gridsToLoad = nullptr;
	float2 center;
	float2 gridCenter;
	float2 slideOrigin;
	float slideProgress = 0.0f;
	bool sliding = false;
	bool hasCenter = false;
};
