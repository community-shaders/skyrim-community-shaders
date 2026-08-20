#pragma once

#include "StreamlineSdk.h"

#include <atomic>

/** Frame identity and fault state shared by regular upscaler evaluations. */
struct UpscalerEvaluatorState
{
	sl::ViewportHandle viewport{ 0 };
	uint32_t renderFrameId = 0;
	uint32_t viewport0ConstantsFrame = UINT32_MAX;
	std::atomic<bool> dispatchFaulted{ false };
};
