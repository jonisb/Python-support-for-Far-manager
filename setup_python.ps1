# ============================================================================
# PythonFar Standalone Python Setup (PowerShell)
# ============================================================================
# Downloads a self-contained Python 3.11 from the official NuGet package and
# sets up BOTH:
#   1. build artifacts for compiling/linking (include/, libs/)
#   2. python_runtime/ for the adapter (DLLs/, Lib/)
#
# This does NOT use any system or pre-installed Python. Everything comes from
# the downloaded package, so the result is identical on any machine.
#
# Usage:
#   .\setup_python.ps1 -Arch x64   -Dest "build\python_sdk"
#   .\setup_python.ps1 -Arch x86   -Dest "build\python_sdk"
#   .\setup_python.ps1 -Arch arm64 -Dest "build\python_sdk"
#
# Outputs (under $Dest):
#   tools\          - python.exe, python311.dll, DLLs, Lib (the NuGet "tools" dir)
#   include\        - Python.h and headers
#   libs\           - python311.lib import library
# And under $RuntimeDest (default <Dest>\..\Release\python_runtime):
#   DLLs\           - python311.dll + .pyd extension modules
#   Lib\            - standard library
# ============================================================================

param(
    [ValidateSet("x64", "x86", "arm64")]
    [string]$Arch = "x64",                 # x64, x86, or arm64
    [string]$Dest = "build\python_sdk",
    [string]$RuntimeDest = "",             # defaults to build\Release\python_runtime
    [string]$PythonVersion = "3.11.9"
)

$ErrorActionPreference = "Stop"

# NuGet package id differs by architecture:
#   x64   -> python
#   x86   -> pythonx86
#   arm64 -> pythonarm64
$pkgId = switch ($Arch) {
    "x86"   { "pythonx86" }
    "arm64" { "pythonarm64" }
    default { "python" }
}
# Use the v3 flat-container endpoint: it serves every package id (including
# pythonarm64, which the legacy v2 /api/v2/package endpoint does not expose).
$pkgIdLower = $pkgId.ToLowerInvariant()
$verLower = $PythonVersion.ToLowerInvariant()
$nupkgUrl = "https://api.nuget.org/v3-flatcontainer/$pkgIdLower/$verLower/$pkgIdLower.$verLower.nupkg"

if ([string]::IsNullOrEmpty($RuntimeDest)) {
    $RuntimeDest = "build\Release\python_runtime"
}

Write-Host "============================================================"
Write-Host "PythonFar Standalone Python Setup"
Write-Host "  Arch:          $Arch"
Write-Host "  Version:       $PythonVersion"
Write-Host "  NuGet package: $pkgId"
Write-Host "  SDK dest:      $Dest"
Write-Host "  Runtime dest:  $RuntimeDest"
Write-Host "============================================================"

$temp = Join-Path $env:TEMP "pythonfar_nuget"
if (Test-Path $temp) { Remove-Item $temp -Recurse -Force }
New-Item -ItemType Directory -Force -Path $temp | Out-Null

$nupkg = Join-Path $temp "python.nupkg.zip"
Write-Host "Downloading $nupkgUrl ..."
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
$ProgressPreference = 'SilentlyContinue'
Invoke-WebRequest -Uri $nupkgUrl -OutFile $nupkg -UseBasicParsing
$ProgressPreference = 'Continue'

Write-Host "Extracting NuGet package ..."
$extract = Join-Path $temp "extracted"
Expand-Archive -Path $nupkg -DestinationPath $extract -Force

# NuGet layout: tools\ (python.exe, DLLs, Lib, include, libs)
$toolsDir = Join-Path $extract "tools"
if (-not (Test-Path $toolsDir)) {
    throw "tools directory not found in NuGet package at $toolsDir"
}

# ---- Set up the SDK (for compiling/linking) ----
if (Test-Path $Dest) { Remove-Item $Dest -Recurse -Force }
New-Item -ItemType Directory -Force -Path $Dest | Out-Null
Write-Host "Copying SDK (tools/include/libs) to $Dest ..."
Copy-Item "$toolsDir\*" $Dest -Recurse -Force

# ---- Set up python_runtime (for the adapter) ----
if (Test-Path $RuntimeDest) { Remove-Item $RuntimeDest -Recurse -Force }
New-Item -ItemType Directory -Force -Path "$RuntimeDest\DLLs" | Out-Null
New-Item -ItemType Directory -Force -Path "$RuntimeDest\Lib" | Out-Null
New-Item -ItemType Directory -Force -Path "$RuntimeDest\site-packages" | Out-Null

Write-Host "Populating python_runtime DLLs ..."
# Root DLLs (python311.dll, python3.dll, vcruntime, etc.)
Get-ChildItem "$toolsDir" -File | Where-Object { $_.Extension -eq ".dll" } |
    ForEach-Object { Copy-Item $_.FullName "$RuntimeDest\DLLs\" -Force }
# Extension modules (.pyd) + their DLLs live in tools\DLLs
if (Test-Path "$toolsDir\DLLs") {
    Get-ChildItem "$toolsDir\DLLs" -File |
        Where-Object { $_.Extension -in @(".dll", ".pyd") } |
        ForEach-Object { Copy-Item $_.FullName "$RuntimeDest\DLLs\" -Force }
}
# Keep python311.dll at runtime root too (adapter pins it via LoadLibraryExW).
foreach ($rootDll in @("python311.dll", "python3.dll")) {
    $src = Join-Path $toolsDir $rootDll
    if (Test-Path $src) { Copy-Item $src "$RuntimeDest\" -Force }
}

Write-Host "Populating python_runtime Lib ..."
Copy-Item "$toolsDir\Lib\*" "$RuntimeDest\Lib\" -Recurse -Force

# ---- Verify ----
$checks = @(
    "$Dest\include\Python.h",
    "$Dest\libs\python311.lib",
    "$RuntimeDest\DLLs\python311.dll",
    "$RuntimeDest\DLLs\_ctypes.pyd",
    "$RuntimeDest\Lib\encodings\__init__.py"
)
$missing = $checks | Where-Object { -not (Test-Path $_) }
if ($missing.Count -gt 0) {
    Write-Host "ERROR: Missing required files:" -ForegroundColor Red
    $missing | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    throw "Setup verification failed"
}

Remove-Item $temp -Recurse -Force -ErrorAction SilentlyContinue

Write-Host "============================================================"
Write-Host "Done. Python SDK in $Dest, runtime in $RuntimeDest"
Write-Host "  CMake hint:  -DPython3_ROOT_DIR=$Dest"
Write-Host "============================================================"
