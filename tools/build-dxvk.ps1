<#
.SYNOPSIS
    Incrementally build the staged DXVK d3d11 and dxgi DLLs.

.PARAMETER BuildType
    Meson build type. Defaults to release.

.PARAMETER Required
    Fail instead of reusing stale output or skipping when build prerequisites
    are unavailable. Shipping and CI-parity builds enable this switch.
#>
[CmdletBinding()]
param(
    [string]$BuildType = 'release',
    [switch]$Required
)

$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir 'DependencyBuild.Common.ps1')
$RepoRoot  = Split-Path -Parent $ScriptDir
$DxvkSrc   = Join-Path $RepoRoot 'extern\dxvk'
$BuildDir  = Join-Path $DxvkSrc 'build'
$D3d11Dll  = Join-Path $BuildDir 'src\d3d11\dxvk_d3d11.dll'
$DxgiDll   = Join-Path $BuildDir 'src\dxgi\dxvk_dxgi.dll'
$Stamp     = Join-Path $BuildDir '.cs-dxvk-sha'

# Initialize the submodule and required nested projects on a cold checkout.
$vulkanHdr = Join-Path $DxvkSrc 'include\vulkan\include\vulkan\vulkan.h'
if ((-not (Test-Path (Join-Path $DxvkSrc 'meson.build'))) -or (-not (Test-Path $vulkanHdr))) {
    Write-Host "[build-dxvk] initializing extern/dxvk submodule (+ nested Vulkan/SPIRV headers)..."
    # PowerShell 5.1 treats Git progress on stderr as a terminating error.
    $prevEAP = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & git -C $RepoRoot submodule update --init --recursive -- extern/dxvk 2>&1 | Write-Host
    } finally {
        $ErrorActionPreference = $prevEAP
    }
}
$missingSources = @()
if (-not (Test-Path (Join-Path $DxvkSrc 'meson.build'))) { $missingSources += 'meson.build' }
if (-not (Test-Path $vulkanHdr)) { $missingSources += 'nested Vulkan headers' }
if ($missingSources.Count -ne 0) {
    $missingDescription = $missingSources -join ', '
    if ($Required) {
        Write-Error "[build-dxvk] extern/dxvk is required but initialization is incomplete ($missingDescription)"
        exit 1
    }
    Write-Warning "[build-dxvk] extern/dxvk initialization is incomplete ($missingDescription). Skipping optional DXVK build; use -Required for packaging/CI."
    exit 0
}

$gitState = Get-DependencyGitState -Path $DxvkSrc -Label 'build-dxvk' -Required:$Required -IncludeSubmodules
$sha = $gitState.Sha
$short = $gitState.Short
$diffHash = $gitState.DiffHash
$stampReusable = $gitState.StampReusable

$haveDlls = (Test-Path $D3d11Dll) -and (Test-Path $DxgiDll)
$buildKey = "$sha-$diffHash|$BuildType|b_ndebug=true|cpp_args=default|apis=d3d11,dxgi"

if ($stampReusable -and $haveDlls -and (Test-Path $Stamp) -and ((Get-Content $Stamp -Raw).Trim() -eq $buildKey)) {
    Write-Host "[build-dxvk] DXVK d3d11+dxgi up to date ($short) - skipping"
    exit 0
}

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
        if ($Required) {
            Write-Error "[build-dxvk] meson is required to verify the DXVK build at $short; install it with 'python -m pip install meson ninja'"
            exit 1
        }
        Write-Warning "[build-dxvk] meson not found; reusing the existing DXVK build (it may be stale vs $short)."
        exit 0
    }
    if ($Required) {
        Write-Error "[build-dxvk] meson is required and no DXVK build is available; install it with 'python -m pip install meson ninja'"
        exit 1
    }
    Write-Warning "[build-dxvk] meson not found and no DXVK build present. Skipping optional DXVK build; use -Required for packaging/CI. Install it with 'pip install meson ninja'."
    exit 0
}

Write-Host "[build-dxvk] building DXVK d3d11+dxgi ($short, $BuildType)..."

# Meson compile reconfigures existing builds and Ninja handles source changes incrementally.
if (-not (Test-Path (Join-Path $BuildDir 'build.ninja'))) {
    & $meson setup $BuildDir $DxvkSrc --vsenv --buildtype $BuildType `
        -Db_ndebug=true `
        -Denable_d3d8=false -Denable_d3d9=false -Denable_d3d10=false
    if ($LASTEXITCODE -ne 0) { Write-Error "[build-dxvk] meson setup failed"; exit 1 }
}

# Reapply the requested build type and fixed project flags to existing build directories.
& $meson configure $BuildDir --buildtype $BuildType `
	-Db_ndebug=true -Dcpp_args= `
    -Denable_d3d8=false -Denable_d3d9=false -Denable_d3d10=false
if ($LASTEXITCODE -ne 0) { Write-Error "[build-dxvk] meson configure ($BuildType, ndebug, d3d11+dxgi only) failed"; exit 1 }

& $meson compile -C $BuildDir
if ($LASTEXITCODE -ne 0) { Write-Error "[build-dxvk] meson compile failed"; exit 1 }

if (-not ((Test-Path $D3d11Dll) -and (Test-Path $DxgiDll))) {
    Write-Error "[build-dxvk] build reported success but dxvk_d3d11.dll/dxvk_dxgi.dll are missing"
    exit 1
}

Set-Content -Path $Stamp -Value $buildKey -Encoding ascii
Write-Host "[build-dxvk] done ($short)"
exit 0
