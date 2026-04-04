param(
    [Parameter(Mandatory = $true)]
    [string]$Name,

    [string]$Branch = $Name,
    [string]$Path,
    [string]$StartPoint = "HEAD",
    [switch]$NoSubmodules,
    [switch]$ForcePresetCopy
)

$repoRoot = ([string](git rev-parse --show-toplevel 2>$null)).Trim()
if (-not $repoRoot) {
    Write-Error "Run this script from within a git checkout or worktree for this repository."
    exit 1
}

$commonDir = ([string](git rev-parse --path-format=absolute --git-common-dir 2>$null)).Trim()
if (-not $commonDir) {
    Write-Error "Failed to resolve the repository common git directory."
    exit 1
}

$mainRepoRoot = Split-Path $commonDir -Parent
$repoName = Split-Path $mainRepoRoot -Leaf
$defaultWorktreeRoot = Join-Path (Split-Path $mainRepoRoot -Parent) ($repoName + ".worktrees")

if (-not $Path) {
    $Path = Join-Path $defaultWorktreeRoot $Name
}

$Path = [System.IO.Path]::GetFullPath($Path)
$targetParent = Split-Path $Path -Parent
if (-not (Test-Path $targetParent)) {
    New-Item -ItemType Directory -Path $targetParent -Force | Out-Null
}

if (Test-Path $Path) {
    Write-Error "Target path already exists: $Path"
    exit 1
}

& git -C $mainRepoRoot show-ref --verify --quiet "refs/heads/$Branch"
$branchExists = $LASTEXITCODE -eq 0

if ($branchExists) {
    Write-Host "Creating worktree for existing branch '$Branch' at $Path"
    & git -C $mainRepoRoot worktree add $Path $Branch
}
else {
    Write-Host "Creating worktree at $Path with new branch '$Branch' from '$StartPoint'"
    & git -C $mainRepoRoot worktree add -b $Branch $Path $StartPoint
}

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if (-not $NoSubmodules) {
    Write-Host "Initializing submodules in new worktree"
    & git -C $Path submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Submodule initialization failed. The worktree was created but may be incomplete."
        exit $LASTEXITCODE
    }
}

$sourcePreset = Join-Path $mainRepoRoot "CMakeUserPresets.json"
$targetPreset = Join-Path $Path "CMakeUserPresets.json"

if (Test-Path $sourcePreset) {
    if ((-not (Test-Path $targetPreset)) -or $ForcePresetCopy) {
        try {
            Copy-Item $sourcePreset $targetPreset -Force -ErrorAction Stop
            Write-Host "Copied CMakeUserPresets.json into the new worktree"
        }
        catch {
            Write-Error "Failed to copy CMakeUserPresets.json: $($_.Exception.Message)"
            exit 1
        }
    }
    else {
        Write-Host "Skipped preset copy because the worktree already has CMakeUserPresets.json"
    }
}
else {
    Write-Host "No CMakeUserPresets.json found in the main repo checkout; skipping preset copy"
}

Write-Host "Worktree ready: $Path"
