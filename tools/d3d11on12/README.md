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

Add `/Zi` to `CMAKE_CXX_FLAGS` and `/DEBUG /OPT:REF /OPT:ICF` to the linker flags to get a PDB.
Profiling this DLL is useless without one: the public symbol server only has symbols for
Microsoft's shipped copy, and against our build every frame resolves to
`d3d11on12!OpenAdapter_D3D11On12+0x...`.

Three things upstream needs patched to build with a current toolchain, all in
`D3D12TranslationLayer.patch`:

- `/WX` removed. The code predates MSVC 14.51 and trips newer warnings that are not defects.
- `WinPixEventRuntime` is linked as a NuGet `.targets` file, which only resolves under MSBuild.
  Repointed at the import library from the same package — the patch hardcodes the path the
  package landed at here, so repoint it at your own `packages/winpixeventruntime/.../x64`.
- `delayimp.lib` added; `dxcore` is delay-loaded and otherwise leaves `__delayLoadHelper2`
  unresolved.

`.gitattributes` normalises `*.patch` to LF while the upstream checkout is CRLF in its own
index, so convert the patch to CRLF before `git apply` — `--ignore-whitespace` does not
cover it, that was tried.

Deploy `d3d11on12.dll` and `WinPixEventRuntime.dll` next to `SkyrimSE.exe`.

## Measuring

Every number here is from a frozen Whiterun exterior (save `csbench2`, freecam driven to a fixed
position, `timescale 0`), RTX 4080, frames counted from the engine's own frame counter over a
40-second window after a 45-second warm-up. **The rig warms up across a session**, so cases are
only comparable interleaved (A,B,A,B) — every optimisation below carries a runtime kill switch
so one binary can be measured against itself in a single session. (The inline recursive lock has
no switch: it is a drop-in replacement with the same semantics, not a behaviour change, so it is
measured only as part of the whole.)

| Variable | Value | Effect |
| --- | --- | --- |
| `D3D12TL_RENAMECACHE` | `0` | disables rename cookie pooling |
| `D3D12TL_PERSISTENTMAP` | `0` | disables persistent mapping of upload heaps |
| `D3D12TL_VACACHE` | `0` | disables the cached buffer GPU VA |
| `D3D12TL_BATCHSIZE` | `0` | idle-only batch kickoff, as upstream; any other int sets the size |
| `D3D12TL_KICKOFF` | *(int)* | overrides the idle-check frequency |
| `D3D12TL_STATS` | *(path)* | appends rename cache hit/miss counts to that file |
| `D3D12TL_REDUNSTATS` | *(path)* | appends redundant constant-buffer bind counts to that file |
| `D3D12TL_CBFILTER` | `0` | disables dropping redundant constant-buffer binds |

## The optimisations

### 1. Kickoff frequency

`BatchedContext::c_CommandKickoffMinThreshold` was 10, commented "Arbitrary for now". Every
translated D3D11 command runs `AddToBatch -> SubmitBatchIfIdle`, and one in every N of those
calls `IsBatchThreadIdle`, which is a `WaitForSingleObject` syscall even at a zero timeout. A
Skyrim frame is roughly 63,000 commands, so at 10 it paid about 6,300 syscalls per frame just
asking whether the worker thread was free.

| kickoff | fps |
| --- | --- |
| 10 (upstream) | 38.81 |
| **64** | **43.86** |
| 256 | 41.93 |
| 1024 | 43.02 |
| 4096 | 42.28 |

### 2. Rename cookie pooling

`ImmediateContext::CreateRenameCookie` built a brand new `Resource` for every
`Map(WRITE_DISCARD)` — upstream's comment there is *"TODO: See if there's a good way to cache
these guys."* That is the single hottest thing in the frame, because D3D11 titles discard a
dynamic constant buffer per draw. Each one cost a `Resource` construction, the
`GetCopyableFootprints` call it makes into the D3D12 runtime, a suballocation, a
`ZeroConstantBufferPadding` (a `Map`/`Unmap` pair of its own), a locked push onto a device-wide
deque, and, on retire, a locked linear search of that deque plus the destruction.

Renaming *swaps identities*, so a resource and its cookies simply rotate a pool of backing
stores between them: a retired cookie is already a complete, correctly sized target for the next
map. Cookies are now pooled on the resource that created them and handed back out once the
command lists that referenced them have completed. Measured hit rate in this scene: **99.9%**
(16.78M hits against 20.8k misses over one run).

The pool owns each cookie for its whole life, in flight or free, so neither the create nor the
delete touches `m_RenamesInFlight`, and the completion test runs against cached fence values
rather than calling `ID3D12Fence::GetCompletedValue` per map.

A pool converges on the number of renames the resource has in flight, so the caps only matter for
pathological apps — but they are a byte budget as well as a count, because 16k cookies is a few
megabytes of 256-byte constant buffers and gigabytes of 1 MB dynamic vertex buffers.

### 3. Persistent mapping of upload heaps

`ID3D12Resource::Map` takes an internal lock and refcounts every call, and the rename path made
two `Map`/`Unmap` pairs per discard (one for the padding zero, one for the app's own write).
Upload heaps are now mapped once and left mapped — explicitly supported by D3D12; the heap is
unmapped implicitly when it is destroyed. The cached base travels with copies of the
suballocation, so it survives into the next rename that reuses the cookie.

### 4. Cached buffer GPU VA

`GetBufferViewDesc` called `ID3D12Resource::GetGPUVirtualAddress()` per constant buffer per stage
per draw, plus once per vertex and index buffer — upstream flags it in a comment as a CPU
hotspot. It cannot change without the identity's underlying resource changing, so it is cached on
the identity and refreshed in the one place that establishes it.

### 5. Size-based batch kickoff

Upstream hands a batch to the worker thread **only once the worker has gone idle**. While the
worker is behind, the recording thread therefore piles the whole frame into a single batch the
worker cannot start on, and the frame ends with the render thread blocked in `Present` waiting
for all of it to drain. Handing a batch over once it reaches `c_CommandBatchMaxSize` commands
keeps the worker fed and leaves only a short tail at the flush; `c_MaxOutstandingBatches` still
supplies the back pressure.

### 6. Dropping redundant constant-buffer binds

The D3D11 runtime forwards every `*SSetConstantBuffers` the app makes, whether or not it changes
anything, and Skyrim re-binds the same buffers constantly. Counted with `D3D12TL_REDUNSTATS` in
the benchmark scene:

| | count | redundant |
| --- | --- | --- |
| calls | 94,000,000 | 87,349,930 (**93%**) |
| slots | 94,444,804 | 87,613,478 (**93%**) |

A bind that sets every slot to what it already holds costs a batch append on the recording thread
and an execution on the worker, for nothing. `BatchedContext` now shadows the constant-buffer
bindings and drops those. The shadow is reset by `ClearStateImpl`, which the runtime guarantees
brackets every command list, and cleared by `ReleaseResource`, so a destroyed resource cannot
linger in it and be matched by a later allocation at the same address.

### 7. An inline recursive lock

`BatchedContext::m_RecordingLock` is taken once per translated D3D11 command — roughly 63,000
times a frame — and `std::recursive_mutex` reaches into `msvcp_win` for every lock and unlock,
each of which calls `GetCurrentThreadId` in kernel32 to track recursion. `RecursiveSRWLock` keeps
the recursion count inline and reads the thread id straight out of the TEB, so the whole
operation is an SRWLOCK acquire plus two loads.

### 8. A syscall-free worker-idle check

`SubmitBatchIfIdle` asks `IsBatchThreadIdle` whether to hand a batch over, and that drained a
semaphore with `WaitForSingleObject` — a syscall even at a zero timeout, and one that only ever
fails while the worker is behind. The worker now bumps a completion count after it signals, so
the common answer costs a load and a compare.

### What they are worth together

| arm | runs | mean |
| --- | --- | --- |
| upstream behaviour (every switch off) | 43.32, 40.86 | **42.09** |
| all of the above | 49.07, 47.44 | **48.25** |
| all but the redundant-bind filter | 47.68, 46.83 | 47.25 |

+14.6% overall, and the bind filter is +2.1% of that. Two rounds, interleaved A,B,C,A,B,C.

## Where the time actually goes

Sampled with a poor-man's profiler (repeated non-invasive `cdb` attaches, 45 per thread; 39 of
the worker's landed while it held a stack) in the benchmark scene. That few samples puts roughly
+/-14 points on each share, so read these as "which pole is which", not as a budget. **The render thread is not the only pole** — this is what
made the first round of render-thread work look worthless:

| Render thread, before these changes | share |
| --- | --- |
| blocked in `Present` → `ReleaseWrappedResources` → `ProcessBatch` (waiting for the worker) | 22% |
| inside the D3D11 stack (runtime + D3D11On12 recording) | 44% |
| engine, CS hooks, other | 33% |

| Translation worker, before these changes | share |
| --- | --- |
| idle, waiting for a batch | 26% |
| `ImmediateContext::PreDraw` (descriptor tables, `CreateConstantBufferView`) | 28% |
| `ResourceStateManager::ApplyAllResourceTransitions` | 15% |
| `Rename` / `RotateResourceIdentities` | 10% |
| D3D12 runtime and driver | 18% |

Cutting render-thread work alone moves nothing while the worker is the longer pole: the first
rename-cache measurement was flat (A 41.65/42.65 vs B 44.56/41.25) despite a 99.9% cache hit
rate, because the saving was absorbed by a longer `Present` wait. Only once the batch kickoff
started feeding the worker continuously did the render-thread savings show up in the frame rate.

Re-profiled afterwards (60 render-thread samples, 41 worker), the shape has changed:

| Render thread, after | share |
| --- | --- |
| blocked, almost all of it the `Present` drain | 42% |
| `CreateRenameCookie` (now the pool's fast path) | 10% |
| everything else in the D3D11 stack | ~15% |
| engine, CS hooks, other | ~33% |

| Translation worker, after | share |
| --- | --- |
| `OptLock::TakeLock` on the rename container | 20% |
| idle, waiting for a batch | 15% |
| `Rename` / `RotateResourceIdentities` | 22% |
| `DeleteRenameCookie` | 10% |
| `ApplyAllResourceTransitions` | 7% |
| `PreDraw` | 7% |

Two things stand out. `PreDraw` fell from 28% to 7% — it was mostly the descriptor and view work
that renaming dirtied, and pooling plus the cached VA took it out. And the lock the two threads
share to hand cookies back and forth is now the largest single item on the worker.

## The ceiling

This back end is not going to overtake DXVK. Measured head to head in one session, same scene,
same settings file:

| | D3D11On12, all of the above | DXVK | ratio |
| --- | --- | --- | --- |
| profiling preset (XeSS quality) | 46.18, 45.95 → **46.07** | 145.63, 147.70 → **146.67** | 3.18x |
| no upscaler on either side | 44.67, 44.51 → **44.59** | 155.78, 152.92 → **154.35** | 3.46x |

Both interleaved A,B,A,B. The second row is the like-for-like one: the D3D12 back end
force-disables Upscaling, so on the profiling preset DXVK is the only side rendering at a reduced
internal resolution — which, on this CPU-bound scene, costs DXVK rather than helping it.

The profile says why rather than guessing. A 44.6 fps frame is 22.4 ms, and 44% of render-thread
samples sit below the D3D11 API boundary — call it **10 ms of the frame inside the D3D11 stack
alone**, against a whole DXVK frame of 6.5 ms. (45 samples, so the 10 carries a wide error bar; it
would have to be wrong by a factor of two to change the conclusion.)

That 10 ms is split between two layers. DXVK has one: it *replaces* `d3d11.dll`, so an app call
lands in code written for the job. D3D11On12 keeps Microsoft's `d3d11.dll` — validation, state
tracking, DDI dispatch, all of it — and hangs a second translation underneath. Everything in this
file tunes the second layer. Nothing here can remove the first, which is why the honest answer to
"can this beat DXVK" is no, and the way to a fast D3D12 back end for this engine would be a
D3D11 implementation of our own rather than the shipped runtime.

## What is left inside the translation layer

In rough order of what the worker profile still shows, for anyone picking this up:

- Splitting the cookie handoff. The two threads share one lock to pass cookies back and forth and
  it is now the largest single item on the worker; a lock-free per-resource queue would remove it
  rather than make it cheaper.
- `ApplyAllResourceTransitions` (15% of worker samples). A renamed constant buffer is put on the
  transition list every draw and can never need a barrier — an upload-heap resource is created in
  `GENERIC_READ` and cannot leave it. Skipping it is not a one-liner: the same pass is what stamps
  `UsedInCommandList` on bound resources, and that bookkeeping is what keeps deferred deletion
  honest.
- The same redundancy filter for shader resources and samplers. Constant buffers were 93%
  redundant; there is no obvious reason SRV and sampler binds would be different, and the shadow
  machinery is already there. They are far less frequent in this scene (`SetShaderResources` did
  not appear once in 45 render-thread samples), so the win is probably small here — but it may
  not be on a different workload.
- Root CBVs instead of a descriptor table. It would remove the CB table rebuild altogether, but
  14 constant buffers across 5 stages does not fit in a 64-DWORD root signature, so it needs a
  fallback, and root CBVs do not bounds-check the way the table path does.

Tried and rejected, both measured:

- Caching a constant-buffer view on each resource identity and copying the whole table with one
  `CopyDescriptors`, instead of a `CreateConstantBufferView` per bound slot per draw. Noise (45.29
  with, 45.20 without, two interleaved rounds), and it puts descriptor lifetime on resource
  identities. The CB table rebuild is not where the worker's time goes.
- Swapping the rename container's `std::mutex` for an SRWLOCK, on the theory that a lock this
  contended would do better spinning than sleeping. It did not (45.72 mutex, 45.41 SRWLOCK, two
  interleaved rounds, DLL swapped between runs since there is no switch for it). The cost is the
  handoff, not the primitive.
