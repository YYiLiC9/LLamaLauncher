#!/usr/bin/env bash
# Compile the yaml probe tool with the same env build_dev.sh assembles.
set -u
export PATH="/usr/bin:/bin:$PATH"

ROOT="D:/Documents/workbuddy/llamacpp-gui-project"
VS="/c/Program Files/Microsoft Visual Studio/18/Community"
SDK="/c/Program Files (x86)/Windows Kits/10"
MSVC_VER="$(ls "$VS/VC/Tools/MSVC" | sort -V | tail -1)"
SDK_VER="$(ls "$SDK/Include" | sort -V | tail -1)"
MSVC="$VS/VC/Tools/MSVC/$MSVC_VER"
SI="$SDK/Include/$SDK_VER"
SL="$SDK/Lib/$SDK_VER"
w() { printf 'C:%s' "${1#/c}" | tr '/' '\\'; }
export INCLUDE="$(w "$MSVC/include");$(w "$SI/ucrt");$(w "$SI/um");$(w "$SI/shared");$(w "$SI/winrt");$(w "$SI/cppwinrt")"
export LIB="$(w "$MSVC/lib/x64");$(w "$SL/ucrt/x64");$(w "$SL/um/x64")"
export PATH="$MSVC/bin/Hostx64/x64:$SDK/bin/$SDK_VER/x64:$PATH"

cd "$ROOT"
cl -nologo -std:c++20 -utf-8 -EHsc -Isrc tools/yaml_dump.cpp src/core/yaml.cpp src/core/util.cpp -Fe:build/yaml_dump.exe -link gdi32.lib user32.lib
