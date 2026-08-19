[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Manifest,
    [Parameter(Mandatory = $true)][string]$InstalledRuntime,
    [Parameter(Mandatory = $true)][string]$AioRuntime
)

$ErrorActionPreference = 'Stop'
$expected = @(Get-Content -LiteralPath $Manifest | Where-Object { $_ })

function Get-RuntimeNames([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "Runtime directory does not exist: $Path"
    }
    return @(Get-ChildItem -LiteralPath $Path -File | Select-Object -ExpandProperty Name | Sort-Object)
}

$installed = Get-RuntimeNames $InstalledRuntime
$aio = Get-RuntimeNames $AioRuntime
$missingInstalled = @($expected | Where-Object { $_ -notin $installed })
$missingAio = @($expected | Where-Object { $_ -notin $aio })
if ($missingInstalled.Count) { throw "Install runtime is missing: $($missingInstalled -join ', ')" }
if ($missingAio.Count) { throw "AIO runtime is missing: $($missingAio -join ', ')" }

$installedDxvk = @($installed | Where-Object { $_ -in $expected })
$aioDxvk = @($aio | Where-Object { $_ -in $expected })
if (Compare-Object $installedDxvk $aioDxvk) {
    throw 'Install and AIO Vulkan runtime inventories differ'
}
if (@($installed + $aio | Where-Object { $_ -match '(?i)(dx12|d3d12)' }).Count) {
    throw 'Legacy DX12 runtime found in packaged output'
}

Write-Host 'DXVK runtime manifests match'
