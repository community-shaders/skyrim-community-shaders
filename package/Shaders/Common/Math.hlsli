#ifndef __MATH_DEPENDENCY_HLSL__
#define __MATH_DEPENDENCY_HLSL__

#define EPSILON_SSS_ALBEDO 1e-3f   // For albedo clamping in SSS calculations
#define EPSILON_DOT_CLAMP 1e-5f    // For dot product clamping
#define EPSILON_DEPTH_SKY 1e-5f    // Depth threshold for sky/unrendered pixel detection (raw reversed-Z near zero)
#define EPSILON_DIVISION 1e-6f     // For division to avoid division by zero
#define EPSILON_GLINTS 1e-8f       // For glints calculations
#define EPSILON_WEIGHT_SUM 1e-10f  // For weight normalization

#define DEPTH_SKY_SENTINEL 999999.0f  // Linearized depth sentinel for sky/unmapped pixels (beyond any real geometry)

// GetWaterData returns .w = INT_MIN (~-2.147e9) when the tile is out of the 5x5 grid.
// Use this threshold to test for "no water body present": waterHeight > WATER_HEIGHT_NO_TILE_SENTINEL.
#define WATER_HEIGHT_NO_TILE_SENTINEL -1e9f

namespace Math
{
	static const float4x4 IdentityMatrix = {
		{ 1, 0, 0, 0 },
		{ 0, 1, 0, 0 },
		{ 0, 0, 1, 0 },
		{ 0, 0, 0, 1 }
	};

	static const float PI = 3.1415926535897932384626433832795f;  // PI
	static const float HALF_PI = PI * 0.5f;                      // PI / 2
	static const float TAU = PI * 2.0f;                          // PI * 2
	static const float INV_PI = 1.0f / PI;                       // 1 / PI
}

#endif  //__MATH_DEPENDENCY_HLSL__