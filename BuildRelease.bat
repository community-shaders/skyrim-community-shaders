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

rem Build the DXVK renderer DLLs (dxvk_d3d11.dll / dxvk_dxgi.dll) the plugin stages, BEFORE configure so
rem CMake's EXISTS guard picks them up. Fast + incremental: a no-op when the dxvk submodule is
rem unchanged (see tools\build-dxvk.ps1). All Build*.bat wrappers reach this through here.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\build-dxvk.ps1"
if errorlevel 1 exit /b 1

rem Build the Streamline fork plugins (sl.interposer / sl.fsr / sl.xess) CMake stages from
rem extern\Streamline\_artifacts\sl.*\Develop_x64. Without them the mod ships with no upscaling/FG
rem (CMake only warns). Fast + incremental: a no-op when the Streamline submodule is unchanged
rem (see tools\build-streamline.ps1). All Build*.bat wrappers reach this through here.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\build-streamline.ps1"
if errorlevel 1 exit /b 1

rem A warm build may have been configured before DXVK existed. In that case the
rem configure-time EXISTS guard omitted the AIO staging command, so configure
rem once more now that build-dxvk.ps1 has produced the DLLs.
if not exist "build\%configpreset%\CMakeCache.txt" goto :configure
if not exist "extern\dxvk\build\src\d3d11\dxvk_d3d11.dll" goto :warm
findstr /s /m /c:"stage-dxvk-dlls.ps1" "build\%configpreset%\*.vcxproj" "build\%configpreset%\build.ninja" >nul 2>&1
if errorlevel 1 (
    echo DXVK was built after the last configure; refreshing CMake staging rules
    goto :configure
)

:warm
rem 'if errorlevel 1' is evaluated at run time; %ERRORLEVEL% inside a
rem parenthesized block expands at parse time and misses failures.
if exist "build\%configpreset%\CMakeCache.txt" (
    echo Build folder warm, skipping configure
    goto :build
)
:configure
cmake -S . --preset=%configpreset%
if errorlevel 1 exit /b 1

:build
cmake --build --preset=%preset%
if errorlevel 1 exit /b 1
