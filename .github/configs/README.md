# Build Configuration Files

This directory contains configuration files used by the CI/CD pipeline for build validation and testing.

## Files

-   `shader-validation.yaml`: Configuration for shader compilation validation using hlslkit (Skyrim SE)
-   `shader-validation-vr.yaml`: VR Configuration for shader compilation validation using hlslkit (Skyrim VR)

## Generating Configuration Files

These configuration files can be regenerated using the `generate-shader-configs.ps1` script in this directory. This script requires:

1. A valid Skyrim installation (SE and/or VR)
2. The [hlslkit](https://github.com/alandtse/hlslkit) package installed (`pip install hlslkit`)
3. Community Shaders to be run once with specific settings to generate the required log data

### Prerequisites

Before running the generation script, you must run each version of Skyrim (SE and VR) **once** with the following Community Shaders settings:

1. **Set Debug Log Level**: In the Community Shaders menu, set the log level to "Debug" or "Trace"
2. **Clear Disk Cache**: Clear the shader disk cache before running
3. **Enable Disk Cache**: Ensure disk cache is enabled and will be saved
4. **Run the Game**: Launch and wait for compilation to complete to generate shader compilation logs

The required log files will be created at:

-   **Skyrim SE**: `%USERPROFILE%\Documents\My Games\Skyrim Special Edition\SKSE\CommunityShaders.log`
-   **Skyrim VR**: `%USERPROFILE%\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`

### Running the Script

```powershell
# From the repository root
.\.github\configs\generate-shader-configs.ps1

# Or from the configs directory
cd .github\configs
.\generate-shader-configs.ps1
```

The script will:

1. Detect available Skyrim installations
2. Check for required log files
3. Generate configuration files using hlslkit
4. Update the files in `.github\configs\`

### Manual Generation

You can also generate the files manually using hlslkit:

```bash
# For Skyrim SE
hlslkit-generate --log "%USERPROFILE%\Documents\My Games\Skyrim Special Edition\SKSE\CommunityShaders.log" --output .\.github\configs\shader-validation.yaml

# For Skyrim VR
hlslkit-generate --log "%USERPROFILE%\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log" --output .\.github\configs\shader-validation-vr.yaml
```

## Usage in CI/CD

These files are automatically used by the GitHub Actions workflows during shader validation. They define:

-   Common shader compilation defines
-   Expected warnings (with suppression)
-   Shader file configurations
-   Compilation parameters

The files should be regenerated when:

-   New shaders are added to the project
-   Shader compilation behavior changes
-   New warnings need to be suppressed
-   Build configurations are modified
