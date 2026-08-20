@echo off
rem One-click developer build: fully optimized DLL + AIO folder, deployed to
rem CommunityShadersOutputDir from CMakeUserPresets.json. Configures on first run.
setlocal
set "SKIP_CONFIGURE=1"
call "%~dp0BuildRelease.bat" Dev-Deploy ALL-WITH-AUTO-DEPLOYMENT
set "exit_code=%ERRORLEVEL%"
endlocal & exit /b %exit_code%
