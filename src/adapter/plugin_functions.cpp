#include "../common_log.hpp"
#include "adapter.hpp"
#include <fstream>
#include <ctime>

static void LogPluginFunc(const char* msg) {
    // Light-weight logging for debug tracing
    try {
        std::string tempPath = PythonFar::SafeGetEnv("TEMP", PythonFar::DEFAULT_TEMP_DIR);
        std::ofstream log((tempPath + "\\pythonfar_loader.log").c_str(), std::ios::app);
        if (log.is_open()) {
            time_t now = time(nullptr);
            char timestamp[26];
            ctime_s(timestamp, sizeof(timestamp), &now);
            timestamp[24] = '\0';
            log << "[" << timestamp << "] [PluginFunctions] " << msg << std::endl;
        }
    } catch (...) {
        // swallow
    }
}

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

// Global functions that Far Manager will call for each plugin instance
// These are exported from the adapter DLL and returned by GetFunctionAddress

extern "C" {

void WINAPI GetGlobalInfoW(GlobalInfo* Info) {
    LogPluginFunc("GetGlobalInfoW called");
    return try_call(
        [&] {
            if (Info && Info->Instance) {
                LogPluginFunc("GetGlobalInfoW: calling PluginModule method");
                static_cast<PluginModule*>(Info->Instance)->GetGlobalInfoW(Info);
            } else {
                LogPluginFunc("GetGlobalInfoW: Info or Instance is null");
            }
        },
        [] {
            LogPluginFunc("GetGlobalInfoW: exception");
        });
}

void WINAPI GetPluginInfoW(PluginInfo* Info) {
    LogPluginFunc("GetPluginInfoW called");
    return try_call(
        [&] {
            if (Info && Info->Instance) {
                LogPluginFunc("GetPluginInfoW: calling PluginModule method");
                static_cast<PluginModule*>(Info->Instance)->GetPluginInfoW(Info);
            } else {
                LogPluginFunc("GetPluginInfoW: Info or Instance is null");
            }
        },
        [] {
            LogPluginFunc("GetPluginInfoW: exception");
        });
}

extern "C" void UpdateBridgeStartupInfo(const PluginStartupInfo* Info);

void WINAPI SetStartupInfoW(const PluginStartupInfo* Info) {
    LogPluginFunc("SetStartupInfoW called");
    LogPluginFunc(("SetStartupInfoW: Info ptr=" + std::to_string(reinterpret_cast<uintptr_t>(Info))).c_str());
    if (Info) {
        LogPluginFunc(("SetStartupInfoW: Info->PanelControl=" + std::to_string(reinterpret_cast<uintptr_t>(Info->PanelControl))).c_str());
        LogPluginFunc(("SetStartupInfoW: Info->FSF=" + std::to_string(reinterpret_cast<uintptr_t>(Info->FSF))).c_str());
        LogPluginFunc(("SetStartupInfoW: Info->StructSize=" + std::to_string(Info->StructSize)).c_str());
        UpdateBridgeStartupInfo(Info);
    }
    return try_call(
        [&] {
            if (Info && Info->Instance) {
                static_cast<PluginModule*>(Info->Instance)->SetStartupInfoW(Info);
            }
        },
        [] {});
}

HANDLE WINAPI OpenW(const OpenInfo* Info) {
    LogPluginFunc("OpenW called");
    return try_call(
        [&]() -> HANDLE {
            LogPluginFunc(("OpenW: Info=" + std::to_string((uintptr_t)Info)).c_str());
            if (Info) {
                LogPluginFunc(("OpenW: Instance=" + std::to_string((uintptr_t)Info->Instance)).c_str());
                if (Info->Instance) {
                    LogPluginFunc("OpenW: calling PluginModule method");
                    return static_cast<PluginModule*>(Info->Instance)->OpenW(Info);
                } else {
                    LogPluginFunc("OpenW: Instance is null");
                }
            } else {
                LogPluginFunc("OpenW: Info is null");
            }
            return INVALID_HANDLE_VALUE;
        },
        []() -> HANDLE {
            LogPluginFunc("OpenW: exception");
            return INVALID_HANDLE_VALUE;
        });
}

void WINAPI ClosePanelW(const ClosePanelInfo* Info) {
    return try_call(
        [&] {
            if (Info && Info->Instance) {
                static_cast<PluginModule*>(Info->Instance)->ClosePanelW(Info);
            }
        },
        [] {});
}

intptr_t WINAPI ConfigureW(const ConfigureInfo* Info) {
    return try_call(
        [&] {
            if (Info && Info->Instance) {
                return static_cast<PluginModule*>(Info->Instance)->ConfigureW(Info);
            }
            return intptr_t{0};
        },
        [] {
            return intptr_t{0};
        });
}

void WINAPI ExitFARW(const ExitInfo* Info) {
    return try_call(
        [&] {
            if (Info && Info->Instance) {
                static_cast<PluginModule*>(Info->Instance)->ExitFARW(Info);
            }
        },
        [] {});
}

intptr_t WINAPI ProcessDialogEventW(const ProcessDialogEventInfo* Info) {
    return try_call(
        [&]() -> intptr_t {
            if (Info && Info->Instance) {
                return static_cast<PluginModule*>(Info->Instance)->ProcessDialogEventW(Info);
            }
            return 0;
        },
        []() -> intptr_t {
            return 0;
        });
}

intptr_t WINAPI ProcessPanelEventW(const ProcessPanelEventInfo* Info) {
    return try_call(
        [&]() -> intptr_t {
            if (Info && Info->Instance) {
                return static_cast<PluginModule*>(Info->Instance)->ProcessPanelEventW(Info);
            }
            return 0;
        },
        []() -> intptr_t {
            return 0;
        });
}

intptr_t WINAPI ProcessPanelInputW(const ProcessPanelInputInfo* Info) {
    return try_call(
        [&]() -> intptr_t {
            if (Info && Info->Instance) {
                return static_cast<PluginModule*>(Info->Instance)->ProcessPanelInputW(Info);
            }
            return 0;
        },
        []() -> intptr_t {
            return 0;
        });
}

intptr_t WINAPI ProcessHostFileW(const ProcessHostFileInfo* Info) {
    return try_call(
        [&]() -> intptr_t {
            if (Info && Info->Instance) {
                return static_cast<PluginModule*>(Info->Instance)->ProcessHostFileW(Info);
            }
            return 0;
        },
        []() -> intptr_t {
            return 0;
        });
}

intptr_t WINAPI CompareW(const CompareInfo* Info) {
    return try_call(
        [&]() -> intptr_t {
            if (Info && Info->Instance) {
                return static_cast<PluginModule*>(Info->Instance)->CompareW(Info);
            }
            return -2; // Default sorting
        },
        []() -> intptr_t {
            return -2;
        });
}

intptr_t WINAPI SetFindListW(const SetFindListInfo* Info) {
    return try_call(
        [&]() -> intptr_t {
            if (Info && Info->Instance) {
                return static_cast<PluginModule*>(Info->Instance)->SetFindListW(Info);
            }
            return 0;
        },
        []() -> intptr_t {
            return 0;
        });
}

HANDLE WINAPI AnalyseW(const AnalyseInfo* Info) {
    return try_call(
        [&]() -> HANDLE {
            if (Info && Info->Instance) {
                return static_cast<PluginModule*>(Info->Instance)->AnalyseW(Info);
            }
            return nullptr;
        },
        []() -> HANDLE {
            return nullptr;
        });
}

void WINAPI CloseAnalyseW(const CloseAnalyseInfo* Info) {
    try_call(
        [&]() {
            if (Info && Info->Instance) {
                static_cast<PluginModule*>(Info->Instance)->CloseAnalyseW(Info);
            }
        },
        []() {});
}

intptr_t WINAPI ProcessEditorEventW(const ProcessEditorEventInfo* Info) {
    return try_call(
        [&]() -> intptr_t {
            if (Info && Info->Instance) {
                return static_cast<PluginModule*>(Info->Instance)->ProcessEditorEventW(Info);
            }
            return 0;
        },
        []() -> intptr_t {
            return 0;
        });
}

intptr_t WINAPI ProcessEditorInputW(const ProcessEditorInputInfo* Info) {
    return try_call(
        [&]() -> intptr_t {
            if (Info && Info->Instance) {
                return static_cast<PluginModule*>(Info->Instance)->ProcessEditorInputW(Info);
            }
            return 0;
        },
        []() -> intptr_t {
            return 0;
        });
}

intptr_t WINAPI ProcessViewerEventW(const ProcessViewerEventInfo* Info) {
    return try_call(
        [&]() -> intptr_t {
            if (Info && Info->Instance) {
                return static_cast<PluginModule*>(Info->Instance)->ProcessViewerEventW(Info);
            }
            return 0;
        },
        []() -> intptr_t {
            return 0;
        });
}

intptr_t WINAPI ProcessConsoleInputW(ProcessConsoleInputInfo* Info) {
    return try_call(
        [&]() -> intptr_t {
            if (Info && Info->Instance) {
                return static_cast<PluginModule*>(Info->Instance)->ProcessConsoleInputW(Info);
            }
            return 0;
        },
        []() -> intptr_t {
            return 0;
        });
}

// ---- VFS exports ----

void WINAPI GetOpenPanelInfoW(OpenPanelInfo* Info) {
    return try_call(
        [&] {
            if (Info && Info->hPanel) {
                PluginModule* module = static_cast<PluginModule*>(Info->Instance);
                if (module) module->GetOpenPanelInfoW(Info);
            }
        },
        [] {});
}

intptr_t WINAPI GetFindDataW(GetFindDataInfo* Info) {
    return try_call(
        [&]() -> intptr_t {
            if (Info && Info->hPanel) {
                return static_cast<PluginModule*>(Info->Instance)->GetFindDataW(Info);
            }
            return 0;
        },
        []() -> intptr_t {
            return 0;
        });
}

void WINAPI FreeFindDataW(const FreeFindDataInfo* Info) {
    return try_call(
        [&] {
            if (Info && Info->hPanel) {
                PluginModule* module = static_cast<PluginModule*>(Info->Instance);
                if (module) module->FreeFindDataW(const_cast<FreeFindDataInfo*>(Info));
            }
        },
        [] {});
}

intptr_t WINAPI SetDirectoryW(const SetDirectoryInfo* Info) {
    return try_call(
        [&]() -> intptr_t {
            if (Info && Info->hPanel) {
                return static_cast<PluginModule*>(Info->Instance)->SetDirectoryW(const_cast<SetDirectoryInfo*>(Info));
            }
            return 0;
        },
        []() -> intptr_t {
            return 0;
        });
}

intptr_t WINAPI DeleteFilesW(const DeleteFilesInfo* Info) {
    return try_call(
        [&]() -> intptr_t {
            if (Info && Info->hPanel) {
                return static_cast<PluginModule*>(Info->Instance)->DeleteFilesW(Info);
            }
            return 0;
        },
        []() -> intptr_t {
            return 0;
        });
}

intptr_t WINAPI MakeDirectoryW(MakeDirectoryInfo* Info) {
    return try_call(
        [&]() -> intptr_t {
            if (Info && Info->hPanel) {
                return static_cast<PluginModule*>(Info->Instance)->MakeDirectoryW(Info);
            }
            return 0;
        },
        []() -> intptr_t {
            return 0;
        });
}

intptr_t WINAPI GetFilesW(GetFilesInfo* Info) {
    return try_call(
        [&]() -> intptr_t {
            if (Info && Info->hPanel) {
                return static_cast<PluginModule*>(Info->Instance)->GetFilesW(Info);
            }
            return 0;
        },
        []() -> intptr_t {
            return 0;
        });
}

intptr_t WINAPI PutFilesW(const PutFilesInfo* Info) {
    return try_call(
        [&]() -> intptr_t {
            if (Info && Info->hPanel) {
                return static_cast<PluginModule*>(Info->Instance)->PutFilesW(const_cast<PutFilesInfo*>(Info));
            }
            return 0;
        },
        []() -> intptr_t {
            return 0;
        });
}

} // extern "C"
