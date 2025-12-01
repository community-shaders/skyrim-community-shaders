#ifndef __GAME_HLSLI__
#define __GAME_HLSLI__

#include "Common/Math.hlsli"

// Conversion constants (game unit to real world)
#define GAME_UNIT_TO_CM     1.428f
#define GAME_UNIT_TO_M      (GAME_UNIT_TO_CM / 100.0f)
#define GAME_UNIT_TO_FEET   (GAME_UNIT_TO_CM / 30.48f)
#define GAME_UNIT_TO_INCHES (GAME_UNIT_TO_CM / 2.54f)

// Conversion constants (real world to game unit)
#define CM_TO_GAME_UNIT     (1.0f / GAME_UNIT_TO_CM)
#define M_TO_GAME_UNIT      (100.0f / GAME_UNIT_TO_CM)
#define FEET_TO_GAME_UNIT   (30.48f / GAME_UNIT_TO_CM)
#define INCHES_TO_GAME_UNIT (2.54f / GAME_UNIT_TO_CM)

// Wind speed conversions
#define WIND_RAW_TO_NORMALIZED 1.0f / 255.0f
#define WIND_RAW_TO_PERCENT    100.0f / 255.0f

// Direction conversions
#define DIR_RAW_TO_DEGREES   360.0f / 256.0f
#define DIR_RANGE_TO_DEGREES 180.0f / 256.0f
#define RADIANS_TO_DEGREES   180.0f / Math::PI

#endif // __GAME_HLSLI__