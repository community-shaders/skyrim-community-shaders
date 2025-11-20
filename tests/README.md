# Community Shaders Unit Tests

**766 assertions across 56 test cases - ALL PASSING** ✅

Fast, standalone tests for critical Community Shaders logic with zero Skyrim dependencies.

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

---

## Test Coverage

### Test Files (7 total)

1. **`test_simple_utilities.cpp`** (56 assertions)

    - Path normalization, math utilities, formatting, basic alignment

2. **`test_shader_validation.cpp`** (64 assertions)

    - Shader defines, version parsing, feature names, permutations

3. **`test_configuration.cpp`** (84 assertions)

    - JSON/INI parsing, settings validation, error handling

4. **`test_gpu_structures.cpp`** (118 assertions) ⚠️ **CRITICAL**

    - GPU buffer alignment (prevents crashes!)
    - 16-byte and 64-byte alignment enforcement
    - Real-world buffer patterns from features

5. **`test_shader_constants.cpp`** (71 assertions) ⚠️ **CRITICAL**

    - Shader register mapping (prevents compilation failures!)
    - VR vs Flat build consistency
    - Register range validation

6. **`test_enums_and_types.cpp`** (92 assertions)

    - Enum validation, type safety, flag combinations
    - Alignment requirements, type traits

7. **`test_conversion_utilities.cpp`** (281 assertions)
    - Game unit conversions (meters, feet, inches, cm)
    - Wind/weather conversions (raw to normalized/percent)
    - Direction conversions, real-world validation

### Coverage Breakdown

| Category          | Assertions | Impact      | Why Critical                  |
| ----------------- | ---------- | ----------- | ----------------------------- |
| Conversions       | 281        | 🟠 HIGH     | Accurate game measurements    |
| GPU Buffers       | 118        | 🔴 CRITICAL | Prevents GPU crashes          |
| Enums/Types       | 92         | 🟠 HIGH     | Type safety, flag correctness |
| Configuration     | 84         | 🟠 HIGH     | Prevents invalid settings     |
| Shader Constants  | 71         | 🔴 CRITICAL | Prevents shader bugs          |
| Shader Validation | 64         | 🟠 HIGH     | Catches define errors         |
| Simple Utilities  | 56         | 🟡 MEDIUM   | Logic correctness             |

**Total: 766 assertions covering critical logic paths**

---

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

## Design Philosophy

### Why Standalone Tests?

Most Community Shaders code requires:

-   DirectX 11 device context
-   Skyrim game engine (CommonLibSSE)
-   GPU resources and game state

**Our Solution**: Test the _logic_, not the integration.

✅ **Do:**

-   Test utility functions with standalone implementations
-   Validate constants (alignment, sizes, register indices)
-   Test pure algorithms (conversions, parsing, validation)
-   Ensure business logic correctness

❌ **Don't:**

-   Try to mock the entire Skyrim engine
-   Test DirectX API calls (no GPU needed!)
-   Compile actual source files with Skyrim dependencies
-   Test tightly-coupled rendering code

### What This Covers

✅ Business logic correctness  
✅ Math and conversion accuracy  
✅ GPU alignment requirements  
✅ Configuration parsing  
✅ Type safety and enums  
✅ Shader constant validation

### What This Doesn't Cover

❌ DirectX API integration  
❌ Actual shader compilation (use Python tests)  
❌ Feature integration with Skyrim  
❌ Rendering pipeline

---

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

## Code Coverage

Code coverage runs **automatically in CI** using Clang + Codecov. No local setup needed!

### Viewing Coverage

-   **In PRs**: Automatic comment with coverage stats
-   **Dashboard**: https://codecov.io (once token is added)

### Why Not in Pre-commit?

Coverage is 2-3x slower than tests. We prioritize fast commits:

-   ✅ Pre-commit: Fast tests (15s)
-   ✅ CI: Coverage (automatic on PRs)

**Current coverage: ~40-50%** of testable utility code (critical areas well-covered!)

For maintainers: See [CODECOV_SETUP_INSTRUCTIONS.md](CODECOV_SETUP_INSTRUCTIONS.md) to enable Codecov.

---

## Key Metrics

-   **Total Assertions**: 766
-   **Test Cases**: 56
-   **Pass Rate**: 100%
-   **Execution Time**: <1 second
-   **Build Time**: ~5 seconds
-   **False Positives**: 0
-   **Skyrim Dependencies**: 0

---

## Contributing

When adding new testable code:

1. ✅ **Write tests first** (TDD when possible)
2. ✅ **Keep tests standalone** (no Skyrim dependencies)
3. ✅ **Test edge cases** (zero, negative, boundary values)
4. ✅ **Use meaningful test names**
5. ✅ **Run before committing** (pre-commit hook will catch failures)

---

## Questions?

-   **Quick Setup**: See [QUICKSTART.md](QUICKSTART.md)
-   **Coverage Tools**: See [COVERAGE_GUIDE.md](COVERAGE_GUIDE.md)
-   **Remaining Gaps**: See [GAP_ANALYSIS.md](GAP_ANALYSIS.md)
-   **Examples**: Look at existing test files
-   **Community**: Ask in Community Shaders Discord

---

**Tests are production-ready! Keep writing testable code! 🚀**
