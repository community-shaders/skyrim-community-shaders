# RenderDoc in-application API (header-only port)
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
2. Load renderdoc.dll at runtime using LoadLibrary/dlopen
3. Initialize the API as described in the documentation

To obtain renderdoc.dll:
- Download from: https://renderdoc.org/builds
- Latest stable: https://renderdoc.org/stable/1.35/renderdoc_1.35_64.msi
- Extract renderdoc.dll from the installer and deploy alongside your application

API Documentation: https://renderdoc.org/docs/in_application_api.html
")
