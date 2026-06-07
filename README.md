[![Latest Release](https://img.shields.io/github/v/release/doodlum/skyrim-community-shaders)](https://github.com/doodlum/skyrim-community-shaders/releases)
[![License](https://img.shields.io/github/license/doodlum/skyrim-community-shaders)](./LICENSE)
[![Last Commit](https://img.shields.io/github/last-commit/doodlum/skyrim-community-shaders)](https://github.com/doodlum/skyrim-community-shaders/commits)
[![Build Status](https://img.shields.io/github/actions/workflow/status/doodlum/skyrim-community-shaders/release-build.yaml?branch=dev)](https://github.com/doodlum/skyrim-community-shaders/actions)
[![Discord](https://img.shields.io/discord/1080142797870485606?label=discord&logo=discord&color=5865F2)](https://discord.com/invite/nkrQybAsyy)
[![Open Issues](https://img.shields.io/github/issues/doodlum/skyrim-community-shaders)](https://github.com/doodlum/skyrim-community-shaders/issues)
[![Contributors](https://img.shields.io/github/contributors/doodlum/skyrim-community-shaders)](https://github.com/doodlum/skyrim-community-shaders/graphs/contributors)
[![Stars](https://img.shields.io/github/stars/doodlum/skyrim-community-shaders?style=social)](https://github.com/doodlum/skyrim-community-shaders/stargazers)

[![Pre-commit CI](https://results.pre-commit.ci/badge/github/doodlum/skyrim-community-shaders/dev.svg)](https://results.pre-commit.ci/latest/github/doodlum/skyrim-community-shaders/dev)
![CodeRabbit Pull Request Reviews](https://img.shields.io/coderabbit/prs/github/doodlum/skyrim-community-shaders?utm_source=oss&utm_medium=github&utm_campaign=doodlum%2Fskyrim-community-shaders&labelColor=171717&color=FF570A&link=https%3A%2F%2Fcoderabbit.ai&label=CodeRabbit+Reviews)

[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/doodlum/skyrim-community-shaders)

# Skyrim Community Shaders

SKSE core plugin for community-driven advanced graphics modifications.

[Nexus](https://www.nexusmods.com/skyrimspecialedition/mods/86492)
[User Wiki](https://modding.wiki/en/skyrim/developers/community-shaders)

## Requirements

-   Any terminal of your choice (e.g., PowerShell)
-   [Visual Studio Community 2026](https://visualstudio.microsoft.com/)
    -   Desktop development with C++
    -   CMake Tools for Windows
    -   HLSL Tools
-   [Git](https://git-scm.com/downloads)
    -   Edit the `PATH` environment variable and add the Git.exe install path as a new value

## Optional Requirements

```
CMake & Vcpkg comes with Visual Studio in Developer Command Prompts already.
Install them manually only if you want them in everywhere.
```

-   [CMake](https://cmake.org/)
    -   No need to install manually if you have Visual Studio CMake Tools installed
    -   CMake 4.2+ is **required** now
    -   Edit the `PATH` environment variable and add the cmake.exe install path as a new value
    -   Instructions for finding and editing the `PATH` environment variable can be found [here](https://www.java.com/en/download/help/path.html)
-   [Vcpkg](https://github.com/microsoft/vcpkg)
    -   Install vcpkg using the directions in vcpkg's [Quick Start Guide](https://github.com/microsoft/vcpkg#quick-start-windows)
    -   After install, add a new environment variable named `VCPKG_ROOT` with the value as the path to the folder containing vcpkg
    -   Make sure your local vcpkg repo matches the commit id specified in `builtin-baseline` in `vcpkg.json` otherwise you might get another version of a non pinned vcpkg dependency causing undefined behaviour

## User Requirements

-   [Address Library for SKSE](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
    -   Needed for SSE/AE

## Build Instructions

### Clone the Repository with submodules

To clone the repository with all submodules, run the following command in your terminal:

```bash
git clone https://github.com/doodlum/skyrim-community-shaders.git --recursive
cd skyrim-community-shaders
```

### Visual Studio build

To build the project, just open `./skyrim-community-shaders` with Visual Studio's "Open Folder" feature. (Ensure you have `CMake Tools for Windows` selected when installing VS)

Follow the prompts to `Configure` and `Build` the project.
It should generate the AIO package in the `./build/ALL/aio` folder by default.

#### Zip package & Optional targets

If you change the `Solution Explorer` into `CMake Targets View`, you can find optional targets to create zip packages for each feature.
Right click on the target and select `Build` to create the zip package in `./dist/`.

### Advanced build with CMake in command line

Open the "Developer PowerShell for VS 2026" or the "x64 Native Tools Command Prompt" (these set up the Visual Studio toolchain for you).

Then from the repository root run:

```pwsh
# Generate the build files (uses the ALL preset)
cmake --preset ALL

# Build using the preset
cmake --build --preset ALL

# Install an AIO package somewhere, e.g. $MOD_FOLDER
cmake --install --preset ALL -- --prefix $MOD_FOLDER
```

# Notes

-   If you prefer to run the VC environment manually, launch Developer PowerShell or the x64 Native Tools prompt instead of calling vcvarsall.bat directly from PowerShell.
-   The convenience wrapper `BuildRelease.bat` also captures these steps.

#### Build a zip package

You can build zip packages for optional cmake targets.
Currently support `AIO_ZIP_PACKAGE`, `Package-AIO-Manual`, `Package-Core`, and `Package-<Feature>`:

```pwsh
# Create a AIO package in ./dist/
# Automated AIO zip (requires AIO_ZIP_TO_DIST=ON)
cmake --build ./build/ALL --config Release --target AIO_ZIP_PACKAGE

# Manual AIO package (install + tar)
cmake --build ./build/ALL --config Release --target Package-AIO-Manual

# Create a CommunityShaders core package in ./dist/
cmake --build ./build/ALL --config Release --target Package-Core

# Create a feature package in ./dist/ (example: GrassLighting)
cmake --build ./build/ALL --config Release --target Package-GrassLighting
```

For more details about packaging targets, options, and the difference between automated and manual packaging, see the "Manual packaging targets (detailed)" section in `.claude/CLAUDE.md`.

#### CMAKE Options (optional)

If you want an example CMakeUserPreset to start off with you can copy the `CMakeUserPresets.json.template` -> `CMakeUserPresets.json`

#### AUTO_PLUGIN_DEPLOYMENT

-   This option is default `"OFF"`
-   Make sure `"AUTO_PLUGIN_DEPLOYMENT"` is set to `"ON"` in `CMakeUserPresets.json`
-   Change the `"CommunityShadersOutputDir"` value to match your desired outputs, if you want multiple folders you can separate them by `;` is shown in the template example

#### TRACY_SUPPORT

-   This option is default `"OFF"`
-   This will enable tracy support, might need to delete build folder when this option is changed

When using custom preset you can call BuildRelease.bat with an parameter to specify which preset to configure eg:
`.\BuildRelease.bat ALL-WITH-AUTO-DEPLOYMENT`

When switching between different presets you might need to remove the build folder

### Build with Docker

For those who prefer to not install Visual Studio or other build dependencies on their machine, this encapsulates it. This uses Windows Containers, so no WSL for now.

1. Install [Docker](https://www.docker.com/products/docker-desktop/) first if not already there.
2. In a shell of your choice run to switch to Windows containers and create the build container:

```pwsh
& 'C:\Program Files\Docker\Docker\DockerCli.exe' -SwitchWindowsEngine; `
docker build -t skyrim-community-shaders .
```

3. Then run the build:

```pwsh
docker run -it --rm -v .:C:/skyrim-community-shaders skyrim-community-shaders:latest
```

4. Retrieve the generated build files from the `build/aio` folder.
5. In subsequent builds only run the build step (3.)

#### Troubleshooting Build with Docker

If you run into `Access violation` build errors during step 3, you can try adding [`--isolation=process`](https://learn.microsoft.com/en-us/virtualization/windowscontainers/manage-containers/hyperv-container):

```pwsh
docker run -it --rm --isolation=process -v .:C:/skyrim-community-shaders skyrim-community-shaders:latest
```

## Debugging

### Launching MO2-SKSE-Skyrim from commandline

1. Open Steam
2. Close ModOrganizer GUI
3. Add `ModOrganizer.exe` (MO2 Folder) to your PATH, or use the path of it
4. Run the commands:

```pwsh
# Change Working Directory
cd "C:/Program Files (x86)/Steam/steamapps/common/Skyrim Special Edition"
# Launch SKSE with MO2
ModOrganizer.exe --log run "C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition\skse64_loader.exe"
```

### Capture with RenderDoc

In Launch Application Menu, use the following settings:

-   Executable Path: `PATH/TO/ModOrganizer.exe`
-   Working Directory: `C:/Program Files (x86)/Steam/steamapps/common/Skyrim Special Edition`
-   Command-line Arguments: `--log run "C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition\skse64_loader.exe"`
-   [x] **Capture Child Process**

## License

### Default

[GPL-3.0-or-later](COPYING) WITH [Modding Exception AND GPL-3.0 Linking Exception (with Corresponding Source)](EXCEPTIONS.md).  
Specifically, the Modded Code includes:

-   Skyrim (and its variants)
-   Hardware drivers to enable additional functionality provided via proprietary SDKs, such as [Nvidia DLSS](https://developer.nvidia.com/rtx/dlss/get-started) and [AMD FidelityFX FSR3](https://gpuopen.com/fidelityfx-super-resolution-3/)

The Modding Libraries include:

-   [SKSE](https://skse.silverlock.org/)
-   Commonlib (and variants).

### Shaders

See LICENSE within each directory; if none, it's [Default](#default)

-   [Features Shaders](features)
-   [Package Shaders](package/Shaders/)

### Icons

-   [Community Shaders Logo](package/Interface/CommunityShaders/Icons/Community%20Shaders%20Logo/) is not covered by the GPL-3.0 license. It is provided solely for personal use (e.g., building from source) and may only be used in unmodified form. There is no license for any other purpose or to distribute the logo. No trademark license is granted for the logo. Any use not expressly permitted is prohibited without the express written consent of the Community Shaders team.
