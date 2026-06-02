#include <windows.h>
#include <fstream>
#include <string>
#include "../common_log.hpp"

BOOL APIENTRY DllMain(HMODULE Module, DWORD Reason, LPVOID Reserved) {
    switch (Reason) {
    case DLL_PROCESS_ATTACH:
        {
            std::string tempPath = PythonFar::SafeGetEnv("TEMP", PythonFar::DEFAULT_TEMP_DIR);
            std::ofstream f((tempPath + "\\PythonFar_loader.log").c_str(), std::ios::app);
            if (f) {
                f << "PythonFar Loader DLL_PROCESS_ATTACH" << std::endl;
            }
        }
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        {
            std::string tempPath = PythonFar::SafeGetEnv("TEMP", PythonFar::DEFAULT_TEMP_DIR);
            std::ofstream f((tempPath + "\\PythonFar_loader.log").c_str(), std::ios::app);
            if (f) {
                f << "PythonFar Loader DLL_PROCESS_DETACH" << std::endl;
            }
        }
        break;
    }
    return TRUE;
}
