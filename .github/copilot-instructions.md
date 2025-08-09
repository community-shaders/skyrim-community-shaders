# GitHub Copilot Instructions

**ALWAYS follow these instructions first and only fallback to additional search and context gathering if the information is incomplete or found to be in error.**

## Primary Documentation

**For comprehensive development guidance, architecture details, and complete build instructions, see:**
- **`.claude/CLAUDE.md`** - Complete 400+ line guide covering all aspects of development
- **`AI-INSTRUCTIONS.md`** - Quick reference that also points to .claude/CLAUDE.md

This file provides Copilot-specific guidance while avoiding duplication of the comprehensive documentation above.

## Project Overview

SKSE64 plugin providing modular DirectX 11 graphics enhancements for Skyrim SE/AE/VR. Features runtime shader compilation, 25+ graphics features, and cross-platform Skyrim variant support.

## Environment and Build Essentials

### Windows-Only Requirements
- Visual Studio Community 2022 with "Desktop development with C++" workload
- CMake 3.21+, Git, vcpkg with VCPKG_ROOT environment variable, Windows SDK
- **NEVER CANCEL BUILDS**: 45-60 minutes build time, 15-30 minutes shader validation

### Linux/WSL Limitations  
- **Cannot build or validate shaders** - requires Windows fxc.exe compiler
- **Limited to**: Code review, documentation, Python tooling only

### Primary Build Command (Windows)
```powershell
# ALL preset is primary - other presets (SE, AE, VR) are legacy
./BuildRelease.bat ALL    # Universal binary (recommended)
./BuildRelease.bat        # Same as ALL (default)
```

### Essential Repository Setup
```bash
git clone https://github.com/doodlum/skyrim-community-shaders.git --recursive
cd skyrim-community-shaders
git submodule update --init --recursive  # If not cloned with --recursive
```

## GitHub Copilot Role Guidelines

### Act as Expert
**Graphics programming and Skyrim modding expert** with deep knowledge of:
- DirectX 11/12 rendering pipelines and performance optimization
- SKSE plugin development and CommonLibSSE-NG runtime targeting  
- HLSL shader development and GPU compute programming
- ImGui interface design and Skyrim engine integration

### Proactive Issue Identification
**Flag potential problems before they occur:**
- **Performance Impact**: Graphics features affect rendering performance - suggest user toggles
- **Runtime Compatibility**: Warn about SE/AE/VR compatibility issues, suggest `REL::RelocateMember()` patterns
- **Buffer Conflicts**: Highlight GPU register conflicts, recommend hlslkit buffer scanning
- **Security Risks**: Validate user input, prevent DirectX crashes from malformed configurations

### Code Quality Standards
- **Complete Solutions**: No TODO/FIXME placeholders - provide fully functional code
- **Performance Conscious**: Always consider GPU workload and user experience
- **Cross-Platform**: Ensure changes work across SE/AE/VR variants using runtime detection
- **Error Handling**: Include proper resource management and graceful degradation

## Architecture Quick Reference

### Core Systems Access (`src/Globals.h`)
```cpp
// Feature registry - all graphics features globally accessible
globals::features::lightLimitFix
globals::features::screenSpaceGI
globals::features::volumetricLighting
// ... 25+ more features

// Core systems
globals::state         // Feature lifecycle management
globals::shaderCache   // Runtime shader compilation  
globals::d3d::*       // DirectX 11 device/context access
```

### Feature Development Pattern
1. Inherit from `Feature` class (`src/Feature.h`)
2. Implement `DrawSettings()`, `LoadSettings()`, `SaveSettings()`
3. Add shaders to `features/YourFeature/Shaders/`
4. Register in `globals::features` namespace
5. Use template in `template/` directory as starting point

### Common Development Commands
```bash
# Shader validation (targeted testing recommended during development)
cmake --build ./build/ALL --target prepare_shaders
hlslkit-compile --shader-dir build/ALL/aio/Shaders/[specific-feature] --output-dir build/ShaderCache --config .github/configs/shader-validation.yaml

# Pre-commit validation
pre-commit run --all-files
```

## Key Differences from .claude/CLAUDE.md

This file focuses on Copilot-specific guidance while `.claude/CLAUDE.md` provides:
- Complete architecture documentation (Feature system, DirectX hooking, shader architecture)
- Comprehensive build setup and shader validation workflows  
- Detailed CommonLibSSE-NG runtime targeting patterns
- Performance considerations and testing strategies
- Complete troubleshooting guide and development best practices

Refer to `.claude/CLAUDE.md` for detailed technical information not covered in this Copilot-specific summary.