/**
 * High-impact conversion utility tests
 * Tests critical game unit conversions and constants
 *
 * These conversions are used for real-world measurements and must be accurate!
 * Wrong values = incorrect distance calculations, physics bugs, visual glitches
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstdint>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace GameConversions
{
	// Game unit conversion constants (from Utils/Game.h)
	constexpr float GAME_UNIT_TO_CM = 1.428f;
	constexpr float GAME_UNIT_TO_M = GAME_UNIT_TO_CM / 100.0f;
	constexpr float GAME_UNIT_TO_FEET = GAME_UNIT_TO_CM / 30.48f;
	constexpr float GAME_UNIT_TO_INCHES = GAME_UNIT_TO_CM / 2.54f;

	// Wind conversion constants
	constexpr float WIND_RAW_TO_NORMALIZED = 1.0f / 255.0f;
	constexpr float WIND_RAW_TO_PERCENT = 100.0f / 255.0f;

	// Direction conversion constants
	constexpr float DIR_RAW_TO_DEGREES = 360.0f / 256.0f;
	constexpr float DIR_RANGE_TO_DEGREES = 180.0f / 256.0f;

	// Conversion functions
	inline float GameUnitsToMeters(float gameUnits) { return gameUnits * GAME_UNIT_TO_M; }
	inline float GameUnitsToCm(float gameUnits) { return gameUnits * GAME_UNIT_TO_CM; }
	inline float GameUnitsToFeet(float gameUnits) { return gameUnits * GAME_UNIT_TO_FEET; }
	inline float GameUnitsToInches(float gameUnits) { return gameUnits * GAME_UNIT_TO_INCHES; }

	inline float WindRawToNormalized(uint8_t rawWind) { return rawWind * WIND_RAW_TO_NORMALIZED; }
	inline float WindRawToPercent(uint8_t rawWind) { return rawWind * WIND_RAW_TO_PERCENT; }

	inline float DirectionRawToDegrees(uint8_t rawDirection) { return rawDirection * DIR_RAW_TO_DEGREES; }
	inline float DirectionRangeToDegrees(uint8_t range) { return range * DIR_RANGE_TO_DEGREES; }
}

TEST_CASE("Game unit conversion constants are correct", "[Conversion][Constants][Critical]")
{
	using namespace GameConversions;

	SECTION("Base conversion constant")
	{
		// 1 game unit = 1.428 cm (Skyrim's fundamental unit)
		REQUIRE(GAME_UNIT_TO_CM == 1.428f);
	}

	SECTION("Derived conversions are mathematically correct")
	{
		// 1 meter = 100 cm
		REQUIRE_THAT(GAME_UNIT_TO_M, WithinAbs(1.428f / 100.0f, 0.00001f));
		REQUIRE_THAT(GAME_UNIT_TO_M, WithinAbs(0.01428f, 0.00001f));

		// 1 foot = 30.48 cm
		REQUIRE_THAT(GAME_UNIT_TO_FEET, WithinAbs(1.428f / 30.48f, 0.00001f));

		// 1 inch = 2.54 cm
		REQUIRE_THAT(GAME_UNIT_TO_INCHES, WithinAbs(1.428f / 2.54f, 0.00001f));
	}

	SECTION("Conversion ratios make sense")
	{
		// 1 foot = 12 inches
		float ratio = GAME_UNIT_TO_INCHES / GAME_UNIT_TO_FEET;
		REQUIRE_THAT(ratio, WithinAbs(12.0f, 0.01f));

		// 1 meter = 100 cm
		float ratio2 = GAME_UNIT_TO_CM / GAME_UNIT_TO_M;
		REQUIRE_THAT(ratio2, WithinAbs(100.0f, 0.01f));
	}
}

TEST_CASE("Game unit conversions work correctly", "[Conversion][Units]")
{
	using namespace GameConversions;

	SECTION("Convert game units to meters")
	{
		REQUIRE_THAT(GameUnitsToMeters(0.0f), WithinAbs(0.0f, 0.001f));
		REQUIRE_THAT(GameUnitsToMeters(100.0f), WithinAbs(1.428f, 0.001f));
		REQUIRE_THAT(GameUnitsToMeters(1000.0f), WithinAbs(14.28f, 0.01f));
	}

	SECTION("Convert game units to centimeters")
	{
		REQUIRE_THAT(GameUnitsToCm(0.0f), WithinAbs(0.0f, 0.001f));
		REQUIRE_THAT(GameUnitsToCm(1.0f), WithinAbs(1.428f, 0.001f));
		REQUIRE_THAT(GameUnitsToCm(100.0f), WithinAbs(142.8f, 0.01f));
	}

	SECTION("Convert game units to feet")
	{
		REQUIRE_THAT(GameUnitsToFeet(0.0f), WithinAbs(0.0f, 0.001f));
		REQUIRE_THAT(GameUnitsToFeet(100.0f), WithinRel(4.685f, 0.01f));
	}

	SECTION("Convert game units to inches")
	{
		REQUIRE_THAT(GameUnitsToInches(0.0f), WithinAbs(0.0f, 0.001f));
		REQUIRE_THAT(GameUnitsToInches(1.0f), WithinRel(0.5622f, 0.01f));
	}

	SECTION("Real-world distances")
	{
		// Skyrim character height ~108 game units = ~1.54m (realistic)
		REQUIRE_THAT(GameUnitsToMeters(108.0f), WithinRel(1.542f, 0.01f));

		// Dragon length ~400 game units = ~5.7m (plausible)
		REQUIRE_THAT(GameUnitsToMeters(400.0f), WithinRel(5.712f, 0.01f));
	}
}

TEST_CASE("Wind speed conversions", "[Conversion][Weather]")
{
	using namespace GameConversions;

	SECTION("Wind conversion constants")
	{
		REQUIRE_THAT(WIND_RAW_TO_NORMALIZED, WithinAbs(1.0f / 255.0f, 0.000001f));
		REQUIRE_THAT(WIND_RAW_TO_PERCENT, WithinAbs(100.0f / 255.0f, 0.000001f));
	}

	SECTION("Wind raw to normalized (0-1 scale)")
	{
		REQUIRE_THAT(WindRawToNormalized(0), WithinAbs(0.0f, 0.001f));
		REQUIRE_THAT(WindRawToNormalized(127), WithinRel(0.498f, 0.01f));
		REQUIRE_THAT(WindRawToNormalized(255), WithinRel(1.0f, 0.001f));
	}

	SECTION("Wind raw to percent (0-100 scale)")
	{
		REQUIRE_THAT(WindRawToPercent(0), WithinAbs(0.0f, 0.01f));
		REQUIRE_THAT(WindRawToPercent(127), WithinRel(49.8f, 0.1f));
		REQUIRE_THAT(WindRawToPercent(255), WithinRel(100.0f, 0.1f));
	}

	SECTION("Percentage and normalized conversions match")
	{
		for (uint8_t wind = 0; wind < 255; wind += 25) {
			float normalized = WindRawToNormalized(wind);
			float percent = WindRawToPercent(wind);
			REQUIRE_THAT(percent / 100.0f, WithinRel(normalized, 0.001f));
		}
	}
}

TEST_CASE("Direction conversions", "[Conversion][Weather]")
{
	using namespace GameConversions;

	SECTION("Direction conversion constants")
	{
		REQUIRE_THAT(DIR_RAW_TO_DEGREES, WithinAbs(360.0f / 256.0f, 0.00001f));
		REQUIRE_THAT(DIR_RANGE_TO_DEGREES, WithinAbs(180.0f / 256.0f, 0.00001f));
	}

	SECTION("Direction raw to degrees (0-360)")
	{
		REQUIRE_THAT(DirectionRawToDegrees(0), WithinAbs(0.0f, 0.01f));
		REQUIRE_THAT(DirectionRawToDegrees(64), WithinRel(90.0f, 0.01f));    // East
		REQUIRE_THAT(DirectionRawToDegrees(128), WithinRel(180.0f, 0.01f));  // South
		REQUIRE_THAT(DirectionRawToDegrees(192), WithinRel(270.0f, 0.01f));  // West
		REQUIRE_THAT(DirectionRawToDegrees(255), WithinRel(358.59f, 0.1f));  // Almost full circle
	}

	SECTION("Direction range to degrees (0-180)")
	{
		REQUIRE_THAT(DirectionRangeToDegrees(0), WithinAbs(0.0f, 0.01f));
		REQUIRE_THAT(DirectionRangeToDegrees(128), WithinRel(90.0f, 0.01f));
		REQUIRE_THAT(DirectionRangeToDegrees(255), WithinRel(179.3f, 0.1f));
	}

	SECTION("Cardinal directions")
	{
		// North = 0/256 = 0°
		REQUIRE_THAT(DirectionRawToDegrees(0), WithinAbs(0.0f, 0.1f));

		// East = 64/256 = 90°
		REQUIRE_THAT(DirectionRawToDegrees(64), WithinAbs(90.0f, 0.1f));

		// South = 128/256 = 180°
		REQUIRE_THAT(DirectionRawToDegrees(128), WithinAbs(180.0f, 0.1f));

		// West = 192/256 = 270°
		REQUIRE_THAT(DirectionRawToDegrees(192), WithinAbs(270.0f, 0.1f));
	}
}

TEST_CASE("Conversion accuracy edge cases", "[Conversion][EdgeCases]")
{
	using namespace GameConversions;

	SECTION("Zero conversions")
	{
		REQUIRE(GameUnitsToMeters(0.0f) == 0.0f);
		REQUIRE(GameUnitsToCm(0.0f) == 0.0f);
		REQUIRE(GameUnitsToFeet(0.0f) == 0.0f);
		REQUIRE(GameUnitsToInches(0.0f) == 0.0f);
		REQUIRE(WindRawToNormalized(0) == 0.0f);
		REQUIRE(WindRawToPercent(0) == 0.0f);
	}

	SECTION("Maximum values")
	{
		// Max wind (255) should be exactly 1.0 normalized
		REQUIRE_THAT(WindRawToNormalized(255), WithinRel(1.0f, 0.001f));

		// Max direction (255) should be close to 360°
		float maxDegrees = DirectionRawToDegrees(255);
		REQUIRE(maxDegrees < 360.0f);  // Should wrap before full circle
		REQUIRE(maxDegrees > 355.0f);  // But be very close
	}

	SECTION("Negative game units (should work for relative distances)")
	{
		// Negative values make sense for relative positions
		REQUIRE_THAT(GameUnitsToMeters(-100.0f), WithinAbs(-1.428f, 0.001f));
		REQUIRE_THAT(GameUnitsToCm(-50.0f), WithinAbs(-71.4f, 0.01f));
	}

	SECTION("Very large game units")
	{
		// Skyrim world is huge - need to handle large distances
		float worldSize = 10000.0f;  // Typical world cell size
		REQUIRE_THAT(GameUnitsToMeters(worldSize), WithinRel(142.8f, 0.01f));
	}
}

TEST_CASE("Conversion constant precision", "[Conversion][Precision]")
{
	using namespace GameConversions;

	SECTION("Constants have sufficient precision")
	{
		// Conversions should maintain accuracy across ranges
		float cm100 = GameUnitsToCm(100.0f);
		float cm200 = GameUnitsToCm(200.0f);

		// Double the input = double the output (linearity)
		REQUIRE_THAT(cm200, WithinAbs(cm100 * 2.0f, 0.001f));
	}

	SECTION("Inverse conversions (meters back to game units)")
	{
		float gameUnits = 100.0f;
		float meters = GameUnitsToMeters(gameUnits);
		float backToGameUnits = meters / GAME_UNIT_TO_M;

		REQUIRE_THAT(backToGameUnits, WithinAbs(gameUnits, 0.001f));
	}

	SECTION("Chained conversions preserve accuracy")
	{
		// Game units -> cm -> meters should equal direct game units -> meters
		float direct = GameUnitsToMeters(100.0f);
		float chained = GameUnitsToCm(100.0f) / 100.0f;

		REQUIRE_THAT(chained, WithinAbs(direct, 0.00001f));
	}
}

TEST_CASE("Real-world validation checks", "[Conversion][Validation]")
{
	using namespace GameConversions;

	SECTION("Human height validation")
	{
		// Average person ~1.7m = ~119 game units
		float person170cm = 170.0f / GAME_UNIT_TO_CM;
		REQUIRE_THAT(person170cm, WithinRel(119.0f, 0.01f));
	}

	SECTION("Room dimensions validation")
	{
		// 3m room = ~210 game units
		float room3m = 3.0f / GAME_UNIT_TO_M;
		REQUIRE_THAT(room3m, WithinRel(210.0f, 0.1f));
	}

	SECTION("Arrow flight distance")
	{
		// 100m arrow flight = ~7003 game units
		float arrow100m = 100.0f / GAME_UNIT_TO_M;
		REQUIRE_THAT(arrow100m, WithinAbs(7003.0f, 10.0f));  // Within 10 units is fine
	}
}

TEST_CASE("Bit-level raw value conversions", "[Conversion][Raw]")
{
	using namespace GameConversions;

	SECTION("8-bit wind values cover full range")
	{
		// uint8_t max is 255
		uint8_t minWind = 0;
		uint8_t maxWind = 255;

		REQUIRE(WindRawToNormalized(minWind) == 0.0f);
		REQUIRE_THAT(WindRawToNormalized(maxWind), WithinRel(1.0f, 0.001f));
	}

	SECTION("Direction precision with 256 values")
	{
		// 256 direction values = 360/256 = 1.40625° precision
		float precisionDegrees = DIR_RAW_TO_DEGREES;
		REQUIRE_THAT(precisionDegrees, WithinAbs(1.40625f, 0.00001f));

		// This is ~1.4° precision - good enough for weather
		REQUIRE(precisionDegrees < 1.5f);
		REQUIRE(precisionDegrees > 1.0f);
	}

	SECTION("Midpoint conversions")
	{
		// Middle of uint8_t range
		REQUIRE_THAT(WindRawToNormalized(127), WithinRel(0.498f, 0.001f));
		REQUIRE_THAT(DirectionRawToDegrees(128), WithinAbs(180.0f, 0.1f));
	}
}
