# RenderDoc in-application API (header-only port)
# Note: The runtime DLL must be manually obtained and deployed with the application
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO baldurk/renderdoc
    REF v1.35
    SHA512 2b4cb86350497af46cb1dc4cbf56f0a1f86aa3d1c6f63262079b6c62343e0fd1304b4d79a273840af595a073ba0a17707cf8c6a59f491b7474e8b634a552e4e2
    HEAD_REF v1.x
)

# Install the API header file
file(INSTALL "${SOURCE_PATH}/renderdoc/api/app/renderdoc_app.h" 
     DESTINATION "${CURRENT_PACKAGES_DIR}/include/Renderdoc")

# Install copyright/license
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.md")

# Create usage file with instructions for getting the runtime DLL
file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/usage" 
"RenderDoc provides an in-application API for frame capture and debugging.

This port provides the renderdoc_app.h header file for compile-time integration.

To use RenderDoc in your application:
1. Include the header: #include <Renderdoc/renderdoc_app.h>
2. Load renderdoc.dll at runtime using LoadLibrary
3. Initialize the API as described in the documentation

To obtain renderdoc.dll:
- Download the portable zip from: https://renderdoc.org/builds
- Or build from source: https://github.com/baldurk/renderdoc
- Deploy renderdoc.dll alongside your application

For Community Shaders:
The DLL is stored in package/SKSE/Plugins/Renderdoc/ and deployed with the mod.
See package/SKSE/Plugins/Renderdoc/README.md for version information.

API Documentation: https://renderdoc.org/docs/in_application_api.html
")
