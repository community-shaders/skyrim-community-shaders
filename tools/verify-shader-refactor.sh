#!/usr/bin/env bash
# Thin wrapper around verify-shader-refactor.ps1 for bash/WSL/git-bash users.
# fxc.exe is Windows-only and MSYS mangles its /switches, so the real work lives
# in PowerShell. This just forwards arguments verbatim.
#
# Usage: tools/verify-shader-refactor.sh package/Shaders/Foo.hlsl [-BaseRef HEAD~1] ...
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ps1="$here/verify-shader-refactor.ps1"

if command -v pwsh >/dev/null 2>&1; then
	exec pwsh -NoProfile -ExecutionPolicy Bypass -File "$ps1" "$@"
elif command -v powershell.exe >/dev/null 2>&1; then
	exec powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$ps1" "$@"
else
	echo "Need PowerShell (pwsh or powershell.exe) on PATH." >&2
	exit 1
fi
