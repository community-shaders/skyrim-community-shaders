# VSCode Development Setup

## Quick Start

1. Install recommended extensions
2. Configure auto-deploy (optional)

## Recommended Extensions

### Required for Shader Development

-   **HLSL Tools** (`TimGJones.hlsltools`)
    -   Syntax highlighting and IntelliSense for HLSL
-   **CMake Tools** (`ms-vscode.cmake-tools`)
    -   Build system integration

### Optional for Productivity

-   **Run On Save** (`emeraldwalk.RunOnSave`)
    -   Auto-deploy shaders when saving files
    -   See configuration below

## Auto-Deploy on Save (Optional)

Automatically deploy shaders when you save `.hlsl` or `.hlsli` files.

**Setup:**

1. Install extension: `ext install emeraldwalk.RunOnSave`
2. Create `.vscode/settings.json` in the repository root (if it doesn't exist)
3. **Add the following** to your `.vscode/settings.json`:
    ```json
    {
        "emeraldwalk.runonsave": {
            "commands": [
                {
                    "match": "\\.(hlsl|hlsli)$",
                    "notMatch": "[\\\\\\/](build|extern|dist|Tests)[\\\\\\/]",
                    "isAsync": true,
                    "cmd": "cmake --build ${workspaceFolder}/build/ALL-WITH-AUTO-DEPLOYMENT --target COPY_SHADERS",
                    "runIn": "terminal",
                    "runningStatusMessage": "Deploying shaders...",
                    "finishStatusMessage": "Shaders deployed!"
                }
            ]
        }
    }
    ```
4. Run `cmake --preset ALL-WITH-AUTO-DEPLOYMENT` once to create the build directory
5. Save any shader → Auto-deploys in seconds!

**Note:** If the build directory doesn't exist, you'll see an error in the terminal. Just run the preset command and try again.

**How it works:**

-   Triggers `COPY_SHADERS` target on shader file saves
-   Only copies shaders (no DLL build, no tests)
-   Excludes build artifacts and test files
-   Shows status messages in VSCode

**Interaction with built-in filewatcher:**

Community Shaders has a built-in filewatcher (**Settings → Advanced → Shader Compilation → Enable File Watcher**) that hot-reloads shaders when files change in the game's `Data/Shaders/` directory. The workflow is:

1. Edit shader in VSCode
2. Save → RunOnSave deploys to `Data/Shaders/`
3. Built-in filewatcher detects change → Recompiles shader in-game
4. See results immediately without restarting Skyrim!

This provides near-instant iteration: edit → save → see changes in seconds.

**To disable auto-deploy:** Remove or comment out the `emeraldwalk.runonsave` section in your local `.vscode/settings.json`

## Customization

Your `.vscode/settings.json` is gitignored, so you can customize it without affecting other developers:

-   Adjust auto-deploy behavior
-   Add your own file associations
-   Configure workspace-specific settings

## See Also

-   [Shader Development Workflow](./shader-workflow.md) - Manual and automated shader deployment
