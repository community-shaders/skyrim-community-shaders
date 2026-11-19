#pragma once

namespace Util::ShaderCompileFlags
{
	/**
	 * @brief Whether a shader compiles without FXC optimization.
	 *
	 * FXC /O3 hurts Lighting pixel shaders on both axes (PR #3196): about 3-4x the fxc compile time,
	 * and 6-13% more Lighting GPU time, likely because it flattens branches the driver would keep.
	 * Debug, partial precision and avoid-flow-control builds keep their own flag policy.
	 */
	[[nodiscard]] constexpr bool SkipsOptimization(bool a_lightingPixel, bool a_debugCompile, bool a_partialPrecision, bool a_avoidFlowControl)
	{
		return a_lightingPixel && !a_debugCompile && !a_partialPrecision && !a_avoidFlowControl;
	}
}
