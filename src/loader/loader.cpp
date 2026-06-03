#include "../common_log.hpp"
#include "../GlobalInfo.hpp"
#include "loader.hpp"
#include <sstream>
#include <vector>

extern "C" IMAGE_DOS_HEADER __ImageBase;

std::wstring GetLastErrorMessage(DWORD errorCode) {
    wchar_t* buffer = nullptr;
    DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr
    );

    std::wstring message;
    if (size > 0 && buffer) {
        message = buffer;
        LocalFree(buffer);
        // Remove trailing newline
        while (!message.empty() && (message.back() == L'\n' || message.back() == L'\r')) {
            message.pop_back();
        }
    } else {
        message = L"Error code: " + std::to_wstring(errorCode);
    }
    return message;
}

bool Adapter::ModuleInit() {
    LOG_TRACE("ModuleInit called");
    m_Summary = L"PythonFar Adapter Loader";
    m_Description = L"Unknown error";
    m_Activated = true;

    // Debug logging
    OutputDebugStringW(L"PythonFar Loader: ModuleInit called\n");

    // Get the path of the loader DLL
    wchar_t adapterPath[MAX_PATH];
    if (!GetModuleFileNameW(reinterpret_cast<HINSTANCE>(&__ImageBase), adapterPath, MAX_PATH)) {
        DWORD err = GetLastError();
        LOG_ERROR("GetModuleFileNameW failed with error: " << err);
        m_Description = GetLastErrorMessage(err);
        return false;
    }

    // Build adapter path: from Adapters/PythonFar.dll -> Adapters/PythonFar.adapter.dll
    std::wstring pathStr(adapterPath);
    LOG_TRACE("Loader path: " << WideToUTF8(pathStr.c_str()));
    OutputDebugStringW((L"PythonFar Loader: Loader path: " + pathStr + L"\n").c_str());
    
    // Find the Adapters directory and replace PythonFar.dll with PythonFar.adapter.dll
    size_t lastSlash = pathStr.find_last_of(L'\\');
    std::wstring adapterDir;
    if (lastSlash != std::wstring::npos) {
        adapterDir = pathStr.substr(0, lastSlash);
        pathStr = adapterDir + L"\\" + PythonFar::ADAPTER_DLL_NAME;
    } else {
        LOG_ERROR("Could not find directory separator in loader path");
        m_Description = L"Invalid loader path";
        return false;
    }
    
    LOG_TRACE("Adapter path: " << WideToUTF8(pathStr.c_str()));
    OutputDebugStringW((L"PythonFar Loader: Adapter path: " + pathStr + L"\n").c_str());

    // Check if adapter DLL exists
    DWORD fileAttrs = GetFileAttributesW(pathStr.c_str());
    if (fileAttrs == INVALID_FILE_ATTRIBUTES) {
        DWORD err = GetLastError();
        LOG_ERROR("Adapter DLL not found, error: " << err << ", path: " << WideToUTF8(pathStr.c_str()));
        OutputDebugStringW((L"PythonFar Loader: Adapter DLL not found at: " + pathStr + L"\n").c_str());
        m_Description = L"Adapter DLL not found: " + pathStr;
        return false;
    }

    LOG_TRACE("Adapter DLL file exists, attempting LoadLibraryExW with altered search path");
    OutputDebugStringW((L"PythonFar Loader: Adapter DLL found, attempting to load from: " + pathStr + L"\n").c_str());

    std::vector<DLL_DIRECTORY_COOKIE> dllDirCookies;
    if (!adapterDir.empty()) {
        std::wstring pythonRuntime = adapterDir + L"\\" + PythonFar::PYTHON_RUNTIME_DIR;
        std::wstring pythonDLLs = pythonRuntime + L"\\" + PythonFar::PYTHON_DLLS_SUBDIR;

        for (const auto& dir : { adapterDir, pythonRuntime, pythonDLLs }) {
            DWORD attrs = GetFileAttributesW(dir.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                DLL_DIRECTORY_COOKIE cookie = AddDllDirectory(dir.c_str());
                if (cookie) {
                    dllDirCookies.push_back(cookie);
                    LOG_TRACE("Added temporary DLL directory: " << WideToUTF8(dir.c_str()));
                } else {
                    LOG_TRACE("AddDllDirectory failed for " << WideToUTF8(dir.c_str()) << ": " << GetLastError());
                }
            }
        }
    }

    // Load the adapter with an explicit, scoped DLL search path. Do not mutate
    // process-wide PATH or SetDllDirectoryW; those affect all Far plugins.
    m_Adapter.reset(LoadLibraryExW(
        pathStr.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
        LOAD_LIBRARY_SEARCH_DEFAULT_DIRS |
        LOAD_LIBRARY_SEARCH_USER_DIRS));
    for (DLL_DIRECTORY_COOKIE cookie : dllDirCookies) {
        RemoveDllDirectory(cookie);
    }
    if (!m_Adapter) {
        DWORD error = GetLastError();
        LOG_ERROR("LoadLibraryExW failed with error: " << error << ", path: " << WideToUTF8(pathStr.c_str()));
        OutputDebugStringW((L"PythonFar Loader: LoadLibraryExW failed with error: " + std::to_wstring(error) + L"\n").c_str());
        OutputDebugStringW((L"PythonFar Loader: Failed path: " + pathStr + L"\n").c_str());
        m_Description = GetLastErrorMessage(error) + 
                        L"\n\nHint: Make sure " + std::wstring(PythonFar::ADAPTER_DLL_NAME) + L" is in the same directory";
        return false;
    }

    LOG_TRACE("Adapter DLL loaded successfully, getting function addresses");
    OutputDebugStringW(L"PythonFar Loader: Adapter loaded successfully\n");

#define INIT_IMPORT(name) \
    m_##name = reinterpret_cast<decltype(m_##name)>(GetProcAddress(m_Adapter.get(), "adapter_" #name)); \
    if (!m_##name) { \
        LOG_ERROR("Failed to get address for adapter_" << #name); \
        Cleanup(); \
        m_Description = L"Failed to load adapter_" L#name L": " + GetLastErrorMessage(GetLastError()); \
        return false; \
    } \
    LOG_TRACE("Got address for adapter_" << #name);

    INIT_IMPORT(Initialize)
    INIT_IMPORT(IsPlugin)
    INIT_IMPORT(CreateInstance)
    INIT_IMPORT(GetFunctionAddress)
    INIT_IMPORT(GetError)
    INIT_IMPORT(DestroyInstance)
    INIT_IMPORT(Free)

#undef INIT_IMPORT

    // Explicitly check for PanelControl export on the adapter DLL
    FARPROC panelBridge = GetProcAddress(m_Adapter.get(), "PythonFar_PanelControl");
    {
        std::ostringstream oss;
        oss << "ModuleInit: GetProcAddress('PythonFar_PanelControl') -> " 
            << (panelBridge ? "non-null" : "NULL");
        LOG_TRACE(oss.str());
    }

    LOG_TRACE("All function addresses obtained, ModuleInit succeeded");
    return true;
}

void Adapter::ModuleFree() {
    Cleanup();
    m_Activated = false;
}

BOOL Adapter::Initialize(GlobalInfo* Info) const noexcept {
    BOOL result = m_Initialize(Info);
    LOG_TRACE("Adapter::Initialize result=" << (result ? "TRUE" : "FALSE"));
    if (Info) {
        LOG_TRACE("Adapter::Initialize Info->StructSize=" << Info->StructSize);
    } else {
        LOG_TRACE("Adapter::Initialize Info is null");
    }
    return result;
}

BOOL Adapter::IsPlugin(const wchar_t* FileName) const noexcept {
    return m_IsPlugin(FileName);
}

HANDLE Adapter::CreateInstance(const wchar_t* FileName) const noexcept {
    return m_CreateInstance(FileName);
}

FARPROC Adapter::GetFunctionAddress(HANDLE Instance, const wchar_t* FunctionName) const noexcept {
    return m_GetFunctionAddress(Instance, FunctionName);
}

BOOL Adapter::GetError(ErrorInfo* Info) const noexcept {
    if (!m_Activated || !Info)
        return FALSE;

    if (m_GetError)
        return m_GetError(Info);

    Info->StructSize = sizeof(*Info);
    Info->Summary = m_Summary.c_str();
    Info->Description = m_Description.c_str();
    return TRUE;
}

BOOL Adapter::DestroyInstance(HANDLE Instance) const noexcept {
    return m_DestroyInstance(Instance);
}

void Adapter::Free(const ExitInfo* Info) const noexcept {
    return m_Free(Info);
}

void Adapter::Cleanup() {
    m_Initialize = nullptr;
    m_IsPlugin = nullptr;
    m_CreateInstance = nullptr;
    m_GetFunctionAddress = nullptr;
    m_GetError = nullptr;
    m_DestroyInstance = nullptr;
    m_Free = nullptr;
}
