#ifndef __GAME_HLSLI__
#define __GAME_HLSLI__

#include "Common/Math.hlsli"

// Conversion constants
//
// All multi-token expressions are wrapped in parentheses so that callers
// can use them inside larger expressions without operator-precedence
// surprises (e.g. `x / GAME_UNIT_TO_M` must mean `x / (CM / 100)`,
// not `(x / CM) / 100`).
#define GAME_UNIT_TO_CM 1.428f
#define GAME_UNIT_TO_M (GAME_UNIT_TO_CM / 100.0f)
#define GAME_UNIT_TO_FEET (GAME_UNIT_TO_CM / 30.48f)
#define GAME_UNIT_TO_INCHES (GAME_UNIT_TO_CM / 2.54f)

// Reciprocal: meters to game units. Mirrors the 70.0f approximation used
// by the Inverse Square Lighting feature so callers can switch to the
// centralized constant without a numeric behavior change. The exact
// reciprocal of GAME_UNIT_TO_M is 70.028011f.
#define METRES_TO_UNITS 70.0f

// Wind speed conversions
#define WIND_RAW_TO_NORMALIZED (1.0f / 255.0f)
#define WIND_RAW_TO_PERCENT (100.0f / 255.0f)

// Direction conversions
#define DIR_RAW_TO_DEGREES (360.0f / 256.0f)
#define DIR_RANGE_TO_DEGREES (180.0f / 256.0f)
#define RADIANS_TO_DEGREES (180.0f / Math::PI)

#endif  // __GAME_HLSLI__