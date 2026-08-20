#pragma once

#include "StreamlineSdk.h"

#include <array>
#include <atomic>
#include <mutex>

/** Cross-thread DLSS-G option, marker, pacing, and acknowledgment state. */
struct DlssgPresenterState
{
	bool modeCached = false;
	std::atomic<bool> modeOn{ false };
	uint32_t cachedRenderW = 0, cachedRenderH = 0;
	uint32_t cachedDisplayW = 0, cachedDisplayH = 0;
	uint32_t cachedNumFrames = 0;
	bool cachedAuto = false;
	bool cachedDynamic = false;
	float cachedDynamicFps = 0.0f;
	std::atomic<uint32_t> maxFramesToGenerate = 0;
	std::atomic<bool> dynamicSupported = false;
	std::atomic<uint32_t> frameGenerationMultiplier = 1;
	sl::DLSSGOptions pendingOptions{};
	std::atomic<bool> optionsPending{ false };
	std::atomic<uint32_t> transitionPresentAcks{ 0 };
	std::atomic<uint64_t> transitionCompletionValue{ 0 };
	bool pendingEnable = false;
	uint32_t pendingRenderW = 0, pendingRenderH = 0;
	uint32_t pendingDisplayW = 0, pendingDisplayH = 0;
	bool taggedThisFrame = false;
	std::atomic<bool> cloneTagsPrimed{ false };

	// Streamline's DLSS-G state query is shared by the game and present threads.
	std::mutex apiMutex;
	std::array<std::atomic<uint32_t>, 64> presentMarkerFrames{};
	std::atomic<uint32_t> presentMarkerHead{ 0 };
	std::atomic<uint32_t> presentMarkerTail{ 0 };
	std::atomic<uint32_t> activePresentMarkerFrame{ 0 };
	std::atomic<uint32_t> activeDxvkAppPresentFrame{ 0 };
	std::atomic<uint32_t> optionsEpoch{ 0 };
	std::atomic<uint32_t> ledgerBudget{ 0 };
	std::atomic<uint64_t> activeDxvkFrameId{ 0 };
	std::atomic<uint64_t> activeSwapchainSerial{ 0 };
	std::atomic<uint64_t> activePresentStartNs{ 0 };
	std::atomic<uint64_t> renderHeartbeatNs{ 0 };
	std::atomic<uint64_t> presentHeartbeatNs{ 0 };
};
