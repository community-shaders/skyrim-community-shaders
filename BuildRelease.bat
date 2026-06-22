@echo off

rem Usage: BuildRelease.bat [BUILD_PRESET] [CONFIGURE_PRESET]
rem Configure runs automatically when build\<CONFIGURE_PRESET>\CMakeCache.txt
rem is missing. CONFIGURE_PRESET defaults to BUILD_PRESET. One-click wrappers:
rem BuildDev.bat, BuildDevFast.bat, BuildPR.bat, BuildDebug.bat.

set "preset=ALL"
if NOT "%~1" == "" (
    set "preset=%~1"
)
set "configpreset=%preset%"
if NOT "%~2" == "" (
    set "configpreset=%~2"
)

echo Running build preset %preset% (configure preset %configpreset%)
if "%preset%" == "ALL" echo TIP: use 'BuildDevFast.bat' for fast warm iteration (Ninja, no LTO, no packaging)

rem Ninja presets need cl.exe on PATH; bootstrap the VS x64 environment via
rem vswhere when invoked from a plain shell.
if NOT "%configpreset%" == "Dev-Fast" goto :skipvsenv
where cl >nul 2>&1
if NOT ERRORLEVEL 1 goto :skipvsenv
echo Locating Visual Studio for the Ninja toolchain...
rem Read vswhere output via a temp file: a for /f backquote command mangles
rem this quoted path (cmd /c quote-stripping; unquoted "(x86)" breaks parsing).
set "VSINSTALL="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found; run from a VS x64 developer prompt instead
    exit /b 1
)
"%VSWHERE%" -latest -products * -property installationPath > "%TEMP%\cs_vsinstall.txt"
set /p VSINSTALL=<"%TEMP%\cs_vsinstall.txt"
del "%TEMP%\cs_vsinstall.txt" >nul 2>&1
if not defined VSINSTALL (
    echo ERROR: No Visual Studio installation found; run from a VS x64 developer prompt instead
    exit /b 1
)
rem 2>&1: vcvars64 prints harmless stderr noise; failure is caught below.
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: failed to initialize VS x64 toolchain environment
    exit /b 1
)
:skipvsenv

rem Parallelize across projects too (MSBuild /m); Ninja is parallel by default.
if not defined CMAKE_BUILD_PARALLEL_LEVEL set "CMAKE_BUILD_PARALLEL_LEVEL=%NUMBER_OF_PROCESSORS%"

rem 'if errorlevel 1' is evaluated at run time; %ERRORLEVEL% inside a
rem parenthesized block expands at parse time and misses failures.
if exist "build\%configpreset%\CMakeCache.txt" (
    echo Build folder warm, skipping configure
    goto :build
)
cmake -S . --preset=%configpreset%
if errorlevel 1 exit /b 1

:build
cmake --build --preset=%preset%
if errorlevel 1 exit /b 1
