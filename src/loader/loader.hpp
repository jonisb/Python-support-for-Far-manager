#pragma once

#include <windows.h>
#include <string>
#include <memory>
#include <type_traits>
#include "../../include/plugin.hpp"

// Module deleter for unique_ptr
struct ModuleDeleter {
    void operator()(HMODULE module) const {
        if (module) {
            FreeLibrary(module);
        }
    }
};

using module_ptr = std::unique_ptr<std::remove_pointer<HMODULE>::type, ModuleDeleter>;

// Adapter class that loads PythonFar.dll and calls adapter functions
class Adapter {
public:
    bool ModuleInit();
    void ModuleFree();
    
    // Check if the adapter is initialized and ready to use
    bool IsInitialized() const noexcept { return m_Adapter && m_CreateInstance; }

    BOOL Initialize(GlobalInfo* Info) const noexcept;
    BOOL IsPlugin(const wchar_t* FileName) const noexcept;
    HANDLE CreateInstance(const wchar_t* FileName) const noexcept;
    FARPROC GetFunctionAddress(HANDLE Instance, const wchar_t* FunctionName) const noexcept;
    BOOL GetError(ErrorInfo* Info) const noexcept;
    BOOL DestroyInstance(HANDLE Instance) const noexcept;
    void Free(const ExitInfo* Info) const noexcept;

private:
    void Cleanup();

    module_ptr m_Adapter;

    BOOL (WINAPI *m_Initialize)(GlobalInfo*){};
    BOOL (WINAPI *m_IsPlugin)(const wchar_t*){};
    HANDLE (WINAPI *m_CreateInstance)(const wchar_t*){};
    FARPROC (WINAPI *m_GetFunctionAddress)(HANDLE, const wchar_t*){};
    BOOL (WINAPI *m_GetError)(ErrorInfo*){};
    BOOL (WINAPI *m_DestroyInstance)(HANDLE){};
    void (WINAPI *m_Free)(const ExitInfo*){};

    std::wstring m_Summary;
    std::wstring m_Description;
    bool m_Activated{};
};

// Helper function to get last error message
std::wstring GetLastErrorMessage(DWORD errorCode);
