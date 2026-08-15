<#
.SYNOPSIS
    Build DXVK's d3d11 + dxgi (the dxvk_d3d11/dxvk_dxgi renderer DLLs Community Shaders stages)
    from the extern/dxvk submodule — fast and incrementally — so every Build*.bat produces them
    with no separate meson step.

.DESCRIPTION
    Called by BuildRelease.bat (and therefore by every one-click wrapper) before the CMake
    configure/build. The configure-time staging guard in CMakeLists.txt
    (if EXISTS extern/dxvk/build/src/d3d11/d3d11.dll) then picks the DLLs up and stages them.

    Speed:
      * Only d3d11 + dxgi are built. CS loads nothing else, and the full DXVK build additionally
        needs the legacy d3d8.h which is absent from the modern Windows SDK (so a full build fails
        locally anyway).
      * Incremental: if the DLLs already exist and were built from the current submodule commit
        (recorded in a stamp file), this is a no-op — warm builds pay nothing for DXVK.
      * meson's --vsenv activates the Visual Studio toolchain itself (via vswhere), so this works
        from a plain shell without a vcvars prompt.

    Exit code: 0 on build / skip / "meson missing but DLLs already present" (non-fatal — the build
    proceeds and CMake's guard just reuses or skips). Non-zero only on a genuine meson build failure.

.PARAMETER BuildType
    meson --buildtype (default 'release'). DXVK is a runtime DLL; a release DXVK is fine even for
    a Debug-config CS build, and keeps it fast.
#>
[CmdletBinding()]
param(
    [string]$BuildType = 'release'
)

$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot  = Split-Path -Parent $ScriptDir
$DxvkSrc   = Join-Path $RepoRoot 'extern\dxvk'
$BuildDir  = Join-Path $DxvkSrc 'build'
$D3d11Dll  = Join-Path $BuildDir 'src\d3d11\dxvk_d3d11.dll'
$DxgiDll   = Join-Path $BuildDir 'src\dxgi\dxvk_dxgi.dll'
$Stamp     = Join-Path $BuildDir '.cs-dxvk-sha'

# Cold checkout: init the dxvk submodule + its nested subprojects (Vulkan-Headers, SPIRV-Headers,
# dxbc-spirv, libdisplay-info) so a fresh clone builds with no manual submodule step. --force
# repairs a half-populated checkout (dir present but empty), which git otherwise skips as
# "already at the recorded commit". Idempotent + fast when everything is already present.
$vulkanHdr = Join-Path $DxvkSrc 'include\vulkan\include\vulkan\vulkan.h'
if ((-not (Test-Path (Join-Path $DxvkSrc 'meson.build'))) -or (-not (Test-Path $vulkanHdr))) {
    Write-Host "[build-dxvk] initializing extern/dxvk submodule (+ nested Vulkan/SPIRV headers)..."
    # git prints progress + a "registered for path" notice to stderr; under EAP=Stop, PowerShell 5.1
    # wraps native stderr as a terminating NativeCommandError, so relax it just for this call.
    $prevEAP = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    & git -C $RepoRoot submodule update --init --recursive --force -- extern/dxvk 2>&1 | Write-Host
    $ErrorActionPreference = $prevEAP
}
if (-not (Test-Path (Join-Path $DxvkSrc 'meson.build'))) {
    Write-Warning "[build-dxvk] extern/dxvk still not checked out after submodule init. Skipping (mod ships without DXVK)."
    exit 0
}

# Current submodule commit drives the incremental skip.
$sha = ''
try { $sha = (& git -C $DxvkSrc rev-parse HEAD 2>$null) } catch {}
if (-not $sha) { $sha = 'unknown' }
$short = $sha.Substring(0, [Math]::Min(8, $sha.Length))
$diffHash = (& git -C $DxvkSrc diff --no-ext-diff HEAD | & git -C $DxvkSrc hash-object --stdin)
if (-not $diffHash) { $diffHash = 'clean' }
$sourceStamp = "$sha-$diffHash"

$haveDlls = (Test-Path $D3d11Dll) -and (Test-Path $DxgiDll)

# Fast path: DLLs present and built from the current submodule commit -> nothing to do.
if ($haveDlls -and (Test-Path $Stamp) -and ((Get-Content $Stamp -Raw).Trim() -eq $sourceStamp)) {
    Write-Host "[build-dxvk] DXVK d3d11+dxgi up to date ($short) - skipping"
    exit 0
}

# pip installs console scripts beside the active Python interpreter, but that
# directory is not necessarily on PATH (notably with the python.org install).
# Discover it from Python so `python -m pip install meson ninja` is sufficient.
$meson = (Get-Command meson -ErrorAction SilentlyContinue).Source
if (-not $meson) {
    $python = (Get-Command python -ErrorAction SilentlyContinue).Source
    if ($python) {
        $pythonScripts = (& $python -c "import sysconfig; print(sysconfig.get_path('scripts'))" 2>$null)
        if ($LASTEXITCODE -eq 0 -and $pythonScripts -and (Test-Path $pythonScripts)) {
            $env:Path = "$pythonScripts;$env:Path"
            $meson = (Get-Command meson -ErrorAction SilentlyContinue).Source
        }
    }
}
if (-not $meson) {
    if ($haveDlls) {
        Write-Warning "[build-dxvk] meson not found; reusing the existing DXVK build (it may be stale vs $short)."
        exit 0
    }
    Write-Warning "[build-dxvk] meson not found and no DXVK build present - the mod will ship WITHOUT the DXVK renderer. Install it with 'pip install meson ninja'."
    exit 0
}

Write-Host "[build-dxvk] building DXVK d3d11+dxgi ($short, $BuildType)..."

# meson setup is needed once (or after a clean). meson compile auto-reconfigures when meson.build
# changes; a plain submodule bump is picked up incrementally by ninja via file timestamps.
#
# NOTE on unity (jumbo) builds: '-Dunity=on' does NOT work with this DXVK, in TWO independent ways,
# so it is intentionally not used (measured + verified 2026-07):
#   1. The vendored 'libdisplay-info' C subproject has file-local helpers (add_failure,
#      parse_data_block, destroy_data_block) that collide when merged into one TU (C2084/C2371).
#      This one IS scope-avoidable: meson >= ~1.10 honors '-Dlibdisplay-info:unity=off' (meson 1.7,
#      the current MSI build, silently ignores it and still emits display-info-unity0.c.obj).
#   2. Even with libdisplay-info excluded, the DXVK CORE (src/dxvk) unity TU fails: the dxbc-spirv
#      sub-subproject header spirv_types.h has no include guard / #pragma once, so two .cpp files in
#      the merged TU re-include it -> C2011 struct redefinitions across SpirvHeader/SpirvBuilder/etc.
#      Fixing this needs include-guard edits inside the dxbc-spirv submodule headers (or excluding
#      src/dxvk from unity, which discards most of the benefit) -- neither is a committable
#      build-tooling change, and excluding the core would leave little to unify.
# Net: a robust committable unity is not achievable here without upstream/fork header fixes. The
# compile is already parallel (ninja, all cores) and the target set is pruned to d3d11+dxgi
# (-Denable_d3d8/9/10=false); that is DXVK's cold-build floor.
if (-not (Test-Path (Join-Path $BuildDir 'build.ninja'))) {
    & $meson setup $BuildDir $DxvkSrc --vsenv --buildtype $BuildType `
        -Denable_d3d8=false -Denable_d3d9=false -Denable_d3d10=false
    if ($LASTEXITCODE -ne 0) { Write-Error "[build-dxvk] meson setup failed"; exit 1 }
}

& $meson compile -C $BuildDir
if ($LASTEXITCODE -ne 0) { Write-Error "[build-dxvk] meson compile failed"; exit 1 }

if (-not ((Test-Path $D3d11Dll) -and (Test-Path $DxgiDll))) {
    Write-Error "[build-dxvk] build reported success but dxvk_d3d11.dll/dxvk_dxgi.dll are missing"
    exit 1
}

Set-Content -Path $Stamp -Value $sourceStamp -Encoding ascii
Write-Host "[build-dxvk] done ($short)"
exit 0
