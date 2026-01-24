# Development Documentation

## Getting Started

-   **[VSCode Setup](./vscode-setup.md)** - IDE configuration, extensions, and auto-deploy
-   **[Shader Workflow](./shader-workflow.md)** - Fast shader iteration and deployment

## Quick Links

### Common Tasks

-   **Fast shader deployment:** `cmake --build build/ALL-WITH-AUTO-DEPLOYMENT --target COPY_SHADERS`
-   **Full build with deployment:** `.\BuildRelease.bat ALL-WITH-AUTO-DEPLOYMENT`
-   **Run tests:** `cmake --build build/ALL --target run_shader_tests`

### Build Presets

-   `ALL` - Standard build (no auto-deployment)
-   `ALL-WITH-AUTO-DEPLOYMENT` - Build + deploy to game directory
-   `Dev` - Fast iteration preset (recommended for development)

See `CMakePresets.json` for all available presets.

## Build Targets

| Target             | Builds DLL | Runs Tests | Copies Shaders | Use Case               |
| ------------------ | ---------- | ---------- | -------------- | ---------------------- |
| `COPY_SHADERS`     | ❌         | ❌         | ✅             | Fast shader iteration  |
| `DEPLOY_ALL`       | ✅         | ✅         | ✅             | Full deployment (auto) |
| `prepare_shaders`  | ❌         | ✅         | ✅ (AIO only)  | CI shader validation   |
| `run_shader_tests` | ❌         | ✅         | ❌             | Test shaders only      |

## Contributing

When adding new features or documentation, please keep development docs organized under `docs/development/`.
