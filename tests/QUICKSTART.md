# Quick Start: Unit Testing

Get up and running with unit tests in 5 minutes.

## 1. Install Dependencies

Catch2 is already added to `vcpkg.json`, so just run:

```bash
# Using vcpkg (recommended)
vcpkg install catch2

# Or let CMake handle it (if vcpkg is integrated)
# Dependencies will be installed automatically during configure
```

## 2. Configure Build with Tests Enabled

```bash
# Option A: Using CMake presets (recommended)
cmake --preset=ALL -DBUILD_TESTS=ON

# Option B: Manual configuration
cmake -B build -DBUILD_TESTS=ON
```

## 3. Build Tests

```bash
cmake --build build/ALL --target CommunityShaders_Tests

# Or build everything
cmake --build build/ALL
```

## 4. Run Tests

```bash
# Run all tests
cd build/ALL/tests
./CommunityShaders_Tests.exe

# Run with detailed output
./CommunityShaders_Tests.exe --success

# Run specific test file
./CommunityShaders_Tests.exe "[Format]"

# List all available tests
./CommunityShaders_Tests.exe --list-tests
```

## Using CTest

```bash
cd build/ALL
ctest --verbose
```

## What Gets Tested?

✅ **Path utilities** - Normalization, Skyrim path patterns  
✅ **Math utilities** - Percentage calculations, cost-per-call  
✅ **Format utilities** - File sizes, millisecond formatting  
✅ **GPU buffers** - 16-byte and 64-byte alignment validation

**All tests are standalone** - No Skyrim dependencies!

## Common Issues

### "Catch2 not found"

```bash
# Make sure vcpkg is set up correctly
vcpkg integrate install

# Or specify the toolchain file
cmake -B build -DBUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=[path-to-vcpkg]/scripts/buildsystems/vcpkg.cmake
```

### "Test executable not found"

Tests are built to `build/ALL/tests/` (or `build/ALL/tests/Debug/` in multi-config generators).

Check both locations:

```bash
dir build\ALL\tests\*.exe
dir build\ALL\tests\Debug\*.exe
```

### "Link errors" or "Unresolved externals"

The test executable only links utility code, not the full plugin. If you get link errors:

1. Check that you're only testing pure utility functions
2. Verify all mocks are defined (logger, globals::d3d, etc.)
3. See `tests/CMakeLists.txt` for which sources are compiled

## Writing Your First Test

1. Create a new file in `tests/`:

```cpp
// tests/test_myfeature.cpp
#include <catch2/catch_test_macros.hpp>

// Add mocks
namespace logger {
    template <typename... Args>
    void warn(const char*, Args&&...) {}
}

// Include your header
#include "Utils/MyFeature.h"

TEST_CASE("MyFeature does something", "[MyFeature]")
{
    SECTION("Normal case")
    {
        auto result = MyFunction(input);
        REQUIRE(result == expected);
    }

    SECTION("Edge case")
    {
        auto result = MyFunction(edgeCase);
        REQUIRE(result == edgeExpected);
    }
}
```

2. Rebuild tests:

```bash
cmake --build build/ALL --target CommunityShaders_Tests
```

3. Run your new tests:

```bash
cd build/ALL/tests
./CommunityShaders_Tests.exe "[MyFeature]"
```

## IDE Integration

### Visual Studio

1. Open the solution: `build/ALL/CommunityShaders.sln`
2. Set `CommunityShaders_Tests` as startup project
3. Press F5 to run tests in debugger

### VS Code

Add to `.vscode/tasks.json`:

```json
{
    "label": "Run Tests",
    "type": "shell",
    "command": "cd build/ALL/tests && ./CommunityShaders_Tests.exe",
    "group": "test"
}
```

## CI Integration

Tests automatically run on:

-   ✅ Push to `main` or `dev`
-   ✅ Pull requests
-   ✅ Manual workflow dispatch

See `.github/workflows/test.yaml` for details.

## Code Coverage

Coverage runs **automatically in CI** on every PR. No local setup needed!

### Running Tests Locally

```powershell
.\tests\run_tests.ps1
```

**Why no local coverage?**

-   Requires Clang (separate from MSVC used for main build)
-   Complex vcpkg/build setup
-   CI handles it automatically on every PR
-   Faster to just push and see CI results

**Pre-commit runs tests automatically** - you'll catch breaks before pushing!

## Next Steps

-   Read `tests/README.md` for detailed documentation
-   Check `tests/test_format.cpp` for example test patterns
-   Add tests for your own code!
-   Aim for >80% coverage of utility functions

## Getting Help

-   Check existing tests for examples
-   See [Catch2 documentation](https://github.com/catchorg/Catch2/tree/devel/docs)
-   Ask in the Community Shaders Discord
