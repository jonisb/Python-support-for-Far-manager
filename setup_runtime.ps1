# ============================================================================
# PythonFar Runtime Setup Script (PowerShell)
# ============================================================================
# This PowerShell script downloads and sets up the Python 3.11 runtime for PythonFar
# It's more robust than the batch version and handles errors better.
#
# By default it downloads the official Python 3.11.9 embeddable package and
# transforms its flat layout into the python_runtime/{DLLs,Lib} structure the
# PythonFar adapter expects. Use -UseFallback to copy from a local install.
#
# Usage:
#   .\setup_runtime.ps1 -BuildDir "build\Release" -Destination "build\Release"
#   .\setup_runtime.ps1 -BuildDir "FarPortable\Adapters" -Arch x86
#   .\setup_runtime.ps1 -UseFallback  # Use local Python installation
#
# Environment Variables (optional):
#   PYTHON_HOME   - Path to existing Python installation
#   SKIP_DOWNLOAD - Set to $true to skip downloading and use local Python
# ============================================================================

param(
    [string]$BuildDir = "build\Release",
    [string]$Destination = "",
    [string]$Arch = "x64",   # x64 or x86
    [switch]$UseFallback = $false,
    [switch]$NoCleanup = $false
)

# Set strict error handling
$ErrorActionPreference = "Stop"
$VerbosePreference = "Continue"

# Configuration
# Note: 3.11.9 is the last Python 3.11 release with an official embeddable package.
# Newer 3.11.x releases (3.11.10+) are source-only and have no -embed-amd64.zip.
$PYTHON_VERSION = "3.11.9"
$embedArch = if ($Arch -eq "x86") { "win32" } else { "amd64" }
$PYTHON_EMBED_URL = "https://www.python.org/ftp/python/$PYTHON_VERSION/python-$PYTHON_VERSION-embed-$embedArch.zip"
$TEMP_DIR = Join-Path $env:TEMP "pythonfar_setup"
$SCRIPT_DIR = Split-Path -Parent $MyInvocation.MyCommand.Path

# Set destination to BuildDir if not specified
if ([string]::IsNullOrEmpty($Destination)) {
    $Destination = $BuildDir
}

$PYTHON_RUNTIME_DIR = Join-Path $Destination "python_runtime"
$PYTHON_PLUGINS_DIR = Join-Path $Destination "python"

# Color output helper
function Write-Info {
    param([string]$Message)
    Write-Host "ℹ $Message" -ForegroundColor Cyan
}

function Write-Success {
    param([string]$Message)
    Write-Host "✓ $Message" -ForegroundColor Green
}

function Write-Warn {
    param([string]$Message)
    Write-Host "⚠ $Message" -ForegroundColor Yellow
}

function Write-Error-Custom {
    param([string]$Message)
    Write-Host "✗ $Message" -ForegroundColor Red
}

# Main execution
Write-Host ""
Write-Host "============================================================================" -ForegroundColor Cyan
Write-Host "PythonFar Runtime Setup Script (PowerShell)" -ForegroundColor Cyan
Write-Host "============================================================================" -ForegroundColor Cyan
Write-Host ""

Write-Info "Configuration:"
Write-Info "  Python Version:     $PYTHON_VERSION"
Write-Info "  Build Directory:    $BuildDir"
Write-Info "  Runtime Dest:       $PYTHON_RUNTIME_DIR"
Write-Info "  Plugins Dest:       $PYTHON_PLUGINS_DIR"
Write-Info "  Use Fallback:       $UseFallback"
Write-Host ""

# Create build directory if missing
if (-not (Test-Path $BuildDir)) {
    Write-Info "Build directory not found, creating: $BuildDir"
    New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
    Write-Success "Created build directory: $BuildDir"
}

# Create destination directory if missing (may differ from build dir)
if (-not (Test-Path $Destination)) {
    Write-Info "Destination not found, creating: $Destination"
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    Write-Success "Created destination directory: $Destination"
}

# ============================================================================
# Helper Functions
# ============================================================================

function Setup-From-Download {
    Write-Host ""
    Write-Info "Step 1: Preparing download environment..."
    
    if (Test-Path $TEMP_DIR) {
        Remove-Item -Path $TEMP_DIR -Recurse -Force
    }
    New-Item -ItemType Directory -Path $TEMP_DIR -Force | Out-Null
    
    $ZIP_FILE = Join-Path $TEMP_DIR "python.zip"
    
    Write-Host ""
    Write-Info "Step 2: Downloading Python $PYTHON_VERSION embedded..."
    Write-Info "  URL: $PYTHON_EMBED_URL"
    Write-Info "  Destination: $ZIP_FILE"
    Write-Host ""
    
    try {
        # Ensure TLS 1.2 is enabled
        [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
        
        $ProgressPreference = 'SilentlyContinue'
        Invoke-WebRequest -Uri $PYTHON_EMBED_URL -OutFile $ZIP_FILE -UseBasicParsing
        $ProgressPreference = 'Continue'
        
        Write-Success "Download successful"
    }
    catch {
        Write-Error-Custom "Failed to download Python runtime"
        Write-Host ""
        Write-Warn "Please ensure you have internet access or use -UseFallback flag"
        throw $_
    }
    
    Write-Host ""
    Write-Info "Step 3: Extracting Python runtime..."
    
    $EXTRACT_DIR = Join-Path $TEMP_DIR "extracted"
    try {
        if (Test-Path $EXTRACT_DIR) {
            Remove-Item -Path $EXTRACT_DIR -Recurse -Force
        }
        Expand-Archive -Path $ZIP_FILE -DestinationPath $EXTRACT_DIR -Force
        Write-Success "Extraction successful"
    }
    catch {
        Write-Error-Custom "Failed to extract Python archive"
        throw $_
    }
    
    # The embeddable package has a flat layout (DLLs/pyds at root, stdlib in
    # python311.zip), which differs from a regular install. Use a dedicated
    # function to transform it into the python_runtime/{DLLs,Lib} layout the
    # adapter expects.
    Install-From-Embeddable $EXTRACT_DIR
}

function Install-From-Embeddable {
    param([string]$SOURCE_DIR)

    Write-Host ""
    Write-Info "Step 4: Installing embeddable runtime..."

    # Reset runtime directory
    if (Test-Path $PYTHON_RUNTIME_DIR) {
        Write-Info "Removing existing runtime directory..."
        Remove-Item -Path $PYTHON_RUNTIME_DIR -Recurse -Force
    }
    New-Item -ItemType Directory -Path "$PYTHON_RUNTIME_DIR\DLLs" -Force | Out-Null
    New-Item -ItemType Directory -Path "$PYTHON_RUNTIME_DIR\Lib" -Force | Out-Null
    Write-Success "Created runtime directory structure"

    # 1. Copy native binaries (.dll, .pyd) from the flat root into DLLs/.
    #    python311.dll must also stay at the runtime root because the adapter
    #    pins it via LoadLibraryExW and Py_Initialize finds it via PYTHONHOME.
    Write-Host ""
    Write-Info "Copying native modules (.dll/.pyd)..."
    $binFiles = Get-ChildItem -Path $SOURCE_DIR -File | Where-Object {
        $_.Extension -in @(".dll", ".pyd")
    }
    foreach ($f in $binFiles) {
        Copy-Item -Path $f.FullName -Destination "$PYTHON_RUNTIME_DIR\DLLs\" -Force
    }
    Write-Success "$($binFiles.Count) native modules copied to DLLs\"

    # Also keep python311.dll / python3.dll at the runtime root for redundancy.
    foreach ($rootDll in @("python311.dll", "python3.dll")) {
        $src = Join-Path $SOURCE_DIR $rootDll
        if (Test-Path $src) {
            Copy-Item -Path $src -Destination "$PYTHON_RUNTIME_DIR\" -Force
        }
    }
    Write-Success "Root python DLLs placed in python_runtime\"

    # 2. Expand the zipped standard library (python311.zip) into Lib/.
    Write-Host ""
    Write-Info "Expanding standard library (python311.zip)..."
    $stdlibZip = Join-Path $SOURCE_DIR "python311.zip"
    if (Test-Path $stdlibZip) {
        # Expand-Archive requires a .zip extension; copy to a temp .zip first.
        $tmpZip = Join-Path $TEMP_DIR "stdlib.zip"
        Copy-Item -Path $stdlibZip -Destination $tmpZip -Force
        Expand-Archive -Path $tmpZip -DestinationPath "$PYTHON_RUNTIME_DIR\Lib" -Force -Verbose:$false | Out-Null
        Remove-Item -Path $tmpZip -Force -ErrorAction SilentlyContinue
        Write-Success "Standard library expanded to Lib\"
    }
    else {
        Write-Error-Custom "python311.zip not found in embeddable package"
        throw "Standard library archive missing"
    }

    # 3. site-packages: embeddable ships none; create an empty directory so the
    #    adapter's sys.path setup does not fail.
    New-Item -ItemType Directory -Path "$PYTHON_RUNTIME_DIR\site-packages" -Force | Out-Null

    # 4. Verify the critical files the adapter requires actually exist.
    #    Note: the embeddable stdlib ships compiled .pyc files only (no .py
    #    source), so accept either form for the encodings package check.
    Write-Host ""
    Write-Info "Verifying runtime..."
    $missing = @()
    if (-not (Test-Path "$PYTHON_RUNTIME_DIR\DLLs\python311.dll")) {
        $missing += "$PYTHON_RUNTIME_DIR\DLLs\python311.dll"
    }
    if (-not (Test-Path "$PYTHON_RUNTIME_DIR\DLLs\_ctypes.pyd")) {
        $missing += "$PYTHON_RUNTIME_DIR\DLLs\_ctypes.pyd"
    }
    $encInit = (Test-Path "$PYTHON_RUNTIME_DIR\Lib\encodings\__init__.py") -or `
               (Test-Path "$PYTHON_RUNTIME_DIR\Lib\encodings\__init__.pyc")
    if (-not $encInit) {
        $missing += "$PYTHON_RUNTIME_DIR\Lib\encodings\__init__.(py|pyc)"
    }
    if ($missing.Count -gt 0) {
        Write-Error-Custom "Runtime verification failed. Missing:"
        foreach ($m in $missing) { Write-Error-Custom "  $m" }
        throw "Runtime verification failed"
    }
    Write-Success "Runtime verified (python311.dll, _ctypes.pyd, encodings present)"

    # 5. Create plugin directory + descript.ion (shared finalization).
    Finalize-Setup
}

function Setup-From-System {
    Write-Host ""
    Write-Info "Attempting to detect local Python installation..."
    
    $PYTHON_PATH = Find-Python-Installation
    
    if ([string]::IsNullOrEmpty($PYTHON_PATH)) {
        Write-Error-Custom "Could not find Python installation"
        Write-Host ""
        Write-Warn "Please either:"
        Write-Warn "  1. Set environment variable: `$env:PYTHON_HOME='C:\path\to\python'"
        Write-Warn "  2. Ensure Python is in PATH"
        Write-Warn "  3. Install Python 3.11 from python.org"
        throw "Python not found"
    }
    
    Write-Success "Found Python at: $PYTHON_PATH"
    Copy-Runtime $PYTHON_PATH
}

function Find-Python-Installation {
    # Check PYTHON_HOME environment variable first
    if (-not [string]::IsNullOrEmpty($env:PYTHON_HOME)) {
        if (Test-Path $env:PYTHON_HOME) {
            return $env:PYTHON_HOME
        }
    }
    
    # Determine the Scoop root: prefer $env:SCOOP, then $env:SCOOP_GLOBAL,
    # otherwise fall back to the per-user default under the current profile.
    # (Never hardcode a specific user's profile path.)
    $SCOOP_ROOT = if (-not [string]::IsNullOrEmpty($env:SCOOP)) {
        $env:SCOOP
    } elseif (-not [string]::IsNullOrEmpty($env:SCOOP_GLOBAL)) {
        $env:SCOOP_GLOBAL
    } else {
        Join-Path $env:USERPROFILE "scoop"
    }
    $SCOOP_APPS = Join-Path $SCOOP_ROOT "apps"

    # Try common Scoop installation (miniconda3-py311)
    $SCOOP_CURRENT = Join-Path $SCOOP_APPS "miniconda3-py311\current"
    if (Test-Path $SCOOP_CURRENT) {
        $hasPython = (Test-Path "$SCOOP_CURRENT\python311.dll") -or (Test-Path "$SCOOP_CURRENT\Lib")
        if ($hasPython) {
            return $SCOOP_CURRENT
        }
    }
    
    # Try to find any Python 3.11 in Scoop apps
    if (Test-Path $SCOOP_APPS) {
        $pythonDirs = @(Get-ChildItem -Path $SCOOP_APPS -Filter "*python*" -Directory -ErrorAction SilentlyContinue)
        foreach ($dir in $pythonDirs) {
            $current = Join-Path $dir.FullName "current"
            if (Test-Path "$current\python311.dll") {
                return $current
            }
        }
    }
    
    # Try to find Python via where command
    try {
        $PYTHON_EXE = (Get-Command python.exe -ErrorAction SilentlyContinue).Source
        if ($PYTHON_EXE) {
            # Navigate up: python.exe -> Scripts -> Python root
            $PYTHON_DIR = Split-Path -Parent (Split-Path -Parent $PYTHON_EXE)
            $hasLib = (Test-Path "$PYTHON_DIR\python311.dll") -or (Test-Path "$PYTHON_DIR\Lib")
            if ($hasLib) {
                return $PYTHON_DIR
            }
        }
    }
    catch { }
    
    # Try common Anaconda installation
    $ANACONDA_PATH = "$env:USERPROFILE\anaconda3"
    if (Test-Path $ANACONDA_PATH) {
        $hasAnaconda = (Test-Path "$ANACONDA_PATH\python311.dll") -or (Test-Path "$ANACONDA_PATH\Lib")
        if ($hasAnaconda) {
            return $ANACONDA_PATH
        }
    }
    
    # Try default install location
    $DEFAULT_PATH = "C:\Python311"
    if (Test-Path $DEFAULT_PATH) {
        $hasDefault = (Test-Path "$DEFAULT_PATH\python311.dll") -or (Test-Path "$DEFAULT_PATH\Lib")
        if ($hasDefault) {
            return $DEFAULT_PATH
        }
    }
    
    return $null
}

function Copy-Runtime {
    param([string]$SOURCE_DIR)
    
    Write-Host ""
    Write-Info "Setting up runtime directory..."
    
    # Remove existing runtime directory
    if (Test-Path $PYTHON_RUNTIME_DIR) {
        Write-Info "Removing existing runtime directory..."
        Remove-Item -Path $PYTHON_RUNTIME_DIR -Recurse -Force
    }
    
    # Create directory structure
    New-Item -ItemType Directory -Path "$PYTHON_RUNTIME_DIR\DLLs" -Force | Out-Null
    New-Item -ItemType Directory -Path "$PYTHON_RUNTIME_DIR\Lib" -Force | Out-Null
    New-Item -ItemType Directory -Path "$PYTHON_RUNTIME_DIR\site-packages" -Force | Out-Null
    Write-Success "Created: $PYTHON_RUNTIME_DIR\DLLs, Lib, site-packages"
    
    # Copy Python DLLs
    Write-Host ""
    Write-Info "Copying Python DLLs..."
    
    Copy-DLL-If-Exists "$SOURCE_DIR\python311.dll" "$PYTHON_RUNTIME_DIR\DLLs\" "python311.dll" | Out-Null
    Copy-DLL-If-Exists "$SOURCE_DIR\DLLs\python311.dll" "$PYTHON_RUNTIME_DIR\DLLs\" "python311.dll" | Out-Null
    
    Copy-DLL-If-Exists "$SOURCE_DIR\python3.dll" "$PYTHON_RUNTIME_DIR\DLLs\" "python3.dll" | Out-Null
    Copy-DLL-If-Exists "$SOURCE_DIR\DLLs\python3.dll" "$PYTHON_RUNTIME_DIR\DLLs\" "python3.dll" | Out-Null
    
    Copy-DLL-If-Exists "$SOURCE_DIR\_ctypes.pyd" "$PYTHON_RUNTIME_DIR\DLLs\" "_ctypes.pyd" | Out-Null
    Copy-DLL-If-Exists "$SOURCE_DIR\DLLs\_ctypes.pyd" "$PYTHON_RUNTIME_DIR\DLLs\" "_ctypes.pyd" | Out-Null
    
    Copy-DLL-If-Exists "$SOURCE_DIR\libffi-8.dll" "$PYTHON_RUNTIME_DIR\DLLs\" "libffi-8.dll" | Out-Null
    Copy-DLL-If-Exists "$SOURCE_DIR\DLLs\libffi-8.dll" "$PYTHON_RUNTIME_DIR\DLLs\" "libffi-8.dll" | Out-Null
    
    # Copy Python standard library
    Write-Host ""
    Write-Info "Copying Python standard library..."
    
    $LIB_SOURCE = if (Test-Path "$SOURCE_DIR\Lib") { "$SOURCE_DIR\Lib" } else { $null }
    
    if ($LIB_SOURCE) {
        Copy-Item -Path "$LIB_SOURCE\*" -Destination "$PYTHON_RUNTIME_DIR\Lib" -Recurse -Force -ErrorAction SilentlyContinue
        Write-Success "Standard library copied"
    }
    else {
        Write-Warn "Lib directory not found at $SOURCE_DIR\Lib"
        Write-Warn "Some Python functionality may be missing"
    }
    
    # Copy site-packages (optional)
    Write-Host ""
    Write-Info "Copying site-packages (optional)..."
    
    $SITE_PACKAGES_SOURCES = @(
        "$SOURCE_DIR\Lib\site-packages",
        "$SOURCE_DIR\site-packages"
    )
    
    foreach ($source in $SITE_PACKAGES_SOURCES) {
        if (Test-Path $source) {
            Copy-Item -Path "$source\*" -Destination "$PYTHON_RUNTIME_DIR\site-packages" -Recurse -Force -ErrorAction SilentlyContinue
            Write-Success "site-packages copied"
            break
        }
    }
    
    # Copy pyvenv.cfg if it exists
    Write-Host ""
    Write-Info "Creating Python path configuration..."
    
    if (Test-Path "$SOURCE_DIR\pyvenv.cfg") {
        Copy-Item -Path "$SOURCE_DIR\pyvenv.cfg" -Destination "$PYTHON_RUNTIME_DIR\" -Force
        Write-Success "pyvenv.cfg copied"
    }
    
    # Create plugin directory + descript.ion (shared finalization).
    Finalize-Setup
}

function Finalize-Setup {
    # Create plugins directory
    Write-Host ""
    Write-Info "Creating plugin directory..."
    
    if (-not (Test-Path $PYTHON_PLUGINS_DIR)) {
        New-Item -ItemType Directory -Path $PYTHON_PLUGINS_DIR -Force | Out-Null
    }
    Write-Success "Plugin directory: $PYTHON_PLUGINS_DIR"
    
    # Create descript.ion for Far Manager
    Write-Host ""
    Write-Info "Creating documentation file..."
    
    $DESCRIPT_FILE = Join-Path $Destination "descript.ion"
    @(
        "1 python_runtime directory containing Python 3.11 runtime DLLs and stdlib",
        "2 python directory for .far.py plugin files"
    ) | Out-File -LiteralPath $DESCRIPT_FILE -Encoding ASCII
    
    Write-Success "descript.ion created"
}

function Copy-DLL-If-Exists {
    param([string]$Source, [string]$Destination, [string]$DisplayName)
    
    if (Test-Path $Source) {
        Copy-Item -Path $Source -Destination $Destination -Force
        Write-Success $DisplayName
        return $true
    }
    return $false
}

try {
    if ($UseFallback -or $env:SKIP_DOWNLOAD -eq "1") {
        Write-Info "Using local Python installation..."
        Setup-From-System
    }
    else {
        Write-Info "Downloading Python runtime..."
        Setup-From-Download
    }
    
    Write-Host ""
    Write-Host "============================================================================" -ForegroundColor Green
    Write-Success "Setup Complete!"
    Write-Host "============================================================================" -ForegroundColor Green
    Write-Host ""
    
    Write-Info "Runtime files installed to:"
    Write-Host "  $PYTHON_RUNTIME_DIR" -ForegroundColor Yellow
    Write-Host ""
    Write-Info "Plugin directory created at:"
    Write-Host "  $PYTHON_PLUGINS_DIR" -ForegroundColor Yellow
    Write-Host ""
    Write-Info "Next steps:"
    Write-Host "  1. Copy your .far.py plugins to $PYTHON_PLUGINS_DIR"
    Write-Host "  2. Place PythonFar.dll and PythonFar.adapter.dll in $Destination"
    Write-Host "  3. Run your Far Manager instance"
    Write-Host ""
}
catch {
    Write-Error-Custom "Setup failed: $_"
    exit 1
}
finally {
    if ($NoCleanup -eq $false) {
        if (Test-Path $TEMP_DIR) {
            Write-Info "Cleaning up temporary files..."
            Remove-Item -Path $TEMP_DIR -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}
