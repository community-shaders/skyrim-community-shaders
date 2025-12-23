# Shader Unit Tests

GPU-executed unit tests for HLSL shader code using [ShaderTestFramework](https://github.com/KStocky/ShaderTestFramework).

## Quick Start

```bash
# Build tests
cmake --preset ALL -DBUILD_SHADER_TESTS=ON
cmake --build build/ALL --target shader_tests --config Release

# Run tests
ctest --test-dir build/ALL -C Release -R ShaderTests --output-on-failure
```

## ⚠️ Note: DX11 vs DX12

**Production**: FXC/DX11 (Shader Model 5.0)  
**Tests**: DXC/D3D12 (Shader Model 6.0+)

Tests focus on pure math functions (no DX12-specific features), so compiler differences have minimal impact.

## Writing New Tests

See existing tests (`test_math.cpp`, `test_brdf.cpp`) for examples. Pattern:

**C++ test harness** (`test_*.cpp`):

```cpp
TEST_CASE("MyShader - MyFunction", "[myshader]")
{
    stf::ShaderTestFixture fixture(...);
    auto result = fixture.RunTest(...);
    REQUIRE(result);
}
```

**HLSL test** (`Tests/Test*.hlsl`):

```hlsl
#include "Common/MyShader.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

[numthreads(1, 1, 1)]
void TestMyFunction()
{
    ASSERT(IsTrue, MyFunction(1.0f) > 0.0f);
}
```

### Test Organization

Use Catch2 tags to organize tests:

-   `[math]` - Mathematical operations
-   `[color]` - Color space operations
-   `[pbr]` - PBR material functions
-   `[lighting]` - Lighting calculations
-   `[utility]` - Utility functions

### Running Specific Tests

```bash
# Run only math tests
shader_tests.exe "[math]"

# Run only color tests
shader_tests.exe "[color]"

# Run a specific test case
shader_tests.exe "Math.hlsli - Constants"
```

## Current Test Coverage

### ✅ GPU-Executed Unit Tests (63 test cases, 268 GPU assertions)

-   ✅ **Math.hlsli** (3 tests)

    -   Math constants (PI, HALF_PI, TAU) with relationship validation
    -   Epsilon constants (exact values + ordering)
    -   Identity matrix structure

-   ✅ **Color.hlsli** (6 tests)

    -   RGB to Luminance conversion
    -   RGB/YCoCg color space roundtrip
    -   Saturation adjustment
    -   Gamma ↔ Linear conversion roundtrip
    -   Multi-bounce AO
    -   Specular AO Lagarde

-   ✅ **BRDF.hlsli** (8 tests)

    -   Diffuse Lambert
    -   Fresnel Schlick (boundary conditions)
    -   GGX Distribution (roughness variations)
    -   Smith Joint Visibility
    -   Neubelt Visibility (symmetry)
    -   Environment BRDF Lazarov
    -   Charlie Distribution (sheen)
    -   Anisotropic GGX

-   ✅ **Random.hlsli** (14 tests)

    -   PCG random number generator (determinism, uniqueness)
    -   Float generators (f1, f2, f3 range validation)
    -   PCG 2D/3D hashing
    -   Interleaved Gradient Noise
    -   R1/R2/R3 quasirandom sequences
    -   Murmur3 hash
    -   Perlin noise (range, continuity)

-   ✅ **FastMath.hlsli** (16 tests)

    -   Fast reciprocal sqrt (NR0, NR1, NR2 iterations - error bounds)
    -   Fast sqrt (NR0, NR1, NR2 iterations - error bounds)
    -   Fast reciprocal (NR0, NR1, NR2 iterations - error bounds)
    -   Fast trig functions (acos, asin, atan variants)
    -   Optimized trig (ACos, ASin, ATan, ATanPos)
    -   Accuracy validation vs standard library functions

-   ✅ **DisplayMapping.hlsli** (11 tests)

    -   Range compression (single, threshold, float3)
    -   PQ (Perceptual Quantizer) encoding/decoding roundtrips
    -   RGB ↔ XYZ color space conversions
    -   XYZ ↔ LMS conversions
    -   RGB ↔ ICtCp conversions (HDR color space)
    -   PQ constants validation
    -   White point and black point accuracy

-   ✅ **LightingCommon.hlsli** (1 test)
    -   ShininessToRoughness conversion (known values, monotonicity, range)

### Coverage Summary

**Files Covered**: 7/21 (33% of all files, ~70% of easily testable files)
**Total Tests**: 63 test functions
**Total Assertions**: 268 GPU-executed assertions

### ⏳ Not Easily Testable

-   ⏳ PBR material functions
-   ⏳ BRDF calculations
-   ⏳ Lighting computations
-   ⏳ Shadow sampling
-   ⏳ Frame buffer operations

## Dependencies

-   **ShaderTestFramework**: Fetched automatically via CMake FetchContent
-   **Catch2 v3**: Fetched automatically via CMake FetchContent
-   **D3D12**: Required for shader execution (Windows only)

## CI Integration

These tests run automatically in GitHub Actions on:

-   Pull requests that modify `.hlsl` or `.hlsli` files
-   Pushes to `main` and `dev` branches

See `.github/workflows/shader-unit-tests.yaml` for CI configuration.

## Troubleshooting

### Graphics Tools Required

**D3D12 shader tests require Windows Graphics Tools to be installed.**

CMake will automatically detect if Graphics Tools are missing and display installation instructions.

**Quick Install:**

```powershell
# Option 1: Automated script (opens settings or auto-installs)
.\tools\install_graphics_tools.ps1

# Option 2: Direct PowerShell command (requires admin)
Enable-WindowsOptionalFeature -Online -FeatureName GraphicsTools -All

# Option 3: Manual via Settings
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
-   Verify Visual Studio 2022 is installed with C++ development tools

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
