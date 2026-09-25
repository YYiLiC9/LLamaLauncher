@echo off
REM ===========================================================================
REM  LlamaLauncher - one-click build script
REM
REM  Locates Visual Studio's build tools, configures CMake with Ninja, and
REM  compiles a release build into build\bin\LlamaLauncher.exe
REM
REM  Usage:  build.bat            (release)
REM          build.bat debug      (debug)
REM          build.bat clean      (wipe the build directory first)
REM ===========================================================================
setlocal EnableDelayedExpansion

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "BUILD_DIR=%ROOT%\build"
set "CONFIG=Release"

if /I "%~1"=="debug" set "CONFIG=Debug"
if /I "%~1"=="clean" (
    echo [clean] removing "%BUILD_DIR%"
    if exist "%BUILD_DIR%" rmdir /S /Q "%BUILD_DIR%"
)

REM ---- 1. Find Visual Studio --------------------------------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "!VSWHERE!" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

set "VSPATH="
if exist "!VSWHERE!" (
    for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
)

if "!VSPATH!"=="" (
    echo [error] Visual Studio with the C++ toolset was not found.
    echo         Install "Desktop development with C++" from the Visual Studio Installer.
    exit /b 1
)
echo [info] Visual Studio: !VSPATH!

REM ---- 2. Import the MSVC environment ----------------------------------------
call "!VSPATH!\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo [error] Failed to initialise the MSVC environment.
    exit /b 1
)

REM ---- 3. Locate CMake and Ninja ---------------------------------------------
set "CMAKE_EXE="
for /f "delims=" %%i in ('where cmake 2^>nul') do if "!CMAKE_EXE!"=="" set "CMAKE_EXE=%%i"
if "!CMAKE_EXE!"=="" set "CMAKE_EXE=!VSPATH!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

set "NINJA_EXE="
for /f "delims=" %%i in ('where ninja 2^>nul') do if "!NINJA_EXE!"=="" set "NINJA_EXE=%%i"
if "!NINJA_EXE!"=="" set "NINJA_EXE=!VSPATH!\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

if not exist "!CMAKE_EXE!" (
    echo [error] cmake.exe not found. Install CMake or the "C++ CMake tools" VS component.
    exit /b 1
)
echo [info] CMake: "!CMAKE_EXE!"
if exist "!NINJA_EXE!" (echo [info] Ninja: "!NINJA_EXE!") else (set "NINJA_EXE=")

REM ---- 4. Configure ----------------------------------------------------------
set "GENERATOR=-G Ninja"
set "EXTRA=-DCMAKE_MAKE_PROGRAM=!NINJA_EXE!"
if "!NINJA_EXE!"=="" (
    set "GENERATOR=-G "Visual Studio 17 2022" -A x64"
    set "EXTRA="
)

if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo [info] configuring...
    "!CMAKE_EXE!" -S "%ROOT%" -B "%BUILD_DIR%" !GENERATOR! !EXTRA! -DCMAKE_BUILD_TYPE=%CONFIG%
    if errorlevel 1 exit /b 1
)

REM ---- 5. Build --------------------------------------------------------------
echo [info] building (%CONFIG%)...
"!CMAKE_EXE!" --build "%BUILD_DIR%" --config %CONFIG%
if errorlevel 1 (
    echo [error] build failed.
    exit /b 1
)

echo.
echo [done] %BUILD_DIR%\bin\LlamaLauncher.exe
endlocal