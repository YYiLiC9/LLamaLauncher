cmake_minimum_required(VERSION 3.20)

project(LlamaLauncher
    VERSION 1.0.0
    DESCRIPTION "A lightweight native Win11-style GUI launcher for llama.cpp"
    LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(LlamaLauncher WIN32
    src/main.cpp
    src/app.cpp
    src/app_paint.cpp
    src/app_input.cpp
    res/app.rc

    src/core/util.cpp
    src/core/paths.cpp
    src/core/i18n.cpp
    src/core/yaml.cpp
    src/core/catalog.cpp
    src/core/store.cpp
    src/core/process.cpp
    src/core/monitor.cpp

    src/ui/theme.cpp
    src/ui/shell.cpp
    src/ui/dialog.cpp
    src/ui/views.cpp
    src/ui/webview.cpp
)

target_include_directories(LlamaLauncher PRIVATE
    src
    third_party/webview2/include
)

# ---- WebView2 --------------------------------------------------------------
# Vendored from the Microsoft.Web.WebView2 NuGet package (see third_party).
# The *static* loader is used on purpose: it links the loader into the exe, so
# there is no WebView2Loader.dll to ship. The browser engine itself comes from
# the Edge WebView2 runtime that ships with Windows 11.
if(MSVC)
    target_link_libraries(LlamaLauncher PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2/lib/x64/WebView2LoaderStatic.lib
        dbghelp)
    # Keep a map file next to the binary: crash dumps record RVA-only fault
    # addresses, and the map is how they get matched back to a symbol.
    target_link_options(LlamaLauncher PRIVATE "/MAP" "/MAPINFO:EXPORTS")
endif()

target_include_directories(LlamaLauncher PRIVATE src)

target_compile_definitions(LlamaLauncher PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    target_compile_options(LlamaLauncher PRIVATE
        /W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc
        /wd4100   # unreferenced formal parameter
        /wd4244   # conversion, possible loss of data
    )
    target_link_options(LlamaLauncher PRIVATE /SUBSYSTEM:WINDOWS)
endif()

# Static CRT keeps the executable self-contained; swap to /MD if you prefer a
# smaller binary that depends on the VC++ redistributable.
if(MSVC)
    set_property(TARGET LlamaLauncher PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

# ---- Libraries -------------------------------------------------------------
# All of these ship with Windows, so there is nothing to download or vendor.
set(LLAMA_SYS_LIBS
    user32
    gdi32
    gdiplus
    msimg32   # GradientFill, used by the accent button
    comctl32
    comdlg32
    shell32
    shlwapi
    ole32
    advapi32
    dwmapi
    uxtheme
    pdh
    dxgi
    winhttp
    version
    bcrypt
)

target_link_libraries(LlamaLauncher PRIVATE ${LLAMA_SYS_LIBS})

# Embed the manifest (Win11 rounded corners, per-monitor DPI awareness, and
# the common-controls v6 dependency that gives us modern-looking dialogs).
if(MSVC)
    set(MANIFEST_FILE ${CMAKE_CURRENT_SOURCE_DIR}/res/app.manifest)
    target_sources(LlamaLauncher PRIVATE ${MANIFEST_FILE})
    set_property(TARGET LlamaLauncher PROPERTY VS_DPI_AWARE "PerMonitor")
endif()