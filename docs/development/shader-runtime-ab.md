# Shader runtime A/B check

A same-frame, GPU-level equivalence check for a shader refactor whose compiled bytecode
legitimately diverges — an op-reordering / algebraic restructure (e.g. the TAA bracket/flicker/
blend core). For refactors that stay **bytecode-identical**, use `tools/verify-shader-refactor.ps1`
instead — a hard offline proof that needs no game.

TAA is the worked example here, but the technique and the [Lessons](#lessons-generalizable-re-technique)
generalize to any shader RE/refactor — see [Generalizing to other shaders](#generalizing-to-other-shaders).

## Why same-frame swap (not two-launch screenshots)

TAA is temporal: its output depends on accumulated history. Two separate game launches never
align frame-for-frame (animated menu, HMD pose, timing/RNG), so a screenshot A/B only yields a
noisy tolerance diff. Instead we capture **one** real frame in RenderDoc and replace just the
TAA pixel shader on that captured frame. The inputs (history `t1`, velocity `t2`, depth `t3`,
mask `t4`, alpha `t5`, cbuffer `b2`) are frozen, so A (shipping) and B (candidate) run on
byte-identical inputs — a near-zero output diff means equivalent behavior on a real frame.

This is the runtime analog of the offline verifier, tolerant of the bytecode divergence an
op-reordering restructure produces.

## Prerequisites

-   RenderDoc, reachable via the `renderdoc` MCP (`Eval`, `Get-Texture`, `Instance`).
-   SkyrimSE or SkyrimVR launchable with RenderDoc injected (`rd.ExecuteAndInject(<loader>, …,
hookIntoChildren=True)`, or launch from the RenderDoc GUI) — `<loader>` is `skse64_loader.exe`
    for SE/AE and `sksevr_loader.exe` for VR. The **main menu** already runs the TAA pass, so it is
    a valid capture surface on either edition.
-   The build must be in **TAA upscaling mode** (DLSS/FSR replace `ISTemporalAA`) and ideally with
    **HDR and frame-generation off** — otherwise Open Shaders presents through a DX12 interop
    swapchain and RenderDoc captures only the D3D12 present (Copy/Present, no draws), not the
    D3D11 TAA pass.
-   `fxc.exe` (Windows SDK) — same compiler the offline verifier uses.

## Steps

### 1. Compile A' (current) and B (candidate) to DXBC — match the build's permutation

Use the SAME defines as the running build (SkyrimSE ⇒ no `VR`; SkyrimVR ⇒ `VR`; add `HDR_OUTPUT`
only if HDR is on) and `/I package/Shaders` so includes resolve. Feeding DXBC sidesteps
RenderDoc's HLSL include/define handling.

**A′ and B must come from different sources** — A′ from the **deployed/shipping** shader (so its
diff vs the live RT establishes the noise floor), B from your **candidate**. Pull A′ from a git
ref and B from the working tree so they can't accidentally be the same file:

```powershell
$fxc = (Get-Command fxc.exe).Source
$inc = "package/Shaders"; $sh = "package/Shaders/ISTemporalAA.hlsl"
$defs = @("/D","PSHADER=1")            # add "/D","VR=1" and/or "/D","HDR_OUTPUT=1" to match the build
# Baseline A' = the DEPLOYED shader (extract the shipping ref to a temp file; UTF-8, not PS UTF-16)
[IO.File]::WriteAllLines("$env:TEMP\taa_A.hlsl", (git show origin/dev:$sh))
& $fxc /nologo /T ps_5_0 /E main @defs /I $inc "$env:TEMP\taa_A.hlsl" /Fo "$env:TEMP\taa_A.dxbc"
# Candidate B = the refactored working tree
& $fxc /nologo /T ps_5_0 /E main @defs /I $inc $sh /Fo "$env:TEMP\taa_B.dxbc"
```

**Baseline = the refactor's BASE, not blindly `origin/dev`.** If your refactor is _stacked_ on a
fix/feature not yet on `dev` (e.g. a VR fix the restructure sits on top of), compiling A′ from
`dev` makes `baseline_vs_live` measure _that fix's effect_, not the compiler noise floor — you'll
see a huge "floor" (e.g. ~0.22 instead of ~0.001) and a meaningless `EQUIVALENT`. Use the
refactor's **parent commit** (`git show <refactor-base>:$sh`) so A′ and B differ only by the
restructure.

**Confirm `git branch` before compiling B.** A wrong-branch compile silently builds the deployed
shader as the "candidate" and reports a meaningless `EQUIVALENT`.

### 2. Capture a frame — MOTION matters

Confirm with `Instance list` (the `renderdoc` MCP) that an instance shows `capture_loaded: true`
after capturing.

-   The **main menu** runs the TAA pass, but its history rectification is near-passthrough on a
    static image. **A menu frame does NOT validate any change that flows through the motion-
    dependent path** (reject / history reproject / clip-to-AABB) — it reads a false `EQUIVALENT`.
    Use a menu frame only for changes you already know are motion-independent. _(This is how a
    gate-inversion bug once slipped through: byte-for-byte `EQUIVALENT` on the menu, ~4× off under
    motion — caught only by re-running on an in-game motion frame and bisecting the commits.)_
-   For anything touching the blend/reject core, capture an **in-game frame with real motion**.
    Via `dev-bench` + `renderdoc`: load a light **interior** save, then `record replay` a movement
    recording (continuous teleport along a path = sustained motion vectors) and fire
    `TargetControl.TriggerCapture(1)` ~3 s into the replay from a **concurrent** `Eval` (the replay
    call blocks for its whole duration, so the trigger must run in parallel). `camera setPov vanity`
    gives orbiting motion on SE; in VR the HMD drives the camera, so **player translation is the
    reliable VR motion source**.
-   **Capture size is the gate.** SE in-game ≈ 1.5 GB; VR interior ≈ 0.5–5.5 GB (loads fine); VR
    **exterior ≈ 14 GB and wedges/crashes RenderDoc** — stay indoors (e.g. Dragonsreach). A
    corrupt/oversized `.rdc` needs a full RenderDoc relaunch, not an MCP reconnect.
-   `TargetControl` sometimes returns a **stale** `NewCapture` path — verify the new `.rdc` by
    timestamp/size on disk, don't trust the returned string.
-   On a huge frame, a full-frame `SetFrameEvent` sweep times out the eval worker — use
    `taa_candidates(reverse=True, stop_after=1)` (or `max_scan_drawcalls=N`) to scan from the end
    (post-process is near there) and stop at the first match.

### 3. Load the harness and run the A/B (via the `renderdoc` MCP `Eval`)

Load the harness, then run the A/B. (If a later call reports the functions undefined, your MCP
uses a fresh namespace per `Eval` — see Lessons; `exec` and call in the same `Eval`.)

```python
exec(open(r"<repo>/tools/taa-renderdoc-ab.py").read())   # <repo> = your local checkout path
taa_candidates()          # exhaustive forward scan — fine for menu / small captures
# On a multi-GB in-game capture, scan from the end and stop at the first hit so the worker
# doesn't time out (the TAA pass is post-process, near the end):
taa_candidates(reverse=True, stop_after=1)   # or max_scan_drawcalls=N to cap the probe
```

(Finds ISTemporalAA by its 6-SRV fingerprint — `t0..t5`: current/history/velocity/depth/mask/alpha.)

Pick the `eventId` of the TAA draw from the candidate list, then:

```python
ab(<eventId>,
   candidate_dxbc=r"%TEMP%/taa_B.dxbc",
   baseline_dxbc=r"%TEMP%/taa_A.dxbc")
```

### 4. Interpret

**Always pass `baseline_dxbc`** — the verdict is _relative to it_. The game compiles the live
shader with slightly different optimization than offline fxc, so even an identical shader leaves
a small **noise floor** (`baseline_vs_live.mean_abs`, e.g. ~2e-4 on a 10-bit RT). `ab()` reports:

-   `noise_floor_mean` — the baseline residue.
-   `verdict`: `EQUIVALENT` if `candidate_vs_live.mean_abs ≤ 3× the floor`, else `DIFFERS`.

Validated on the SE main menu (TAA on, HDR/frame-gen off): the pre-rename `dev` shader read
`EQUIVALENT` (mean = floor), a deliberately ×0.5 shader read `DIFFERS` (mean ≈ 83× floor, 61%
of samples). If `baseline_vs_live.mean_abs` is _large_ (not a tiny floor), the permutation is
wrong (e.g. `HDR_OUTPUT` mismatch) — fix step 1 before trusting the candidate.

### 5. (Optional) visual evidence

Use the `Get-Texture` MCP tool on the output RT before vs after replacement (and over the
`x≈0.5` eye-seam region where the original VR bug showed) with `diff_amplify` for a diff image
in the report.

## Generalizing to other shaders

TAA is just the worked example — `grab_rt()`, `replace_ps_with_dxbc()`, `restore()`, `_diff()`, and
`ab(eid, …)` are all **shader-agnostic** (give `ab()` an event ID and it validates any pass). The
only TAA-specific piece is the fingerprint used to _find_ the pass, and that's a parameter:

```python
# Generic finder — match a pass by the SRV names its pixel shader binds (case-insensitive):
cands = find_candidates({"mycolortex", "mydepthtex", "myhistorytex"}, reverse=True, stop_after=1)
ab(cands[0]["eventId"], candidate_dxbc=..., baseline_dxbc=...)
# taa_candidates() is just find_candidates(_TAA_TEX) — copy that one-liner for your own preset.
```

So to point it at a different shader you change only:

-   **The pass fingerprint** — pass your shader's distinctive SRV names to `find_candidates()` (no
    source edit needed). Match by bound resources, not draw index (indices shift frame to frame).
-   **The permutation defines** — compile A′/B with the exact `/D` set the running build used.
-   **The capture state** — pick a frame that actually exercises the code path (see lessons).

## Lessons (generalizable RE technique)

These transfer to any GPU-shader RE/validation work, not just TAA:

-   **Offline byte-identity beats any runtime check — try it first.** Far more refactoring stays
    bytecode-identical than you'd expect (even destructuring deeply-aliased decompile scratch, since
    the compiler already SSA's it). Prove those with an `fxc` byte-compare
    (`verify-shader-refactor.ps1`), one small step at a time — no game needed. Reserve a runtime swap
    for changes where the compiler legitimately emits different-but-equivalent code (op reordering,
    algebraic rewrites).
-   **Freeze inputs with a same-frame swap.** Any pass whose inputs are bound resources can be
    validated by replacing only that shader on one captured frame: A and B then run on byte-identical
    inputs, so the output diff is pure shader behaviour — no cross-launch timing/RNG/pose noise.
-   **Judge against a baseline noise floor, not zero.** The runtime compiler differs from offline
    `fxc` by a few LSBs, so even an identical shader isn't bit-exact vs the live RT. Always swap the
    _shipping_ shader in too and measure the candidate relative to that floor.
-   **Capture a state that exercises the path under test.** A degenerate frame (idle menu, static
    camera, untaken branch) gives a false pass for any state-dependent code — temporal history,
    motion vectors, conditionally-enabled features. Identify what state your code depends on and
    capture a frame in it. _(TAA: a static menu read `EQUIVALENT` even with a real motion-path bug;
    only an in-game motion frame exposed it.)_
-   **Baseline against the change's true parent, not a distant branch.** If your work is stacked on
    an unmerged fix, diffing against `dev` folds that fix into the "floor" → a huge bogus residue and
    a meaningless verdict. Compile the baseline from the immediate parent commit.
-   **Confirm which source you compiled.** A wrong-branch/ref compile silently validates the deployed
    shader against itself and always reports `EQUIVALENT`. Check `git branch`/the ref first.
-   **Find the pass by its resource fingerprint, and reverse-scan.** Match a draw by its bound
    textures/buffers, not its index (indices shift frame to frame). Post-process sits near frame end,
    so scan drawcalls in reverse — a full-frame state sweep can time out on a large capture.
-   **Interop swapchains hide the inner API.** When an app presents D3D11 work through a DX12 interop
    swapchain (here: HDR / frame-gen / upscaler paths), RenderDoc hooks the D3D12 present and the
    D3D11 draws aren't in the frame at all. Disable the feature that triggers interop to capture the
    inner API; if you can't, fall back to offline byte-identity for that permutation.
-   **Captures scale with scene complexity.** A heavy scene (especially stereo/VR) can produce a
    multi-GB capture that wedges the tools; shrink the scene (interior, lower res) to keep captures
    loadable. Verify a new capture by file timestamp/size — capture APIs sometimes return a stale path.
-   **Know your tooling's quirks.** (Here, the `renderdoc` MCP `Eval`: a fresh namespace per call can
    drop your defs — `exec` the harness and call it in the _same_ `Eval`; and stdout may lag one call
    behind, so poll again to drain it.)
-   One frame proves equivalence on that frame; loop over a few representative frames for confidence.
    This is a regression check against a known-good baseline, not a formal proof — pair it with the
    offline verifier, which formally covers everything that stays bytecode-identical.
