// Runtime-discovered HLSL tests
// This file discovers and runs all HLSL tests at runtime - no code generation needed!

#include "runtime_test_discovery.h"
#include <catch2/catch_test_macros.hpp>
#include <iostream>

TEST_CASE("Auto-discovered HLSL tests", "[autodiscovery]")
{
	// Discover all tests at runtime
	auto tests = HLSLTestDiscovery::discoverAllTests();

	INFO("Discovered " << tests.size() << " HLSL test functions");
	REQUIRE(tests.size() > 0);  // Should find at least some tests

	// Run each discovered test
	for (const auto& test : tests) {
		DYNAMIC_SECTION(test.displayName)
		{
			std::string errorMsg;
			bool success = HLSLTestDiscovery::runTest(test, errorMsg);

			if (!success) {
				FAIL("Test failed: " << errorMsg);
			}

			INFO("✓ " << test.displayName);
		}
	}
}
