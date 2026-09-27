# AI Development Instructions

This file provides guidance for AI assistants working with the Skyrim Community Shaders codebase.

## Primary Documentation

**For development guidance, see `.claude/CLAUDE.md`** which covers:

-   Build wrappers and shader validation
-   Feature layout and SE/AE runtime targeting (CommonLibSSE-NG)
-   Feature release stages (Alpha / Beta / Unreleased) and versioning
-   Project conventions (D3D11 resource naming, UI constants, i18n)
-   Commit and branch rules

## Quick Reference

### Project Type

SKSE plugin providing advanced DirectX 11 graphics modifications for Skyrim SE/AE.

### Essential Commands

-   **Build**: `./BuildRelease.bat [PRESET]` (WSL: use `powershell.exe -Command`)
-   **Shader Test**: `hlslkit-compile --shader-dir [target]` (install via pip first)
-   **Feature Access**: `globals::features::*` namespace

### Build Options

**Presets**: `ALL` (universal SE/AE, default), `ALL-VS2022`, `ALL-DEBUG`, `Dev-Fast`, `PR` (see `CMakePresets.json`)

**CMake Options** (set in user preset):

-   `AUTO_PLUGIN_DEPLOYMENT=ON` - Auto-copy to `CommunityShadersOutputDir`
-   `ZIP_TO_DIST=ON` (default) - Create individual feature 7z packages
-   `AIO_ZIP_TO_DIST=ON` (default) - Create all-in-one 7z package
-   `TRACY_SUPPORT=ON` - Enable Tracy profiler integration

### Custom CMake Targets

**Quick targets** (common):

-   `PREPARE_AIO`, `prepare_shaders`, `COPY_SHADERS`, `AIO_ZIP_PACKAGE`
-   `FORMAT_CODE`, `generate_shader_configs`

For full details about manual packaging targets (Package-Core, Package-AIO-Manual, Package-<Feature>, AIO) and example workflows, see the "Build Instructions" section in `README.md` to avoid duplication.

### AI Assistant Role

**Act as an experienced graphics programming and Skyrim modding expert.**

**Key Focus**: Performance impact awareness, runtime compatibility (SE/AE), complete working solutions, DirectX/HLSL best practices.

For detailed explanations, examples, and comprehensive guidance, refer to `.claude/CLAUDE.md`.
