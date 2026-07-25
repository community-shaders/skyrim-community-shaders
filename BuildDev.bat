@echo off
rem One-click developer build: fully optimized DLL + AIO folder in
rem build\ALL\aio ready to copy into a mod folder. Configures on first run.
setlocal
set "SKIP_CONFIGURE=1"
call "%~dp0BuildRelease.bat" Dev ALL
set "exit_code=%ERRORLEVEL%"
endlocal & exit /b %exit_code%
