# Development Documentation

## Getting Started

-   **[VSCode Setup](./vscode-setup.md)** - IDE configuration, extensions, and auto-deploy
-   **[Shader Workflow](./shader-workflow.md)** - Fast shader iteration and deployment

## Quick Links

### Common Tasks

-   **Fast shader deployment:** `cmake --build build/ALL --target COPY_SHADERS`
-   **Full build with deployment:** `.\BuildRelease.bat ALL-WITH-AUTO-DEPLOYMENT`
-   **Run tests:** `cmake --build build/ALL --target run_shader_tests`
-   **Create a worktree with submodules + local preset:** `pwsh ./tools/new-worktree.ps1 -Name my-branch`
-   **Install optional git alias:** `pwsh ./tools/install-worktree-alias.ps1`

### Build Presets

-   `ALL` - Standard build (no auto-deployment)
-   `ALL-WITH-AUTO-DEPLOYMENT` - Build + deploy to game directory
-   `Dev` - Fast iteration preset (recommended for development)

See `CMakePresets.json` for all available presets.

## Worktrees

Use `tools/new-worktree.ps1` when creating a new worktree for development. The script:

-   Creates the worktree under a sibling `<repo>.worktrees/` directory by default
-   Reuses an existing local branch or creates a new one from `HEAD`
-   Runs `git submodule update --init --recursive` in the new worktree
-   Copies `CMakeUserPresets.json` from the main checkout if it exists there
-   Does not overwrite an existing `CMakeUserPresets.json` unless `-ForcePresetCopy` is passed

Examples:

-   `pwsh ./tools/new-worktree.ps1 -Name reproj_fixes`
-   `pwsh ./tools/new-worktree.ps1 -Name vr-debug -StartPoint dev`
-   `pwsh ./tools/new-worktree.ps1 -Name clean-build -NoSubmodules`

If you want a Git-native command, install the optional repo-local alias:

-   `pwsh ./tools/install-worktree-alias.ps1`
-   Then use `git new-worktree reproj_fixes`

The alias is installed into local Git config by default, so it does not affect other users unless they opt in.

## Build Targets

| Target             | Builds DLL | Runs Tests | Copies Shaders | Use Case               |
| ------------------ | ---------- | ---------- | -------------- | ---------------------- |
| `COPY_SHADERS`     | ❌         | ❌         | ✅             | Fast shader iteration  |
| `DEPLOY_ALL`       | ✅         | ✅         | ✅             | Full deployment (auto) |
| `prepare_shaders`  | ❌         | ✅         | ✅ (AIO only)  | CI shader validation   |
| `run_shader_tests` | ❌         | ✅         | ❌             | Test shaders only      |

## Contributing

When adding new features or documentation, please keep development docs organized under `docs/development/`.
