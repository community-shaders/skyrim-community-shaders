#pragma once

#include "EngineFix.h"

/**
 * @brief Polls the sun's occlusion queries with D3D11_ASYNC_GETDATA_DONOTFLUSH.
 *
 * The sun update polls without waiting yet still passes the flush flag, so every pending poll submits
 * the queued command buffer early. A pending poll leaves the glare factor untouched, so dropping the flush is safe.
 */
struct SunOcclusionPollFix : EngineFix
{
	std::string GetName() override { return "Sun Occlusion Poll Fix"; }

	void Install() override;
};
