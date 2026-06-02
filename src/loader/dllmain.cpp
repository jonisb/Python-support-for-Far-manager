#include <windows.h>
#include "../common_log.hpp"

BOOL APIENTRY DllMain(HMODULE Module, DWORD Reason, LPVOID Reserved) {
    switch (Reason) {
    case DLL_PROCESS_ATTACH:
        // Force logger initialization immediately
        PythonFar::GetLoaderLogger();
        LOG_TRACE("PythonFar Loader: DLL_PROCESS_ATTACH");
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        LOG_TRACE("PythonFar Loader: DLL_PROCESS_DETACH");
        break;
    }
    return TRUE;
}
