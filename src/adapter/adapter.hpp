#pragma once

#include <windows.h>
#include "../../include/plugin.hpp"
#include "../GlobalInfo.hpp"
#include <Python.h>
#include <string>
#include <vector>
#include <map>
#include <memory>

// Forward declarations
class PluginModule;

class ScopedGIL {
public:
    ScopedGIL() {
        state = PyGILState_Ensure();
    }
    ~ScopedGIL() {
        PyGILState_Release(state);
    }
private:
    PyGILState_STATE state;
};

class PyObj {
public:
    PyObj(PyObject* obj = nullptr) : m_obj(obj) {}
    ~PyObj() {
        Py_XDECREF(m_obj);
    }
    
    // Disable copy
    PyObj(const PyObj&) = delete;
    PyObj& operator=(const PyObj&) = delete;
    
    // Enable move
    PyObj(PyObj&& other) noexcept : m_obj(other.m_obj) {
        other.m_obj = nullptr;
    }
    PyObj& operator=(PyObj&& other) noexcept {
        if (this != std::addressof(other)) {
            Py_XDECREF(m_obj);
            m_obj = other.m_obj;
            other.m_obj = nullptr;
        }
        return *this;
    }
    
    PyObject* get() const { return m_obj; }
    operator PyObject*() const { return m_obj; }
    PyObject* operator->() const { return m_obj; }
    explicit operator bool() const { return m_obj != nullptr; }
    
    // Release ownership
    PyObject* release() {
        PyObject* temp = m_obj;
        m_obj = nullptr;
        return temp;
    }
    
    // Re-assign without increasing refcount (takes ownership)
    void reset(PyObject* obj = nullptr) {
        Py_XDECREF(m_obj);
        m_obj = obj;
    }

private:
    PyObject* m_obj;
};

// PythonFar Adapter - manages Python interpreter and plugin instances
class PythonFarAdapter {
public:
    explicit PythonFarAdapter(GlobalInfo* globalInfo);
    ~PythonFarAdapter();

    // Check if a file is a Python plugin
    bool IsModule(const wchar_t* filename);

    // Create a plugin instance from a .py file
    std::unique_ptr<PluginModule> CreatePluginModule(const wchar_t* filename);

    // Run a plain .py script (not a plugin) via the embedded Python engine
    bool RunScript(const wchar_t* path);

    // Get a function pointer for a plugin instance
    FARPROC GetFunction(HANDLE instance, const wchar_t* functionName);
    void LogExportStatus();

    // Get error information
    bool GetError(ErrorInfo* info);

    // Set the pending error that Far will retrieve via GetError(). Used by
    // plugin instances (e.g. PluginModule) to report failures back to the host.
    void SetError(const std::wstring& summary, const std::wstring& description);

private:
    void InitializePython();
    void FinalizePython();

    std::wstring m_PluginDir;
    GlobalInfo m_GlobalInfo;
    PyThreadState* m_mainThreadState = nullptr;
    std::wstring m_ErrorSummary;
    std::wstring m_ErrorDescription;
    std::wstring m_LastErrorSummary;
    std::wstring m_LastErrorDescription;
    std::vector<DLL_DIRECTORY_COOKIE> m_DllDirectoryCookies;
};

// Plugin module instance - represents a loaded .py plugin
class PluginModule {
public:
    PluginModule(const std::wstring& filename, PyObject* module, PyObject* pluginClass);
    ~PluginModule();

    // Far Manager API functions (will be called via GetFunctionAddress)
    void GetGlobalInfoW(GlobalInfo* Info);
    void GetPluginInfoW(PluginInfo* Info);
    void SetStartupInfoW(const PluginStartupInfo* Info);
    HANDLE OpenW(const OpenInfo* Info);
    void ClosePanelW(const ClosePanelInfo* Info);
    intptr_t ConfigureW(const ConfigureInfo* Info);
    void ExitFARW(const ExitInfo* Info);
    intptr_t ProcessDialogEventW(const ProcessDialogEventInfo* Info);
    intptr_t ProcessPanelEventW(const ProcessPanelEventInfo* Info);
    intptr_t ProcessPanelInputW(const ProcessPanelInputInfo* Info);
    intptr_t ProcessHostFileW(const ProcessHostFileInfo* Info);
    intptr_t CompareW(const CompareInfo* Info);
    intptr_t SetFindListW(const SetFindListInfo* Info);
    HANDLE AnalyseW(const AnalyseInfo* Info);
    void CloseAnalyseW(const CloseAnalyseInfo* Info);
    intptr_t ProcessEditorEventW(const ProcessEditorEventInfo* Info);
    intptr_t ProcessEditorInputW(const ProcessEditorInputInfo* Info);
    intptr_t ProcessViewerEventW(const ProcessViewerEventInfo* Info);
    intptr_t ProcessConsoleInputW(const ProcessConsoleInputInfo* Info);

    // Helper to check if a method exists on the python instance
    bool HasMethod(const char* methodName);

    // Virtual File System (VFS) exports
    void GetOpenPanelInfoW(OpenPanelInfo* Info);
    intptr_t GetFindDataW(GetFindDataInfo* Info);
    void FreeFindDataW(const FreeFindDataInfo* Info);
    intptr_t SetDirectoryW(const SetDirectoryInfo* Info);
    intptr_t DeleteFilesW(const DeleteFilesInfo* Info);
    intptr_t MakeDirectoryW(MakeDirectoryInfo* Info);
    intptr_t GetFilesW(GetFilesInfo* Info);
    intptr_t PutFilesW(const PutFilesInfo* Info);

    // Helper to call Python methods
    PyObject* CallMethod(const char* methodName, const char* format = nullptr, ...);

private:
    // Report a fatal PluginInfo failure: log it, record it for Far's GetError,
    // and show a message box to the user. `detail` may contain a Python
    // traceback (multi-line).
    void ReportPluginInfoFailure(const std::wstring& summary, const std::wstring& detail);

    std::wstring m_Filename;
    PyObj m_Module;
    PyObj m_PluginClass;
    PyObj m_PluginInstance;

    // Plugin metadata
    std::wstring m_Title;
    std::wstring m_Description;
    std::wstring m_Author;
    std::wstring m_Version;
    // Min Far version defaults to GlobalInfo's configured minimum (3.0.0.0),
    // overridden by Python plugin's min_far_version tuple if specified
    DWORD m_MinFarVersionMajor = PythonFar::MIN_FAR_VERSION_MAJOR;
    DWORD m_MinFarVersionMinor = PythonFar::MIN_FAR_VERSION_MINOR;
    DWORD m_MinFarVersionBuild = PythonFar::MIN_FAR_VERSION_BUILD;
    DWORD m_MinFarVersionRevision = PythonFar::MIN_FAR_VERSION_REVISION;
    UUID m_Guid;

    // Buffers for string data
    std::vector<wchar_t> titleBuffer;
    std::vector<wchar_t> descBuffer;
    std::vector<wchar_t> authorBuffer;
    std::vector<wchar_t> versionBuffer;

    // Arrays of string pointers for PluginInfo
    std::vector<const wchar_t*> menuStrings;
    std::vector<const wchar_t*> configStrings;
    std::vector<GUID> menuGuids;
};

// Global adapter instance
extern PythonFarAdapter* g_Adapter;
