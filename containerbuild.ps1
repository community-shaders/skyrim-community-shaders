param (
    [string]$preset = "ALL"  # Default value for preset is "ALL"
)

Write-Host "Starting build..."

# Setup
& "C:\Program Files (x86)\Microsoft Visual Studio\2026\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" amd64

if (-Not (Test-Path -Path "CMakeUserPresets.json")) {
    Copy-Item -Path "CMakeUserPresets.json.template" -Destination "CMakeUserPresets.json"
    (Get-Content -Path "CMakeUserPresets.json") -replace 'F:/MySkyrimModpack/mods/CommunityShaders;F:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data', 'C:/skyrim-community-shaders/build' | Set-Content -Path "CMakeUserPresets.json"
    Write-Host "CMakeUserPresets.json created and modified."
} else {
    Write-Host "CMakeUserPresets.json already exists. No action taken."
}

# Generate
cmake -S . --preset=$preset --check-stamp-file build/$preset/CMakeFiles/generate.stamp
if ($LASTEXITCODE -ne 0) {
    Write-Host "CMake configuration failed."
    exit 1
}

# Build
cmake --build build/$preset --config Release
if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed."
    exit 1
}

Write-Host "Build completed successfully."
