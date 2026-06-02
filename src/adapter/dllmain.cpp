#include <windows.h>
#include "adapter_log.hpp"

BOOL APIENTRY DllMain(HMODULE Module, DWORD Reason, LPVOID Reserved) {
    switch (Reason) {
    case DLL_PROCESS_ATTACH:
        // Force logger initialization immediately
        PythonFar::GetAdapterLogger();
        LOG_TRACE("PythonFar Adapter: DLL_PROCESS_ATTACH");
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        LOG_TRACE("PythonFar Adapter: DLL_PROCESS_DETACH");
        break;
    }
    return TRUE;
}
