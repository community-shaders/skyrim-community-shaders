function Get-DependencyGitState {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Label,
        [switch]$Required,
        [switch]$IncludeSubmodules
    )

    $sha = ''
    try { $sha = (& git -C $Path rev-parse HEAD 2>$null) } catch {}
    if ($LASTEXITCODE -ne 0 -or -not $sha) {
        if ($Required) { throw "[$Label] could not resolve the dependency revision" }
        $sha = 'unknown'
    }

    $statusArgs = @('-C', $Path, 'status', '--porcelain', '--untracked-files=no')
    if ($IncludeSubmodules) { $statusArgs += '--ignore-submodules=none' }
    $statusOutput = $null
    try { $statusOutput = (& git @statusArgs 2>$null) } catch {}
    $statusAvailable = $LASTEXITCODE -eq 0
    if (-not $statusAvailable -and $Required) {
        throw "[$Label] could not verify the dependency working tree"
    }

    $dirty = -not $statusAvailable -or [bool]$statusOutput
    $stampReusable = $statusAvailable
    if ($IncludeSubmodules -and $statusAvailable) {
        $nestedDirty = @($statusOutput | Where-Object { $_ -cmatch '^.m\s' }).Count -ne 0
        if ($nestedDirty -and $Required) {
            throw "[$Label] a nested dependency has uncommitted changes"
        }
        if ($nestedDirty) { $stampReusable = $false }
    }

    $diffHash = 'clean'
    if ($dirty) {
        $diffFile = Join-Path ([System.IO.Path]::GetTempPath()) ("cs-dependency-{0}.patch" -f [guid]::NewGuid())
        try {
            $diffArgs = @('-C', $Path, 'diff', '--no-ext-diff', '--binary')
            if ($IncludeSubmodules) { $diffArgs += @('--ignore-submodules=none', '--submodule=diff') }
            $diffArgs += @("--output=$diffFile", 'HEAD')
            & git @diffArgs
            if ($LASTEXITCODE -eq 0) { $diffHash = (& git -C $Path hash-object $diffFile 2>$null) }
            if ($LASTEXITCODE -ne 0 -or -not $diffHash) { throw 'fingerprint failed' }
        } catch {
            if ($Required) { throw "[$Label] could not fingerprint dependency changes" }
            $diffHash = 'unavailable'
            $stampReusable = $false
        } finally {
            Remove-Item -LiteralPath $diffFile -Force -ErrorAction SilentlyContinue
        }
    }

    [pscustomobject]@{
        Sha = $sha
        Short = $sha.Substring(0, [Math]::Min(8, $sha.Length))
        Dirty = $dirty
        DiffHash = $diffHash
        StampReusable = $stampReusable
    }
}
