# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Commands for Development

### Building the Project

-   **Primary build command**: `.\BuildRelease.bat` - Builds the entire project with default settings
-   **Custom preset**: `.\BuildRelease.bat <preset-name>` - Builds with specific CMake preset (e.g., `ALL-WITH-AUTO-DEPLOYMENT`)
-   **CMake presets**: Use `cmake --list-presets` to see available presets
-   **Clean build**: Remove the `build/` folder before running BuildRelease.bat if switching presets

### Build Configuration Options (CMakeUserPresets.json)

-   `AUTO_PLUGIN_DEPLOYMENT`: When ON, automatically deploys to paths specified in `CommunityShadersOutputDir` environment variable
-   `AIO_ZIP_TO_DIST`: Creates all-in-one archive in `/dist` folder
-   `ZIP_TO_DIST`: Creates individual feature archives in `/dist` folder
-   `TRACY_SUPPORT`: Enables Tracy profiler support (may require clean build when toggled)

### Linting and Formatting

-   **Format code**: `cmake --build --preset=ALL --target FORMAT_CODE` (requires clang-format in PATH)
-   **Shader validation**: Use GitHub workflows or hlslkit directly
-   **Pre-commit hooks**: Repository uses pre-commit.ci for automated checks

### Testing and Validation

-   **Shader compilation validation**: Automated via GitHub Actions for both Flatrim and VR configurations
-   **Feature version audit**: `python tools/feature_version_audit.py` - Audits feature versions and metadata
-   **PR validation**: Run `python tools/feature_version_audit.py --pr-check` to check only changed features

## Project Architecture

### Core Structure

This is an SKSE (Skyrim Script Extender) plugin that provides a shader framework for community-driven graphics modifications for Skyrim SE/AE and VR.

### Key Components

#### Main Plugin Architecture (`src/`)

-   **XSEPlugin.cpp**: Main SKSE plugin entry point and initialization
-   **Hooks.cpp/h**: Core game engine hooks and interception points
-   **ShaderCache.cpp/h**: Manages compiled shader caching and loading
-   **Deferred.cpp/h**: Deferred rendering pipeline management
-   **State.cpp/h**: Game state tracking and management
-   **Menu.cpp/h**: In-game configuration interface

#### Feature System (`src/Features/` & `features/`)

-   Each feature is implemented as a C++ class in `src/Features/` with corresponding shaders in `features/`
-   Features use a standardized interface with version tracking via `.ini` files
-   **Core vs. Non-Core Features**: Core features (marked with `IsCore() { return true; }`) are bundled with base mod
-   **Feature Structure**:
    -   `src/Features/FeatureName.cpp/.h` - C++ implementation
    -   `features/Feature Name/Shaders/` - HLSL shaders and configuration
    -   `features/Feature Name/Shaders/Features/FeatureName.ini` - Version and metadata

#### Shader System (`package/Shaders/`)

-   **Common Libraries**: Shared HLSL utilities in `package/Shaders/Common/`
-   **Base Shaders**: Core game shader modifications in `package/Shaders/`
-   **Feature Shaders**: Individual feature shaders in `features/*/Shaders/`

#### Advanced Graphics Features

-   **TruePBR**: Physically-based rendering implementation
-   **Upscaling**: Integration with DLSS, FSR, and XeSS via Streamline
-   **FidelityFX**: AMD FidelityFX SDK integration for various effects
-   **Frame Generation**: DLSS Frame Generation support

### Key Design Patterns

#### Feature Registration and Management

-   Features inherit from base `Feature` class with standardized lifecycle methods
-   Automatic feature discovery and version tracking via CMake configuration generation
-   Runtime feature enabling/disabling based on user configuration

#### Shader Compilation and Caching

-   Dynamic shader compilation with macro-based feature toggling
-   Persistent shader cache with validation and hot-reload support
-   Multi-platform shader validation (SE/AE vs VR) via CI/CD

#### Memory and Resource Management

-   Custom buffer management for GPU resources
-   Integration with game's rendering pipeline without breaking compatibility
-   Careful hooking to avoid conflicts with other mods

## Development Workflow Guidelines

### Adding New Features

1. **Create feature structure**: Use `template/` directory as starting point
2. **Implement C++ class**: Add to `src/Features/` following existing patterns
3. **Add shaders**: Place in `features/Feature Name/Shaders/` with proper `.ini` version file
4. **Update metadata**: Ensure `GetFeatureModLink()`, `GetFeatureSummary()`, and `GetFeatureDescription()` are implemented
5. **Test compilation**: Run shader validation to ensure compatibility

### Versioning and Releases

-   **Feature Versions**: Use semantic versioning format `major-minor-patch` in `.ini` files
-   **Bump Requirements**: Increment minor version for new features, patch for fixes
-   **Release Process**: Automated via GitHub Actions with comprehensive artifact generation

### CI/CD Integration

-   **Build Validation**: Automatic C++ compilation and shader validation on PRs
-   **Pre-release Generation**: Automatic pre-release builds for PR testing
-   **Feature Auditing**: Automated feature metadata and version validation
-   **Release Automation**: Tag-based releases with comprehensive changelogs

### Code Organization Best Practices

-   **Header Organization**: Keep public interfaces in `.h`, implementation details in `.cpp`
-   **Shader Organization**: Use `.hlsli` for shared code, `.hlsl` for entry points
-   **Feature Independence**: Features should be self-contained and toggleable
-   **Platform Considerations**: Account for differences between SE/AE and VR versions

### External Dependencies

-   **SKSE**: Core dependency for Skyrim modding
-   **CommonLibSSE-NG**: Modern C++ wrapper for SKSE functionality
-   **vcpkg**: Package manager for C++ dependencies
-   **Streamline**: NVIDIA's multi-vendor upscaling framework
-   **FidelityFX**: AMD's graphics effects library

### Important Notes for Development

-   **Never modify base game files directly** - all changes are injected at runtime
-   **Shader compatibility** - ensure shaders compile for both DirectX 11 and target platforms
-   **Memory safety** - use RAII and smart pointers extensively due to runtime injection nature
-   **Feature metadata** - always provide complete metadata for user-facing features to ensure proper documentation generation

## AI Release Notes Instructions

When generating release notes, follow the detailed instructions in `.github/copilot-instructions.md` which includes:

-   User-focused organization by conventional commit types
-   Proper attribution with GitHub handles and PR links
-   Statistics tracking and contributor recognition
-   Structured sections for features, fixes, performance improvements, etc.
