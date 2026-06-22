# devbench-api vcpkg port

Vendor the devbench cross-plugin API (`DevBenchAPI.h` + `.cpp`) into your SKSE plugin
via vcpkg — **do not copy the files into your tree** (they drift).

## Consume (once devbench is published)

`vcpkg.json`:

```json
{ "dependencies": ["devbench-api"] }
```

`CMakeLists.txt`:

```cmake
find_package(devbench-api CONFIG REQUIRED)
target_link_libraries(YourPlugin PRIVATE DevBench::API)
```

Then, after SKSE sends `kPostLoad`:

```cpp
#include <DevBenchAPI.h>
if (auto* dvb = DevBenchAPI::GetDevBenchInterface001()) {
    dvb->RegisterTool("yourmod.dothing", R"({"description":"...","inputSchema":{...}})",
                      &YourHandler, yourCtx);
}
```

Linking `DevBench::API` puts `DevBenchAPI.h` on the include path and compiles
`DevBenchAPI.cpp` (the messaging-handshake helper) into your plugin. The API glue is
**MIT** (`DevBenchAPI.LICENSE.txt`); the devbench plugin itself is GPL-3.0.

## Pinning / bumping

`portfile.cmake` is pinned to a concrete devbench commit via `vcpkg_from_github`
(`REF` + `SHA512`) — no placeholders to fill in; the overlay works as-is. To pull a
newer API revision, update `REF` to the new commit and replace `SHA512` with the value
vcpkg reports on the first (failed) build, or precompute it with
`vcpkg hash <downloaded-tarball>`. The API header is ABI-versioned, so a bump is only
needed to adopt a new interface revision.
