# Shader Unit Tests

GPU-executed unit tests for HLSL shader code using [ShaderTestFramework](https://github.com/KStocky/ShaderTestFramework).

## Quick Start

```bash
# Configure with shader tests enabled (ON by default)
cmake --preset ALL -DBUILD_SHADER_TESTS=ON

# Build and run tests
cmake --build build/ALL --config Release --target run_shader_tests

# Or build then run separately
cmake --build build/ALL --target shader_tests --config Release
ctest --test-dir build/ALL -C Release -R ShaderTests --output-on-failure
```

**Note**: Tests run automatically when building Package targets (e.g., `Package-AIO-Manual`, `Package-Core`).

## ⚠️ Note: DX11 vs DX12

**Production**: FXC/DX11 (Shader Model 5.0)  
**Tests**: DXC/D3D12 (Shader Model 6.0+)

Tests focus on pure math functions (no DX12-specific features), so compiler differences have minimal impact.

## Writing New Tests

Tests are automatically discovered at runtime by scanning HLSL files in `package/Shaders/Tests/Test*.hlsl`.

**Create a test file** (`package/Shaders/Tests/TestMyModule.hlsl`):

```hlsl
#include "/Shaders/Common/MyShader.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

/// @tags math, utility
/// Test description (optional)
[numthreads(1, 1, 1)]
void TestMyFunction()
{
    ASSERT(IsTrue, MyFunction(1.0f) > 0.0f);
}

/// @tag performance
[numthreads(1, 1, 1)]
void TestMyFunctionPerformance()
{
    // Performance test
    ASSERT(IsTrue, MyFunction(100.0f) > 0.0f);
}
```

### Organizing Tests with Tags

Use doxygen-style `@tag` or `@tags` comments before test functions to organize them:

```hlsl
/// @tags brdf, specular
[numthreads(1, 1, 1)]
void TestFresnelSchlick() { ... }

/// @tags color, gamma
[numthreads(1, 1, 1)]
void TestGammaConversion() { ... }
```

**Tag format:**

-   `/// @tag tagname` - Single tag
-   `/// @tags tag1, tag2, tag3` - Multiple tags (comma-separated)
-   Multiple comment lines are combined
-   Tags are optional; tests without tags will have no tag filtering available

### Running Specific Tests

```bash
# Run all tests
shader_tests.exe

# Run tests with specific tag
shader_tests.exe "[brdf]"

# Run multiple tags
shader_tests.exe "[math][color]"

# Run a specific test by name
shader_tests.exe "BRDF - Fresnel Schlick"

# List all available tags
shader_tests.exe --list-tags
```

## Test Coverage

Tests are automatically discovered from HLSL files in `package/Shaders/Tests/`. To see the current test modules, tags, and results:

```bash
# Show test count and run all tests
shader_tests.exe

# List all test cases with their tags
shader_tests.exe --list-tests

# List all available tags
shader_tests.exe --list-tags
```

### Adding New Test Coverage

To add tests for a new shader module:

1. Create `package/Shaders/Tests/TestYourModule.hlsl`
2. Add test functions with `[numthreads(1,1,1)]` attribute
3. Use `/// @tags` comments to organize tests
4. Tests are automatically discovered and run

## Dependencies

-   **ShaderTestFramework**: Fetched automatically via CMake FetchContent
-   **Catch2 v3**: Fetched automatically via CMake FetchContent
-   **D3D12**: Required for shader execution (Windows only)

## CI Integration

These tests run automatically in GitHub Actions on:

-   Pull requests that modify `.hlsl`, `.hlsli`, or test-related files
-   Pushes to tags starting with `v`
-   Manual workflow dispatches

See the `shader-unit-tests` job in `.github/workflows/build.yaml` for CI integration.

## Troubleshooting

### Graphics Tools Required

**D3D12 shader tests require Windows Graphics Tools to be installed.**

CMake will automatically detect if Graphics Tools are missing and display installation instructions.

**Quick Install:**

```powershell
# Option 1: Direct PowerShell command (requires admin)
Enable-WindowsOptionalFeature -Online -FeatureName GraphicsTools -All

# Option 2: Manual via Settings
# Windows Settings → Apps → Optional Features → Add "Graphics Tools"
```

**Automatic detection during CMake:**

```bash
# CMake will warn if Graphics Tools are missing
cmake --preset ALL -DBUILD_SHADER_TESTS=ON

# To automatically open Windows Optional Features dialog:
cmake --preset ALL -DBUILD_SHADER_TESTS=ON -DAUTO_OPEN_OPTIONAL_FEATURES=ON
```

**Common error without Graphics Tools:**

```
DXGI_ERROR_SDK_COMPONENT_MISSING (0x887A002D)
```

This means `d3d12SDKLayers.dll` is missing. Install Graphics Tools and reboot to fix.

### Build Errors

**FetchContent fails:**

```bash
# Clear CMake cache and rebuild
rm -rf build/ALL
cmake --preset ALL
```

**Linker errors:**

-   Ensure you're building on Windows with D3D12 support
-   Verify Visual Studio 2026 is installed with C++ development tools

**CMake 4.0 Compatibility:**

If you encounter `unresolved external symbol main` errors, this is due to a known incompatibility between CMake 4.0 and Catch2's `Catch2WithMain` target. The build has been updated to work around this by providing an explicit `main()` function.

### Test Failures

**Shader compilation errors:**

-   Check that shader include paths are correct
-   Verify shader code compiles with fxc/dxc separately

**Runtime errors:**

-   Ensure D3D12-capable GPU is available
-   Verify Graphics Tools are installed (see above)

## References

-   [ShaderTestFramework Documentation](https://github.com/KStocky/ShaderTestFramework/blob/main/docs/Tutorial.md)
-   [Catch2 Documentation](https://github.com/catchorg/Catch2/tree/devel/docs)
-   [D3D12 Documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d12/directx-12-programming-guide)
