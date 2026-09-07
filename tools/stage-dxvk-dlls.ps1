<#
.SYNOPSIS
    Stage prefixed DXVK d3d11 and dxgi DLLs into the mod runtime directory.

.PARAMETER Src
    DXVK build directory. Nested and flat DLL layouts are accepted.

.PARAMETER Dst
    Destination directory, created when missing.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Src,
    [Parameter(Mandatory = $true)][string]$Dst
)

$ErrorActionPreference = 'Stop'

function Resolve-DxvkDll {
    param([string]$Base, [string]$Subdir, [string]$Name)
    $nested = Join-Path $Base (Join-Path $Subdir $Name)
    if (Test-Path -LiteralPath $nested) { return (Resolve-Path -LiteralPath $nested).Path }
    $flat = Join-Path $Base $Name
    if (Test-Path -LiteralPath $flat) { return (Resolve-Path -LiteralPath $flat).Path }
    throw "Could not find $Name under '$Base' (looked for '$nested' and '$flat')"
}

# Preserve timestamps when the staged bytes are unchanged.
function Write-IfDifferent {
    param([string]$Path, [byte[]]$Bytes)
    if (Test-Path -LiteralPath $Path) {
        $existing = [System.IO.File]::ReadAllBytes($Path)
        if ($existing.Length -eq $Bytes.Length) {
            $same = $true
            for ($i = 0; $i -lt $Bytes.Length; $i++) {
                if ($existing[$i] -ne $Bytes[$i]) { $same = $false; break }
            }
            if ($same) { Write-Host "  unchanged: $Path"; return }
        }
    }
    [System.IO.File]::WriteAllBytes($Path, $Bytes)
    Write-Host "  wrote:     $Path"
}

$d3d11Src = Resolve-DxvkDll -Base $Src -Subdir 'd3d11' -Name 'dxvk_d3d11.dll'
$dxgiSrc = Resolve-DxvkDll -Base $Src -Subdir 'dxgi' -Name 'dxvk_dxgi.dll'

if (-not (Test-Path -LiteralPath $Dst)) {
    New-Item -ItemType Directory -Path $Dst -Force | Out-Null
}
$Dst = (Resolve-Path -LiteralPath $Dst).Path

Write-Host "Staging DXVK DLLs:"
Write-Host "  d3d11 src: $d3d11Src"
Write-Host "  dxgi  src: $dxgiSrc"
Write-Host "  dst:       $Dst"

Write-IfDifferent -Path (Join-Path $Dst 'dxvk_dxgi.dll') -Bytes ([System.IO.File]::ReadAllBytes($dxgiSrc))
Write-IfDifferent -Path (Join-Path $Dst 'dxvk_d3d11.dll') -Bytes ([System.IO.File]::ReadAllBytes($d3d11Src))

Write-Host "Done."
