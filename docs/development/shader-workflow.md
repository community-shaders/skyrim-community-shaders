# Shader Development Workflow

## Quick Reference

```bash
# Fast shader-only deployment (recommended for dev iteration)
cmake --build build/ALL --target COPY_SHADERS

# Full deployment (DLL + tests + shaders)
cmake --build build/ALL --target DEPLOY_ALL

# Prove an HLSL refactor changed no behavior (compares compiled DXBC vs a git ref)
pwsh tools/verify-shader-refactor.ps1 package/Shaders/Foo.hlsl   # or tools/verify-shader-refactor.sh
```

## Verifying refactors

`tools/verify-shader-refactor.ps1` (bash wrapper: `tools/verify-shader-refactor.sh`)
compiles a shader from a base git ref and from the working tree across the
`VR` × `HDR_OUTPUT` permutations, then compares the compiled bytecode. The base
ref's whole include tree is materialized (via `git archive`), so the base compiles
against base-ref `.hlsli` headers and the working tree against working headers — a
refactor that also edits a shared header is compared correctly, not masked:

-   **IDENTICAL** SHA-256 of the `.cso` ⇒ the refactor is a provable no-op (fxc emits
    no timestamps without `/Zi`, so identical source ⇒ identical bytes).
-   **DIFFERS** ⇒ it dumps and diffs the `/Fc` assembly so a legitimate-but-non-identical
    change can be reviewed.

Exit codes: `0` all identical, `2` some differ, `1` compile error. Defaults to comparing
the working tree against `merge-base(HEAD, origin/dev)`; pass `-BaseRef <ref>` to override.
Requires `fxc.exe` from the Windows SDK. The permutation sweep is strong evidence, not the
full `shader-validation.yaml` matrix — pass `-Permutations` for exotic define combos.

## Overview

Two deployment targets for different workflows:

-   **`COPY_SHADERS`** - Fast shader-only deployment (seconds)
-   **`DEPLOY_ALL`** - Full build + tests + deployment (minutes)

### Requirements

-   Must have `AUTO_PLUGIN_DEPLOYMENT=ON` in your CMake preset
-   Must have `CommunityShadersOutputDir` environment variable set to your Skyrim directory

### Usage

#### Manual

```bash
# Fast iteration: Only copy changed shaders to game directory
cmake --build build/ALL --target COPY_SHADERS

# Or in Visual Studio: Right-click "COPY_SHADERS" target -> Build

# Full deployment (same as running cmake --build with no target):
cmake --build build/ALL --target DEPLOY_ALL
```

#### Automatic (VSCode)

You can configure VSCode to automatically deploy shaders when you save `.hlsl` or `.hlsli` files using the [RunOnSave](https://marketplace.visualstudio.com/items?itemName=emeraldwalk.RunOnSave) extension.

**See [VSCode Setup](../development/vscode-setup.md) for complete configuration instructions.**

### Prerequisites

1. Run `cmake --preset ALL-WITH-AUTO-DEPLOYMENT` at least once to create build directory
2. Set `CommunityShadersOutputDir` environment variable to your Skyrim `Data` directory
3. Ensure `AUTO_PLUGIN_DEPLOYMENT=ON` in your CMake preset

### What COPY_SHADERS does now

1. ✅ Transforms shaders from source layout → game layout (via AIO staging)
2. ✅ Copies only changed shader files (incremental robocopy)
3. ✅ Deploys to `$CommunityShadersOutputDir/Shaders`
4. ❌ Does NOT build the DLL
5. ❌ Does NOT run shader tests
6. ❌ Does NOT deploy non-shader files

### Target Comparison

| Target            | Builds DLL | Runs Tests | Copies Shaders | Use Case              |
| ----------------- | ---------- | ---------- | -------------- | --------------------- |
| `COPY_SHADERS`    | ❌         | ❌         | ✅             | Fast shader iteration |
| `DEPLOY_ALL`      | ✅         | ✅         | ✅             | Full deployment       |
| `prepare_shaders` | ❌         | ✅         | ✅ (AIO only)  | CI validation         |
