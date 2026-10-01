#pragma once

#include "EngineFix.h"

struct WaterReflectionCubemapFix : EngineFix
{
	std::string GetName() override { return "Water Reflection Cubemap Fix"; }

	void Install() override;

private:
	static void ResetCubemapOnSceneChange();

	struct Main_RenderWaterEffects
	{
		static void thunk();
		static inline REL::Relocation<decltype(thunk)> func;
	};
};
