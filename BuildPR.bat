@echo off
rem One-click CI-parity build (PR tier): /O2 without LTO, no compile-time
rem debug info (public-symbols PDB), AIO zip package. Mirrors what
rem pull-request CI compiles.
call "%~dp0BuildRelease.bat" PR
exit /b %ERRORLEVEL%
