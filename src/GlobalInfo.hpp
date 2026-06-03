#pragma once

#include <windows.h>
#include <plugin.hpp>

namespace PythonFar {

// ===== Plugin Metadata =====
// Loader plugin strings
static const wchar_t LOADER_TITLE[] = L"PythonFar Adapter";
static const wchar_t LOADER_DESCRIPTION[] = L"Adapter for Python plugins";
static const wchar_t LOADER_AUTHOR[] = L"PythonFar Developer";

// Main plugin strings
static const wchar_t PLUGIN_TITLE[] = L"PythonFar";
static const wchar_t PLUGIN_DESCRIPTION[] = L"Python bridge for Far Manager";
static const wchar_t PLUGIN_AUTHOR[] = L"PythonFar Developer";

// Command prefix for plugin menu
static const wchar_t PLUGIN_PREFIX[] = L"py";

// ===== File Extensions & Patterns =====
// Python Far plugin file extension
static const wchar_t PLUGIN_EXTENSION[] = L".far.py";

// Plugin search pattern
static const wchar_t PLUGIN_SEARCH_PATTERN[] = L"*.far.py";

// ===== Plugin Version Information =====
// Far Manager 3.0 minimum version
static constexpr unsigned long MIN_FAR_VERSION_MAJOR = 3;
static constexpr unsigned long MIN_FAR_VERSION_MINOR = 0;
static constexpr unsigned long MIN_FAR_VERSION_BUILD = 0;
static constexpr unsigned long MIN_FAR_VERSION_REVISION = 0;

// PythonFar plugin version
static constexpr unsigned long PLUGIN_VERSION_MAJOR = 1;
static constexpr unsigned long PLUGIN_VERSION_MINOR = 0;
static constexpr unsigned long PLUGIN_VERSION_BUILD = 0;
static constexpr unsigned long PLUGIN_VERSION_REVISION = 0;

// ===== Plugin GUIDs =====
// Loader plugin GUID (adapter loader)
static const GUID LOADER_GUID = 
    { 0x04e0ea82, 0x1cf3, 0x43c1, { 0x8c, 0xaa, 0xa8, 0x12, 0x59, 0x90, 0xb0, 0xc8 } };

// Main PythonFar plugin GUID
static const GUID PYTHONFAR_GUID = 
    { 0x308868ba, 0x5773, 0x4c89, { 0x81, 0x42, 0xdf, 0x87, 0x78, 0x68, 0xe0, 0x6a } };

// ===== Plugin Runtime Paths =====
// Python plugin directory (relative to adapter DLL directory)
static const wchar_t PYTHON_PLUGINS_DIR[] = L"python";

// Python runtime directory (next to adapter DLL)
static const wchar_t PYTHON_RUNTIME_DIR[] = L"python_runtime";

// Python DLLs directory (within python_runtime)
static const wchar_t PYTHON_DLLS_SUBDIR[] = L"DLLs";

// Python cache directory name
static const wchar_t PYTHON_CACHE_DIR[] = L"__pycache__";

// ===== DLL Names =====
// Loader DLL name
static const wchar_t LOADER_DLL_NAME[] = L"PythonFar.dll";

// Adapter DLL name
static const wchar_t ADAPTER_DLL_NAME[] = L"PythonFar.adapter.dll";

// ===== Python Runtime DLLs =====
// Main Python DLL (version 3.11+)
static const wchar_t PYTHON_DLL[] = L"python311.dll";

// Python compatibility DLL
static const wchar_t PYTHON_COMPAT_DLL[] = L"python3.dll";

// ctypes module
static const wchar_t CTYPES_MODULE[] = L"_ctypes.pyd";

// libffi library for ctypes
static const wchar_t LIBFFI_DLL[] = L"libffi-8.dll";

// ===== Helper function to initialize GlobalInfo struct =====
inline void InitializeGlobalInfo(GlobalInfo* Info, const GUID& guid, 
                                  const wchar_t* title, const wchar_t* description, 
                                  const wchar_t* author) {
    if (!Info) return;
    
    Info->StructSize = sizeof(GlobalInfo);
    Info->MinFarVersion = MAKEFARVERSION(MIN_FAR_VERSION_MAJOR, MIN_FAR_VERSION_MINOR, 
                                        MIN_FAR_VERSION_BUILD, MIN_FAR_VERSION_REVISION, 
                                        VS_RELEASE);
    Info->Version = MAKEFARVERSION(PLUGIN_VERSION_MAJOR, PLUGIN_VERSION_MINOR, 
                                   PLUGIN_VERSION_BUILD, PLUGIN_VERSION_REVISION, 
                                   VS_RELEASE);
    Info->Guid = guid;
    Info->Title = title;
    Info->Description = description;
    Info->Author = author;
}

} // namespace PythonFar
