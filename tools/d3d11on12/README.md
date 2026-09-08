# Building an optimised d3d11on12.dll

`d3d11on12.dll` is the D3D11 usermode DDI driver that the D3D11 runtime loads when a device is
created through `D3D11On12CreateDevice`. It is **not** a KnownDLL, so a copy next to the game
executable wins the loader search order over `System32` — which is what makes replacing it
possible at all, and was verified here by checking the loaded module list of the running game.

Source: <https://github.com/microsoft/D3D11On12> and <https://github.com/microsoft/D3D12TranslationLayer>.

## Prerequisites without installing the WDK

The projects need `d3d10umddi.h` (a WDK-only DDI header) and `pix3.h`. Both are available as
NuGet packages, so no system-wide WDK install is required:

```
dotnet restore   # Microsoft.Windows.WDK.x64 10.0.26100.1591
                 # WinPixEventRuntime 1.0.190604001
```

## Configure and build

```
cmake -S D3D11On12 -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DFETCHCONTENT_SOURCE_DIR_D3D12TRANSLATIONLAYER=<path to D3D12TranslationLayer> ^
  "-DCMAKE_CXX_FLAGS=/WX- /I<wdk>/um /I<wdk>/shared /I<pix>" ^
  -DCMAKE_SHARED_LINKER_FLAGS=delayimp.lib
```

Three things upstream needs patched to build with a current toolchain, all in
`D3D12TranslationLayer.patch`:

- `/WX` removed. The code predates MSVC 14.51 and trips newer warnings that are not defects.
- `WinPixEventRuntime` is linked as a NuGet `.targets` file, which only resolves under MSBuild.
  Repointed at the import library from the same package.
- `delayimp.lib` added; `dxcore` is delay-loaded and otherwise leaves `__delayLoadHelper2`
  unresolved.

Deploy `d3d11on12.dll` and `WinPixEventRuntime.dll` next to `SkyrimSE.exe`.

## The optimisation

`BatchedContext::c_CommandKickoffMinThreshold` was 10, commented "Arbitrary for now". Every
translated D3D11 command runs `AddToBatch -> SubmitBatchIfIdle`, and one in every N of those
calls `IsBatchThreadIdle`, which is a `WaitForSingleObject` syscall even at a zero timeout. A
Skyrim frame is roughly 63,000 commands, so at 10 it paid about 6,300 syscalls per frame just
asking whether the worker thread was free.

Measured in a frozen CPU-bound Whiterun exterior, RTX 4080, three 20-second windows per run:

| kickoff | fps |
| --- | --- |
| 10 (upstream) | 38.81 |
| **64** | **43.86** |
| 256 | 41.93 |
| 1024 | 43.02 |
| 4096 | 42.28 |

64 is the default in the patch. `D3D12TL_KICKOFF` overrides it at runtime for re-measurement.

## What this does not fix

+13% is real but it does not close the gap to DXVK (127.67 fps on the same scene). Re-profiling
the render thread after the change shows the bottleneck simply moved:

| | before | after |
| --- | --- | --- |
| `NtWaitForSingleObject` | 58 | 14 |
| SRW lock contention | 22 | 37 |
| heap allocation | 3 | 8 |

Lock contention is now 31% of render-thread samples. `AddToBatch` takes `m_RecordingLock` on
every single command, and that lock cannot simply be dropped: creating the device with
`D3D11_CREATE_DEVICE_SINGLETHREADED` (which disables it) **crashes the game**, so Skyrim really
does drive the D3D11 context from more than one thread.

The remaining cost is the batched-context architecture itself — a per-command lock plus a
cross-thread handoff plus per-rename heap allocation. Closing 2.9x means replacing that command
path, not tuning it.
