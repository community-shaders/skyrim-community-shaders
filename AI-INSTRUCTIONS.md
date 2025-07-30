# AI Development Instructions

This file provides guidance for AI assistants working with the Skyrim Community Shaders codebase.

## Primary Documentation

**For comprehensive development guidance, see `.claude/CLAUDE.md`** which provides detailed information on:

-   Build commands and development setup
-   Architecture overview and critical dependencies (CommonLibSSE-NG)
-   Runtime targeting system for SE/AE/VR compatibility
-   Core architecture including Globals system and feature registry
-   Shader architecture (base shaders in `package/Shaders/`, feature shaders, compute shader patterns)
-   Development workflows and best practices
-   Common pitfalls and testing requirements

## Quick Reference

### Project Type

SKSE plugin providing advanced DirectX 11 graphics modifications for Skyrim SE/AE/VR.

### Essential Commands

-   **Build**: `./BuildRelease.bat [PRESET]` (WSL: use `powershell.exe -Command`)
-   **Shader Test**: `hlslkit-compile --shader-dir [target]` (install via pip first)
-   **Feature Access**: `globals::features::*` namespace

### AI Assistant Role

**Act as an experienced graphics programming and Skyrim modding expert.**

**Key Focus**: Performance impact awareness, runtime compatibility (SE/AE/VR), complete working solutions, DirectX/HLSL best practices.

For detailed explanations, examples, and comprehensive guidance, refer to `.claude/CLAUDE.md`.
