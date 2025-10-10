# RenderDoc Runtime DLL

This directory contains the RenderDoc runtime library for frame capture functionality.

## Version

Current version: **1.35** (or latest stable from renderdoc.org)

## Source

The `renderdoc.dll` file is obtained from the official RenderDoc releases:
- Website: https://renderdoc.org/builds
- Direct download: https://renderdoc.org/stable/1.35/renderdoc_1.35_64.msi

## Installation

The DLL is extracted from the Windows x64 MSI installer and placed in this directory for deployment with Community Shaders.

## License

RenderDoc is licensed under the MIT License. See LICENSE.md for details.

## Updating

To update to a newer version of RenderDoc:

1. Download the latest Windows x64 installer from https://renderdoc.org/builds
2. Extract `renderdoc.dll` from the MSI (typically located in `Program Files/RenderDoc/`)
3. Replace the DLL in this directory
4. Update the version number in this README
5. Verify the LICENSE.md is still current with the version

## API Header

The compile-time API header (`renderdoc_app.h`) is provided by the vcpkg port and is installed via vcpkg from the RenderDoc GitHub repository.
