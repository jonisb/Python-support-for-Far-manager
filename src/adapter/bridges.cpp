#include "adapter_log.hpp"
#include "adapter.hpp"
#include <sstream>
#include <string>
#include <unordered_set>
#include <thread>
#include <chrono>

static PluginStartupInfo g_BridgeStartupInfo = {};
static FarStandardFunctions g_BridgeFSF = {};
static bool g_BridgeValid = false;

extern "C" {
    void UpdateBridgeStartupInfo(const PluginStartupInfo* Info) {
        if (Info) {
            g_BridgeStartupInfo = *Info;
            if (Info->FSF) {
                g_BridgeFSF = *Info->FSF;
                g_BridgeStartupInfo.FSF = &g_BridgeFSF;
            }
            g_BridgeValid = true;
        }
    }
}

static std::unordered_set<HANDLE> g_PendingCloseDialogs;

static void BridgeLog(const char* message) {
    LOG_TRACE(message);
}

extern "C" __declspec(dllexport) intptr_t WINAPI PythonFar_AdvControl(
    const GUID* PluginId, int command, intptr_t param1, void* param2) {
    if (!g_BridgeValid || !g_BridgeStartupInfo.AdvControl) return 0;
    return g_BridgeStartupInfo.AdvControl(PluginId, static_cast<ADVANCED_CONTROL_COMMANDS>(command), param1, param2);
}

extern "C" __declspec(dllexport) intptr_t WINAPI PythonFar_PanelControl(
    HANDLE hPanel, int command, intptr_t param1, void* param2) {
    if (!g_BridgeValid || !g_BridgeStartupInfo.PanelControl) return 0;
    return g_BridgeStartupInfo.PanelControl(hPanel, static_cast<FILE_CONTROL_COMMANDS>(command), param1, param2);
}

static intptr_t WINAPI DefaultDlgProcBridge(HANDLE hDlg, intptr_t Msg, intptr_t Param1, void* Param2) {
    if (g_BridgeValid && g_BridgeStartupInfo.DefDlgProc) {
        return g_BridgeStartupInfo.DefDlgProc(hDlg, Msg, Param1, Param2);
    }
    return 0;
}

extern "C" __declspec(dllexport) intptr_t WINAPI PythonFar_DialogInit(
    const GUID* PluginId, const GUID* Id, intptr_t X1, intptr_t Y1, intptr_t X2, intptr_t Y2,
    const wchar_t* HelpTopic, const struct FarDialogItem* Item, size_t ItemsNumber,
    intptr_t Reserved, unsigned __int64 Flags, FARWINDOWPROC DlgProc, void* Param) {

    FARWINDOWPROC effectiveProc = DlgProc;
    if (effectiveProc == reinterpret_cast<FARWINDOWPROC>(-1)
        || effectiveProc == reinterpret_cast<FARWINDOWPROC>(0)
        || effectiveProc == nullptr) {
        effectiveProc = DefaultDlgProcBridge;
    }

    if (!g_BridgeValid || !g_BridgeStartupInfo.DialogInit) {
        BridgeLog("DialogInit unavailable");
        return -1;
    }
    HANDLE hDlg = g_BridgeStartupInfo.DialogInit(
        PluginId, Id, X1, Y1, X2, Y2, HelpTopic, Item, ItemsNumber, Reserved, Flags, effectiveProc, Param);
    BridgeLog(hDlg == INVALID_HANDLE_VALUE ? "DialogInit returned INVALID_HANDLE_VALUE" : "DialogInit succeeded");
    return reinterpret_cast<intptr_t>(hDlg);
}

extern "C" __declspec(dllexport) intptr_t WINAPI PythonFar_DialogRun(HANDLE hDlg) {
    if (!g_BridgeValid || !g_BridgeStartupInfo.DialogRun) return -1;
    return g_BridgeStartupInfo.DialogRun(hDlg);
}

extern "C" __declspec(dllexport) intptr_t WINAPI PythonFar_DialogRunWithTimeout(
    const GUID* PluginId, HANDLE hDlg, unsigned long TimeoutMs) {

    if (!g_BridgeValid || !g_BridgeStartupInfo.DialogRun) return -1;

    GUID pluginGuid = *PluginId;

    std::thread([pluginGuid, hDlg, TimeoutMs]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(TimeoutMs));

        bool shouldClose = true;
        if (g_BridgeValid && g_BridgeStartupInfo.SendDlgMessage) {
            intptr_t isVisible = g_BridgeStartupInfo.SendDlgMessage(hDlg, DM_GETDIALOGINFO, 0, nullptr);
            if (isVisible == 0) shouldClose = false;
        }

        if (shouldClose) {
            HANDLE dialogToClose = hDlg;
            g_PendingCloseDialogs.insert(dialogToClose);
            if (g_BridgeValid && g_BridgeStartupInfo.AdvControl) {
                g_BridgeStartupInfo.AdvControl(&pluginGuid, ACTL_SYNCHRO, 0, nullptr);
            }
        }
    }).detach();

    intptr_t result = g_BridgeStartupInfo.DialogRun(hDlg);
    g_PendingCloseDialogs.erase(hDlg);
    return result;
}

extern "C" __declspec(dllexport) void WINAPI PythonFar_DialogFree(HANDLE hDlg) {
    if (g_BridgeValid && g_BridgeStartupInfo.DialogFree) {
        g_BridgeStartupInfo.DialogFree(hDlg);
    }
}

extern "C" __declspec(dllexport) intptr_t WINAPI PythonFar_SendDlgMessage(
    HANDLE hDlg, intptr_t Msg, intptr_t Param1, void* Param2) {
    if (!g_BridgeValid || !g_BridgeStartupInfo.SendDlgMessage) return -1;
    return g_BridgeStartupInfo.SendDlgMessage(hDlg, Msg, Param1, Param2);
}

extern "C" __declspec(dllexport) intptr_t WINAPI PythonFar_DefDlgProc(
    HANDLE hDlg, intptr_t Msg, intptr_t Param1, void* Param2) {
    if (!g_BridgeValid || !g_BridgeStartupInfo.DefDlgProc) return 0;
    return g_BridgeStartupInfo.DefDlgProc(hDlg, Msg, Param1, Param2);
}

extern "C" __declspec(dllexport) intptr_t WINAPI PythonFar_EditorControl(
    intptr_t EditorID, intptr_t Command, intptr_t Param1, void* Param2) {
    if (!g_BridgeValid || !g_BridgeStartupInfo.EditorControl) return 0;
    return g_BridgeStartupInfo.EditorControl(EditorID, static_cast<EDITOR_CONTROL_COMMANDS>(Command), Param1, Param2);
}

extern "C" __declspec(dllexport) intptr_t WINAPI PythonFar_ViewerControl(
    intptr_t ViewerID, intptr_t Command, intptr_t Param1, void* Param2) {
    if (!g_BridgeValid || !g_BridgeStartupInfo.ViewerControl) return 0;
    return g_BridgeStartupInfo.ViewerControl(ViewerID, static_cast<VIEWER_CONTROL_COMMANDS>(Command), Param1, Param2);
}

extern "C" __declspec(dllexport) wchar_t* WINAPI PythonFar_FSFTrim(wchar_t* Str) {
    if (!g_BridgeValid || !g_BridgeStartupInfo.FSF || !g_BridgeStartupInfo.FSF->Trim || !Str) return nullptr;
    return g_BridgeStartupInfo.FSF->Trim(Str);
}

extern "C" __declspec(dllexport) wchar_t* WINAPI PythonFar_FSFLTrim(wchar_t* Str) {
    if (!g_BridgeValid || !g_BridgeStartupInfo.FSF || !g_BridgeStartupInfo.FSF->LTrim || !Str) return nullptr;
    return g_BridgeStartupInfo.FSF->LTrim(Str);
}

extern "C" __declspec(dllexport) wchar_t* WINAPI PythonFar_FSFRTrim(wchar_t* Str) {
    if (!g_BridgeValid || !g_BridgeStartupInfo.FSF || !g_BridgeStartupInfo.FSF->RTrim || !Str) return nullptr;
    return g_BridgeStartupInfo.FSF->RTrim(Str);
}

extern "C" __declspec(dllexport) void WINAPI PythonFar_FSFUnquote(wchar_t* Str) {
    if (!g_BridgeValid || !g_BridgeStartupInfo.FSF || !g_BridgeStartupInfo.FSF->Unquote || !Str) return;
    g_BridgeStartupInfo.FSF->Unquote(Str);
}

extern "C" __declspec(dllexport) const wchar_t* WINAPI PythonFar_FSFPointToName(const wchar_t* Path) {
    if (!g_BridgeValid || !g_BridgeStartupInfo.FSF || !g_BridgeStartupInfo.FSF->PointToName || !Path) return nullptr;
    return g_BridgeStartupInfo.FSF->PointToName(Path);
}

extern "C" __declspec(dllexport) int WINAPI PythonFar_FSFAddEndSlash(wchar_t* Path) {
    if (!g_BridgeValid || !g_BridgeStartupInfo.FSF || !g_BridgeStartupInfo.FSF->AddEndSlash || !Path) return 0;
    return g_BridgeStartupInfo.FSF->AddEndSlash(Path) ? 1 : 0;
}

extern "C" __declspec(dllexport) intptr_t WINAPI PythonFar_SettingsControl(
    HANDLE hHandle, int command, intptr_t param1, void* param2) {
    if (!g_BridgeValid || !g_BridgeStartupInfo.SettingsControl) {
        BridgeLog("SettingsControl: bridge not valid or pointer null");
        return 0;
    }
    if (command == 0 && param2) {
        // SCTL_CREATE — log the GUID being passed
        struct FarSettingsCreate* fsc = reinterpret_cast<struct FarSettingsCreate*>(param2);
        std::ostringstream oss;
        oss << "SettingsControl SCTL_CREATE: StructSize=" << fsc->StructSize
            << " GUID={" << std::hex << fsc->Guid.Data1
            << "-" << fsc->Guid.Data2
            << "-" << fsc->Guid.Data3 << "}";
        BridgeLog(oss.str().c_str());
    }
    intptr_t result = g_BridgeStartupInfo.SettingsControl(
        hHandle, static_cast<FAR_SETTINGS_CONTROL_COMMANDS>(command), param1, param2);
    {
        std::ostringstream oss;
        oss << "SettingsControl: cmd=" << command << " result=" << result;
        if (command == 0 && param2) {
            struct FarSettingsCreate* fsc = reinterpret_cast<struct FarSettingsCreate*>(param2);
            oss << " out_handle=" << fsc->Handle;
        }
        BridgeLog(oss.str().c_str());
    }
    return result;
}

extern "C" __declspec(dllexport) void WINAPI PythonFar_ProcessSynchroEvent(const GUID* PluginId) {
    if (g_BridgeValid && g_BridgeStartupInfo.SendDlgMessage) {
        auto toClose = g_PendingCloseDialogs;
        g_PendingCloseDialogs.clear();
        for (const auto hDlg : toClose) {
            g_BridgeStartupInfo.SendDlgMessage(hDlg, DM_CLOSE, -1, nullptr);
        }
    }
}

extern "C" __declspec(dllexport) BOOL WINAPI PythonFar_FSFCopyToClipboard(
    int Type, const wchar_t* Data) {
    if (!g_BridgeValid || !g_BridgeStartupInfo.FSF || !g_BridgeStartupInfo.FSF->CopyToClipboard)
        return FALSE;
    return g_BridgeStartupInfo.FSF->CopyToClipboard(
        static_cast<FARCLIPBOARD_TYPE>(Type), Data);
}

extern "C" __declspec(dllexport) size_t WINAPI PythonFar_FSFPasteFromClipboard(
    int Type, wchar_t* Data, size_t Size) {
    if (!g_BridgeValid || !g_BridgeStartupInfo.FSF || !g_BridgeStartupInfo.FSF->PasteFromClipboard)
        return 0;
    return g_BridgeStartupInfo.FSF->PasteFromClipboard(
        static_cast<FARCLIPBOARD_TYPE>(Type), Data, Size);
}

extern "C" __declspec(dllexport) size_t WINAPI PythonFar_FSFMkTemp(
    wchar_t* Dest, size_t DestSize, const wchar_t* Prefix) {
    if (!g_BridgeValid || !g_BridgeStartupInfo.FSF || !g_BridgeStartupInfo.FSF->MkTemp)
        return 0;
    return g_BridgeStartupInfo.FSF->MkTemp(Dest, DestSize, Prefix);
}

extern "C" __declspec(dllexport) size_t WINAPI PythonFar_FSFFormatFileSize(
    unsigned long long Size, intptr_t Width, unsigned long long Flags,
    wchar_t* Dest, size_t DestSize) {
    if (!g_BridgeValid || !g_BridgeStartupInfo.FSF || !g_BridgeStartupInfo.FSF->FormatFileSize)
        return 0;
    return g_BridgeStartupInfo.FSF->FormatFileSize(Size, Width, Flags, Dest, DestSize);
}

extern "C" __declspec(dllexport) unsigned long long WINAPI PythonFar_FSFFarClock(void) {
    if (!g_BridgeValid || !g_BridgeStartupInfo.FSF || !g_BridgeStartupInfo.FSF->FarClock)
        return 0;
    return g_BridgeStartupInfo.FSF->FarClock();
}

extern "C" __declspec(dllexport) size_t WINAPI PythonFar_FSFConvertPath(
    int Mode, const wchar_t* Src, wchar_t* Dest, size_t DestSize) {
    if (!g_BridgeValid || !g_BridgeStartupInfo.FSF || !g_BridgeStartupInfo.FSF->ConvertPath)
        return 0;
    return g_BridgeStartupInfo.FSF->ConvertPath(
        static_cast<CONVERTPATHMODES>(Mode), Src, Dest, DestSize);
}

extern "C" __declspec(dllexport) size_t WINAPI PythonFar_FSFInputRecordToName(
    const INPUT_RECORD* Key, wchar_t* KeyText, size_t Size) {
    if (!g_BridgeValid || !g_BridgeStartupInfo.FSF || !g_BridgeStartupInfo.FSF->FarInputRecordToName || !Key)
        return 0;
    return g_BridgeStartupInfo.FSF->FarInputRecordToName(Key, KeyText, Size);
}

extern "C" __declspec(dllexport) BOOL WINAPI PythonFar_FSFNameToInputRecord(
    const wchar_t* Name, INPUT_RECORD* Key) {
    if (!g_BridgeValid || !g_BridgeStartupInfo.FSF || !g_BridgeStartupInfo.FSF->FarNameToInputRecord || !Key)
        return FALSE;
    return g_BridgeStartupInfo.FSF->FarNameToInputRecord(Name, Key);
}

extern "C" __declspec(dllexport) wchar_t* WINAPI PythonFar_FSFTruncStr(wchar_t* Str, intptr_t MaxLength) {
    if (!g_BridgeValid || !g_BridgeStartupInfo.FSF || !g_BridgeStartupInfo.FSF->TruncStr)
        return Str;
    return g_BridgeStartupInfo.FSF->TruncStr(Str, MaxLength);
}

extern "C" __declspec(dllexport) wchar_t* WINAPI PythonFar_FSFTruncPathStr(wchar_t* Str, intptr_t MaxLength) {
    if (!g_BridgeValid || !g_BridgeStartupInfo.FSF || !g_BridgeStartupInfo.FSF->TruncPathStr)
        return Str;
    return g_BridgeStartupInfo.FSF->TruncPathStr(Str, MaxLength);
}

extern "C" __declspec(dllexport) wchar_t* WINAPI PythonFar_FSFQuoteSpaceOnly(wchar_t* Str) {
    if (!g_BridgeValid || !g_BridgeStartupInfo.FSF || !g_BridgeStartupInfo.FSF->QuoteSpaceOnly)
        return Str;
    return g_BridgeStartupInfo.FSF->QuoteSpaceOnly(Str);
}
