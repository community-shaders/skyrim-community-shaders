# Nsight Perf GPU counters

Developer Tools can measure NVIDIA GPU hardware counters per render pass with the
[Nsight Perf SDK](https://developer.nvidia.com/nsight-perf-sdk) range profiler. A capture records
metrics such as `gpu__time_duration`, `sm__throughput` and the warp stall reasons over the
frame's perf events (`State::BeginPerfEvent` / `EndPerfEvent`). These are the same named ranges
RenderDoc and Tracy show.

It is a developer build option, D3D11 only (the game's immediate context), and needs an NVIDIA GPU
the SDK supports.

## Building

The SDK is a login-gated NVIDIA download, so the build never fetches it.

1. Download and extract the Nsight Perf SDK. The directory to point at is the one holding
   `include/nvperf_host.h` (in the SDK archive this is `NvPerf/`).
2. Configure with `NVPERF_SUPPORT=ON` and `NVPERF_SDK_ROOT=<that directory>`. The
   `ALL-WITH-NVPERF` preset in `CMakeUserPresets.json.template` does this on top of
   `ALL-WITH-AUTO-DEPLOYMENT`.

With the option on:

-   the plugin links `nvperf_grfx_host.lib` with `/DELAYLOAD:nvperf_grfx_host.dll`;
-   the AIO folder and `cmake --install` get `SKSE/Plugins/CommunityShaders/NvPerf/nvperf_grfx_host.dll`.

The plugin loads the DLL from that path the first time the profiler is used. If the file is
missing, profiling reports itself unavailable and nothing else changes.

With the option off (the default, and CI), the profiler compiles to stubs. The panel and the
devbench tool then report that the build has no Nsight Perf.

If a session fails with `NVPA_STATUS_INSUFFICIENT_PRIVILEGE`, enable GPU performance counters for
all users in the NVIDIA Control Panel ([ERR_NVGPUCTRPERM](https://developer.nvidia.com/ERR_NVGPUCTRPERM)).

## How a capture runs

-   **Passes.** Each frame is one profiler pass, ended before the swap chain's Present (so the ImGui
    overlay is not measured) and begun after it.
-   **Samples.** The chip collects only so many counters per pass, so one sample takes as many
    frames as its metrics need passes. A capture collects `samples` of them one after another.
-   **Ranges.**
    -   The ranges are the perf events whose names match the capture's filters. A trailing `*`
        matches a prefix, and no filters means every range.
    -   With one nesting level only the outermost selected ranges are measured. Each extra level also
        measures selected ranges nested one deeper, and multiplies the passes.
-   **Frame Annotations.**
    -   A capture turns Frame Annotations on for its duration, since most perf events are only emitted
        with it on.
    -   Engine passes (shadow maps, `BSShaderAccumulator` batches, cubemaps) are hooked only when Frame
        Annotations is enabled at startup.

Results:

-   **Summary:** per filter (or per name, with no filters) and per frame. Counters are summed and
    rates are weighted by each range's `gpu__time_duration`. A range nested inside one that
    matches the same filter is already counted in it, so only the outer one is summarized.
-   **CSV:** every range of every sample is appended to `CommunityShaders-NvPerf.csv` next to
    `CommunityShaders.log`.

Metrics are written `name[:kind]`, where the kind is one of:

| Kind                | Meaning                                                |
| ------------------- | ------------------------------------------------------ |
| `counter` (default) | sum                                                    |
| `ratio`             | the ratio submetric                                    |
| `throughput`        | average % of peak sustained, elapsed                   |
| `active`            | counter average as % of peak sustained, active cycles  |
| `elapsed`           | counter average as % of peak sustained, elapsed cycles |
| `percycle`          | counter average per active cycle                       |

A metric the chip lacks is reported as unavailable, and the rest of the capture still runs.

## In game

Open **Developer Tools → Nsight Perf**:

1. Pick metrics and range filters, and set samples, nesting levels and delay.
2. Press **Capture**. The status line shows the sample and pass while it runs.
3. The latest result appears as a table, with the CSV path below it.

## Through devbench

The `communityshaders.nvperf` tool (MCP and REST) has these actions:

-   `status`
-   `metrics`
-   `capture`, which returns a `captureId` immediately
-   `cancel`
-   `result`, which returns `pending` until the capture ends; `detail: true` adds every range

The host also emits `communityshaders.nvperf.completed` (`{captureId, success, error}`) when a
capture ends. For example:

```json
{ "action": "capture", "ranges": ["Deferred*", "Directional Light Shadowmaps"], "samples": 3 }
{ "action": "result", "captureId": 1 }
```

## Code

-   `src/Features/RemoteControl/NvPerf.{h,cpp}`: the profiler.
    -   It configures the metrics (metrics evaluator → raw counters → config image), runs the D3D11
        session and passes, decodes and evaluates the ranges, and publishes the result.
    -   Every profiler call runs on the render thread. Requests and results cross threads under a mutex.
-   `src/Features/RemoteControl/NvPerfHost.{h,cpp}`: what the profiler needs from the plugin (device, frame
    counter, annotations, paths, build hint). Beyond these, `NvPerf.cpp` needs only the precompiled header's
    `logger` namespace, which another host must also provide.
-   `src/Hooks.cpp` (`IDXGISwapChain_Present`): `NvPerf::OnPresentBegin` / `OnPresentEnd`.
-   `src/State.cpp` (`BeginPerfEvent` / `EndPerfEvent`): `NvPerf::PushRange` / `PopRange`, behind
    one atomic check when no pass is open.
-   `src/Features/RemoteControl.cpp`: the panel, shown as a tab of `src/Features/DeveloperTools.cpp`.
-   `src/Features/RemoteControl/DevBenchBridge.cpp`: the tool.
