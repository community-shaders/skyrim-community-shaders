# Community Shaders Unit Tests

**812 assertions across 66 test cases** testing actual production code with proper mocks.

## Quick Start

```bash
# 1. Build tests (vcpkg installs Catch2 automatically)
cmake --preset=ALL -DBUILD_TESTS=ON
cmake --build build/ALL --target CommunityShaders_Tests --config Release

# 2. Run tests
cd build/ALL/tests/Release
./CommunityShaders_Tests.exe

# Expected output: "All tests passed (766 assertions in 56 test cases)"
```

**That's it!** See [QUICKSTART.md](QUICKSTART.md) for 5-minute setup guide.

## What's Tested (Real Production Code!)

-   **Format.cpp**: String formatting, math utilities, path normalization (ACTUAL code)
-   **FileSystem.cpp**: Path helpers, file operations, JSON diff (ACTUAL code with mocked imgui/json)
-   **GPU Buffers**: 16-byte and 64-byte alignment validation
-   **Shader Constants**: Register mapping, VR vs Flat build consistency
-   **Configuration**: JSON/INI parsing, settings validation
-   **Game Conversions**: Unit conversions, weather data transformations
-   **Enums & Types**: Type safety, flag combinations
-   **API Contracts**: Function signature validation at compile-time

## Running Tests

### Basic Usage

```bash
# Run all tests
./CommunityShaders_Tests.exe

# Run specific category
./CommunityShaders_Tests.exe "[GPU]"
./CommunityShaders_Tests.exe "[Config]"
./CommunityShaders_Tests.exe "[Critical]"

# Run with verbose output
./CommunityShaders_Tests.exe --success

# List all tests
./CommunityShaders_Tests.exe --list-tests

# List all tags
./CommunityShaders_Tests.exe --list-tags
```

### Via CTest

```bash
cd build/ALL
ctest --verbose
ctest -R gpu  # Run GPU tests only
```

---

## Architecture (Industry Best Practice)

Tests compile actual production code using **proper dependency mocking**:

1. **Static Library**: Compiles `src/Utils/*.cpp` with test-compatible settings
2. **Precompiled Headers**: Provides standard library + mocks for external dependencies
3. **Mock External Dependencies**: ImGui, nlohmann/json, Skyrim SDK, DirectX
4. **No Code Duplication**: Tests use real production code, not duplicates

**Why Mock?** Industry standard practice - isolates business logic from external dependencies, enabling fast, deterministic tests without heavy UI/game engine dependencies.

## Writing New Tests

### Test Template

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE("Description of what you're testing", "[Category][Tag]")
{
    SECTION("Specific behavior to validate")
    {
        // Arrange - Set up test data
        float input = 100.0f;

        // Act - Execute the function
        float result = YourFunction(input);

        // Assert - Verify correctness
        REQUIRE_THAT(result, WithinAbs(142.8f, 0.01f));
    }

    SECTION("Edge case: zero input")
    {
        REQUIRE(YourFunction(0.0f) == 0.0f);
    }
}
```

### Common Patterns

```cpp
// Exact equality
REQUIRE(value == 42);

// Floating-point comparison (absolute tolerance)
REQUIRE_THAT(result, WithinAbs(1.428f, 0.001f));

// Floating-point comparison (relative tolerance, 1% = 0.01)
REQUIRE_THAT(result, WithinRel(100.0f, 0.01f));

// String equality
REQUIRE(str == "expected");

// Alignment checks
REQUIRE(sizeof(Buffer) % 16 == 0);
REQUIRE(alignof(Buffer) == 16);

// Enum validation
REQUIRE(static_cast<uint32_t>(MyEnum::Value) == 42);
```

### Best Practices

1. **Test pure functions first** - No side effects = easy testing
2. **Test edge cases** - Zero, negative, max values, overflow
3. **Use descriptive test names** - "What am I validating?"
4. **One assertion per concept** - Easy to debug failures
5. **Keep tests fast** - Each test <1ms
6. **Document complex logic** - Especially GPU-specific

---

## CI Integration

Tests run automatically via:

### GitHub Actions

-   On every push to main/dev
-   On every pull request
-   See `.github/workflows/test.yaml`

### Pre-commit Hook

-   Runs before each commit
-   Configured in `.pre-commit-config.yaml`
-   Catches regressions early

### Manual Run

```bash
# From project root
cmake --build build/ALL --target CommunityShaders_Tests --config Release
cd build/ALL/tests/Release && ./CommunityShaders_Tests.exe
```

---

## Resources

-   [QUICKSTART.md](QUICKSTART.md) - 5-minute setup guide
-   [TROUBLESHOOTING.md](TROUBLESHOOTING.md) - Common issues
-   [CODECOV_SETUP_INSTRUCTIONS.md](CODECOV_SETUP_INSTRUCTIONS.md) - Coverage setup
