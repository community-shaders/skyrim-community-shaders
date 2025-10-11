# RenderDoc in-application API (header-only port)
# Note: The runtime DLL must be manually obtained and deployed with the application
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO baldurk/renderdoc
    REF v1.40
    SHA512 6581d1fe7ba069e74d09b64a1de0a413bc0d1e775a45cce87bb8ea125fc2d67e9846439acc802882c0d717028e251f9f44fd896e2022e310456adafa675bf85a
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

API Documentation: https://renderdoc.org/docs/in_application_api.html
")
