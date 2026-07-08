<#
.SYNOPSIS
    Build the Community Shaders Streamline fork plugins (sl.interposer / sl.fsr / sl.xess) from the
    extern/Streamline submodule — fast and incrementally — so every Build*.bat produces them with no
    separate manual step.

.DESCRIPTION
    Called by BuildRelease.bat (and therefore by every one-click wrapper) right after
    tools/build-dxvk.ps1, BEFORE the CMake configure/build. CMakeLists.txt globs the staged plugins
    from extern/Streamline/_artifacts/sl.*/Develop_x64/sl.*.dll and installs the
    sl.(fsr|xess|interposer) subset; without this step that directory is empty and a cold boot ships
    a mod with no upscaling / frame-generation (CMake only prints a WARNING).

    Only the three CS-built fork plugins are compiled — the signed production runtimes ship committed
    under package/SKSE/Plugins/CommunityShaders/Streamline/. sl.interposer, sl.fsr and sl.xess have no
    inter-project dependencies (they compile the core sl.* sources they need directly and LoadLibrary
    the runtime DLLs), so targeting just these three keeps the build minimal.

    Speed / correctness:
      * Incremental: msbuild is invoked WITHOUT a Clean target, so an unchanged tree relinks nothing.
        A stamp (recorded submodule commit) short-circuits the whole script when the DLLs are already
        built from the current commit — warm builds pay nothing.
      * Config is Develop_x64 (what CMake globs). Develop is the fork's profiling config (optimized
        with logging), the intended shipping-with-diagnostics build for these plugins.
      * setup.bat (packman pull + premake) is only run when the VS solution is missing.

    Exit code: 0 on build / skip / "toolchain missing but DLLs already present" (non-fatal — CMake's
    glob then reuses whatever is there). Non-zero only on a genuine msbuild failure.

.PARAMETER Config
    msbuild Configuration (default 'Develop'). Must stay Develop_x64 to match the CMake glob.
#>
[CmdletBinding()]
param(
    [string]$Config = 'Develop'
)

$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot  = Split-Path -Parent $ScriptDir
$SlSrc     = Join-Path $RepoRoot 'extern\Streamline'
$Artifacts = Join-Path $SlSrc '_artifacts'
$ProjDir   = Join-Path $SlSrc '_project\vs2022'
$Sln       = Join-Path $ProjDir 'streamline.sln'

# Plugins CS stages — MUST match the CMake install() filter sl.(fsr|fsr_g|xess|interposer). These
# have no inter-project dependencies (each compiles the core sl.* sources it needs and LoadLibrary's
# the runtime DLLs), so we build the individual .vcxproj files directly. Passing project targets to
# the .sln (msbuild /t:sl_fsr) is rejected as MSB4057 by this premake-generated solution.
$plugins = @('sl.interposer', 'sl.fsr', 'sl.fsr_g', 'sl.xess')

function Get-PluginDll([string]$name) { Join-Path $Artifacts "$name\${Config}_x64\$name.dll" }

# Cold checkout: init the Streamline submodule so a fresh clone builds with no manual step. --force
# repairs a half-populated checkout (dir present but empty). Idempotent + fast when already present.
if (-not (Test-Path (Join-Path $SlSrc 'premake.lua'))) {
    Write-Host "[build-streamline] initializing extern/Streamline submodule..."
    # git's stderr progress is not an error; relax EAP=Stop so PowerShell 5.1 doesn't treat the
    # native stderr as a terminating NativeCommandError (same guard as the nested init below).
    $prevEAP = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    & git -C $RepoRoot submodule update --init --force -- extern/Streamline 2>&1 | Write-Host
    $ErrorActionPreference = $prevEAP
}
if (-not (Test-Path (Join-Path $SlSrc 'premake.lua'))) {
    Write-Warning "[build-streamline] extern/Streamline still not checked out after submodule init. Skipping (mod ships without the fork plugins)."
    exit 0
}

# Nested submodules that provide the FFX / XeSS PUBLIC HEADERS sl.fsr / sl.xess compile against
# (the runtimes themselves ship prebuilt and are LoadLibrary'd). Tolerate a failed nested init (a
# historically unfetchable xess pin) — the recorded commit still checks out a usable tree; only warn
# if the headers are genuinely absent afterwards.
$ffxInc  = Join-Path $SlSrc 'external\fidelityfx-sdk\ffx-api\include'
$xessInc = Join-Path $SlSrc 'external\xess\inc'
if ((-not (Test-Path $ffxInc)) -or (-not (Test-Path $xessInc))) {
    Write-Host "[build-streamline] initializing Streamline nested submodules (FFX / XeSS headers)..."
    $prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    & git -C $SlSrc submodule update --init --force -- external/fidelityfx-sdk external/xess 2>&1 | Write-Host
    $ErrorActionPreference = $prev
    if (-not (Test-Path $ffxInc))  { Write-Warning "[build-streamline] FFX headers ($ffxInc) missing; sl.fsr may fail to compile." }
    if (-not (Test-Path $xessInc)) { Write-Warning "[build-streamline] XeSS headers ($xessInc) missing; sl.xess may fail to compile." }
}

# Current submodule commit drives the incremental skip.
$sha = ''
try { $sha = (& git -C $SlSrc rev-parse HEAD 2>$null) } catch {}
if (-not $sha) { $sha = 'unknown' }
$short = $sha.Substring(0, [Math]::Min(8, $sha.Length))
$Stamp = Join-Path $Artifacts ".cs-sl-sha-$Config"

$dlls     = $plugins | ForEach-Object { Get-PluginDll $_ }
$haveDlls = ($dlls | ForEach-Object { Test-Path $_ }) -notcontains $false

# Fast path: all plugins present and built from the current submodule commit -> nothing to do.
if ($haveDlls -and (Test-Path $Stamp) -and ((Get-Content $Stamp -Raw).Trim() -eq $sha)) {
    Write-Host "[build-streamline] fork plugins up to date ($short) - skipping"
    exit 0
}

# Locate MSBuild via vswhere (this script runs before CMake bootstraps the VS toolchain, so cl/msbuild
# are not assumed to be on PATH). VS2022 or newer; the vs2022 project tree is format-compatible.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = $null
if (Test-Path $vswhere) {
    $msbuild = (& $vswhere -latest -products * -requires Microsoft.Component.MSBuild `
        -find 'MSBuild\**\Bin\amd64\MSBuild.exe' 2>$null | Select-Object -First 1)
    if (-not $msbuild) {
        $msbuild = (& $vswhere -latest -products * -requires Microsoft.Component.MSBuild `
            -find 'MSBuild\**\Bin\MSBuild.exe' 2>$null | Select-Object -First 1)
    }
}
if (-not $msbuild -or -not (Test-Path $msbuild)) {
    if ($haveDlls) {
        Write-Warning "[build-streamline] MSBuild not found; reusing the existing fork plugins (they may be stale vs $short)."
        exit 0
    }
    Write-Warning "[build-streamline] MSBuild not found and no fork plugins present - the mod will ship WITHOUT the fork sl.* plugins (DLSS/FSR/XeSS unavailable). Install Visual Studio 2022+ with the C++ workload."
    exit 0
}

# Generate the VS solution (packman pull + premake) only when it is missing. setup.bat regenerates
# the whole _project tree; a plain submodule bump is picked up incrementally by msbuild via timestamps.
if (-not (Test-Path $Sln)) {
    Write-Host "[build-streamline] generating VS solution (setup.bat: packman + premake)..."
    # setup.bat uses paths relative to the Streamline root (.\tools\packman, .\_project). cmd cannot find
    # a bare 'setup.bat' via the cwd (NoDefaultCurrentDirectoryInExePath) and Push-Location does NOT
    # change the process cwd cmd inherits — so cd inside cmd itself. EAP relaxed: setup.bat's tooling
    # (packman/premake/vswhere) writes benign stderr that EAP=Stop would otherwise treat as fatal.
    $prevEAP = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    & cmd /c "cd /d $SlSrc && .\setup.bat vs2022" 2>&1 | Write-Host
    $ErrorActionPreference = $prevEAP
    if (-not (Test-Path $Sln)) { Write-Error "[build-streamline] setup.bat did not produce $Sln"; exit 1 }
}

Write-Host "[build-streamline] building fork plugins [$($plugins -join ', ')] ($short, ${Config}_x64)..."

# Build each plugin's .vcxproj directly (default Build target => incremental; no Clean). /m
# parallelizes the compile within each project across all cores.
foreach ($p in $plugins) {
    $vcxproj = Join-Path $ProjDir "$p.vcxproj"
    if (-not (Test-Path $vcxproj)) { Write-Error "[build-streamline] missing project $vcxproj"; exit 1 }
    & $msbuild $vcxproj /m /p:Configuration=$Config /p:Platform=x64 /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) { Write-Error "[build-streamline] msbuild failed for $p"; exit 1 }
}

$missing = $plugins | Where-Object { -not (Test-Path (Get-PluginDll $_)) }
if ($missing) {
    Write-Error "[build-streamline] build reported success but missing: $($missing -join ', ')"
    exit 1
}

Set-Content -Path $Stamp -Value $sha -Encoding ascii
Write-Host "[build-streamline] done ($short)"
exit 0
