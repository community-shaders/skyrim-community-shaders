# Windows Build Testing Required

## Status
⚠️ **This PR updates CommonLibSSE-NG submodule but cannot be built in Linux environment**

## Changes Made
- Updated CommonLibSSE-NG from `f343b8cf` → `9228685c` (ng branch, 307 commits)
- Code analysis performed: No breaking API changes detected for this codebase

## Windows Testing Required

This project requires Windows-specific tools that are not available in Linux:
- Visual Studio 2022 with "Desktop development with C++" workload
- Windows SDK
- DirectX SDK (via Windows SDK)

### Build Commands
```powershell
# Configure
cmake --preset ALL

# Build
cmake --build ./build/ALL --config Release

# If successful, test shader compilation
cmake --build ./build/ALL --target prepare_shaders
```

### What to Verify
1. ✅ CMake configuration succeeds
2. ✅ Project builds without errors
3. ✅ No new compiler warnings
4. ✅ Shader compilation works
5. ✅ Runtime testing in Skyrim SE/AE/VR

## Code Analysis Summary

### Files Using CommonLib APIs (Verified Compatible)
- `src/TruePBR/BSLightingShaderMaterialPBR.cpp` - Uses BSLightingShaderMaterialBase methods
- `src/TruePBR/BSLightingShaderMaterialPBRLandscape.cpp` - Uses BSLightingShaderMaterialBase methods  
- `src/Features/{SkySync,WeatherPicker,Skylighting}.cpp` - Uses TESDataHandler, MapMenu APIs

### Changes in CommonLib Update
- BSLightingShaderMaterialBase: Methods remain compatible (temporary removal/re-add in same form)
- Offsets.h removed: Not used by this codebase
- TESDataHandler/MapMenu refactoring: Internal only, public APIs unchanged
- Build deps: Upgraded to fmt 12.0.0, spdlog 1.16.0

### Expected Result
✅ Should build successfully with no code changes required

## Next Steps
1. Run build on Windows runner or local Windows development environment
2. If build succeeds, merge PR
3. If build fails, document error and fix or report upstream to https://github.com/alandtse/CommonLibVR/issues
