# Quick Test: Run existing tests
# Tests if the test suite works (uses existing MSVC build)

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Running Unit Tests" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Get script directory
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptDir

# Check if tests are already built
$testDirs = @(
    "$projectRoot\build\ALL\tests\Release",
    "$projectRoot\build\ALL\tests\Debug",
    "$projectRoot\build\ALL\tests"
)

$testExe = $null
foreach ($dir in $testDirs) {
    $path = Join-Path $dir "CommunityShaders_Tests.exe"
    if (Test-Path $path) {
        $testExe = $path
        break
    }
}

if (-not $testExe) {
    Write-Host "Tests not built yet. Building..." -ForegroundColor Yellow
    Write-Host ""

    Push-Location $projectRoot
    try {
        cmake --build build/ALL --target CommunityShaders_Tests --config Release

        if ($LASTEXITCODE -ne 0) {
            Write-Host "❌ Build failed" -ForegroundColor Red
            exit 1
        }
    }
    finally {
        Pop-Location
    }

    # Try finding again
    foreach ($dir in $testDirs) {
        $path = Join-Path $dir "CommunityShaders_Tests.exe"
        if (Test-Path $path) {
            $testExe = $path
            break
        }
    }
}

if (-not $testExe) {
    Write-Host "❌ Could not find test executable" -ForegroundColor Red
    exit 1
}

Write-Host "Found test executable: $testExe" -ForegroundColor Gray
Write-Host ""
Write-Host "Running tests..." -ForegroundColor Yellow
Write-Host ""

# Run tests
& $testExe
$testExitCode = $LASTEXITCODE

if ($testExitCode -eq 0) {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "✅ All Tests Passed!" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Cyan
} else {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "❌ Some Tests Failed" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "[INFO] Code coverage runs automatically in CI on every PR" -ForegroundColor Cyan
Write-Host ""
