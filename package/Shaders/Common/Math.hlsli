#ifndef __MATH_DEPENDENCY_HLSL__
#define __MATH_DEPENDENCY_HLSL__

#define EPSILON_SSS_ALBEDO 1e-3f  // For albedo clamping in SSS calculations
#define EPSILON_DOT_CLAMP 1e-5f   // For dot product clamping
#define EPSILON_DIVISION 1e-6f    // For division to avoid division by zero

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
}

#endif  //__MATH_DEPENDENCY_HLSL__