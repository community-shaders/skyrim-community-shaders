#ifndef __GAME_HLSLI__
#define __GAME_HLSLI__

#include "Common/Math.hlsli"

// Conversion constants
#define GAME_UNIT_TO_CM     1.428f
#define GAME_UNIT_TO_M      (GAME_UNIT_TO_CM / 100.0f)
#define GAME_UNIT_TO_FEET   (GAME_UNIT_TO_CM / 30.48f)
#define GAME_UNIT_TO_INCHES (GAME_UNIT_TO_CM / 2.54f)

// Wind speed conversions
#define WIND_RAW_TO_NORMALIZED 1.0f / 255.0f
#define WIND_RAW_TO_PERCENT    100.0f / 255.0f

// Direction conversions
#define DIR_RAW_TO_DEGREES   360.0f / 256.0f
#define DIR_RANGE_TO_DEGREES 180.0f / 256.0f
#define RADIANS_TO_DEGREES   180.0f / Math::PI

#endif // __GAME_HLSLI__