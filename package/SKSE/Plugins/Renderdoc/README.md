# RenderDoc Runtime DLL

This directory contains the RenderDoc runtime library for frame capture functionality.

## Version

Current version: **1.35**

## Source

The `renderdoc.dll` file must be manually obtained from official RenderDoc releases:
- Website: https://renderdoc.org/builds
- Direct download (MSI): https://renderdoc.org/stable/1.35/renderdoc_1.35_64.msi
- GitHub releases: https://github.com/baldurk/renderdoc/releases/tag/v1.35

## Installation Steps

1. Download the Windows x64 installer (MSI) from the link above
2. Install RenderDoc or extract the MSI using a tool like 7-Zip
3. Copy `renderdoc.dll` from the installation directory (typically `C:\Program Files\RenderDoc\`)
4. Place it in this directory (`package/SKSE/Plugins/Renderdoc/`)
5. The DLL will be deployed with Community Shaders mod package

## License

RenderDoc is licensed under the MIT License. See LICENSE.md for details.

## Updating

To update to a newer version of RenderDoc:

1. Update the vcpkg port version in `cmake/ports/renderdoc/vcpkg.json`
2. Update the REF in `cmake/ports/renderdoc/portfile.cmake`
3. Download the new Windows x64 installer from https://renderdoc.org/builds
4. Extract `renderdoc.dll` and replace it in this directory
5. Update the version number in this README
6. Verify LICENSE.md is current (check RenderDoc repository)

## API Header

The compile-time API header (`renderdoc_app.h`) is automatically managed by the vcpkg port at `cmake/ports/renderdoc/` and is fetched from the RenderDoc GitHub repository during build.
