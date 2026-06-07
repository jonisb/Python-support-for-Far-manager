# ============================================================================
# PythonFar - Far Manager Test Setup (PowerShell)
# ============================================================================
# Downloads a specific Far Manager release from GitHub and deploys the built
# PythonFar adapter into it for local testing.
#
# The asset filename is NOT static (it contains a commit hash), so this script
# uses the GitHub releases API to find the correct asset rather than guessing
# the URL.
#
# Usage:
#   .\setup_far.ps1 -Latest -Arch x64
#   .\setup_far.ps1 -Tag "ci/v3.0.6690.4875" -Arch x64
#   .\setup_far.ps1 -Tag "ci/v3.0.6690.4875" -Arch x86
#   .\setup_far.ps1 -Tag "ci/v3.0.6690.4875" -Arch arm64
#
# Parameters:
#   -Latest Switch to download the latest release (mutually exclusive with -Tag)
#   -Tag    Far Manager release tag (e.g. "ci/v3.0.6690.4875")
#   -Arch   Target architecture: x64, x86, or arm64
#   -Dest   Where to extract Far Manager (default: far_test\<tag-safe-name>)
#   -Build  Path to the CMake build output directory (default: build\Release)
# ============================================================================

param(
    [switch]$Latest = $false,

    [string]$Tag,

    [ValidateSet("x64", "x86", "arm64")]
    [string]$Arch = "",

    [string]$Dest = "",

    [string]$Build = "build\Release\PythonFar",

    # Skip download+extract if Far.exe already exists in $Dest (cache hit).
    # Pass -Force to always re-download.
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$ProgressPreference = 'SilentlyContinue'

# Validate: must supply exactly one of -Latest or -Tag
if ($Latest -and $Tag) { throw "Specify either -Latest or -Tag, not both." }
if (-not $Latest -and -not $Tag) { throw "Specify either -Latest or -Tag." }

# Auto-detect arch from current machine if not specified
if ([string]::IsNullOrEmpty($Arch)) {
    $Arch = switch ($env:PROCESSOR_ARCHITECTURE) {
        "AMD64" { "x64" }
        "x86"   { "x86" }
        "ARM64" { "arm64" }
        default { throw "Unknown processor architecture: $($env:PROCESSOR_ARCHITECTURE). Specify -Arch explicitly." }
    }
    Write-Host "Auto-detected arch: $Arch"
}

# Far Manager filenames use "ARM64" (capitalised), not "arm64"
$archInFilename = switch ($Arch) {
    "arm64" { "ARM64" }
    default { $Arch }   # x64, x86 are already correct
}

# Default destination resolved after $Tag is known (may come from -Latest)
# See below, after API query.

Write-Host "============================================================"
Write-Host "PythonFar - Far Manager Test Setup"
Write-Host "  Arch:  $Arch  (filename: $archInFilename)"
Write-Host "  Build: $Build"
Write-Host "============================================================"

# ---- Verify build output exists ----
$adapterDll = Join-Path $Build "PythonFar.adapter.dll"
$loaderDll  = Join-Path $Build "PythonFar.dll"
foreach ($f in @($adapterDll, $loaderDll)) {
    if (-not (Test-Path $f)) {
        throw "Build output not found: $f`nRun cmake --build first."
    }
}

# ---- Verify 7-Zip is available ----
$sevenZip = Get-Command "7z.exe" -ErrorAction SilentlyContinue
if (-not $sevenZip) {
    # Common install locations
    $candidates = @(
        "C:\Program Files\7-Zip\7z.exe",
        "C:\Program Files (x86)\7-Zip\7z.exe"
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { $sevenZip = $c; break }
    }
}
if (-not $sevenZip) {
    throw "7z.exe not found. Install 7-Zip: https://www.7-zip.org/`nor: choco install 7zip"
}
$sevenZipExe = if ($sevenZip -is [string]) { $sevenZip } else { $sevenZip.Source }
Write-Host "Using 7-Zip: $sevenZipExe"

# ---- Query GitHub API for the release ----
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$headers = @{ "User-Agent" = "PythonFar-setup_far" }
# Add token if available (avoids 60 req/hr unauthenticated rate limit)
if ($env:GITHUB_TOKEN) {
    $headers["Authorization"] = "Bearer $env:GITHUB_TOKEN"
}

if ($Latest) {
    $apiUrl = "https://api.github.com/repos/FarGroup/FarManager/releases?per_page=1"
    Write-Host "Querying GitHub API for latest release: $apiUrl"
    $release = (Invoke-RestMethod -Uri $apiUrl -Headers $headers) | Select-Object -First 1
    $Tag = $release.tag_name
    Write-Host "Latest release: $Tag"
} else {
    $encodedTag = [Uri]::EscapeDataString($Tag)
    $apiUrl = "https://api.github.com/repos/FarGroup/FarManager/releases/tags/$encodedTag"
    Write-Host "Querying GitHub API: $apiUrl"
    $release = Invoke-RestMethod -Uri $apiUrl -Headers $headers
}

# Resolve default destination now that $Tag is known
if ([string]::IsNullOrEmpty($Dest)) {
    $safeTag = $Tag -replace '[/\\]', '-'
    $Dest = "far_test\$safeTag-$Arch"   # version-specific only for local use
}
Write-Host "  Tag:   $Tag"
Write-Host "  Dest:  $Dest"

# ---- Find the correct .7z asset ----
# Pattern: Far.<arch>.<version>.<hash>.7z
# Exclude .pdb.7z and .msi
$asset = $release.assets | Where-Object {
    $_.name -like "Far.$archInFilename.*.7z" -and
    $_.name -notlike "*.pdb.7z"
} | Select-Object -First 1

if (-not $asset) {
    Write-Host "Available assets:" -ForegroundColor Yellow
    $release.assets | ForEach-Object { Write-Host "  $($_.name)" }
    throw "No .7z asset found for arch '$archInFilename' in release '$Tag'"
}

Write-Host "Found asset: $($asset.name)"
Write-Host "Size: $([math]::Round($asset.size / 1MB, 1)) MB"

# ---- Skip download if already extracted (cache hit) ----
if (-not $Force -and (Test-Path (Join-Path $Dest "Far.exe"))) {
    Write-Host "Far Manager already extracted at $Dest — skipping download."
} else {
    # ---- Download ----
    $temp = Join-Path $env:TEMP "pythonfar_far_setup"
    if (Test-Path $temp) { Remove-Item $temp -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $temp | Out-Null

    $archive = Join-Path $temp $asset.name
    Write-Host "Downloading $($asset.browser_download_url) ..."
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $archive -Headers $headers -UseBasicParsing

    # ---- Extract ----
    if (Test-Path $Dest) { Remove-Item $Dest -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $Dest | Out-Null

    Write-Host "Extracting to $Dest ..."
    & $sevenZipExe x $archive "-o$Dest" -y | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "7z extraction failed (exit $LASTEXITCODE)" }

    Remove-Item $temp -Recurse -Force -ErrorAction SilentlyContinue
}

# ---- Deploy adapter (always runs, even on cache hit) ----
$adaptersDir = Join-Path $Dest "Adapters\PythonFar"
New-Item -ItemType Directory -Force -Path $adaptersDir | Out-Null

Write-Host "Copying PythonFar files to $adaptersDir ..."
Copy-Item $loaderDll  $adaptersDir -Force
Copy-Item $adapterDll $adaptersDir -Force

# Copy python scripts from build output only
$pythonSrc = Join-Path $Build "python"
if (Test-Path $pythonSrc) {
    $pythonDest = Join-Path $adaptersDir "python"
    if (Test-Path $pythonDest) { Remove-Item $pythonDest -Recurse -Force }
    Copy-Item $pythonSrc $pythonDest -Recurse -Force
    Write-Host "Copied python scripts to $pythonDest"
}

# Deploy py_handler.far.py to Plugins\PythonFar\
$handlerSrc = Join-Path $pythonSrc "py_handler.far.py"
if (Test-Path $handlerSrc) {
    $pluginsDir = Join-Path $Dest "Plugins\PythonFar"
    New-Item -ItemType Directory -Force -Path $pluginsDir | Out-Null
    Copy-Item $handlerSrc $pluginsDir -Force
    Write-Host "Copied py_handler to $pluginsDir"
}

# Copy python_runtime if present in build output
$runtimeSrc = Join-Path $Build "python_runtime"
if (Test-Path $runtimeSrc) {
    $runtimeDest = Join-Path $adaptersDir "python_runtime"
    if (Test-Path $runtimeDest) { Remove-Item $runtimeDest -Recurse -Force }
    Copy-Item $runtimeSrc $runtimeDest -Recurse -Force
    Write-Host "Copied python_runtime to $runtimeDest"
}

# ---- Verify ----
$checks = @(
    (Join-Path $adaptersDir "PythonFar.dll"),
    (Join-Path $adaptersDir "PythonFar.adapter.dll")
)
$missing = $checks | Where-Object { -not (Test-Path $_) }
if ($missing.Count -gt 0) {
    Write-Host "ERROR: Missing files:" -ForegroundColor Red
    $missing | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    throw "Deploy verification failed"
}

Write-Host "============================================================"
Write-Host "Done. Far Manager ready at: $Dest"
Write-Host "  Run: $Dest\Far.exe"
Write-Host "============================================================"
