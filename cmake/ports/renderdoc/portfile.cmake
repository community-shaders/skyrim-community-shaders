# RenderDoc in-application API (header-only port)
# Note: The runtime DLL must be manually obtained and deployed with the application
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO baldurk/renderdoc
    REF v1.43
    SHA512 555d3ff84787db45807f29e67c9e8ea3d475e101ceb01d281b80a74d470b7aa5ef6e39731a8a1a06ca547729b74e316ff2d8394a126ecde9b637a1ca94ef95f3
    HEAD_REF v1.x
)

# Install the API header file
file(INSTALL "${SOURCE_PATH}/renderdoc/api/app/renderdoc_app.h"
     DESTINATION "${CURRENT_PACKAGES_DIR}/include/Renderdoc")

# Install copyright/license
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.md")

# Runtime DLL automation template:
# The RenderDoc runtime DLL is distributed separately from the API header.
# To automate runtime retrieval, use the official stable package URL pattern:
#
#   https://renderdoc.org/stable/${VERSION}/RenderDoc_${VERSION}_64.zip
#
# Example vcpkg template:
#
#   set(RENDERDOC_VERSION 1.40)
#   set(RENDERDOC_RUNTIME_ARCHIVE "RenderDoc_${RENDERDOC_VERSION}_64.zip")
#   vcpkg_download_distfile(
#       OUT_FILE "${SOURCE_PATH}/${RENDERDOC_RUNTIME_ARCHIVE}"
#       URL "https://renderdoc.org/stable/${RENDERDOC_VERSION}/${RENDERDOC_RUNTIME_ARCHIVE}"
#       SHA512 <SHA512>
#   )
#   vcpkg_extract_source_archive_ex(
#       OUT_SOURCE_PATH RUNTIME_SOURCE_PATH
#       ARCHIVE "${SOURCE_PATH}/${RENDERDOC_RUNTIME_ARCHIVE}"
#   )
#   file(INSTALL "${RUNTIME_SOURCE_PATH}/renderdoc.dll"
#        DESTINATION "${CURRENT_PACKAGES_DIR}/bin")
#
# Replace <SHA512> with the archive checksum for the selected RenderDoc version.

# Create usage file with instructions for getting the runtime DLL
file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/usage"
"RenderDoc provides an in-application API for frame capture and debugging.

This port provides the renderdoc_app.h header file for compile-time integration.

To use RenderDoc in your application:
1. Include the header: #include <Renderdoc/renderdoc_app.h>
2. Load renderdoc.dll at runtime using LoadLibrary
3. Initialize the API as described in the documentation

To obtain renderdoc.dll:
- Download the portable zip from: https://renderdoc.org/stable/${VERSION}/RenderDoc_${VERSION}_64.zip
- Or build from source: https://github.com/baldurk/renderdoc
- Deploy renderdoc.dll alongside your application

API Documentation: https://renderdoc.org/docs/in_application_api.html
")
