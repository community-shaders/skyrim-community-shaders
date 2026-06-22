@echo off
rem One-click fast iteration build: Ninja, /Od, incremental link, full PDB,
rem DLL only (no packaging, no shader tests). Bootstraps the VS x64
rem environment automatically. Never ship these binaries.
call "%~dp0BuildRelease.bat" Dev-Fast
exit /b %ERRORLEVEL%
