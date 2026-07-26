@echo off
rem One-click Debug build: Debug-config DLL + AIO folder using the
rem ALL-DEBUG preset (CommonLib built from source, since the prebuilt
rem package is Release-only). Use this for debugger sessions.
setlocal
set "SKIP_CONFIGURE=1"
call "%~dp0BuildRelease.bat" Debug ALL-DEBUG
set "exit_code=%ERRORLEVEL%"
endlocal & exit /b %exit_code%
