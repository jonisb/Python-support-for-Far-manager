#include "adapter_log.hpp"
#include "../GlobalInfo.hpp"
#include "adapter.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>

extern "C" IMAGE_DOS_HEADER __ImageBase;

// Implemented in bridges.cpp (same DLL). Shows a Far message box; the body may
// contain '\n'-separated lines. Returns FALSE if the Far bridge is unavailable.
extern "C" __declspec(dllexport) BOOL WINAPI PythonFar_ShowMessage(
    const GUID* PluginId, const wchar_t* title, const wchar_t* body);

// Global Python initialization reference counter
// Python can only be initialized once per process, so we use a reference counter
// to ensure Py_Finalize() is not called more than once
static int g_PythonRefCount = 0;
static PyThreadState* g_MainThreadState = nullptr;

// ===== PythonFarAdapter Implementation =====

PythonFarAdapter::PythonFarAdapter(GlobalInfo* globalInfo) {
    LOG_TRACE("PythonFarAdapter constructor");
    OutputDebugStringW(L"PythonFar Adapter: Constructor called\n");
    
    if (globalInfo) {
        m_GlobalInfo = *globalInfo;
    }

    // Get adapter DLL path
    wchar_t modulePath[MAX_PATH];
    GetModuleFileNameW(reinterpret_cast<HINSTANCE>(&__ImageBase), modulePath, MAX_PATH);
    std::wstring pathStr(modulePath);
    
    m_PluginDir = pathStr.substr(0, pathStr.find_last_of(L"\\/"));
}

PythonFarAdapter::~PythonFarAdapter() {
    LOG_TRACE("PythonFarAdapter destructor");
    FinalizePython();
}

void PythonFarAdapter::InitializePython() {
    LogExportStatus();
    HMODULE hSelf = GetModuleHandleW(PythonFar::ADAPTER_DLL_NAME);
    FARPROC bridge = hSelf ? GetProcAddress(hSelf, "PythonFar_DialogInit") : nullptr;
    if (!bridge) {
        LOG_TRACE("InitializePython: PythonFar_DialogInit not available, falling back to loader module");
        HMODULE hLoader = GetModuleHandleW(PythonFar::LOADER_DLL_NAME);
        bridge = hLoader ? GetProcAddress(hLoader, "PythonFar_DialogInit") : nullptr;
    }
    LOG_TRACE(std::string("InitializePython: GetProcAddress('PythonFar_DialogInit') -> ") + (bridge ? "non-null" : "NULL"));
    LOG_TRACE("Initializing Python...");

    auto ToUtf8 = [](const std::wstring& ws) {
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
        std::string narrow(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &narrow[0], size_needed, nullptr, nullptr);
        return narrow;
    };

    // Use m_PluginDir set in constructor instead of searching again
    std::wstring pluginDir = m_PluginDir;
    LogExportStatus();
    std::wstring pythonHome = pluginDir.empty() ? L"" : pluginDir + L"\\" + PythonFar::PYTHON_RUNTIME_DIR;
    DWORD homeAttrs = pythonHome.empty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(pythonHome.c_str());
    if (homeAttrs == INVALID_FILE_ATTRIBUTES || !(homeAttrs & FILE_ATTRIBUTE_DIRECTORY)) {
        pythonHome.clear();
    }
    std::wstring dllsDir = pythonHome.empty() ? pluginDir : (pythonHome + L"\\" + PythonFar::PYTHON_DLLS_SUBDIR);

    if (pythonHome.empty()) {
        LOG_TRACE(WideToUTF8(PythonFar::PYTHON_RUNTIME_DIR) << " directory not found; aborting initialization");
        m_ErrorSummary = L"Python Runtime Missing";
        m_ErrorDescription = L"Expected " + std::wstring(PythonFar::PYTHON_RUNTIME_DIR) + L" next to PythonFar.dll";
        return;
    }

    // Register runtime DLL directories without mutating process PATH or the
    // process-wide SetDllDirectory setting. AddDllDirectory cookies are removed
    // during FinalizePython(). Keep them active while Python may import .pyd
    // extension modules that have side-by-side dependencies.
    for (const auto& dir : { pluginDir, pythonHome, dllsDir }) {
        if (dir.empty()) continue;
        DWORD attrs = GetFileAttributesW(dir.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            DLL_DIRECTORY_COOKIE cookie = AddDllDirectory(dir.c_str());
            if (cookie) {
                m_DllDirectoryCookies.push_back(cookie);
                LOG_TRACE("Added Python runtime DLL directory: " + ToUtf8(dir));
            } else {
                LOG_TRACE("AddDllDirectory failed for " + ToUtf8(dir) + ": " + std::to_string(GetLastError()));
            }
        }
    }

    if (!pythonHome.empty()) {
        // Use PyConfig instead of deprecated Py_SetPythonHome (deprecated in 3.11)
        // Note: PyConfig is used via Py_InitializeFromConfig for more control,
        // but we use the simple approach here since the Python DLL is explicitly
        // pinned below and runtime DLL directories are registered via
        // AddDllDirectory (not process PATH / SetDllDirectoryW).
        LOG_TRACE("Python home prepared (using modern initialization approach)");
    }

    std::wstring pythonDllPath = dllsDir.empty() ? (pythonHome + L"\\" + PythonFar::PYTHON_DLL) : (dllsDir + L"\\" + PythonFar::PYTHON_DLL);
    HMODULE hPinnedPython = LoadLibraryExW(
        pythonDllPath.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
        LOAD_LIBRARY_SEARCH_DEFAULT_DIRS |
        LOAD_LIBRARY_SEARCH_USER_DIRS);
    if (!hPinnedPython) {
        DWORD err = GetLastError();
        LOG_TRACE("Failed to load " << WideToUTF8(PythonFar::PYTHON_DLL) << " from runtime: " + std::to_string(err) + " path: " + ToUtf8(pythonDllPath));
        m_ErrorSummary = L"Python DLL Load Error";
        m_ErrorDescription = L"Could not load " + std::wstring(PythonFar::PYTHON_DLL) + L" from python_runtime";
        return;
    }
    LOG_TRACE("Pinned " << WideToUTF8(PythonFar::PYTHON_DLL) << " from runtime: " + ToUtf8(pythonDllPath));

    // Set PYTHONHOME environment variable so Python can find its stdlib
    SetEnvironmentVariableW(L"PYTHONHOME", pythonHome.c_str());
    LOG_TRACE("Set PYTHONHOME to: " + ToUtf8(pythonHome));

    LOG_TRACE("Calling Py_Initialize()");
    Py_Initialize();

    if (!Py_IsInitialized()) {
        LOG_TRACE("Python initialization failed!");
        m_ErrorSummary = L"Python Initialization Error";
        m_ErrorDescription = L"Failed to initialize Python interpreter";
        return;
    }

    LOG_TRACE("Python initialized successfully");
    
    // Increment global Python reference counter
    g_PythonRefCount++;
    LOG_TRACE("Python ref count: " + std::to_string(g_PythonRefCount));

    HMODULE hPy = GetModuleHandleW(L"python311.dll");
    if (hPy) {
        wchar_t pyPath[MAX_PATH];
        GetModuleFileNameW(hPy, pyPath, MAX_PATH);
        std::wstring ws(pyPath);
        LOG_TRACE("Loaded python311.dll from: " + ToUtf8(ws));
    } else {
        LOG_TRACE("Could not find handle for python311.dll (not loaded yet?)");
    }

    std::wstring ctypesPath = dllsDir + L"\\" + PythonFar::CTYPES_MODULE;
    HMODULE hCtypes = LoadLibraryExW(
        ctypesPath.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
        LOAD_LIBRARY_SEARCH_DEFAULT_DIRS |
        LOAD_LIBRARY_SEARCH_USER_DIRS);
    if (hCtypes) {
        LOG_TRACE("Successfully loaded " << WideToUTF8(PythonFar::CTYPES_MODULE) << " via LoadLibraryW (keeping it loaded)");
    } else {
        LOG_TRACE("Failed to load " << WideToUTF8(PythonFar::CTYPES_MODULE) << " via LoadLibraryW: " + std::to_string(GetLastError()) + " path: " + ToUtf8(ctypesPath));
    }

    std::wstring libffiRootPath = pythonHome + L"\\" + PythonFar::LIBFFI_DLL;
    HMODULE hLibFFI = LoadLibraryExW(
        libffiRootPath.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
        LOAD_LIBRARY_SEARCH_DEFAULT_DIRS |
        LOAD_LIBRARY_SEARCH_USER_DIRS);
    if (hLibFFI) {
         LOG_TRACE("Successfully pre-loaded " << WideToUTF8(PythonFar::LIBFFI_DLL) << " (root)");
    } else {
         std::wstring libffiDllsPath = dllsDir + L"\\" + PythonFar::LIBFFI_DLL;
         hLibFFI = LoadLibraryExW(
             libffiDllsPath.c_str(), nullptr,
             LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
             LOAD_LIBRARY_SEARCH_DEFAULT_DIRS |
             LOAD_LIBRARY_SEARCH_USER_DIRS);
         if (hLibFFI) {
             LOG_TRACE("Successfully pre-loaded " << WideToUTF8(PythonFar::LIBFFI_DLL) << " (DLLs)");
         } else {
             LOG_TRACE("Failed to pre-load " << WideToUTF8(PythonFar::LIBFFI_DLL) << ": " + std::to_string(GetLastError()) + " path: " + ToUtf8(libffiDllsPath));
         }
    }

    std::wstring pythonSubDir = m_PluginDir + L"\\" + PythonFar::PYTHON_PLUGINS_DIR;
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, pythonSubDir.c_str(), (int)pythonSubDir.size(), nullptr, 0, nullptr, nullptr);
    std::string pathStr(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, pythonSubDir.c_str(), (int)pythonSubDir.size(), &pathStr[0], size_needed, nullptr, nullptr);
    std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
    
    std::string syspath = "import sys; sys.path.insert(0, r'" + pathStr + "')";
    LOG_TRACE("Adding to sys.path: " + pathStr);
    PyRun_SimpleString(syspath.c_str());

    LOG_TRACE("Releasing GIL from main thread initialization");
    // Note: We save the thread state but don't actually release the GIL yet
    // We'll release it lazily when needed, or keep it for the process lifetime
    // g_MainThreadState = PyEval_SaveThread();

    LOG_TRACE("Python initialization complete");
}

void PythonFarAdapter::FinalizePython() {
    LOG_TRACE("Finalizing Python...");
    
    // Python can only be finalized once per process - use reference counting
    if (Py_IsInitialized() && g_PythonRefCount > 0) {
        g_PythonRefCount--;
        LOG_TRACE("Python ref count after decrement: " + std::to_string(g_PythonRefCount));
        
        if (g_PythonRefCount == 0) {
            if (g_MainThreadState) {
                PyEval_RestoreThread(g_MainThreadState);
                g_MainThreadState = nullptr;
            }
            Py_Finalize();
            LOG_TRACE("Python finalized");
        }
    }

    if (!Py_IsInitialized() || g_PythonRefCount == 0) {
        for (DLL_DIRECTORY_COOKIE cookie : m_DllDirectoryCookies) {
            RemoveDllDirectory(cookie);
        }
        if (!m_DllDirectoryCookies.empty()) {
            LOG_TRACE("Removed Python runtime DLL directories");
            m_DllDirectoryCookies.clear();
        }
    }
}

bool PythonFarAdapter::IsModule(const wchar_t* filename) {
    m_ErrorSummary.clear();
    m_ErrorDescription.clear();

    if (!filename) return false;

    // Check if file ends with PLUGIN_EXTENSION (e.g. ".far.py")
    std::wstring filenameStr(filename);
    const std::wstring pluginExt(PythonFar::PLUGIN_EXTENSION);
    if (filenameStr.length() < pluginExt.length()) return false;

    std::wstring ext = filenameStr.substr(filenameStr.length() - pluginExt.length());
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);

    bool isPlugin = (ext == pluginExt);

    if (isPlugin) {
        {
            int size_needed = WideCharToMultiByte(CP_UTF8, 0, filenameStr.c_str(), (int)filenameStr.size(), nullptr, 0, nullptr, nullptr);
            std::string narrow_str(size_needed, 0);
            WideCharToMultiByte(CP_UTF8, 0, filenameStr.c_str(), (int)filenameStr.size(), &narrow_str[0], size_needed, nullptr, nullptr);
            LOG_TRACE("IsModule: TRUE for " + narrow_str);
        }
    } else {
        {
            int size_needed = WideCharToMultiByte(CP_UTF8, 0, filenameStr.c_str(), (int)filenameStr.size(), nullptr, 0, nullptr, nullptr);
            std::string narrow_str(size_needed, 0);
            WideCharToMultiByte(CP_UTF8, 0, filenameStr.c_str(), (int)filenameStr.size(), &narrow_str[0], size_needed, nullptr, nullptr);
            LOG_TRACE("IsModule: FALSE for " + narrow_str);
        }
    }

    return isPlugin;
}

std::unique_ptr<PluginModule> PythonFarAdapter::CreatePluginModule(const wchar_t* filename) {
    m_ErrorSummary.clear();
    m_ErrorDescription.clear();

    {
        std::wstring filenameStr(filename);
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, filenameStr.c_str(), (int)filenameStr.size(), nullptr, 0, nullptr, nullptr);
        std::string narrow_str(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, filenameStr.c_str(), (int)filenameStr.size(), &narrow_str[0], size_needed, nullptr, nullptr);
        LOG_TRACE("CreatePluginModule: " + narrow_str);
    }

    // Log PanelControl and export status before creating the module
    LogExportStatus();

    if (!Py_IsInitialized()) {
        InitializePython();
    }

    if (!Py_IsInitialized()) {
        LOG_TRACE("Python not initialized");
        m_ErrorSummary = L"Python Error";
        m_ErrorDescription = L"Python interpreter not initialized";
        return nullptr;
    }

    LOG_TRACE("Python is initialized, proceeding with module creation");

    ScopedGIL gil;

    try {
        // Extract module name from filename (remove .far.py extension)
        std::wstring filenameStr(filename);
        std::string filenameNarrow;
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, filenameStr.c_str(), (int)filenameStr.size(), nullptr, 0, nullptr, nullptr);
        filenameNarrow.assign(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, filenameStr.c_str(), (int)filenameStr.size(), &filenameNarrow[0], size_needed, nullptr, nullptr);

        // Find the module name by removing the plugin extension (e.g. ".far.py")
        std::string moduleNameStr;
        std::string pluginExtNarrow = WideToUTF8(PythonFar::PLUGIN_EXTENSION);
        size_t farPyPos = filenameNarrow.find(pluginExtNarrow);
        if (farPyPos != std::string::npos) {
            // Extract from last directory separator to the extension
            size_t lastSlash = filenameNarrow.find_last_of("\\/");
            if (lastSlash != std::string::npos && lastSlash < farPyPos) {
                moduleNameStr = filenameNarrow.substr(lastSlash + 1, farPyPos - lastSlash - 1);
            } else {
                moduleNameStr = filenameNarrow.substr(0, farPyPos);
            }
        } else {
            // Fallback: use filename without extension
            size_t lastSlash = filenameNarrow.find_last_of("\\/");
            size_t lastDot = filenameNarrow.find_last_of('.');
            if (lastSlash != std::string::npos && lastDot != std::string::npos && lastDot > lastSlash) {
                moduleNameStr = filenameNarrow.substr(lastSlash + 1, lastDot - lastSlash - 1);
            } else if (lastDot != std::string::npos) {
                moduleNameStr = filenameNarrow.substr(0, lastDot);
            } else {
                moduleNameStr = filenameNarrow;
            }
        }

        // Replace backslashes with forward slashes for Python compatibility
        std::replace(filenameNarrow.begin(), filenameNarrow.end(), '\\', '/');

        LOG_TRACE("Full file path: " + filenameNarrow);

        // Check if the file exists. Use the wide path with the Win32 API:
        // a narrow std::ifstream interprets its path in the current ANSI code
        // page (not UTF-8), so a UTF-8 path with non-ASCII characters would
        // fail to open on runners whose ACP differs (e.g. CI), even though the
        // file exists. GetFileAttributesW is encoding-correct on all systems.
        {
            DWORD attrs = GetFileAttributesW(filename);
            if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                LOG_TRACE("File does not exist: " + filenameNarrow);
                m_ErrorSummary = L"Load Error";
                std::wstring wideFilename = PythonFar::UTF8ToWide(filenameNarrow);
                m_ErrorDescription = L"File not found: " + wideFilename;

                return nullptr;
            }
        }

        // Import the module using importlib.util to handle .far.py extension
        PyObj importlib_util = PyImport_ImportModule("importlib.util");
        if (!importlib_util) {
            LOG_TRACE("Failed to import importlib.util");
            PyErr_Print();
            m_ErrorSummary = L"Import Error";
            m_ErrorDescription = L"Failed to import importlib.util";
            
            return nullptr;
        }

        PyObj spec_from_file_location = PyObject_GetAttrString(importlib_util, "spec_from_file_location");
        if (!spec_from_file_location) {
            LOG_TRACE("Failed to get spec_from_file_location");
            PyErr_Print();
            m_ErrorSummary = L"Import Error";
            m_ErrorDescription = L"Failed to get spec_from_file_location function";
            
            return nullptr;
        }

        PyObj spec = PyObject_CallFunction(spec_from_file_location, "ss", moduleNameStr.c_str(), filenameNarrow.c_str());
        if (!spec) {
            LOG_TRACE("Failed to create spec from file location");
            PyErr_Print();
            m_ErrorSummary = L"Import Error";
            std::wstring wideModuleName = PythonFar::UTF8ToWide(moduleNameStr);
            m_ErrorDescription = L"Failed to create module spec for: " + wideModuleName;
            
            return nullptr;
        }

        PyObj loader = PyObject_GetAttrString(spec, "loader");
        if (!loader) {
            LOG_TRACE("Failed to get loader from spec");
            PyErr_Print();
            m_ErrorSummary = L"Import Error";
            m_ErrorDescription = L"Failed to get loader from module spec";
            
            return nullptr;
        }

        PyObj module_from_spec = PyObject_GetAttrString(importlib_util, "module_from_spec");
        if (!module_from_spec) {
            LOG_TRACE("Failed to get module_from_spec");
            PyErr_Print();
            m_ErrorSummary = L"Import Error";
            m_ErrorDescription = L"Failed to get module_from_spec function";
            
            return nullptr;
        }

        PyObj pModule = PyObject_CallFunction(module_from_spec, "O", spec.get());
        if (!pModule) {
            LOG_TRACE("Failed to create module from spec");
            PyErr_Print();
            m_ErrorSummary = L"Import Error";
            m_ErrorDescription = L"Failed to create module from spec";
            
            return nullptr;
        }

        PyObj exec_result = PyObject_CallMethod(loader, "exec_module", "O", pModule.get());
        if (!exec_result) {
            LOG_TRACE("Failed to execute module");
            
            // Capture the actual Python error message
            PyObject *ptype, *pvalue, *ptraceback;
            PyErr_Fetch(&ptype, &pvalue, &ptraceback);
            std::string errorMsg = "Unknown error";
            if (pvalue) {
                PyObj str = PyObject_Str(pvalue);
                if (str) {
                    const char* errStr = PyUnicode_AsUTF8(str);
                    if (errStr) {
                        errorMsg = errStr;
                        LOG_TRACE(std::string("Python error: ") + errorMsg);
                    }
                }
            }
            
            m_ErrorSummary = L"Import Error";
            // errorMsg is UTF-8 (from PyUnicode_AsUTF8); decode it properly to
            // UTF-16 so non-ASCII Python error text is not corrupted.
            m_ErrorDescription = L"Failed to execute module: " + PythonFar::UTF8ToWide(errorMsg);
            
            return nullptr;
        }

        // Clean up

        // Get the Plugin class
        PyObj pPluginClass = PyObject_GetAttrString(pModule, "Plugin");
        if (!pPluginClass) {
            LOG_TRACE("Module has no Plugin class");
            m_ErrorSummary = L"Plugin Error";
            std::wstring wideModuleName = PythonFar::UTF8ToWide(moduleNameStr);
            m_ErrorDescription = L"Module does not have a Plugin class: " + wideModuleName;
            
            return nullptr;
        }

        LOG_TRACE("Successfully loaded plugin module");

        auto pluginModule = std::make_unique<PluginModule>(filename, pModule.release(), pPluginClass.release());
        
        
        return pluginModule;

    } catch (...) {
        LOG_TRACE("Exception in CreatePluginModule");
        
        return nullptr;
    }
}

bool PythonFarAdapter::RunScript(const wchar_t* path) {
    if (!Py_IsInitialized()) {
        InitializePython();
    }
    if (!Py_IsInitialized()) {
        LOG_ERROR("RunScript: Python not initialized");
        return false;
    }

    std::string narrowPath = PythonFar::WideToUTF8(path);
    LOG_TRACE("RunScript: " << narrowPath);

    {
        DWORD attrs = GetFileAttributesW(path);
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            LOG_ERROR("RunScript: File not found: " << narrowPath);
            return false;
        }
    }

    ScopedGIL gil;

    FILE* fp = _wfsopen(path, L"rb", _SH_DENYNO);
    if (!fp) {
        LOG_ERROR("RunScript: Cannot open file: " << narrowPath);
        PyErr_Print();
        return false;
    }

    int result = PyRun_SimpleFileExFlags(fp, narrowPath.c_str(), 1, nullptr);
    if (result != 0) {
        LOG_ERROR("RunScript: Script execution failed");
        PyErr_Print();
        return false;
    }

    LOG_TRACE("RunScript: Script executed successfully");
    return true;
}

// Forward declarations of global wrapper functions
extern "C" {
    void WINAPI GetGlobalInfoW(GlobalInfo* Info);
    void WINAPI GetPluginInfoW(PluginInfo* Info);
    void WINAPI SetStartupInfoW(const PluginStartupInfo* Info);
    HANDLE WINAPI OpenW(const OpenInfo* Info);
    void WINAPI ClosePanelW(const ClosePanelInfo* Info);
    intptr_t WINAPI ConfigureW(const ConfigureInfo* Info);
    void WINAPI ExitFARW(const ExitInfo* Info);
    intptr_t WINAPI ProcessDialogEventW(const ProcessDialogEventInfo* Info);
    intptr_t WINAPI ProcessEditorEventW(const ProcessEditorEventInfo* Info);
    intptr_t WINAPI ProcessEditorInputW(const ProcessEditorInputInfo* Info);
    intptr_t WINAPI ProcessViewerEventW(const ProcessViewerEventInfo* Info);
    intptr_t WINAPI ProcessConsoleInputW(ProcessConsoleInputInfo* Info);
    intptr_t WINAPI ProcessPanelEventW(const ProcessPanelEventInfo* Info);
    intptr_t WINAPI ProcessPanelInputW(const ProcessPanelInputInfo* Info);
    intptr_t WINAPI ProcessHostFileW(const ProcessHostFileInfo* Info);
    intptr_t WINAPI CompareW(const CompareInfo* Info);
    intptr_t WINAPI SetFindListW(const SetFindListInfo* Info);

    // VFS exports
    void WINAPI GetOpenPanelInfoW(OpenPanelInfo* Info);
    intptr_t WINAPI GetFindDataW(GetFindDataInfo* Info);
    void WINAPI FreeFindDataW(const FreeFindDataInfo* Info);
    intptr_t WINAPI SetDirectoryW(const SetDirectoryInfo* Info);
    intptr_t WINAPI DeleteFilesW(const DeleteFilesInfo* Info);
    intptr_t WINAPI MakeDirectoryW(MakeDirectoryInfo* Info);
    intptr_t WINAPI GetFilesW(GetFilesInfo* Info);
    intptr_t WINAPI PutFilesW(const PutFilesInfo* Info);
    HANDLE WINAPI AnalyseW(const AnalyseInfo* Info);
    void WINAPI CloseAnalyseW(const CloseAnalyseInfo* Info);
}

FARPROC PythonFarAdapter::GetFunction(HANDLE instance, const wchar_t* functionName) {
    if (!instance || !functionName) return nullptr;
    std::wstring funcName(functionName);
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, funcName.c_str(), (int)funcName.size(), nullptr, 0, nullptr, nullptr);
    std::string funcNarrow(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, funcName.c_str(), (int)funcName.size(), &funcNarrow[0], size_needed, nullptr, nullptr);
    LOG_TRACE("GetFunction requested: " + funcNarrow);

    FARPROC fp = nullptr;
    PluginModule* mod = static_cast<PluginModule*>(instance);

    // Required/core functions
    if (funcName == L"GetGlobalInfoW") fp = reinterpret_cast<FARPROC>(::GetGlobalInfoW);
    else if (funcName == L"GetPluginInfoW") fp = reinterpret_cast<FARPROC>(::GetPluginInfoW);
    else if (funcName == L"SetStartupInfoW") fp = reinterpret_cast<FARPROC>(::SetStartupInfoW);
    
    // Optional functions - only export if python class actually has the method
    else if (funcName == L"OpenW" && (mod->HasMethod("OpenW") || mod->HasMethod("open"))) fp = reinterpret_cast<FARPROC>(::OpenW);
    else if (funcName == L"ClosePanelW" && mod->HasMethod("close_panel")) fp = reinterpret_cast<FARPROC>(::ClosePanelW);
    else if (funcName == L"ConfigureW" && mod->HasMethod("configure")) fp = reinterpret_cast<FARPROC>(::ConfigureW);
    else if (funcName == L"ExitFARW" && mod->HasMethod("exit_far")) fp = reinterpret_cast<FARPROC>(::ExitFARW);
    else if (funcName == L"ProcessDialogEventW" && mod->HasMethod("process_dialog_event")) fp = reinterpret_cast<FARPROC>(::ProcessDialogEventW);
    else if (funcName == L"ProcessPanelEventW" && mod->HasMethod("process_panel_event")) fp = reinterpret_cast<FARPROC>(::ProcessPanelEventW);
    else if (funcName == L"ProcessPanelInputW" && mod->HasMethod("process_panel_input")) fp = reinterpret_cast<FARPROC>(::ProcessPanelInputW);
    else if (funcName == L"ProcessHostFileW" && mod->HasMethod("process_host_file")) fp = reinterpret_cast<FARPROC>(::ProcessHostFileW);
    else if (funcName == L"CompareW" && mod->HasMethod("compare")) fp = reinterpret_cast<FARPROC>(::CompareW);
    else if (funcName == L"SetFindListW" && mod->HasMethod("set_find_list")) fp = reinterpret_cast<FARPROC>(::SetFindListW);
    else if (funcName == L"AnalyseW" && mod->HasMethod("analyse")) fp = reinterpret_cast<FARPROC>(::AnalyseW);
    else if (funcName == L"CloseAnalyseW" && mod->HasMethod("close_analyse")) fp = reinterpret_cast<FARPROC>(::CloseAnalyseW);
    else if (funcName == L"ProcessEditorEventW" && mod->HasMethod("process_editor_event")) fp = reinterpret_cast<FARPROC>(::ProcessEditorEventW);
    else if (funcName == L"ProcessEditorInputW" && mod->HasMethod("process_editor_input")) fp = reinterpret_cast<FARPROC>(::ProcessEditorInputW);
    else if (funcName == L"ProcessViewerEventW" && mod->HasMethod("process_viewer_event")) fp = reinterpret_cast<FARPROC>(::ProcessViewerEventW);
    else if (funcName == L"ProcessConsoleInputW" && mod->HasMethod("process_console_input")) fp = reinterpret_cast<FARPROC>(::ProcessConsoleInputW);
    else if (funcName == L"GetOpenPanelInfoW" && mod->HasMethod("get_open_panel_info")) fp = reinterpret_cast<FARPROC>(::GetOpenPanelInfoW);
    else if (funcName == L"GetFindDataW" && mod->HasMethod("get_find_data")) fp = reinterpret_cast<FARPROC>(::GetFindDataW);
    else if (funcName == L"FreeFindDataW" && mod->HasMethod("free_find_data")) fp = reinterpret_cast<FARPROC>(::FreeFindDataW);
    else if (funcName == L"SetDirectoryW" && mod->HasMethod("set_directory")) fp = reinterpret_cast<FARPROC>(::SetDirectoryW);
    else if (funcName == L"DeleteFilesW" && mod->HasMethod("delete_files")) fp = reinterpret_cast<FARPROC>(::DeleteFilesW);
    else if (funcName == L"MakeDirectoryW" && mod->HasMethod("make_directory")) fp = reinterpret_cast<FARPROC>(::MakeDirectoryW);
    else if (funcName == L"GetFilesW" && mod->HasMethod("get_files")) fp = reinterpret_cast<FARPROC>(::GetFilesW);
    else if (funcName == L"PutFilesW" && mod->HasMethod("put_files")) fp = reinterpret_cast<FARPROC>(::PutFilesW);

    LOG_TRACE("GetFunction result for '" + funcNarrow + "' -> " + (fp ? std::string("non-null") : std::string("NULL")));
    return fp;
}

void PythonFarAdapter::LogExportStatus() {
    LOG_TRACE("LogExportStatus: checking exported bridge and PanelControl pointer");

    // Check if exported bridge is resolvable from this module
    HMODULE hSelf = GetModuleHandleW(PythonFar::ADAPTER_DLL_NAME);
    if (!hSelf) {
        LOG_TRACE("LogExportStatus: GetModuleHandle self failed");
        return;
    }
    FARPROC bridge = GetProcAddress(hSelf, "PythonFar_DialogInit");
    if (!bridge) {
        HMODULE hLoader = GetModuleHandleW(PythonFar::LOADER_DLL_NAME);
        bridge = hLoader ? GetProcAddress(hLoader, "PythonFar_DialogInit") : nullptr;
    }
    LOG_TRACE(std::string("LogExportStatus: GetProcAddress('PythonFar_DialogInit') -> ") + (bridge ? "non-null" : "NULL"));
}

bool PythonFarAdapter::GetError(ErrorInfo* info) {

    if (!info) return false;

    if (m_ErrorSummary.empty()) return false;

    info->StructSize = sizeof(ErrorInfo);
    
    // Transfer to LastError buffers so the pointers remain valid for Far,
    // but we can clear the active error state to prevent infinite dialog loops.
    m_LastErrorSummary = std::move(m_ErrorSummary);
    m_LastErrorDescription = std::move(m_ErrorDescription);
    
    // Explicitly clear them because std::move on SSO (Small String Optimization) 
    // strings in MSVC does NOT clear the source string's length!
    m_ErrorSummary.clear();
    m_ErrorDescription.clear();
    
    info->Summary = m_LastErrorSummary.c_str();
    info->Description = m_LastErrorDescription.c_str();

    return true;
}

void PythonFarAdapter::SetError(const std::wstring& summary, const std::wstring& description) {
    m_ErrorSummary = summary;
    m_ErrorDescription = description;
}

// ===== PluginModule Implementation =====

// Convert a UTF-8 C string to std::wstring (empty on null/failure).
static std::wstring Utf8ToWideStr(const char* utf8) {
    if (!utf8) return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (len <= 0) return std::wstring();
    std::wstring ws(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, ws.data(), len);
    if (!ws.empty()) ws.pop_back();
    return ws;
}

// Capture the currently-set Python exception (and its traceback) as a wide
// string, then clear the error indicator. Must be called with the GIL held.
// Returns an empty string if no error is set. The error is consumed (cleared).
static std::wstring CapturePythonTraceback() {
    if (!PyErr_Occurred()) return std::wstring();

    PyObject *ptype = nullptr, *pvalue = nullptr, *ptraceback = nullptr;
    PyErr_Fetch(&ptype, &pvalue, &ptraceback);
    PyErr_NormalizeException(&ptype, &pvalue, &ptraceback);
    if (ptraceback) {
        PyException_SetTraceback(pvalue, ptraceback);
    }

    std::wstring result;

    // Try traceback.format_exception() for a full, Python-style traceback.
    PyObj tbModule(PyImport_ImportModule("traceback"));
    if (tbModule) {
        PyObj formatted(PyObject_CallMethod(
            tbModule, "format_exception", "OOO",
            ptype ? ptype : Py_None,
            pvalue ? pvalue : Py_None,
            ptraceback ? ptraceback : Py_None));
        if (formatted && PyList_Check(formatted)) {
            Py_ssize_t n = PyList_Size(formatted);
            std::string utf8;
            for (Py_ssize_t i = 0; i < n; ++i) {
                PyObject* line = PyList_GetItem(formatted, i);  // borrowed
                if (line && PyUnicode_Check(line)) {
                    const char* s = PyUnicode_AsUTF8(line);
                    if (s) utf8 += s;
                }
            }
            result = Utf8ToWideStr(utf8.c_str());
        }
    }

    // Fallback: just stringify the exception value.
    if (result.empty() && pvalue) {
        PyObj str(PyObject_Str(pvalue));
        if (str) {
            const char* s = PyUnicode_AsUTF8(str);
            if (s) result = Utf8ToWideStr(s);
        }
    }

    Py_XDECREF(ptype);
    Py_XDECREF(pvalue);
    Py_XDECREF(ptraceback);
    PyErr_Clear();
    return result;
}

static bool ReadUnsignedAttr(PyObject* obj, const char* name, unsigned long* value) {
    PyObj attr = PyObject_GetAttrString(obj, name);
    if (!attr) {
        PyErr_Clear();
        return false;
    }

    unsigned long result = PyLong_AsUnsignedLong(attr);
    if (PyErr_Occurred()) {
        PyErr_Clear();
        return false;
    }

    *value = result;
    return true;
}

static bool ReadPythonGuid(PyObject* obj, GUID* guid) {
    unsigned long data1 = 0;
    unsigned long data2 = 0;
    unsigned long data3 = 0;
    if (!ReadUnsignedAttr(obj, "Data1", &data1) ||
        !ReadUnsignedAttr(obj, "Data2", &data2) ||
        !ReadUnsignedAttr(obj, "Data3", &data3)) {
        return false;
    }

    PyObj data4 = PyObject_GetAttrString(obj, "Data4");
    if (!data4) {
        PyErr_Clear();
        return false;
    }

    if (PySequence_Size(data4) < 8) {
        return false;
    }

    GUID parsed = {};
    parsed.Data1 = static_cast<unsigned long>(data1);
    parsed.Data2 = static_cast<unsigned short>(data2);
    parsed.Data3 = static_cast<unsigned short>(data3);

    for (Py_ssize_t i = 0; i < 8; ++i) {
        PyObj item = PySequence_GetItem(data4, i);
        if (!item) {
            PyErr_Clear();
            return false;
        }
        unsigned long byteValue = PyLong_AsUnsignedLong(item);
        if (PyErr_Occurred()) {
            PyErr_Clear();
            return false;
        }
        parsed.Data4[i] = static_cast<unsigned char>(byteValue & 0xFF);
    }

    *guid = parsed;
    return true;
}

PluginModule::PluginModule(const std::wstring& filename, PyObject* module, PyObject* pluginClass)
    : m_Filename(filename)
    , m_Module(module)
    , m_PluginClass(pluginClass)
    , m_PluginInstance(nullptr) {
    
    LOG_TRACE("PluginModule constructor");

    // Generate a unique GUID for this plugin
    CoCreateGuid(&m_Guid);

    // Create an instance of the plugin class
    ScopedGIL gil;
    
    m_PluginInstance.reset(PyObject_CallObject(m_PluginClass, nullptr));
    if (!m_PluginInstance) {
        LOG_TRACE("Failed to create plugin instance");
        PyErr_Print();
    } else {
        LOG_TRACE("Plugin instance created successfully");

        // Read metadata from the Python instance. Defaults are provided by
        // the far_plugin.Plugin base class so no C++ fallbacks are needed.
        auto ReadStrAttr = [&](const char* attr, std::wstring& dest) {
            PyErr_Clear();
            PyObj obj = PyObject_GetAttrString(m_PluginInstance, attr);
            if (obj && PyUnicode_Check(obj)) {
                const char* s = PyUnicode_AsUTF8(obj);
                // PyUnicode_AsUTF8 returns UTF-8; decode it properly to UTF-16
                // so non-ASCII metadata (titles, descriptions, authors) is not
                // corrupted by byte-wise widening.
                if (s) dest = PythonFar::UTF8ToWide(s);
            }
            PyErr_Clear();
        };

        ReadStrAttr("title",       m_Title);
        ReadStrAttr("description", m_Description);
        ReadStrAttr("author",      m_Author);

        // version: tuple (major, minor, build, revision) -> "major.minor.build.revision"
        {
            PyErr_Clear();
            PyObj verObj = PyObject_GetAttrString(m_PluginInstance, "version");
            if (verObj && PyTuple_Check(verObj) && PyTuple_Size(verObj) >= 2) {
                std::string ver;
                for (Py_ssize_t i = 0; i < PyTuple_Size(verObj); ++i) {
                    if (i > 0) ver += ".";
                    PyObject* item = PyTuple_GetItem(verObj, i); // borrowed
                    ver += std::to_string(PyLong_AsLong(item));
                }
                m_Version = std::wstring(ver.begin(), ver.end());
            }
            PyErr_Clear();
        }

        // min_far_version: tuple (major, minor, build, revision) with Far 3.0.0.0 default
        {
            PyErr_Clear();
            PyObj minVerObj = PyObject_GetAttrString(m_PluginInstance, "min_far_version");
            if (minVerObj && PyTuple_Check(minVerObj) && PyTuple_Size(minVerObj) >= 4) {
                m_MinFarVersionMajor = (DWORD)PyLong_AsLong(PyTuple_GetItem(minVerObj, 0));
                m_MinFarVersionMinor = (DWORD)PyLong_AsLong(PyTuple_GetItem(minVerObj, 1));
                m_MinFarVersionBuild = (DWORD)PyLong_AsLong(PyTuple_GetItem(minVerObj, 2));
                m_MinFarVersionRevision = (DWORD)PyLong_AsLong(PyTuple_GetItem(minVerObj, 3));
            }
            PyErr_Clear();
        }

        // guid
        {
            PyErr_Clear();
            PyObj guidObj = PyObject_GetAttrString(m_PluginInstance, "guid");
            if (guidObj) {
                if (ReadPythonGuid(guidObj, &m_Guid)) {
                    LOG_TRACE("PluginModule: using Python plugin guid");
                } else {
                    LOG_TRACE("PluginModule: failed to read Python plugin guid, using generated guid");
                }
            }
            PyErr_Clear();
        }
    }

    
}

PluginModule::~PluginModule() {
    LOG_TRACE("PluginModule destructor");

    ScopedGIL gil;

    if (m_PluginInstance) {
    }
    if (m_PluginClass) {
    }
    if (m_Module) {
    }

    
}

PyObject* PluginModule::CallMethod(const char* methodName, const char* format, ...) {
    LOG_TRACE(std::string("CallMethod: ") + methodName);
    
    if (!m_PluginInstance) {
        LOG_TRACE("CallMethod: m_PluginInstance is null!");
        return nullptr;
    }
    
    LOG_TRACE(std::string("CallMethod: m_PluginInstance = ") + std::to_string((uintptr_t)m_PluginInstance.get()));

    LOG_TRACE("Before ScopedGIL on thread " + std::to_string(GetCurrentThreadId()));
    try {
        ScopedGIL gil;
        LOG_TRACE("After ScopedGIL");
        
        // Debug: Check if the instance is still valid
        LOG_TRACE("Before HasAttrString");
        if (!PyObject_HasAttrString(m_PluginInstance, "__class__")) {
            LOG_TRACE("CallMethod: m_PluginInstance seems invalid (no __class__ attr)");
            
            return nullptr;
        }

        LOG_TRACE("CallMethod: Fetching method attribute...");
        PyObj method = PyObject_GetAttrString(m_PluginInstance, methodName);
        if (!method) {
            LOG_TRACE(std::string("Method not found: ") + methodName);
            
            // Print the exception if any
            if (PyErr_Occurred()) {
                PyObject *ptype, *pvalue, *ptraceback;
                PyErr_Fetch(&ptype, &pvalue, &ptraceback);
                LOG_TRACE("Python error occurred during GetAttrString!");
                if (pvalue) {
                    PyObj str = PyObject_Str(pvalue);
                    if (str) {
                        const char* errStr = PyUnicode_AsUTF8(str);
                        if (errStr) {
                            LOG_TRACE(std::string("Python error detail: ") + errStr);
                        }
                    }
                }
                Py_XDECREF(ptype);
                Py_XDECREF(pvalue);
                Py_XDECREF(ptraceback);
            } else {
                LOG_TRACE("No Python exception was set.");
            }
            
            return nullptr;
        }
        
        if (!PyCallable_Check(method)) {
            LOG_TRACE(std::string("Method not callable: ") + methodName);
            return nullptr;
        }

        LOG_TRACE("CallMethod: Building arguments...");
        PyObj args;
        if (format && *format) {
            va_list vargs;
            va_start(vargs, format);
            args.reset(Py_VaBuildValue(format, vargs));
            va_end(vargs);
        }

        LOG_TRACE("CallMethod: Invoking method...");
        PyObj result = PyObject_CallObject(method, args);
        if (!result) {
            LOG_TRACE(std::string("Method invocation failed: ") + methodName);
            if (PyErr_Occurred()) {
                PyObject *ptype, *pvalue, *ptraceback;
                PyErr_Fetch(&ptype, &pvalue, &ptraceback);
                if (pvalue) {
                    PyObj str = PyObject_Str(pvalue);
                    if (str) {
                        const char* errStr = PyUnicode_AsUTF8(str);
                        if (errStr) {
                            LOG_TRACE(std::string("Python error detail: ") + errStr);
                        }
                    }
                }
                Py_XDECREF(ptype);
                Py_XDECREF(pvalue);
                Py_XDECREF(ptraceback);
            }
        }
        return result.release();
    } catch (...) {
        LOG_TRACE("EXCEPTION in CallMethod!");
        return nullptr;
    }
}

bool PluginModule::HasMethod(const char* methodName) {
    if (!m_PluginInstance) return false;
    try {
        ScopedGIL gil;
        bool has = PyObject_HasAttrString(m_PluginInstance, methodName);
        LOG_TRACE(std::string("HasMethod checking ") + methodName + " -> " + (has ? "yes" : "no"));
        if (has) {
            PyObj method = PyObject_GetAttrString(m_PluginInstance, methodName);
            if (method && PyCallable_Check(method)) {
                return true;
            }
            if (!method) PyErr_Clear();
        }
        return false;
    } catch (...) {
        return false;
    }
}

void PluginModule::GetGlobalInfoW(GlobalInfo* Info) {
    LOG_TRACE("PluginModule::GetGlobalInfoW");

    if (!Info) return;

    // Parse version string "major.minor.build.revision" back into numbers.
    // Defaults to 0 if parsing fails — the base class guarantees a valid version tuple.
    DWORD major = 0, minor = 0, build = 0, revision = 0;
    {
        std::wstring v = m_Version;
        auto nextPart = [&](DWORD& out) {
            size_t dot = v.find(L'.');
            out = v.empty() ? 0 : (DWORD)std::stoul(v);
            v = (dot != std::wstring::npos) ? v.substr(dot + 1) : L"";
        };
        try { nextPart(major); nextPart(minor); nextPart(build); nextPart(revision); }
        catch (...) {}
    }

    Info->StructSize   = sizeof(GlobalInfo);
    Info->MinFarVersion = MAKEFARVERSION(m_MinFarVersionMajor, m_MinFarVersionMinor,
                                         m_MinFarVersionBuild, m_MinFarVersionRevision,
                                         VS_RELEASE);
    Info->Version      = MAKEFARVERSION(major, minor, build, revision, VS_RELEASE);
    Info->Guid         = m_Guid;
    Info->Title        = m_Title.c_str();
    Info->Description  = m_Description.c_str();
    Info->Author       = m_Author.c_str();
}

void PluginModule::ReportPluginInfoFailure(const std::wstring& summary, const std::wstring& detail) {
    LOG_ERROR("PluginModule::GetPluginInfoW failure in plugin '" + WideToUTF8(m_Filename.c_str())
              + "': " + WideToUTF8(summary.c_str())
              + " | " + WideToUTF8(detail.c_str()));

    // Build a user-facing description: which plugin, what went wrong, traceback.
    std::wstring description = L"Plugin: " + m_Filename + L"\n" + summary;
    if (!detail.empty()) {
        description += L"\n\n" + detail;
    }

    // 1) Record for Far's GetError export so the host can surface it.
    if (g_Adapter) {
        g_Adapter->SetError(summary, description);
    }

    // 2) Show a message box to the user immediately.
    PythonFar_ShowMessage(&m_Guid, L"PythonFar: Plugin Error", description.c_str());
}

void PluginModule::GetPluginInfoW(PluginInfo* Info) {
    LOG_TRACE("PluginModule::GetPluginInfoW");
    
    if (!Info) return;

    Info->StructSize = sizeof(PluginInfo);
    Info->Flags = 0;
    Info->CommandPrefix = nullptr;
    Info->Instance = nullptr;
    
    Info->DiskMenu.Count = 0;
    Info->DiskMenu.Strings = nullptr;
    Info->DiskMenu.Guids = nullptr;
    
    Info->PluginMenu.Count = 0;
    Info->PluginMenu.Strings = nullptr;
    Info->PluginMenu.Guids = nullptr;
    
    Info->PluginConfig.Count = 0;
    Info->PluginConfig.Strings = nullptr;
    Info->PluginConfig.Guids = nullptr;

    ScopedGIL gil;

    // Every plugin MUST provide get_plugin_info() (directly or via the base
    // class) returning a dict with at least a "title". If the method is
    // missing, raises, or returns invalid data we fail loudly: record the
    // error for Far's GetError export AND show the user a message box with the
    // Python traceback, then leave a safe default PluginInfo so Far does not
    // read partially-initialized memory.
    //
    // We invoke the method directly (not through CallMethod) so we can capture
    // the Python traceback on failure instead of having it silently cleared.
    if (!m_PluginInstance) {
        ReportPluginInfoFailure(
            L"Plugin instance is not available.",
            L"The plugin failed to construct; cannot query get_plugin_info().");
        return;
    }

    PyObj method(PyObject_GetAttrString(m_PluginInstance, "get_plugin_info"));
    if (!method || !PyCallable_Check(method)) {
        if (PyErr_Occurred()) PyErr_Clear();
        ReportPluginInfoFailure(
            L"Plugin does not implement get_plugin_info().",
            L"Define get_plugin_info() returning a dict with at least a 'title', "
            L"or inherit from the far.Plugin base class which provides it.");
        return;
    }

    PyObj result(PyObject_CallObject(method, nullptr));
    if (!result) {
        // The method raised: capture the full traceback for the user.
        std::wstring tb = CapturePythonTraceback();
        ReportPluginInfoFailure(
            L"get_plugin_info() raised an exception.",
            tb.empty()
                ? L"See %TEMP%\\pythonfar_adapter.log for details."
                : tb);
        return;
    }

    if (!PyDict_Check(result)) {
        if (PyErr_Occurred()) PyErr_Clear();
        ReportPluginInfoFailure(
            L"get_plugin_info() did not return a dict.",
            L"It must return a dict with at least a 'title' key.");
        return;
    }

    // A plugin must provide at least a non-empty title (its name).
    {
        PyObject* titleCheck = PyDict_GetItemString(result, "title");  // borrowed
        bool hasTitle = titleCheck && PyUnicode_Check(titleCheck)
                        && PyUnicode_GetLength(titleCheck) > 0;
        if (!hasTitle) {
            ReportPluginInfoFailure(
                L"get_plugin_info() did not provide a plugin name.",
                L"The returned dict must contain a non-empty 'title'.");
            return;
        }
    }
    
    // Parse the dictionary result
    PyObject* flagsObj = PyDict_GetItemString(result, "flags");
    if (flagsObj && PyLong_Check(flagsObj)) {
        Info->Flags = PyLong_AsLong(flagsObj);
    }

    PyObject* titleObj = PyDict_GetItemString(result, "title");
    if (titleObj && PyUnicode_Check(titleObj)) {
        const char* titleStr = PyUnicode_AsUTF8(titleObj);
        if (titleStr) {
            int len = MultiByteToWideChar(CP_UTF8, 0, titleStr, -1, nullptr, 0);
            titleBuffer.resize(len);
            MultiByteToWideChar(CP_UTF8, 0, titleStr, -1, &titleBuffer[0], len);
            menuStrings.clear();
            menuStrings.push_back(&titleBuffer[0]);
            menuGuids.clear();
            menuGuids.push_back(m_Guid);  // Use the plugin's GUID
            Info->PluginMenu.Guids = menuGuids.data();
            Info->PluginMenu.Strings = menuStrings.data();
            Info->PluginMenu.Count = menuStrings.size();
        }
    }

    PyObject* descObj = PyDict_GetItemString(result, "description");
    if (descObj && PyUnicode_Check(descObj)) {
        const char* descStr = PyUnicode_AsUTF8(descObj);
        if (descStr) {
            int len = MultiByteToWideChar(CP_UTF8, 0, descStr, -1, nullptr, 0);
            descBuffer.resize(len);
            MultiByteToWideChar(CP_UTF8, 0, descStr, -1, &descBuffer[0], len);
            configStrings.clear();
            configStrings.push_back(&descBuffer[0]);
            // Re-use menuGuids or populate a new guids array for config. We can just point it to menuGuids since it has m_Guid
            if (menuGuids.empty()) {
                menuGuids.push_back(m_Guid);
            }
            Info->PluginConfig.Guids = menuGuids.data();
            Info->PluginConfig.Strings = configStrings.data();
            Info->PluginConfig.Count = configStrings.size();
        }
    }

    PyObject* authorObj = PyDict_GetItemString(result, "author");
    if (authorObj && PyUnicode_Check(authorObj)) {
        const char* authorStr = PyUnicode_AsUTF8(authorObj);
        if (authorStr) {
            int len = MultiByteToWideChar(CP_UTF8, 0, authorStr, -1, nullptr, 0);
            authorBuffer.resize(len);
            MultiByteToWideChar(CP_UTF8, 0, authorStr, -1, &authorBuffer[0], len);
        }
    }

    PyObject* versionObj = PyDict_GetItemString(result, "version");
    if (versionObj && PyUnicode_Check(versionObj)) {
        const char* versionStr = PyUnicode_AsUTF8(versionObj);
        if (versionStr) {
            int len = MultiByteToWideChar(CP_UTF8, 0, versionStr, -1, nullptr, 0);
            versionBuffer.resize(len);
            MultiByteToWideChar(CP_UTF8, 0, versionStr, -1, &versionBuffer[0], len);
        }
    }

    PyObject* prefixObj = PyDict_GetItemString(result, "command_prefix");
    if (prefixObj && PyUnicode_Check(prefixObj)) {
        const char* prefixStr = PyUnicode_AsUTF8(prefixObj);
        if (prefixStr) {
            int len = MultiByteToWideChar(CP_UTF8, 0, prefixStr, -1, nullptr, 0);
            commandPrefixBuffer.resize(len);
            MultiByteToWideChar(CP_UTF8, 0, prefixStr, -1, &commandPrefixBuffer[0], len);
            Info->CommandPrefix = commandPrefixBuffer.data();
        }
    }
}

void PluginModule::SetStartupInfoW(const PluginStartupInfo* Info) {
    LOG_TRACE("PluginModule::SetStartupInfoW");
    
    // Pass the pointer to Python so it can be used with ctypes
    CallMethod("set_startup_info", "(n)", (Py_ssize_t)Info);
}

HANDLE PluginModule::OpenW(const OpenInfo* Info) {
    LOG_TRACE("PluginModule::OpenW called");
    
    // Try to call the Python plugin's OpenW or open method
    // We pass the Info pointer as an integer argument
    PyObj result = CallMethod("OpenW", "(n)", (Py_ssize_t)Info);
    if (!result) {
        // Try lowercase 'open' as fallback
        result = CallMethod("open", "(n)", (Py_ssize_t)Info);
    }
    
    if (result) {
        LOG_TRACE("PluginModule::OpenW - Python method returned successfully");
        
        // If the result is an integer, return it as the HANDLE
        if (PyLong_Check(result)) {
            HANDLE hResult = (HANDLE)PyLong_AsVoidPtr(result);
            return hResult;
        }
        
    } else {
        LOG_TRACE("PluginModule::OpenW - Python method not found or failed");
    }
    
    return INVALID_HANDLE_VALUE;
}

void PluginModule::ClosePanelW(const ClosePanelInfo* Info) {
    LOG_TRACE("PluginModule::ClosePanelW");
    CallMethod("close_panel", "(n)", (Py_ssize_t)Info);
}

intptr_t PluginModule::ConfigureW(const ConfigureInfo* Info) {
    LOG_TRACE("PluginModule::ConfigureW");
    
    // Try to call the Python plugin's configure method
    PyObj result = CallMethod("configure", "(n)", (Py_ssize_t)Info);
    if (result) {
        // If configure returns True, return 1 (success), else 0
        int ret = PyObject_IsTrue(result) ? 1 : 0;
        return ret;
    }
    
    return 0; // No configure method, or failed
}

void PluginModule::ExitFARW(const ExitInfo* Info) {
    LOG_TRACE("PluginModule::ExitFARW");
    CallMethod("exit_far", "(n)", (Py_ssize_t)Info);
}

intptr_t PluginModule::ProcessDialogEventW(const ProcessDialogEventInfo* Info) {
    LOG_TRACE("PluginModule::ProcessDialogEventW");
    if (!Info) return 0;
    PyObj result = CallMethod("process_dialog_event", "(n)", (Py_ssize_t)Info);
    if (result) {
        int ret = PyObject_IsTrue(result) ? 1 : 0;
        return ret;
    }
    return 0; // No handler or failed — let Far handle it
}

intptr_t PluginModule::ProcessPanelEventW(const ProcessPanelEventInfo* Info) {
    LOG_TRACE("PluginModule::ProcessPanelEventW");
    if (!Info) return 0;
    PyObj result = CallMethod("process_panel_event", "(n)", (Py_ssize_t)Info);
    if (result) {
        intptr_t ret = PyLong_AsSsize_t(result);
        return ret;
    }
    return 0;
}

intptr_t PluginModule::ProcessPanelInputW(const ProcessPanelInputInfo* Info) {
    LOG_TRACE("PluginModule::ProcessPanelInputW");
    if (!Info) return 0;
    PyObj result = CallMethod("process_panel_input", "(n)", (Py_ssize_t)Info);
    if (result) {
        intptr_t ret = PyLong_AsSsize_t(result);
        return ret;
    }
    return 0;
}

intptr_t PluginModule::ProcessHostFileW(const ProcessHostFileInfo* Info) {
    LOG_TRACE("PluginModule::ProcessHostFileW");
    if (!Info) return 0;
    PyObj result = CallMethod("process_host_file", "(n)", (Py_ssize_t)Info);
    if (result) {
        intptr_t ret = PyLong_AsSsize_t(result);
        return ret;
    }
    return 0;
}

intptr_t PluginModule::CompareW(const CompareInfo* Info) {
    LOG_TRACE("PluginModule::CompareW");
    if (!Info) return -2;
    PyObj result = CallMethod("compare", "(n)", (Py_ssize_t)Info);
    if (result) {
        intptr_t ret = PyLong_AsSsize_t(result);
        return ret;
    }
    return -2;
}

intptr_t PluginModule::SetFindListW(const SetFindListInfo* Info) {
    LOG_TRACE("PluginModule::SetFindListW");
    if (!Info) return 0;
    PyObj result = CallMethod("set_find_list", "(n)", (Py_ssize_t)Info);
    if (result) {
        intptr_t ret = PyLong_AsSsize_t(result);
        return ret;
    }
    return 0;
}

HANDLE PluginModule::AnalyseW(const AnalyseInfo* Info) {
    LOG_TRACE("PluginModule::AnalyseW");
    if (!Info) return nullptr;
    PyObj result = CallMethod("analyse", "(n)", (Py_ssize_t)Info);
    if (result) {
        HANDLE ret = reinterpret_cast<HANDLE>(PyLong_AsVoidPtr(result));
        return ret;
    }
    return nullptr;
}

void PluginModule::CloseAnalyseW(const CloseAnalyseInfo* Info) {
    LOG_TRACE("PluginModule::CloseAnalyseW");
    if (!Info) return;
    PyObj result = CallMethod("close_analyse", "(n)", (Py_ssize_t)Info);
    if (result) {
    }
}

intptr_t PluginModule::ProcessEditorEventW(const ProcessEditorEventInfo* Info) {
    LOG_TRACE("PluginModule::ProcessEditorEventW");
    if (!Info) return 0;
    PyObj result = CallMethod("process_editor_event", "(n)", (Py_ssize_t)Info);
    if (result) {
        int ret = PyObject_IsTrue(result) ? 1 : 0;
        return ret;
    }
    return 0;
}

intptr_t PluginModule::ProcessEditorInputW(const ProcessEditorInputInfo* Info) {
    LOG_TRACE("PluginModule::ProcessEditorInputW");
    if (!Info) return 0;
    PyObj result = CallMethod("process_editor_input", "(n)", (Py_ssize_t)Info);
    if (result) {
        int ret = PyObject_IsTrue(result) ? 1 : 0;
        return ret;
    }
    return 0;
}

intptr_t PluginModule::ProcessViewerEventW(const ProcessViewerEventInfo* Info) {
    LOG_TRACE("PluginModule::ProcessViewerEventW");
    if (!Info) return 0;
    PyObj result = CallMethod("process_viewer_event", "(n)", (Py_ssize_t)Info);
    if (result) {
        int ret = PyObject_IsTrue(result) ? 1 : 0;
        return ret;
    }
    return 0;
}

intptr_t PluginModule::ProcessConsoleInputW(const ProcessConsoleInputInfo* Info) {
    LOG_TRACE("PluginModule::ProcessConsoleInputW");
    if (!Info) return 0;
    PyObj result = CallMethod("process_console_input", "(n)", (Py_ssize_t)Info);
    if (result) {
        int ret = PyObject_IsTrue(result) ? 1 : 0;
        return ret;
    }
    return 0;
}

// ---- VFS PluginModule methods ----

void PluginModule::GetOpenPanelInfoW(OpenPanelInfo* Info) {
    LOG_TRACE("PluginModule::GetOpenPanelInfoW");
    if (!Info) return;
    PyObj result = CallMethod("get_open_panel_info", "(n)", (Py_ssize_t)Info);
    if (result) {
    }
}

intptr_t PluginModule::GetFindDataW(GetFindDataInfo* Info) {
    LOG_TRACE("PluginModule::GetFindDataW");
    if (!Info) return 0;
    PyObj result = CallMethod("get_find_data", "(n)", (Py_ssize_t)Info);
    if (result) {
        int ret = PyObject_IsTrue(result) ? 1 : 0;
        return ret;
    }
    return 0;
}

void PluginModule::FreeFindDataW(const FreeFindDataInfo* Info) {
    LOG_TRACE("PluginModule::FreeFindDataW");
    if (!Info) return;
    PyObj result = CallMethod("free_find_data", "(n)", (Py_ssize_t)Info);
    if (result) {
    }
}

intptr_t PluginModule::SetDirectoryW(const SetDirectoryInfo* Info) {
    LOG_TRACE("PluginModule::SetDirectoryW");
    if (!Info) return 0;
    PyObj result = CallMethod("set_directory", "(n)", (Py_ssize_t)Info);
    if (result) {
        int ret = PyObject_IsTrue(result) ? 1 : 0;
        return ret;
    }
    return 0;
}

intptr_t PluginModule::DeleteFilesW(const DeleteFilesInfo* Info) {
    LOG_TRACE("PluginModule::DeleteFilesW");
    if (!Info) return 0;
    PyObj result = CallMethod("delete_files", "(n)", (Py_ssize_t)Info);
    if (result) {
        int ret = PyObject_IsTrue(result) ? 1 : 0;
        return ret;
    }
    return 0;
}

intptr_t PluginModule::MakeDirectoryW(MakeDirectoryInfo* Info) {
    LOG_TRACE("PluginModule::MakeDirectoryW");
    if (!Info) return 0;
    PyObj result = CallMethod("make_directory", "(n)", (Py_ssize_t)Info);
    if (result) {
        int ret = PyObject_IsTrue(result) ? 1 : 0;
        return ret;
    }
    return 0;
}

intptr_t PluginModule::GetFilesW(GetFilesInfo* Info) {
    LOG_TRACE("PluginModule::GetFilesW");
    if (!Info) return 0;
    PyObj result = CallMethod("get_files", "(n)", (Py_ssize_t)Info);
    if (result) {
        int ret = PyObject_IsTrue(result) ? 1 : 0;
        return ret;
    }
    return 0;
}

intptr_t PluginModule::PutFilesW(const PutFilesInfo* Info) {
    LOG_TRACE("PluginModule::PutFilesW");
    if (!Info) return 0;
    PyObj result = CallMethod("put_files", "(n)", (Py_ssize_t)Info);
    if (result) {
        int ret = PyObject_IsTrue(result) ? 1 : 0;
        return ret;
    }
    return 0;
}
