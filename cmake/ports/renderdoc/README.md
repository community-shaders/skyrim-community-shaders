# RenderDoc vcpkg Port

This is a custom vcpkg port for RenderDoc's in-application API.

## What This Port Provides

This port installs:
- **API Header**: `renderdoc_app.h` from the RenderDoc GitHub repository
- **Documentation**: Usage instructions and license information

## What This Port Does NOT Provide

This port does **not** include the `renderdoc.dll` runtime library because:

1. **Size**: The DLL is ~24MB, which is large for vcpkg package management
2. **Runtime Deployment**: The DLL must be deployed with the application at runtime, not at build time
3. **Version Flexibility**: Users may want specific versions for testing or compatibility
4. **Distribution**: The DLL should be packaged with the final application distribution

## Getting the Runtime DLL

The `renderdoc.dll` should be obtained from official RenderDoc releases:
- Download page: https://renderdoc.org/builds
- Stable releases: https://renderdoc.org/stable/

For Community Shaders, the DLL is stored in `package/SKSE/Plugins/Renderdoc/renderdoc.dll` and is deployed as part of the mod package.

## Usage

After vcpkg installs this port:

```cpp
#include <Renderdoc/renderdoc_app.h>

// Load the DLL at runtime
HMODULE mod = LoadLibraryW(L"renderdoc.dll");
if(mod) {
    pRENDERDOC_GetAPI RENDERDOC_GetAPI = 
        (pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
    // ... use the API
}
```

See the [official documentation](https://renderdoc.org/docs/in_application_api.html) for complete usage details.

## Port Maintenance

To update the RenderDoc version:
1. Update the `REF` in `portfile.cmake` to the new tag/version
2. Update the `SHA512` hash (vcpkg will provide the correct hash on first build attempt)
3. Update the `version` in `vcpkg.json`
4. Test the build with `vcpkg install renderdoc --overlay-ports=cmake/ports`

## License

RenderDoc is licensed under the MIT License. See the LICENSE.md file installed by this port.
