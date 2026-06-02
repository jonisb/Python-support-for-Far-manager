#include "../common_log.hpp"
#include "adapter.hpp"
#include <memory>

// Error handling wrapper
template<typename F, typename E>
auto try_call(F&& function, E&& error_handler) noexcept {
    try {
        return function();
    }
    catch (...) {
        return error_handler();
    }
}

PythonFarAdapter* g_Adapter = nullptr;

extern "C" {

__declspec(dllexport) BOOL WINAPI adapter_Initialize(GlobalInfo* Info) noexcept {
    return try_call(
        [&] {
            if (g_Adapter) delete g_Adapter;
            g_Adapter = new PythonFarAdapter(Info);
            return TRUE;
        },
        [] {
            return FALSE;
        });
}

__declspec(dllexport) BOOL WINAPI adapter_IsPlugin(const wchar_t* FileName) noexcept {
    return try_call(
        [&] {
            if (!g_Adapter) return FALSE;
            return g_Adapter->IsModule(FileName) ? TRUE : FALSE;
        },
        [] {
            return FALSE;
        });
}

__declspec(dllexport) HANDLE WINAPI adapter_CreateInstance(const wchar_t* FileName) noexcept {
    return try_call(
        [&] {
            if (!g_Adapter) return static_cast<HANDLE>(nullptr);
            auto module = g_Adapter->CreatePluginModule(FileName);
            return static_cast<HANDLE>(module.release());
        },
        [] {
            return static_cast<HANDLE>(nullptr);
        });
}

__declspec(dllexport) FARPROC WINAPI adapter_GetFunctionAddress(HANDLE Instance, const wchar_t* FunctionName) noexcept {
    return try_call(
        [&] {
            if (!g_Adapter) return static_cast<FARPROC>(nullptr);
            if (!FunctionName) return static_cast<FARPROC>(nullptr);
            std::wstring funcName(FunctionName);
            int size_needed = WideCharToMultiByte(CP_UTF8, 0, funcName.c_str(), (int)funcName.size(), nullptr, 0, nullptr, nullptr);
            std::string funcNarrow(size_needed, 0);
            WideCharToMultiByte(CP_UTF8, 0, funcName.c_str(), (int)funcName.size(), &funcNarrow[0], size_needed, nullptr, nullptr);
            LOG_TRACE("adapter_GetFunctionAddress request: " + funcNarrow);
            auto fp = g_Adapter->GetFunction(Instance, FunctionName);
            LOG_TRACE("adapter_GetFunctionAddress result for '" + funcNarrow + "' -> " + (fp ? std::string("non-null") : std::string("NULL")));
            return fp;
        },
        [] {
            return static_cast<FARPROC>(nullptr);
        });
}

__declspec(dllexport) BOOL WINAPI adapter_GetError(ErrorInfo* Info) noexcept {
    return try_call(
        [&] {
            if (!g_Adapter) return FALSE;
            return g_Adapter->GetError(Info) ? TRUE : FALSE;
        },
        [] {
            return FALSE;
        });
}

__declspec(dllexport) BOOL WINAPI adapter_DestroyInstance(HANDLE Instance) noexcept {
    return try_call(
        [&] {
            std::unique_ptr<PluginModule>(static_cast<PluginModule*>(Instance));
            return TRUE;
        },
        [] {
            return FALSE;
        });
}

__declspec(dllexport) void WINAPI adapter_Free(const ExitInfo* Info) noexcept {
    return try_call(
        [&] {
            delete g_Adapter;
            g_Adapter = nullptr;
        },
        [] {
        });
}

} // extern "C"
