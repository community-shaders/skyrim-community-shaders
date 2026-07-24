@echo off
rem One-click developer build: fully optimized DLL + AIO folder in
rem build\ALL\aio ready to copy into a mod folder. Configures on first run.
set "SKIP_CONFIGURE=1"
call "%~dp0BuildRelease.bat" Dev ALL
exit /b %ERRORLEVEL%
