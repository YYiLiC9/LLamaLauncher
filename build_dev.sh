#!/usr/bin/env bash
# build_dev.sh - configure + build without vcvars (which needs reg.exe).
# Usage: bash build_dev.sh [--clean] [--jobs N]
set -u

export PATH="/usr/bin:/bin:$PATH"

ROOT="D:/Documents/workbuddy/llamacpp-gui-project"
BUILD="$ROOT/build"
VS="/c/Program Files/Microsoft Visual Studio/18/Community"
SDK="/c/Program Files (x86)/Windows Kits/10"

MSVC_VER="$(ls "$VS/VC/Tools/MSVC" | sort -V | tail -1)"
SDK_VER="$(ls "$SDK/Include" | sort -V | tail -1)"
MSVC="$VS/VC/Tools/MSVC/$MSVC_VER"
SI="$SDK/Include/$SDK_VER"
SL="$SDK/Lib/$SDK_VER"

# Windows-style paths for cl.exe's INCLUDE / LIB.
w() { printf 'C:%s' "${1#/c}" | tr '/' '\\'; }

# The resource compiler (rc.exe) and manifest tool (mt.exe) live in the SDK's
# bin folder; CMake looks them up on PATH.
export PATH="$MSVC/bin/Hostx64/x64:$SDK/bin/$SDK_VER/x64:$PATH"
export INCLUDE="$(w "$MSVC/include");$(w "$SI/ucrt");$(w "$SI/um");$(w "$SI/shared");$(w "$SI/winrt");$(w "$SI/cppwinrt")"
export LIB="$(w "$MSVC/lib/x64");$(w "$SL/ucrt/x64");$(w "$SL/um/x64")"

CMAKE="$VS/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
NINJA="$VS/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"

echo "[env] MSVC $MSVC_VER / SDK $SDK_VER"

CLEAN=0
JOBS=""
while [ $# -gt 0 ]; do
  case "$1" in
    --clean) CLEAN=1 ;;
    --jobs) shift; JOBS="$1" ;;
  esac
  shift
done

if [ "$CLEAN" = "1" ]; then rm -rf "$BUILD"; fi

if [ ! -f "$BUILD/CMakeCache.txt" ]; then
  echo "[cmake] configuring"
  # CMake is a Windows binary: it needs native paths for the tool paths, while
  # -S/-B accept the forward-slash form fine.
  "$CMAKE" -S "$ROOT" -B "$BUILD" \
      -G Ninja \
      -DCMAKE_MAKE_PROGRAM="$(w "$NINJA")" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER="$(w "$MSVC/bin/Hostx64/x64/cl.exe")" \
      -DCMAKE_CXX_COMPILER="$(w "$MSVC/bin/Hostx64/x64/cl.exe")" 2>&1 | tail -25
  [ -f "$BUILD/CMakeCache.txt" ] || { echo "[cmake] CONFIGURE FAILED"; exit 1; }
fi

echo "[ninja] building"
"$NINJA" -C "$BUILD" ${JOBS:+-j "$JOBS"} 2>&1 | tail -80
echo "[ninja] exit=$?"
ls -la "$BUILD/bin/" 2>&1