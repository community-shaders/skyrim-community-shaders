#pragma once

#include "EngineFix.h"

/**
 * @brief Answers BSShaderPropertyLightData::AddLight's common "already listed" case with an AVX2 search.
 *
 * Misses, and calls made while the engine rebuilds the list, still run the engine routine.
 */
struct LightListSearchFix : EngineFix
{
	std::string GetName() override { return "Light List Search Fix"; }

	void Install() override;
};
