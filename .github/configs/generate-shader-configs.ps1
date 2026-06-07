#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Generates shader validation configuration files for Community Shaders.

.DESCRIPTION
    This script generates shader-validation.yaml by analyzing Community Shaders log files from
    Skyrim Special Edition installations. It requires hlslkit to be installed and Skyrim Special
    Edition to have been run with specific settings.

.PARAMETER OutputDir
    Directory where the generated YAML files will be saved. Defaults to current directory.

.PARAMETER Force
    Force generation even if log files are not recent.

.PARAMETER LogFile
    Process a specific log file directly instead of searching for Skyrim installations.
    When used, also specify -OutputName for the generated config file name.

.PARAMETER OutputName
    Name of the output YAML file when using -LogFile. Defaults to "shader-validation.yaml".

.EXAMPLE
    .\generate-shader-configs.ps1

.EXAMPLE
    .\generate-shader-configs.ps1 -OutputDir "custom/path" -Force

.EXAMPLE
    .\generate-shader-configs.ps1 -LogFile "C:\Path\To\CommunityShaders.log" -OutputName "my-validation.yaml"

.NOTES
    Prerequisites:
    1. Install hlslkit: pip install hlslkit
    2. For automatic detection (default mode):
       a. For each Skyrim version you want to generate configs for:
          - Clear the disk cache (Community Shaders menu -> Advanced -> Clear Disk Cache)
          - Set log level to Debug or Trace (Community Shaders menu -> Advanced -> Log Level)
          - Enable disk cache if not already enabled
          - Run the game and wait for shader compilation to complete.
       b. The log files should be recent (generated after clearing cache)
    3. For direct log file processing:
       - Use -LogFile parameter to specify the path to a Community Shaders log file
       - Use -OutputName to specify the name of the generated config file
#>

param(
    [Parameter(Mandatory=$false)]
    [string]$OutputDir = ".",

    [Parameter(Mandatory=$false)]
    [switch]$Force,

    [Parameter(Mandatory=$false)]
    [string]$LogFile,

    [Parameter(Mandatory=$false)]
    [string]$OutputName = "shader-validation.yaml"
)

# Check if hlslkit is installed
try {
    $null = Get-Command "hlslkit-generate" -ErrorAction Stop
    Write-Host "hlslkit-generate found" -ForegroundColor Green
} catch {
    Write-Error "hlslkit-generate not found. Please install hlslkit: pip install hlslkit"
    exit 1
}

# Function to find Skyrim installation paths
function Find-SkyrimPaths {
    $paths = @()

    # Check common document locations
    $documentsPath = [Environment]::GetFolderPath("MyDocuments")
    $myGamesPath = Join-Path $documentsPath "My Games"

    # Check for Skyrim Special Edition
    $sePath = Join-Path $myGamesPath "Skyrim Special Edition"
    if (Test-Path $sePath) {
        $paths += @{
            Name = "Skyrim Special Edition"
            Path = $sePath
            LogPath = Join-Path $sePath "SKSE\CommunityShaders.log"
            ConfigName = "shader-validation.yaml"
            Type = "SE"
        }
    }

    # Check CommunityShadersOutputDir environment variable
    $outputDir = $env:CommunityShadersOutputDir
    if ($outputDir -and (Test-Path $outputDir)) {
        Write-Host "Found CommunityShadersOutputDir: $outputDir" -ForegroundColor Yellow

        $skyrimExe = Get-ChildItem -Path $outputDir -Recurse -Name "SkyrimSE.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($skyrimExe) {
            Write-Host "Detected Skyrim SE installation in CommunityShadersOutputDir" -ForegroundColor Green
        }
    }

    return $paths
}

# Function to check if log file is recent and valid
function Test-LogFile {
    param(
        [string]$LogPath,
        [string]$GameName
    )

    if (-not (Test-Path $LogPath)) {
        Write-Warning "Log file not found for $GameName`: $LogPath"
        return $false
    }

    $logFile = Get-Item $LogPath
    $age = (Get-Date) - $logFile.LastWriteTime

    if ($age.TotalHours -gt 24 -and -not $Force) {
        Write-Warning "Log file for $GameName is older than 24 hours. Use -Force to generate anyway."
        Write-Host "Log file age: $($age.TotalHours.ToString('F1')) hours" -ForegroundColor Yellow
        return $false
    }

    # Check if log contains shader compilation activity
    $content = Get-Content $LogPath -Tail 1000 | Out-String
    if ($content -notmatch "shader|compilation|cache") {
        Write-Warning "Log file for $GameName doesn't appear to contain shader compilation activity."
        if (-not $Force) {
            Write-Host "Make sure you've cleared the disk cache and run the game to trigger shader compilation." -ForegroundColor Yellow
            return $false
        }
    }

    Write-Host "Log file for $GameName is valid" -ForegroundColor Green
    return $true
}

# Main script
Write-Host "Community Shaders Configuration Generator" -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan

# Ensure output directory exists
if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
    Write-Host "Created output directory: $OutputDir" -ForegroundColor Green
}

# Handle direct log file processing
if ($LogFile) {
    Write-Host "Processing log file directly: $LogFile" -ForegroundColor Yellow

    if (-not (Test-Path $LogFile)) {
        Write-Error "Log file not found: $LogFile"
        exit 1
    }

    if (-not (Test-LogFile -LogPath $LogFile -GameName "Direct Log File")) {
        Write-Host "Log file validation failed. Use -Force to process anyway." -ForegroundColor Red
        if (-not $Force) {
            exit 1
        }
    }

    $outputPath = Join-Path $OutputDir $OutputName
    try {
        Write-Host "Generating $OutputName..." -ForegroundColor Blue
        Write-Host "Running: hlslkit-generate --log `"$LogFile`" --output `"$outputPath`"" -ForegroundColor Gray

        & hlslkit-generate --log $LogFile --output $outputPath

        if ($LASTEXITCODE -eq 0) {
            Write-Host "Successfully generated $OutputName" -ForegroundColor Green
            Write-Host "File saved to: $outputPath" -ForegroundColor Gray
        } else {
            Write-Error "Failed to generate $OutputName (exit code: $LASTEXITCODE)"
            exit 1
        }
    } catch {
        Write-Error "Error generating $OutputName`: $($_.Exception.Message)"
        exit 1
    }

    exit 0
}

# Find Skyrim installations
$skyrimPaths = Find-SkyrimPaths

if ($skyrimPaths.Count -eq 0) {
    Write-Error "No Skyrim installations found. Please ensure Skyrim Special Edition is installed."
    exit 1
}

Write-Host "Found $($skyrimPaths.Count) Skyrim installation(s):" -ForegroundColor Green
foreach ($path in $skyrimPaths) {
    Write-Host "  - $($path.Name): $($path.Path)" -ForegroundColor Gray
}

# Process each installation
$generated = 0
foreach ($skyrim in $skyrimPaths) {
    Write-Host "`nProcessing $($skyrim.Name)..." -ForegroundColor Yellow

    if (-not (Test-LogFile -LogPath $skyrim.LogPath -GameName $skyrim.Name)) {
        Write-Host "Skipping $($skyrim.Name) due to invalid/missing log file." -ForegroundColor Red
        continue    }

    $outputPath = Join-Path $OutputDir $skyrim.ConfigName

    try {
        Write-Host "Generating $($skyrim.ConfigName)..." -ForegroundColor Blue
        Write-Host "Running: hlslkit-generate --log `"$($skyrim.LogPath)`" --output `"$outputPath`"" -ForegroundColor Gray

        & hlslkit-generate --log $skyrim.LogPath --output $outputPath

        if ($LASTEXITCODE -eq 0) {
            Write-Host "Successfully generated $($skyrim.ConfigName)" -ForegroundColor Green
            $generated++
        } else {
            Write-Error "Failed to generate $($skyrim.ConfigName) (exit code: $LASTEXITCODE)"
        }
    } catch {
        Write-Error "Error generating $($skyrim.ConfigName): $($_.Exception.Message)"
    }
}

Write-Host "`n=========================================" -ForegroundColor Cyan
if ($generated -gt 0) {
    Write-Host "Successfully generated $generated configuration file(s)" -ForegroundColor Green
    Write-Host "Files saved to: $OutputDir" -ForegroundColor Gray
} else {
    Write-Host "No configuration files were generated" -ForegroundColor Red
    Write-Host "To generate shader validation configs:" -ForegroundColor Yellow
    Write-Host "1. Clear the disk cache in Community Shaders menu" -ForegroundColor Gray
    Write-Host "2. Set log level to Debug in Community Shaders menu" -ForegroundColor Gray
    Write-Host "3. Run the game and load a save to trigger shader compilation" -ForegroundColor Gray
    Write-Host "4. Run this script again" -ForegroundColor Gray
}
