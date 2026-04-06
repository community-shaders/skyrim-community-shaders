param(
    [switch]$Global
)

$repoRoot = ([string](git rev-parse --show-toplevel 2>$null)).Trim()
if (-not $repoRoot) {
    Write-Error "Run this script from within a git checkout or worktree for this repository."
    exit 1
}

$scriptPath = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "new-worktree.ps1"))
if (-not (Test-Path $scriptPath)) {
    Write-Error "Worktree helper not found at $scriptPath"
    exit 1
}

$configScope = if ($Global) { "--global" } else { "--local" }
$scriptPathForAlias = $scriptPath -replace '\\', '/'
$aliasValue = "!powershell.exe -NoProfile -ExecutionPolicy Bypass -File '$scriptPathForAlias' -Name"

git config $configScope alias.new-worktree $aliasValue
if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to install git new-worktree alias."
    exit $LASTEXITCODE
}

if ($Global) {
    Write-Host "Installed global alias: git new-worktree"
}
else {
    Write-Host "Installed repo-local alias: git new-worktree"
}

Write-Host "Usage: git new-worktree <name> [additional new-worktree.ps1 options]"
