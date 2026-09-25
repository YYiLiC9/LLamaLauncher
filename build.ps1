# ============================================================================
#  LlamaLauncher - build script
#
#  Works even when vcvars64.bat cannot run (locked-down shells, sandboxes):
#  it discovers the MSVC toolset and Windows SDK on disk and builds the
#  environment itself, then drives CMake + Ninja.
#
#      .\build.ps1              # release
#      .\build.ps1 -Config Debug
#      .\build.ps1 -Clean
# ============================================================================
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Config = 'Release',
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$buildDir = Join-Path $root 'build'

function Write-Step($msg) { Write-Host "[build] $msg" -ForegroundColor Cyan }
function Write-Err($msg)  { Write-Host "[error] $msg" -ForegroundColor Red }

# --------------------------------------------------------------- 1. Visual Studio
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    $vswhere = "$env:ProgramFiles\Microsoft Visual Studio\Installer\vswhere.exe"
}
if (-not (Test-Path $vswhere)) { Write-Err 'vswhere.exe not found.'; exit 1 }

$vsPath = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { Write-Err 'Visual Studio with the C++ toolset is not installed.'; exit 1 }
$vsPath = $vsPath.Trim()
Write-Step "Visual Studio: $vsPath"

# --------------------------------------------------------------- 2. MSVC toolset
$vcTools = Join-Path $vsPath 'VC\Tools\MSVC'
$toolset = Get-ChildItem $vcTools -Directory | Sort-Object Name -Descending | Select-Object -First 1
if (-not $toolset) { Write-Err "No MSVC toolset under $vcTools"; exit 1 }
Write-Step "MSVC toolset : $($toolset.Name)"

# --------------------------------------------------------------- 3. Windows SDK
$sdkRoot = "${env:ProgramFiles(x86)}\Windows Kits\10"
$sdkVer  = Get-ChildItem (Join-Path $sdkRoot 'Include') -Directory |
           Sort-Object Name -Descending | Select-Object -First 1
if (-not $sdkVer) { Write-Err 'Windows 10/11 SDK not found.'; exit 1 }
Write-Step "Windows SDK  : $($sdkVer.Name)"

# --------------------------------------------------------------- 4. Environment
$msvcBin  = Join-Path $toolset.FullName 'bin\Hostx64\x64'
$msvcInc  = Join-Path $toolset.FullName 'include'
$msvcLib  = Join-Path $toolset.FullName 'lib\x64'
$sdkInc   = Join-Path $sdkVer.FullName '..' | Resolve-Path | ForEach-Object { $_ }
$sdkIncludeRoot = Join-Path $sdkRoot "Include\$($sdkVer.Name)"
$sdkLibRoot     = Join-Path $sdkRoot "Lib\$($sdkVer.Name)"

$env:Path = "$msvcBin;$env:Path"
$env:INCLUDE = @(
    $msvcInc,
    (Join-Path $sdkIncludeRoot 'ucrt'),
    (Join-Path $sdkIncludeRoot 'um'),
    (Join-Path $sdkIncludeRoot 'shared'),
    (Join-Path $sdkIncludeRoot 'winrt'),
    (Join-Path $sdkIncludeRoot 'cppwinrt')
) -join ';'
$env:LIB = @(
    $msvcLib,
    (Join-Path $sdkLibRoot 'ucrt\x64'),
    (Join-Path $sdkLibRoot 'um\x64')
) -join ';'
$env:VSCMD_ARG_TGT_ARCH = 'x64'

# --------------------------------------------------------------- 5. CMake + Ninja
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (-not $cmake) {
    $cmake = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
if (-not (Test-Path $cmake)) {
    Write-Err 'cmake.exe not found. Install CMake or the "C++ CMake tools for Windows" component.'
    exit 1
}
$ninja = (Get-Command ninja -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (-not $ninja) {
    $ninja = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
}
if (-not (Test-Path $ninja)) { $ninja = $null }
Write-Step "CMake        : $cmake"
if ($ninja) { Write-Step "Ninja        : $ninja" }

# --------------------------------------------------------------- 6. Configure
if ($Clean -and (Test-Path $buildDir)) {
    Write-Step "Removing $buildDir"
    Remove-Item $buildDir -Recurse -Force
}

$genArgs = @()
if ($ninja) {
    $genArgs = @('-G', 'Ninja', "-DCMAKE_MAKE_PROGRAM=$ninja")
} else {
    $genArgs = @('-G', 'Visual Studio 17 2022', '-A', 'x64')
}

if (-not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
    Write-Step "Configuring ($Config)"
    & $cmake -S $root -B $buildDir @genArgs "-DCMAKE_BUILD_TYPE=$Config" `
             "-DCMAKE_C_COMPILER=$msvcBin\cl.exe" "-DCMAKE_CXX_COMPILER=$msvcBin\cl.exe"
    if ($LASTEXITCODE -ne 0) { Write-Err 'CMake configure failed.'; exit $LASTEXITCODE }
}

# --------------------------------------------------------------- 7. Build
Write-Step "Compiling ($Config)"
& $cmake --build $buildDir --config $Config
if ($LASTEXITCODE -ne 0) { Write-Err 'Build failed.'; exit $LASTEXITCODE }

$exe = Join-Path $buildDir 'bin\LlamaLauncher.exe'
if (Test-Path $exe) {
    Write-Host ''
    Write-Host "[done] $exe" -ForegroundColor Green
} else {
    Write-Err "Expected output not found: $exe"
    exit 1
}