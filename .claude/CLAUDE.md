# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

### WSL/Linux Environment Note

This is a Windows-specific project requiring Visual Studio and Windows SDK. If working in WSL, use PowerShell to execute build commands:

```bash
# In WSL, use powershell.exe to run Windows commands
powershell.exe -Command "./BuildRelease.bat [PRESET_NAME]"
```

### Primary Build Command

```bash
./BuildRelease.bat [PRESET_NAME]
```

**Available Presets** (from CMakePresets.json):

-   `ALL` (default) - Builds for SE/AE/VR in single binary
-   `SE` - Skyrim Special Edition only
-   `AE` - Anniversary Edition only
-   `VR` - Skyrim VR only
-   `ALL-TRACY` - Includes Tracy profiler support
-   `ALL-WITH-AUTO-DEPLOYMENT` - Auto-deploys to configured Skyrim directories when template used.

### Development Setup

1. Copy `CMakeUserPresets.json.template` → `CMakeUserPresets.json`
2. Configure `CommunityShadersOutputDir` for auto-deployment to Skyrim installations
3. Set build options in user preset:
    - `AUTO_PLUGIN_DEPLOYMENT`: Auto-copy to Skyrim dirs
    - `AIO_ZIP_TO_DIST`: Creates all-in-one distribution package
    - `ZIP_TO_DIST`: Creates individual feature packages
    - `TRACY_SUPPORT`: Enables performance profiling

### Shader Development and Testing

```bash
# Install hlslkit (external dependency)
pip install git+https://github.com/alandtse/hlslkit.git

# Prepare shaders for validation (builds shader directory structure)
cmake --build ./build/ALL --target prepare_shaders

# Full shader suite validation (can be time-consuming)
hlslkit-compile --shader-dir build/ALL/aio/Shaders --output-dir build/ShaderCache --config .github/configs/shader-validation.yaml --max-warnings 0 --suppress-warnings X1519

# VR-specific validation
hlslkit-compile --shader-dir build/ALL/aio/Shaders --output-dir build/ShaderCache --config .github/configs/shader-validation-vr.yaml --max-warnings 0 --suppress-warnings X1519

# Targeted testing for faster development (recommended during development)
# Test specific base shader
hlslkit-compile --shader-dir build/ALL/aio/Shaders/Lighting.hlsl --output-dir build/ShaderCache --config .github/configs/shader-validation.yaml

# Test specific compute shader
hlslkit-compile --shader-dir build/ALL/aio/Shaders/DeferredCompositeCS.hlsl --output-dir build/ShaderCache --config .github/configs/shader-validation.yaml

# Test specific feature directory
hlslkit-compile --shader-dir build/ALL/aio/Shaders/ScreenSpaceGI/ --output-dir build/ShaderCache --config .github/configs/shader-validation.yaml

# Test feature-specific compute shader
hlslkit-compile --shader-dir build/ALL/aio/Shaders/LightLimitFix/ClusterBuildingCS.hlsl --output-dir build/ShaderCache --config .github/configs/shader-validation.yaml

# Generate shader defines from game log (requires CommunityShaders.log from game)
hlslkit-generate-defines --log CommunityShaders.log

# Scan for buffer conflicts across features
hlslkit-buffer-scan --features-dir features/
```

## Architecture Overview

### Plugin Architecture

**Core Pattern**: Feature-driven modular system where each graphics enhancement is an independent `Feature` class that can be enabled/disabled at runtime.

**Key Classes**:

-   `Feature` (src/Feature.h) - Base class for all graphics features
-   `State` (src/State.h) - Global singleton managing feature lifecycle
-   `ShaderCache` (src/ShaderCache.h) - Runtime shader compilation and caching
-   `Menu` (src/Menu.h) - ImGui-based in-game configuration interface

### Feature Implementation Pattern

Each feature follows consistent structure:

1. **C++ Implementation**: `src/Features/FeatureName.cpp/h` inheriting from `Feature`
2. **Shader Assets**: `features/FeatureName/Shaders/` containing HLSL shaders
3. **Configuration**: `features/FeatureName/Shaders/Features/FeatureName.ini` with versioned settings
4. **Core Features**: Features with `CORE` marker file bundle with main mod

### DirectX Integration

**Hooking System**: Uses Detours library to intercept DirectX 11 API calls in `src/Hooks.cpp`
**Deferred Rendering**: Custom deferred pipeline in `src/Deferred.cpp` with feature integration points
**Shader Management**: Runtime compilation with include system (`package/Shaders/Common/`) for shared utilities
**Base Shader Library**: `package/Shaders/` contains Skyrim's core rendering shaders (Lighting.hlsl, Water.hlsl, Sky.hlsl, etc.)

### Cross-Platform Support

**Single Binary**: Supports SE/AE/VR through CommonLibSSE-NG runtime detection
**VR Adaptations**: Specialized rendering paths in `src/Features/VR/`
**API Abstraction**: Dual DirectX 11 support with feature-specific rendering strategies

## Critical Dependencies

### CommonLibSSE-NG (`extern/CommonLibSSE-NG`)

**Essential reverse engineering library** providing reverse-engineered interfaces to interact with Skyrim's game engine safely.

**Core Functionality**:

-   **Game Object Access**: RE namespace with Skyrim's internal classes and structures
-   **Memory Management**: Safe access to game memory with proper lifetime management
-   **Event System**: Hook into Skyrim's event dispatching (rendering, input, etc.)
-   **Address Library Integration**: Runtime address resolution for different game versions

**Key Namespaces**:

-   `RE::` - Skyrim game objects and classes (BSShader, TESObjectREFR, etc.)
-   `REL::` - Relative addressing and version management
-   `SKSE::` - SKSE plugin interfaces and utilities

### Runtime Targeting System

CommonLibSSE-NG supports multiple Skyrim versions through sophisticated runtime targeting. Further information is available at https://github.com/CharmedBaryon/CommonLibSSE-NG/wiki/Runtime-Targeting

**Build Presets**:

-   `SE` - Skyrim Special Edition only
-   `AE` - Anniversary Edition only
-   `VR` - Skyrim VR only
-   `ALL` - Multi-runtime support (default for this project)

**Compile-Time vs Runtime Patterns**:

**Single Runtime (compile-time)**: When targeting one version, `#ifdef ENABLE_SKYRIM_VR` conditionally compiles VR-specific code:

```cpp
#ifdef ENABLE_SKYRIM_VR
    virtual void Unk_09(UI_MENU_Unk09 a_unk);  // VR-only vfunc
#endif
```

**Multi-Runtime (runtime detection)**: When targeting ALL, uses runtime accessors:

```cpp
// Runtime member access with different offsets per version
auto& GetRuntimeData() {
    return REL::RelocateMemberIfNewer<PLAYER_RUNTIME_DATA>(
        SKSE::RUNTIME_SSE_1_6_629, this, 0x3D8, 0x3E0);
}

// VR-specific runtime data (only exists in VR)
auto& GetVRRuntimeData() {
    return REL::RelocateMember<VR_PLAYER_RUNTIME_DATA>(this, 0, 0x3D8);
}

// Runtime detection
if (REL::Module::IsVR()) {
    // VR-specific code path
}
```

**Key Runtime Utilities**:

-   `REL::RelocateMember<T>()` - Access members with different offsets
-   `REL::RelocateVirtual<T>()` - Call virtual functions with variant vtables
-   `REL::Module::IsVR()`, `IsAE()`, `IsSE()` - Runtime version detection
-   `REL::RelocationID()` - Dynamic address resolution based on version

**Critical for Development**: When modifying classes that inherit from game objects, always check if they have runtime-specific variations and use appropriate accessor patterns.

## Core Architecture

### Global System (`src/Globals.h`)

Central coordination point providing access to all major subsystems:

**Core Systems**:

-   `globals::state` - Main plugin state and feature lifecycle management
-   `globals::deferred` - Deferred rendering pipeline coordinator
-   `globals::menu` - ImGui-based in-game configuration interface
-   `globals::shaderCache` - Runtime shader compilation and caching

**Graphics Integration**:

-   `globals::d3d::*` - DirectX 11 device, context, and swapchain access
-   `globals::game::*` - Skyrim graphics state (shadowState, renderer, shaders)
-   `globals::upscaling` - FidelityFX and Streamline integration
-   `globals::dx12SwapChain` - DirectX 12 support for advanced features

**Feature Registry** (`globals::features::`):
All graphics features are globally accessible for cross-feature coordination:

-   Lighting: `lightLimitFix`, `volumetricLighting`, `skylighting`, `ibl`
-   Terrain: `terrainShadows`, `terrainBlending`, `terrainVariation`, `terrainHelper`
-   Materials: `extendedMaterials`, `hairSpecular`, `subsurfaceScattering`
-   Effects: `screenSpaceGI`, `screenSpaceShadows`, `waterEffects`, `wetnessEffects`
-   Environment: `cloudShadows`, `dynamicCubemaps`, `weatherPicker`, `skySync`
-   VR: `vr` - VR-specific adaptations and coordinate transformations

### Shared Utilities (`src/Utils/`)

Common functionality organized by domain:

-   `UI.h/cpp` - ImGui utilities, input mapping, and UI helper functions
-   `D3D.h/cpp` - DirectX utilities and helper functions
-   `Game.h/cpp` - Skyrim-specific game state and object utilities
-   `VRUtils.h/cpp` - VR-specific utilities and coordinate transformations
-   `FileSystem.h/cpp` - File I/O and path manipulation helpers
-   `Format.h/cpp` - String formatting and conversion utilities
-   `Serialize.h/cpp` - JSON serialization helpers

### Shader Architecture

**Base Shader Library** (`package/Shaders/`):

-   **Core Rendering**: `Lighting.hlsl`, `Water.hlsl`, `Sky.hlsl`, `Particle.hlsl` - Skyrim's main rendering pipeline
-   **Image Space Effects**: `IS*.hlsl` files - Post-processing effects (blur, depth of field, volumetric lighting)
-   **Compute Shaders**: `*CS.hlsl` files - GPU parallel processing (deferred composite, ambient composite)
-   **Common Utilities**: `Common/` directory with shared includes (BRDF.hlsli, Math.hlsli, GBuffer.hlsli)

**Feature Shaders** (`features/*/Shaders/`):

-   **Feature-Specific**: Each feature has its own shader directory (e.g., `ScreenSpaceGI/`, `LightLimitFix/`)
-   **Compute-Heavy Features**: Many use compute shaders for performance (ClusterBuildingCS.hlsl, gi.cs.hlsl)
-   **Include Integration**: Features can use shared utilities from `package/Shaders/Common/`

### Menu System

Modular ImGui-based configuration interface with specialized renderers for different UI sections and centralized constants in `ThemeManager::Constants`.

## Feature Development Workflow

### Adding New Features

1. Use template in `template/` directory as starting point
2. Implement `Feature` interface with required methods:
    - `DrawSettings()` - ImGui configuration UI with performance impact warnings
    - `LoadSettings()` - JSON deserialization
    - `SaveSettings()` - JSON serialization
    - Feature-specific rendering hooks with performance considerations
3. Add shader files to `features/NewFeature/Shaders/` with compute shader optimization
4. Create versioned `.ini` configuration file with performance-related settings
5. Register feature in appropriate source files and `globals::features`
6. **Performance Testing**: Measure GPU impact and provide user toggles for heavy features

### Testing and Validation

-   **Shader Compilation**: Use hlslkit tools for validation before commit
-   **Buffer Conflicts**: Run buffer_scan.py to detect register conflicts
-   **Integration Testing**: Build and test in-game with various Skyrim editions
-   **A/B Testing**: Use built-in A/B testing framework for performance comparisons

### Version Management

Feature versions are automatically extracted from `.ini` files and compiled into `FeatureVersions.h` at build time for backward compatibility checking.

## Key Development Patterns

### Memory Management

-   Modern C++23 with RAII principles
-   Smart pointers for automatic resource management
-   Thread pool (bshoshany-thread-pool) for parallel operations

### Configuration System

-   JSON-based settings with nlohmann_json
-   Hot-reload capability through ImGui interface
-   Versioned feature configurations for compatibility

### Error Handling

-   **Comprehensive Logging**: Integrated with SKSE logging system with different severity levels
-   **Graceful Degradation**: Features should disable cleanly on shader compilation failures
-   **User-Friendly Errors**: Report errors through ImGui interface with actionable guidance
-   **Graphics-Specific Errors**: Handle DirectX device lost scenarios and shader compilation failures
-   **Recovery Mechanisms**: Provide fallback rendering paths when advanced features fail
-   **Error Context**: Include relevant graphics state (current shader, buffer sizes) in error messages

### Performance Considerations

**Runtime Graphics Performance** (Critical for Skyrim gameplay):

-   **Deferred Rendering Impact**: Features hook into Skyrim's rendering pipeline, adding GPU workload
-   **Feature Toggles**: Users can disable individual features at boot if performance is impacted (`Disable at Boot` buttons)
-   **A/B Testing Framework**: Built-in performance comparison system for measuring feature impact
-   **VR Performance**: VR has higher performance requirements; some features may need different settings
-   **Tracy Profiler**: Optional build-time integration (`TRACY_SUPPORT`) for detailed performance analysis

**Shader Performance Patterns**:

-   **Compute Shaders**: Many features use compute shaders for parallel GPU processing (Screen Space GI, Light Limit Fix)
-   **Buffer Management**: Careful GPU buffer allocation to avoid conflicts and minimize memory transfers
-   **LOD Considerations**: Features should respect Skyrim's LOD system to maintain performance at distance
-   **Resolution Scaling**: Consider how features scale with different rendering resolutions

**Performance Testing**:

-   **In-Game Profiling**: Use Tracy integration to measure actual frame impact
-   **Feature Isolation**: Test features individually to identify performance bottlenecks
-   **Cross-Edition Impact**: SE/AE/VR may have different performance characteristics for the same feature

### Development Performance

-   **Shader Testing**: Full validation suite can be time-consuming; use targeted testing during development
-   **Build Performance**: Multi-threaded compilation with job control (`hlslkit-compile --jobs N`)
-   **Iterative Development**: Test specific shader files/directories rather than entire shader suite

## AI Assistant Guidelines

### Role and Expertise

**Act as an experienced graphics programming and Skyrim modding expert** with deep knowledge of:

-   DirectX 11/12 rendering pipelines and performance optimization
-   SKSE plugin development and Skyrim's game engine internals
-   CommonLibSSE-NG runtime targeting and cross-version compatibility
-   HLSL shader development and GPU compute programming
-   ImGui interface design and user experience considerations

### Constructive Proactivity

**Identify and address issues proactively**:

-   **Performance Concerns**: If code could impact rendering performance, suggest optimizations or user toggles
-   **Security Risks**: Flag potential crashes from unvalidated user input, malformed configs, or unsafe DirectX operations
-   **Runtime Compatibility**: Warn when code might break SE/AE/VR compatibility or suggest `REL::RelocateMember()` patterns
-   **Buffer Conflicts**: Highlight potential GPU register conflicts and recommend hlslkit buffer scanning
-   **Graphics Best Practices**: Suggest more idiomatic DirectX/HLSL patterns when appropriate

**Implementation Standards**:

-   Provide complete, working solutions rather than TODO/FIXME placeholders
-   Explain reasoning for graphics/performance-related changes
-   Consider the full rendering pipeline impact of modifications
-   Always include necessary error handling for graphics operations

### Code Quality Expectations

-   **No Placeholders**: Never include TODO, FIXME, or incomplete implementations unless explicitly requested for planning
-   **Complete Solutions**: Provide fully functional code with proper error handling and resource management
-   **Performance Conscious**: Always consider GPU workload and user experience impact
-   **Documentation**: Include Doxygen comments for public methods, especially graphics-related functions

## Development Best Practices (Learned from Codebase)

### Commit Message Standards

Follow conventional commit format for consistency:

-   **Format**: `type(scope): description`
-   **Title Limit**: 50 characters maximum
-   **Body Wrap**: 72 characters per line
-   **Types**: `feat`, `fix`, `refactor`, `docs`, `style`, `test`, `chore`
-   **Examples**:
    -   `feat(menu): extract DrawMenuVisitor helper methods`
    -   `fix(imgui): resolve orphaned TableNextColumn calls`
    -   `refactor(constants): centralize UI constants in ThemeManager`

### Code Organization and Refactoring Patterns

-   **Extract Large Functions**: Functions over ~200 lines should be broken into focused helper methods (see `FeatureListRenderer::DrawMenuVisitor` refactoring)
-   **Centralize Constants**: Magic numbers should be extracted to named constants in appropriate classes (see `ThemeManager::Constants`)
-   **Modular UI Design**: UI components should be separated by responsibility (Menu system uses HeaderRenderer, FeatureListRenderer, etc.)

### ImGui Integration Patterns

-   **Table API Compliance**: Always pair `ImGui::BeginTable()` with `ImGui::EndTable()` - orphaned `TableNextColumn()` calls will cause issues
-   **Style Management**: Use RAII pattern for ImGui style changes; avoid save/restore without actual modifications
-   **Consistent Spacing**: Use centralized constants for UI spacing and padding rather than hardcoded values

### Menu System Development

-   **Callback Pattern**: Use callbacks to access private methods from extracted UI components rather than making methods public
-   **State Management**: UI state should be managed centrally in Menu class, with components receiving state as parameters
-   **Documentation Standards**: Use Doxygen comments for all public methods, especially extracted utilities

### Shader Development Workflow

-   **Build Before Test**: Always run `cmake --build ./build/ALL --target prepare_shaders` before shader validation
-   **Targeted Testing**: Use specific shader/directory paths with hlslkit-compile during development to avoid full suite delays
-   **Performance Optimization**: Use `--jobs`, `--strip-debug-defines`, and `--optimization-level` flags for faster compilation
-   **Validation Early**: Use hlslkit validation in development, not just CI, to catch issues early

### Testing and Validation

-   **Build Verification**: Always test builds after significant refactoring - this codebase has complex dependencies
-   **Cross-Edition Testing**: Changes may affect SE/AE/VR differently due to engine differences
-   **Memory Management**: Pay attention to smart pointer usage and RAII patterns when modifying existing code

### Security and Input Validation

-   **Configuration Files**: Always validate `.ini` files and user settings - malformed configurations can crash Skyrim
-   **Shader Input Validation**: Validate shader parameters and buffer sizes to prevent GPU driver crashes
-   **File Path Validation**: Sanitize file paths for texture/asset loading to prevent directory traversal
-   **Memory Safety**: Use bounds checking for buffer operations, especially with DirectX resource management
-   **Resource Limits**: Enforce reasonable limits on user-configurable values (texture sizes, buffer counts, etc.)

### Code Quality Standards

-   **Descriptive Naming**: Use domain-specific names that clearly indicate graphics/rendering purpose
    -   `screenSpaceAmbientOcclusion` not `ssao`
    -   `UpdateShadowCascades()` not `UpdateSC()`
-   **Single Responsibility**: Each feature class should handle one graphics technique only
-   **Function Complexity**: Keep rendering functions focused; extract complex GPU operations into separate methods
-   **Resource Management**: Always pair graphics resource creation with proper cleanup (RAII)

### Common Pitfalls to Avoid

-   **Include Dependencies**: New features often require adding includes (ShaderCache.h, imgui_stdlib.h, etc.)
-   **Forward Declarations**: Use forward declarations in headers when possible, full includes in .cpp files
-   **VR Considerations**: VR has different rendering requirements - check VR-specific code paths when modifying graphics features
-   **Feature Versioning**: Feature .ini files use semantic versioning - increment appropriately when changing settings structure
-   **Performance Impact**: Always consider GPU workload when adding new rendering features - provide toggle options for users
-   **Buffer Conflicts**: Check hlslkit buffer scanning to avoid GPU register conflicts that cause rendering issues
-   **Graphics State Corruption**: Minimize DirectX state changes; restore state after modifications
-   **Thread Safety**: Graphics operations must consider Skyrim's rendering thread vs game logic thread
