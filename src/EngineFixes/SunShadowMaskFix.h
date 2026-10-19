#pragma once

#include "EngineFix.h"

/**
 * @brief Gives the sun shadow to geometry the main world cull would otherwise build without it.
 *
 * The main world cull builds each property's render passes on a worker while the sun cascade culls run on
 * the render thread and tag that property's light mask with the sun's bits. Geometry entering view
 * the same frame is therefore often built before it is tagged and draws fully sunlit for one frame.
 * This sets the sun bits ahead of the build for geometry the cascades will cover.
 */
struct SunShadowMaskFix : EngineFix
{
	/** @brief Returns the human-readable name of this fix. */
	std::string GetName() override { return "Sun Shadow Mask Fix"; }

	/** @brief Hooks the lighting property render pass builder. */
	void Install() override;

	/** @brief Caches the sun's light mask bits for the next frame's cull. Render thread, once per frame. */
	static void Update();

private:
	/** @brief Light mask bits the sun shadow light occupies, zero when the fix is inactive. */
	inline static uint32_t sunLightMask = 0;

	inline static RE::BSShaderAccumulator** mainAccumulator = nullptr;
	inline static const uint8_t* sunShadowSkipped = nullptr;
	inline static const float* shadowDistance = nullptr;

	/** @brief Whether the property's sun bits should be set for this geometry. */
	static bool ShouldSetSunBits(const RE::BSLightingShaderProperty* property, const RE::BSGeometry* geometry, uint32_t renderMode, const RE::BSShaderAccumulator* accumulator);

	struct BSLightingShaderProperty_GetRenderPasses
	{
		static RE::BSShaderProperty::RenderPassArray* thunk(RE::BSLightingShaderProperty* property, RE::BSGeometry* geometry, uint32_t renderMode, RE::BSShaderAccumulator* accumulator);
		static inline REL::Relocation<decltype(thunk)> func;
	};
};
