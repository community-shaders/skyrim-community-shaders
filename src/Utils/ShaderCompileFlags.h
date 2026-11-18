#pragma once

namespace Util::ShaderCompileFlags
{
	/**
	 * @brief Whether a shader compiles without FXC optimization.
	 *
	 * Lighting pixel shaders dominate cold compile time, and the driver re-optimizes the bytecode,
	 * so skipping FXC optimization shortens startup without a measurable runtime cost. Debug,
	 * partial precision and avoid-flow-control builds keep their own flag policy.
	 */
	[[nodiscard]] constexpr bool SkipsOptimization(bool a_lightingPixel, bool a_debugCompile, bool a_partialPrecision, bool a_avoidFlowControl)
	{
		return a_lightingPixel && !a_debugCompile && !a_partialPrecision && !a_avoidFlowControl;
	}
}
