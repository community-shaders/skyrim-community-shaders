# DXVK backend architecture

Community Shaders loads its prefixed DXVK runtime from
`SKSE/Plugins/CommunityShaders/bin` before Skyrim creates the D3D11 device.
`DxvkLoader` owns the modules and is the only code allowed to resolve the
private CS/DXVK extension ABI. The shared `cs_dxvk_api.h` remains the binary
contract with the bundled DXVK fork.

## Ownership and threads

- `Upscaling` owns one process-lifetime `UpscalingRuntime`. Hooks enter through
  `Upscaling`, rather than backend singletons.
- `StreamlineSession` owns the interposer module, resolved exports, feature
  capabilities, evaluation state, Reflex controller, and callback state.
- `FrameGen::FrameGenerationCoordinator` owns render-thread transitions between
  DLSS-G and FSR-FG. Feature-load changes occur only while the DXVK swapchain is
  torn down.
- `VulkanDeviceContext` owns the bridged Vulkan device, command slots,
  acknowledged present waits, presenter state, and deferred resource
  retirement. Its recursive submission mutex serializes the coupled
  slot/wait/retirement lifecycle and teardown.
- `DxvkControl` owns change-only queue-depth and swapchain-recreation requests;
  `DxvkLoader` remains the sole raw ABI boundary.
- DXVK present callbacks run on DXVK's Vulkan present thread. Streamline calls
  shared by that thread and the render thread remain serialized.
- `FrameGenWatchdog` only detects stalls. `WindowsGpuRecovery` owns the Windows
  scheduled-task recovery policy.

Resource destruction stays conservative: if queue ownership or GPU completion
cannot be proven, resources are quarantined instead of being destroyed.

## Tests

Configure with `DXVK_CLEANUP_TESTS=ON`, build `DXVKCleanupTests`, then run:

```powershell
ctest --test-dir build/ALL -C Release --output-on-failure -R '^DXVKCleanupTests$'
./tests/DependencyBuildTests.ps1
```

PR CI enables these tests. It also installs the build into a temporary prefix
and runs `tests/RuntimeManifestTests.ps1` to ensure the install and AIO runtime
inventories match the manifest in `cmake/DxvkRuntimeManifest.cmake` and contain
no legacy DX12 binaries.
