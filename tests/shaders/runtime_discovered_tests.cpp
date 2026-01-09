// Runtime-discovered HLSL tests
// This file discovers and runs all HLSL tests at runtime - no code generation needed!

#include "runtime_test_discovery.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <iostream>

TEST_CASE("Auto-discovered HLSL tests", "[autodiscovery]")
{
	// Discover all tests at runtime
	auto discoveryStart = std::chrono::high_resolution_clock::now();
	auto tests = HLSLTestDiscovery::discoverAllTests();
	auto discoveryEnd = std::chrono::high_resolution_clock::now();
	auto discoveryMs = std::chrono::duration_cast<std::chrono::milliseconds>(discoveryEnd - discoveryStart).count();

	REQUIRE(tests.size() > 0);  // Should find at least some tests

	// Print count once before running tests
	static bool printed = false;
	if (!printed) {
		std::cout << "Discovered " << tests.size() << " HLSL test functions in " << discoveryMs << "ms\n";
		printed = true;
	}

	// Run each discovered test
	for (const auto& test : tests) {
		DYNAMIC_SECTION(test.displayName)
		{
			std::string errorMsg;
			bool success = HLSLTestDiscovery::runTest(test, errorMsg);

			if (!success) {
				FAIL("Test failed: " << errorMsg);
			}

			INFO("[PASS] " << test.displayName);
		}
	}
}
