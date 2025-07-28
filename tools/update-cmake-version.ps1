param(
    [string]$Tag
)

if (-not $Tag) {
    Write-Error "No tag provided. Use -Tag vX.Y.Z"
    exit 1
}

# Remove leading 'v' if present
$version = $Tag.TrimStart('v')

# Path to CMakeLists.txt (assume script run from repo root or CI)
$cmakeFile = "CMakeLists.txt"

if (-not (Test-Path $cmakeFile)) {
    Write-Error "CMakeLists.txt not found in current directory."
    exit 1
}

# Read all lines
$lines = Get-Content $cmakeFile

# Find the line with 'VERSION'
$versionLineIndex = $lines | Select-String -Pattern '^\s*VERSION\s+\d+\.\d+\.\d+(\.\d+)?' | Select-Object -First 1 | ForEach-Object { $_.LineNumber - 1 }

if ($null -eq $versionLineIndex) {
    Write-Error "VERSION line not found in CMakeLists.txt."
    exit 1
}

$currentVersion = ($lines[$versionLineIndex] -replace '^\s*VERSION\s+([0-9.]+).*', '$1')

if ($currentVersion -eq $version) {
    Write-Host "CMakeLists.txt already at version $version. No change needed."
    exit 0
}

Write-Host "Updating CMakeLists.txt from version $currentVersion to $version."
$lines[$versionLineIndex] = $lines[$versionLineIndex] -replace 'VERSION\s+[0-9.]+', "VERSION $version"
$lines | Set-Content $cmakeFile
Write-Host "CMakeLists.txt updated."
