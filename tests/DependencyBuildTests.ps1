$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\tools\DependencyBuild.Common.ps1')

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("cs-dependency-test-{0}" -f [guid]::NewGuid())
try {
    New-Item -ItemType Directory -Path $tempRoot | Out-Null
    & git -C $tempRoot init --quiet
    & git -C $tempRoot config user.email 'tests@community-shaders.invalid'
    & git -C $tempRoot config user.name 'Community Shaders Tests'
    Set-Content -LiteralPath (Join-Path $tempRoot 'tracked.txt') -Value 'baseline' -Encoding ascii
    & git -C $tempRoot add tracked.txt
    & git -C $tempRoot commit --quiet -m baseline

    $clean = Get-DependencyGitState -Path $tempRoot -Label test
    if ($clean.Dirty -or $clean.DiffHash -ne 'clean' -or -not $clean.StampReusable) {
        throw 'clean dependency state was classified incorrectly'
    }

    Set-Content -LiteralPath (Join-Path $tempRoot 'tracked.txt') -Value 'modified' -Encoding ascii
    $dirty = Get-DependencyGitState -Path $tempRoot -Label test
    if (-not $dirty.Dirty -or $dirty.DiffHash -in @('', 'clean', 'unavailable') -or -not $dirty.StampReusable) {
        throw 'dirty dependency state was not reproducibly fingerprinted'
    }

    Write-Host 'Dependency build state tests passed'
} finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}
