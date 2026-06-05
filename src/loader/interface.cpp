#include "../common_log.hpp"
#include "../GlobalInfo.hpp"
#include "loader.hpp"
#include <map>
#include <vector>
#include <string>
#include <sstream>
#include <mutex>
#include <thread>
#include <chrono>
#include <unordered_set>

// Global map to track loaded plugin instances
static std::map<std::string, HANDLE> g_LoadedPlugins;

// Struct for loaded plugins
struct LoadedPlugin {
    std::string name;
    std::string title;
};

// List of loaded plugins for the list command
static std::vector<LoadedPlugin> g_Plugins;

// Menu strings for Far Manager (must persist between calls)
static std::vector<std::wstring> g_MenuStrings;
static std::vector<const wchar_t*> g_MenuStringPtrs;
static std::vector<GUID> g_MenuGuids;

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

static Adapter g_Adapter;

// ===== Message-box seam (testable) =====
// All user-facing message boxes go through g_ShowMessageBox so tests can
// install a non-modal hook and assert which messages would be shown. In
// production this points at the real MessageBoxA wrapper below.
typedef void (*ShowMessageBoxFn)(const char* text, const char* caption, unsigned int type);

static void DefaultShowMessageBox(const char* text, const char* caption, unsigned int type) {
    MessageBoxA(NULL, text, caption, type);
}

static ShowMessageBoxFn g_ShowMessageBox = &DefaultShowMessageBox;

static void ShowMessage(const char* text, const char* caption, unsigned int type) {
    if (g_ShowMessageBox) {
        g_ShowMessageBox(text, caption, type);
    }
}

// Test-only hook: install a custom message-box handler (pass nullptr to reset
// to the default MessageBoxA behavior). Returns the previously installed hook.
extern "C" __declspec(dllexport) ShowMessageBoxFn WINAPI PythonFar_TestSetMessageBoxHook(ShowMessageBoxFn hook) {
    ShowMessageBoxFn previous = g_ShowMessageBox;
    g_ShowMessageBox = hook ? hook : &DefaultShowMessageBox;
    return previous;
}

// Helper to load a plugin by name
static bool LoadPlugin(const std::string& pluginName) {
    LOG_TRACE("LoadPlugin: Attempting to load " << pluginName);

    // Check if adapter is initialized
    if (!g_Adapter.IsInitialized()) {
        LOG_TRACE("LoadPlugin: Adapter not initialized, trying to initialize...");
        GlobalInfo gi = { sizeof(GlobalInfo) };
        if (!g_Adapter.ModuleInit() || !g_Adapter.Initialize(&gi)) {
            LOG_TRACE("LoadPlugin: Failed to initialize PythonFar adapter.");
            return false;
        }
        LOG_TRACE("LoadPlugin: Adapter initialized successfully");
    }
    
    // Check if plugin is already loaded
    if (g_LoadedPlugins.find(pluginName) != g_LoadedPlugins.end()) {
        LOG_TRACE("LoadPlugin: Plugin already loaded.");
        return true;
    }
    
    // Get the loader DLL directory to construct absolute path
    HMODULE hModule = GetModuleHandleW(PythonFar::LOADER_DLL_NAME);
    std::wstring basePath;
    if (hModule) {
        WCHAR dllPath[MAX_PATH];
        GetModuleFileNameW(hModule, dllPath, MAX_PATH);
        basePath = dllPath;
        size_t lastSlash = basePath.find_last_of(L'\\');
        if (lastSlash != std::wstring::npos) {
            basePath = basePath.substr(0, lastSlash + 1);
        }
    }
    
    // Construct full absolute path to plugin file.
    // pluginName is UTF-8 (produced via WideCharToMultiByte(CP_UTF8,...)); decode
    // it properly so non-ASCII plugin names/paths are not corrupted.
    std::wstring widePluginName = PythonFar::UTF8ToWide(pluginName);
    std::wstring widePluginPath = basePath + PythonFar::PYTHON_PLUGINS_DIR + L"\\" + widePluginName;
    if (widePluginPath.find(PythonFar::PLUGIN_EXTENSION) == std::wstring::npos) {
        widePluginPath += PythonFar::PLUGIN_EXTENSION;
    }
    
    // Try to create plugin instance
    HANDLE instance = g_Adapter.CreateInstance(widePluginPath.c_str());
    if (instance) {
        g_LoadedPlugins[pluginName] = instance;
        LOG_TRACE("LoadPlugin: Plugin loaded with instance: " << reinterpret_cast<uintptr_t>(instance));

        // Add to plugins list for the list command
        g_Plugins.push_back({pluginName, pluginName});

        return true;
    } else {
        // Get detailed error information
        ErrorInfo errorInfo = { sizeof(ErrorInfo) };
        if (g_Adapter.GetError(&errorInfo)) {
            if (errorInfo.Summary) {
                int size = WideCharToMultiByte(CP_UTF8, 0, errorInfo.Summary, -1, nullptr, 0, nullptr, nullptr);
                std::string summary(size, 0);
                WideCharToMultiByte(CP_UTF8, 0, errorInfo.Summary, -1, &summary[0], size, nullptr, nullptr);
                if (!summary.empty() && summary.back() == '\0') summary.pop_back();
                LOG_TRACE("LoadPlugin Error Summary: " << summary);
            }
        }
        LOG_TRACE("LoadPlugin: Failed to create plugin instance");
        return false;
    }
}

// We need to COPY the PluginStartupInfo, not just store a pointer,
// because Far Manager may reuse/invalidate the memory it points to.
static PluginStartupInfo g_StartupInfoCopy = {};
static FarStandardFunctions g_FSFCopy = {};
static bool g_StartupInfoValid = false;

struct PendingDialogClose {
    HANDLE hDlg;
    intptr_t result;
};

static std::mutex g_SynchroMutex;
static std::unordered_set<HANDLE> g_PendingCloseDialogs;

// ===== PanelControl Bridge Export =====
// This export allows Python code to call PanelControl through the DLL
// since the function pointer in the copied PluginStartupInfo struct
// may not be directly callable from Python ctypes.

extern "C" __declspec(dllexport) intptr_t WINAPI PythonFar_AdvControl(
    const GUID* PluginId, int command, intptr_t param1, void* param2) {

    std::ostringstream oss;
    oss << "PythonFar_AdvControl: valid=" << g_StartupInfoValid
        << " AdvControlPtr=" << reinterpret_cast<uintptr_t>(g_StartupInfoCopy.AdvControl)
        << " PluginId=" << reinterpret_cast<uintptr_t>(PluginId)
        << " cmd=" << command << " p1=" << param1
        << " p2=" << reinterpret_cast<uintptr_t>(param2);
    LOG_TRACE(oss.str());

    if (!g_StartupInfoValid || !g_StartupInfoCopy.AdvControl) {
        LOG_TRACE("PythonFar_AdvControl: g_StartupInfo not valid or AdvControl is null");
        return 0;
    }

    const intptr_t result = g_StartupInfoCopy.AdvControl(
        PluginId,
        static_cast<ADVANCED_CONTROL_COMMANDS>(command),
        param1,
        param2);

    oss.str("");
    oss << "PythonFar_AdvControl: result=" << result;
    LOG_TRACE(oss.str());

    return result;
}

extern "C" __declspec(dllexport) intptr_t WINAPI PythonFar_PanelControl(
    HANDLE hPanel, int command, intptr_t param1, void* param2) {
    
    std::ostringstream oss;
    oss << "PythonFar_PanelControl: hPanel=" << reinterpret_cast<uintptr_t>(hPanel)
        << " cmd=" << command << " p1=" << param1 
        << " p2=" << reinterpret_cast<uintptr_t>(param2)
        << " valid=" << g_StartupInfoValid
        << " PanelControl=" << reinterpret_cast<uintptr_t>(g_StartupInfoCopy.PanelControl);
    LOG_TRACE(oss.str());
    
    if (!g_StartupInfoValid || !g_StartupInfoCopy.PanelControl) {
        LOG_TRACE("PythonFar_PanelControl: g_StartupInfo not valid or PanelControl is null");
        return 0;
    }
    
    intptr_t result = g_StartupInfoCopy.PanelControl(
        hPanel, 
        static_cast<FILE_CONTROL_COMMANDS>(command), 
        param1, 
        param2);
    
    oss.str("");
    oss << "PythonFar_PanelControl: result=" << result;
    LOG_TRACE(oss.str());
    
    return result;
}

static intptr_t WINAPI DefaultDlgProcBridge(
    HANDLE hDlg, intptr_t Msg, intptr_t Param1, void* Param2) {

    if (g_StartupInfoValid && g_StartupInfoCopy.DefDlgProc) {
        return g_StartupInfoCopy.DefDlgProc(hDlg, Msg, Param1, Param2);
    }
    return 0;
}

extern "C" __declspec(dllexport) intptr_t WINAPI PythonFar_DialogInit(
    const GUID* PluginId, const GUID* Id, intptr_t X1, intptr_t Y1, intptr_t X2, intptr_t Y2,
    const wchar_t* HelpTopic, const struct FarDialogItem* Item, size_t ItemsNumber,
    intptr_t Reserved, unsigned __int64 Flags, FARWINDOWPROC DlgProc, void* Param) {

    FARWINDOWPROC effectiveProc = DlgProc;
    if (!effectiveProc) {
        if (g_StartupInfoCopy.DefDlgProc) {
            effectiveProc = reinterpret_cast<FARWINDOWPROC>(g_StartupInfoCopy.DefDlgProc);
        } else {
            effectiveProc = DefaultDlgProcBridge;
        }
    }

    std::ostringstream oss;
    oss << "PythonFar_DialogInit: valid=" << g_StartupInfoValid
        << " DialogInitPtr=" << reinterpret_cast<uintptr_t>(g_StartupInfoCopy.DialogInit)
        << " PluginId=" << reinterpret_cast<uintptr_t>(PluginId)
        << " Id=" << reinterpret_cast<uintptr_t>(Id)
        << " X1=" << X1 << " Y1=" << Y1 << " X2=" << X2 << " Y2=" << Y2
        << " HelpTopic=" << reinterpret_cast<uintptr_t>(HelpTopic)
        << " Item=" << reinterpret_cast<uintptr_t>(Item)
        << " ItemsNumber=" << ItemsNumber
        << " Reserved=" << Reserved
        << " Flags=" << Flags
        << " DlgProc=" << reinterpret_cast<uintptr_t>(DlgProc)
        << " EffectiveProc=" << reinterpret_cast<uintptr_t>(effectiveProc)
        << " Param=" << reinterpret_cast<uintptr_t>(Param);
    LOG_TRACE(oss.str());

    if (!g_StartupInfoValid || !g_StartupInfoCopy.DialogInit) {
        LOG_TRACE("PythonFar_DialogInit: g_StartupInfo not valid or DialogInit is null");
        return static_cast<intptr_t>(-1);
    }

    const HANDLE hDlg = g_StartupInfoCopy.DialogInit(
        PluginId, Id, X1, Y1, X2, Y2, HelpTopic, Item, ItemsNumber, Reserved, Flags, effectiveProc, Param);

    oss.str("");
    oss << "PythonFar_DialogInit: returned=" << reinterpret_cast<uintptr_t>(hDlg);
    LOG_TRACE(oss.str());

    return reinterpret_cast<intptr_t>(hDlg);
}

extern "C" __declspec(dllexport) intptr_t WINAPI PythonFar_DialogRun(HANDLE hDlg) {
    std::ostringstream oss;
    oss << "PythonFar_DialogRun: valid=" << g_StartupInfoValid
        << " DialogRunPtr=" << reinterpret_cast<uintptr_t>(g_StartupInfoCopy.DialogRun)
        << " hDlg=" << reinterpret_cast<uintptr_t>(hDlg);
    LOG_TRACE(oss.str());

    if (!g_StartupInfoValid || !g_StartupInfoCopy.DialogRun) {
        LOG_TRACE("PythonFar_DialogRun: g_StartupInfo not valid or DialogRun is null");
        return static_cast<intptr_t>(-1);
    }

    const intptr_t result = g_StartupInfoCopy.DialogRun(hDlg);

    oss.str("");
    oss << "PythonFar_DialogRun: result=" << result;
    LOG_TRACE(oss.str());

    return result;
 }

extern "C" __declspec(dllexport) intptr_t WINAPI PythonFar_DialogRunWithTimeout(
    const GUID* PluginId, HANDLE hDlg, unsigned long TimeoutMs) {

    std::ostringstream oss;
    oss << "PythonFar_DialogRunWithTimeout: valid=" << g_StartupInfoValid
        << " hDlg=" << reinterpret_cast<uintptr_t>(hDlg)
        << " TimeoutMs=" << TimeoutMs
        << " AdvControlPtr=" << reinterpret_cast<uintptr_t>(g_StartupInfoCopy.AdvControl)
        << " SendDlgMessagePtr=" << reinterpret_cast<uintptr_t>(g_StartupInfoCopy.SendDlgMessage);
    LOG_TRACE(oss.str());

    if (!g_StartupInfoValid || !g_StartupInfoCopy.DialogRun) {
        LOG_TRACE("PythonFar_DialogRunWithTimeout: g_StartupInfo not valid or DialogRun is null");
        return static_cast<intptr_t>(-1);
    }

    if (TimeoutMs > 0 && g_StartupInfoCopy.AdvControl && PluginId) {
        const HANDLE dialogToClose = hDlg;
        const GUID pluginGuid = *PluginId;

        std::thread([dialogToClose, pluginGuid, TimeoutMs]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(TimeoutMs));

            {
                std::lock_guard<std::mutex> lock(g_SynchroMutex);
                g_PendingCloseDialogs.insert(dialogToClose);
            }

            if (g_StartupInfoValid && g_StartupInfoCopy.AdvControl) {
                g_StartupInfoCopy.AdvControl(&pluginGuid, ACTL_SYNCHRO, 0, nullptr);
            }
        }).detach();
    }

    return PythonFar_DialogRun(hDlg);
 }


extern "C" __declspec(dllexport) void WINAPI PythonFar_DialogFree(HANDLE hDlg) {
    std::ostringstream oss;
    oss << "PythonFar_DialogFree: valid=" << g_StartupInfoValid
        << " DialogFreePtr=" << reinterpret_cast<uintptr_t>(g_StartupInfoCopy.DialogFree)
        << " hDlg=" << reinterpret_cast<uintptr_t>(hDlg);
    LOG_TRACE(oss.str());

    if (g_StartupInfoValid && g_StartupInfoCopy.DialogFree) {
        g_StartupInfoCopy.DialogFree(hDlg);
    } else {
        LOG_TRACE("PythonFar_DialogFree: g_StartupInfo not valid or DialogFree is null");
    }
}

extern "C" __declspec(dllexport) intptr_t WINAPI PythonFar_SendDlgMessage(
    HANDLE hDlg, intptr_t Msg, intptr_t Param1, void* Param2) {

    std::ostringstream oss;
    oss << "PythonFar_SendDlgMessage: valid=" << g_StartupInfoValid
        << " SendDlgMessagePtr=" << reinterpret_cast<uintptr_t>(g_StartupInfoCopy.SendDlgMessage)
        << " hDlg=" << reinterpret_cast<uintptr_t>(hDlg)
        << " Msg=" << Msg
        << " Param1=" << Param1
        << " Param2=" << reinterpret_cast<uintptr_t>(Param2);
    LOG_TRACE(oss.str());

    if (!g_StartupInfoValid || !g_StartupInfoCopy.SendDlgMessage) {
        LOG_TRACE("PythonFar_SendDlgMessage: g_StartupInfo not valid or SendDlgMessage is null");
        return static_cast<intptr_t>(-1);
    }

    const intptr_t result = g_StartupInfoCopy.SendDlgMessage(hDlg, Msg, Param1, Param2);

    oss.str("");
    oss << "PythonFar_SendDlgMessage: result=" << result;
    LOG_TRACE(oss.str());

    return result;
}

extern "C" __declspec(dllexport) intptr_t WINAPI PythonFar_DefDlgProc(
    HANDLE hDlg, intptr_t Msg, intptr_t Param1, void* Param2) {
    if (!g_StartupInfoValid || !g_StartupInfoCopy.DefDlgProc) return 0;
    return g_StartupInfoCopy.DefDlgProc(hDlg, Msg, Param1, Param2);
}

extern "C" __declspec(dllexport) intptr_t WINAPI PythonFar_EditorControl(
    intptr_t EditorID, intptr_t Command, intptr_t Param1, void* Param2) {
    if (!g_StartupInfoValid || !g_StartupInfoCopy.EditorControl) return 0;
    return g_StartupInfoCopy.EditorControl(EditorID, static_cast<EDITOR_CONTROL_COMMANDS>(Command), Param1, Param2);
}

extern "C" __declspec(dllexport) intptr_t WINAPI PythonFar_ViewerControl(
    intptr_t ViewerID, intptr_t Command, intptr_t Param1, void* Param2) {
    if (!g_StartupInfoValid || !g_StartupInfoCopy.ViewerControl) return 0;
    return g_StartupInfoCopy.ViewerControl(ViewerID, static_cast<VIEWER_CONTROL_COMMANDS>(Command), Param1, Param2);
}


extern "C" {

// ===== Standard Far Manager Plugin API =====

void WINAPI GetGlobalInfoW(GlobalInfo* Info) {
    LOG_TRACE("GetGlobalInfoW called - Loader is being initialized by Far Manager");
    PythonFar::InitializeGlobalInfo(Info, PythonFar::LOADER_GUID, 
                                    PythonFar::LOADER_TITLE, 
                                    PythonFar::LOADER_DESCRIPTION, 
                                    PythonFar::LOADER_AUTHOR);
}

void WINAPI SetStartupInfoW(const PluginStartupInfo* Info) {
    LOG_TRACE("SetStartupInfoW called");

    // Copy the PluginStartupInfo struct - we can't just store a pointer
    // because Far Manager may reuse/invalidate the memory
    if (Info) {
        g_StartupInfoCopy = *Info;
        if (Info->FSF) {
            g_FSFCopy = *Info->FSF;
            g_StartupInfoCopy.FSF = &g_FSFCopy;
        }
        g_StartupInfoValid = true;
        LOG_TRACE("SetStartupInfoW: Copied PluginStartupInfo, PanelControl ptr=" << 
                  reinterpret_cast<uintptr_t>(g_StartupInfoCopy.PanelControl));
    } else {
        LOG_TRACE("SetStartupInfoW: Info is null");
        g_StartupInfoValid = false;
    }
    
    // Auto-discover and load plugins from python/ directory
    if (!g_Adapter.IsInitialized()) {
        GlobalInfo gi = { sizeof(GlobalInfo) };
        if (g_Adapter.ModuleInit() && g_Adapter.Initialize(&gi)) {
             // Adapter initialized
        }
    }

    HMODULE hModule = GetModuleHandleW(PythonFar::LOADER_DLL_NAME);
    if (hModule) {
        WCHAR dllPath[MAX_PATH];
        GetModuleFileNameW(hModule, dllPath, MAX_PATH);
        std::wstring basePath = dllPath;
        size_t lastSlash = basePath.find_last_of(L'\\');
        if (lastSlash != std::wstring::npos) {
            basePath = basePath.substr(0, lastSlash + 1);
        }
        
        std::wstring searchPath = basePath + PythonFar::PYTHON_PLUGINS_DIR + L"\\" + PythonFar::PLUGIN_SEARCH_PATTERN;
        LOG_TRACE("SetStartupInfoW: Searching for plugins in " << WideToUTF8(searchPath.c_str()));

        WIN32_FIND_DATAW findData;
        HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
        
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                std::wstring fileName = findData.cFileName;
                // Check if it ends with .far.py
                size_t extLen = wcslen(PythonFar::PLUGIN_EXTENSION);
                if (fileName.length() > extLen && fileName.substr(fileName.length() - extLen) == PythonFar::PLUGIN_EXTENSION) {
                    std::wstring nameW = fileName.substr(0, fileName.length() - extLen);
                    
                    // Convert to std::string for LoadPlugin
                    int size_needed = WideCharToMultiByte(CP_UTF8, 0, nameW.c_str(), -1, nullptr, 0, nullptr, nullptr);
                    std::string pluginName(size_needed, 0);
                    WideCharToMultiByte(CP_UTF8, 0, nameW.c_str(), -1, &pluginName[0], size_needed, nullptr, nullptr);
                    if (!pluginName.empty() && pluginName.back() == '\0') pluginName.pop_back();
                    
                    LOG_TRACE("SetStartupInfoW: Found plugin file: " << pluginName);
                    
                    LoadPlugin(pluginName);
                    
                    // Now that the plugin is loaded, call SetStartupInfoW on its instance
                    if (g_LoadedPlugins.find(pluginName) != g_LoadedPlugins.end()) {
                        HANDLE instance = g_LoadedPlugins[pluginName];
                        
                        FARPROC func = g_Adapter.GetFunctionAddress(instance, L"SetStartupInfoW");
                        if (func) {
                             typedef void (WINAPI *SetStartupInfoFunc)(const PluginStartupInfo*);
                             SetStartupInfoFunc setStartup = reinterpret_cast<SetStartupInfoFunc>(func);
                             
                             PluginStartupInfo modifiedInfo = *Info;
                             modifiedInfo.Instance = instance;
                             
                             setStartup(&modifiedInfo);
                             LOG_TRACE("SetStartupInfoW: Initialized plugin instance");
                        } else {
                            LOG_TRACE("SetStartupInfoW: Failed to get SetStartupInfoW for plugin instance");
                        }
                    }
                }
            } while (FindNextFileW(hFind, &findData));
            FindClose(hFind);
        } else {
             LOG_TRACE("SetStartupInfoW: No plugins found.");
        }
    }
}

void WINAPI GetPluginInfoW(PluginInfo* Info) {
    LOG_TRACE("GetPluginInfoW called");
    Info->StructSize = sizeof(PluginInfo);
    Info->Flags = PF_PRELOAD | PF_FULLCMDLINE;
    Info->CommandPrefix = PythonFar::PLUGIN_PREFIX;
    
    // Build menu from loaded plugins
    g_MenuStrings.clear();
    g_MenuStringPtrs.clear();
    g_MenuGuids.clear();
    
    // Always add "PythonFar Manager" as first menu item
    g_MenuStrings.push_back(L"PythonFar Manager");
    g_MenuGuids.push_back(PythonFar::LOADER_GUID);
    
    // Add loaded plugins to menu
    for (const auto& pair : g_LoadedPlugins) {
        // pair.first is a UTF-8 plugin name; decode properly for the menu.
        std::wstring pluginName = PythonFar::UTF8ToWide(pair.first);
        g_MenuStrings.push_back(L"[Py] " + pluginName);
        // Generate a GUID for this plugin (simple hash-based)
        GUID pluginGuid = PythonFar::LOADER_GUID;
        pluginGuid.Data1 = static_cast<unsigned long>(std::hash<std::string>{}(pair.first));
        g_MenuGuids.push_back(pluginGuid);
    }
    
    // Build pointer array
    for (const auto& str : g_MenuStrings) {
        g_MenuStringPtrs.push_back(str.c_str());
    }
    
    if (!g_MenuStringPtrs.empty()) {
        Info->PluginMenu.Guids = g_MenuGuids.data();
        Info->PluginMenu.Strings = g_MenuStringPtrs.data();
        Info->PluginMenu.Count = g_MenuStringPtrs.size();
    } else {
        Info->PluginMenu.Count = 0;
        Info->PluginMenu.Strings = nullptr;
        Info->PluginMenu.Guids = nullptr;
    }
    
    Info->PluginConfig.Count = 0;
    Info->PluginConfig.Strings = nullptr;
    LOG_TRACE("GetPluginInfoW: Command prefix 'py' registered, menu items: " << g_MenuStringPtrs.size());
}

intptr_t WINAPI ProcessSynchroEventW(const ProcessSynchroEventInfo* Info) {
    if (!Info || Info->Event != SE_COMMONSYNCHRO) {
        return 0;
    }

    // Special request: Param == 1 => quit Far.
    if (Info->Param == reinterpret_cast<void*>(1)) {
        LOG_TRACE("ProcessSynchroEventW: Quit request received");
        if (g_StartupInfoValid && g_StartupInfoCopy.AdvControl) {
            g_StartupInfoCopy.AdvControl(&PythonFar::LOADER_GUID, ACTL_QUIT, 0, nullptr);
        }
        return 0;
    }

    std::unordered_set<HANDLE> toClose;
    {
        std::lock_guard<std::mutex> lock(g_SynchroMutex);
        toClose.swap(g_PendingCloseDialogs);
    }

    for (const auto hDlg : toClose) {
        if (g_StartupInfoValid && g_StartupInfoCopy.SendDlgMessage) {
            g_StartupInfoCopy.SendDlgMessage(hDlg, DM_CLOSE, -1, nullptr);
        }
    }

    return 0;
}

void WINAPI ExitFARW(const ExitInfo* Info) {
    LOG_TRACE("ExitFARW called");
    // Cleanup if needed
}

HANDLE WINAPI OpenW(const OpenInfo* Info) {
    LOG_TRACE("OpenW called - NEW DEBUG VERSION");
    if (!Info) {
        LOG_TRACE("OpenW: Info is null");
        return nullptr;
    }
    
    LOG_TRACE("OpenW: OpenFrom = " << Info->OpenFrom);
    
    // Handle plugin menu selection
    if (Info->OpenFrom == OPEN_PLUGINSMENU) {
        LOG_TRACE("OpenW: Plugin menu selection");
        
        // Info->Guid points to the GUID of the selected menu item
        const GUID* selectedGuid = Info->Guid;
        if (!selectedGuid) {
            LOG_TRACE("OpenW: No GUID provided");
            return nullptr;
        }
        
        // Log the GUID
        LOG_TRACE("OpenW: Selected GUID Data1 = " << selectedGuid->Data1);
        
        // Check if it's the loader's own GUID (PythonFar Manager)
        if (memcmp(selectedGuid, &PythonFar::LOADER_GUID, sizeof(GUID)) == 0) {
            // "PythonFar Manager" selected - show help/status
            std::string msg = "PythonFar Plugin Manager\n\n";
            msg += "Loaded plugins: " + std::to_string(g_LoadedPlugins.size()) + "\n\n";
            msg += "Commands:\n";
            msg += "  py:list    - List available plugins\n";
            msg += "  py:loaded  - List loaded plugins\n";
            msg += "  py:load <name>   - Load a plugin\n";
            msg += "  py:unload <name> - Unload a plugin\n";
            ShowMessage(msg.c_str(), "PythonFar", MB_OK | MB_ICONINFORMATION);
            return nullptr;
        }
        
        // Find the plugin by matching GUID (we use Data1 as a hash of the plugin name)
         for (const auto& pair : g_LoadedPlugins) {
            GUID pluginGuid = PythonFar::LOADER_GUID;
            pluginGuid.Data1 = static_cast<unsigned long>(std::hash<std::string>{}(pair.first));
            
            if (memcmp(selectedGuid, &pluginGuid, sizeof(GUID)) == 0) {
                LOG_TRACE("OpenW: Invoking plugin: " << pair.first);
                
                // Get the OpenW function from the adapter and call it
                if (g_Adapter.IsInitialized()) {
                    FARPROC openFunc = g_Adapter.GetFunctionAddress(pair.second, L"OpenW");
                    if (openFunc) {
                        LOG_TRACE("OpenW: Got OpenW function, calling it");
                        
                        // Create a modified OpenInfo with our plugin instance
                        OpenInfo modifiedInfo = *Info;
                        modifiedInfo.Instance = pair.second;  // Set to our PluginModule handle
                        
                        LOG_TRACE("OpenW: Setting Instance to " << reinterpret_cast<uintptr_t>(pair.second));
                        
                        typedef HANDLE (WINAPI *OpenWFunc)(const OpenInfo*);
                        OpenWFunc pluginOpen = reinterpret_cast<OpenWFunc>(openFunc);
                        return pluginOpen(&modifiedInfo);
                    } else {
                        LOG_TRACE("OpenW: Failed to get OpenW function");
                    }
                }
                break;
            }
        }
        
        LOG_TRACE("OpenW: Plugin GUID not found in loaded plugins");
        // FALLBACK: If we have exactly ONE plugin loaded, and it's the example plugin, just run it!
        // This handles cases where GUID matching might be failing or simplified
        if (g_LoadedPlugins.size() == 1 && g_LoadedPlugins.begin()->first == "example") {
             LOG_TRACE("OpenW: Fallback - executing single loaded 'example' plugin");
             auto pair = *g_LoadedPlugins.begin();
             if (g_Adapter.IsInitialized()) {
                 FARPROC openFunc = g_Adapter.GetFunctionAddress(pair.second, L"OpenW");
                 if (openFunc) {
                     OpenInfo modifiedInfo = *Info;
                     modifiedInfo.Instance = pair.second;
                     typedef HANDLE (WINAPI *OpenWFunc)(const OpenInfo*);
                     OpenWFunc pluginOpen = reinterpret_cast<OpenWFunc>(openFunc);
                     return pluginOpen(&modifiedInfo);
                 }
             }
        }

        return nullptr;
    }
    
    // Check if this is a command line call
    if (Info->OpenFrom == OPEN_COMMANDLINE) {
        LOG_TRACE("OpenW: Command line call detected");
        
        // Get command line info
        const OpenCommandLineInfo* cmdInfo = (const OpenCommandLineInfo*)Info->Data;
        if (!cmdInfo) {
            LOG_TRACE("OpenW: cmdInfo is null");
            return nullptr;
        }
        
        if (!cmdInfo->CommandLine) {
            LOG_TRACE("OpenW: CommandLine is null");
            return nullptr;
        }
        
        // Convert wide string to narrow string
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, cmdInfo->CommandLine, -1, nullptr, 0, nullptr, nullptr);
        std::string commandLine(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, cmdInfo->CommandLine, -1, &commandLine[0], size_needed, nullptr, nullptr);
        
        // Remove null terminator from the end
        if (!commandLine.empty() && commandLine.back() == '\0') {
            commandLine.pop_back();
        }
        
        LOG_TRACE("OpenW: Command line: " << commandLine);
        
        // Parse command (should be "py:command")
        size_t colonPos = commandLine.find(':');
        if (colonPos == std::string::npos) {
            LOG_TRACE("OpenW: No colon found in command");
            return nullptr;
        }
        
        std::string prefix = commandLine.substr(0, colonPos);
        std::string command = commandLine.substr(colonPos + 1);
        
        LOG_TRACE("OpenW: Parsed prefix: '" << prefix << "'");
        LOG_TRACE("OpenW: Parsed command: '" << command << "'");
        
        // Convert PLUGIN_PREFIX from wchar_t to string for comparison
        std::string expectedPrefix(WideToUTF8(PythonFar::PLUGIN_PREFIX));
        if (prefix != expectedPrefix) {
            LOG_TRACE("OpenW: Prefix is not '" << expectedPrefix << "'");
            return nullptr;
        }
        
        // Handle commands.
        // We are inside the OPEN_COMMANDLINE branch, i.e. the user typed a
        // "py:..." command interactively at Far's command line, so show
        // feedback (message boxes) for results and errors. (The previous
        // `Info->OpenFrom != OPEN_COMMANDLINE` test was always false here,
        // making every MessageBoxA below dead code.)
        const bool showUI = true;

        if (command == "list") {
            LOG_TRACE("OpenW: Handling list command");
            
            // Simple directory scan for .far.py files
            // Change to plugin directory
            HMODULE hModule = GetModuleHandleW(PythonFar::LOADER_DLL_NAME);
            std::string pluginList = "Available Python plugins:\n";
            if (hModule) {
                WCHAR dllPath[MAX_PATH];
                GetModuleFileNameW(hModule, dllPath, MAX_PATH);
                // Remove the DLL filename to get directory (but do NOT change process CWD)
                WCHAR* lastSlash = wcsrchr(dllPath, L'\\');
                if (lastSlash) {
                    *lastSlash = L'\0';
                }

                WIN32_FIND_DATAW findData;
                std::wstring searchPattern = std::wstring(dllPath) + L"\\" + PythonFar::PYTHON_PLUGINS_DIR + L"\\" + PythonFar::PLUGIN_SEARCH_PATTERN;
                HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &findData);
                if (hFind != INVALID_HANDLE_VALUE) {
                    do {
                        // Convert wide string to narrow for display
                        int size_needed = WideCharToMultiByte(CP_UTF8, 0, findData.cFileName, -1, nullptr, 0, nullptr, nullptr);
                        std::string filename(size_needed, 0);
                        WideCharToMultiByte(CP_UTF8, 0, findData.cFileName, -1, &filename[0], size_needed, nullptr, nullptr);
                        if (!filename.empty() && filename.back() == '\0') {
                            filename.pop_back();
                        }
                        pluginList += "- " + filename + "\n";
                    } while (FindNextFileW(hFind, &findData));
                    FindClose(hFind);
                } else {
                    pluginList += "(No " + std::string(WideToUTF8(PythonFar::PLUGIN_EXTENSION)) + " files found in " + std::string(WideToUTF8(PythonFar::PYTHON_PLUGINS_DIR)) + "/ directory)\n";
                }
            }
            
            if (showUI) {
                ShowMessage(pluginList.c_str(), "PythonFar Plugin List", MB_OK);
            } else {
                LOG_TRACE(pluginList);
            }
            return (HANDLE)1;
        }
        else if (command == "loaded") {
            LOG_TRACE("OpenW: Handling loaded command");
            
            std::string loadedList = "Loaded Python plugins:\n";
            if (g_LoadedPlugins.empty()) {
                loadedList += "(No plugins currently loaded)\n";
            } else {
                for (const auto& pair : g_LoadedPlugins) {
                    loadedList += "- " + pair.first + "\n";
                }
            }
            
            if (showUI) {
                ShowMessage(loadedList.c_str(), "PythonFar Loaded Plugins", MB_OK);
            } else {
                LOG_TRACE(loadedList);
            }
            return (HANDLE)1;
        }
        else if (command.substr(0, 5) == "load ") {
            LOG_TRACE("OpenW: Handling load command");
            std::string pluginName = command.substr(5);
            LOG_TRACE("OpenW: Loading plugin: " << pluginName);

            // Only allow bare plugin names (no paths) to avoid directory traversal.
            if (pluginName.empty() ||
                pluginName.find("..") != std::string::npos ||
                pluginName.find_first_of("\\/:") != std::string::npos) {
                if (showUI) {
                    ShowMessage("Invalid plugin name. Use a bare name without path separators.", "PythonFar Error", MB_OK | MB_ICONERROR);
                }
                return nullptr;
            }
            // Check if adapter is initialized
            if (!g_Adapter.IsInitialized()) {
                LOG_TRACE("OpenW: Adapter not initialized, trying to initialize...");
                GlobalInfo gi = { sizeof(GlobalInfo) };
                if (!g_Adapter.ModuleInit() || !g_Adapter.Initialize(&gi)) {
                    if (showUI) {
                        std::string msg = "Failed to initialize PythonFar adapter. Make sure " + std::string(WideToUTF8(PythonFar::ADAPTER_DLL_NAME)) + " is in the Adapters directory.";
                        ShowMessage(msg.c_str(), "PythonFar Error", MB_OK | MB_ICONERROR);
                    }
                    return nullptr;
                }
                LOG_TRACE("OpenW: Adapter initialized successfully");
            }
            
            // Check if plugin is already loaded
            if (g_LoadedPlugins.find(pluginName) != g_LoadedPlugins.end()) {
                if (showUI) {
                    ShowMessage(("Plugin '" + pluginName + "' is already loaded!").c_str(), "PythonFar", MB_OK | MB_ICONWARNING);
                }
                return (HANDLE)1;
            }
            
            // Get the loader DLL directory to construct absolute path
            HMODULE hModule = GetModuleHandleW(PythonFar::LOADER_DLL_NAME);
            std::wstring basePath;
            if (hModule) {
                WCHAR dllPath[MAX_PATH];
                GetModuleFileNameW(hModule, dllPath, MAX_PATH);
                basePath = dllPath;
                size_t lastSlash = basePath.find_last_of(L'\\');
                if (lastSlash != std::wstring::npos) {
                    basePath = basePath.substr(0, lastSlash + 1);
                }
            }
            
            // Construct full absolute path to plugin file.
            // pluginName is UTF-8 (decoded from the command line via
            // WideCharToMultiByte(CP_UTF8,...)); decode it properly so non-ASCII
            // plugin names/paths are not corrupted.
            std::wstring widePluginName = PythonFar::UTF8ToWide(pluginName);
            std::wstring widePluginPath = basePath + PythonFar::PYTHON_PLUGINS_DIR + L"\\" + widePluginName;
            if (widePluginPath.find(PythonFar::PLUGIN_EXTENSION) == std::wstring::npos) {
                widePluginPath += PythonFar::PLUGIN_EXTENSION;
            }
            
            // Log the path we're trying to load
            {
                int size = WideCharToMultiByte(CP_UTF8, 0, widePluginPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
                std::string pathNarrow(size, 0);
                WideCharToMultiByte(CP_UTF8, 0, widePluginPath.c_str(), -1, &pathNarrow[0], size, nullptr, nullptr);
                LOG_TRACE("OpenW: Full plugin path: " << pathNarrow);
            }
            
            // Try to create plugin instance
            HANDLE instance = g_Adapter.CreateInstance(widePluginPath.c_str());
            if (instance) {
                g_LoadedPlugins[pluginName] = instance;
                LOG_TRACE("OpenW: Plugin '" << pluginName << "' loaded successfully (instance: " 
                          << reinterpret_cast<uintptr_t>(instance) << ")");
                if (showUI) {
                    ShowMessage(("Plugin '" + pluginName + "' loaded successfully!").c_str(), "PythonFar", MB_OK | MB_ICONINFORMATION);
                }
            } else {
                // Get detailed error information
                ErrorInfo errorInfo = { sizeof(ErrorInfo) };
                std::string errorMsg = "Failed to load plugin '" + pluginName + "'\n\n";
                
                if (g_Adapter.GetError(&errorInfo)) {
                    if (errorInfo.Summary) {
                        int size = WideCharToMultiByte(CP_UTF8, 0, errorInfo.Summary, -1, nullptr, 0, nullptr, nullptr);
                        std::string summary(size, 0);
                        WideCharToMultiByte(CP_UTF8, 0, errorInfo.Summary, -1, &summary[0], size, nullptr, nullptr);
                        if (!summary.empty() && summary.back() == '\0') summary.pop_back();
                        errorMsg += "Summary: " + summary + "\n\n";
                    }
                    
                    if (errorInfo.Description) {
                        int size = WideCharToMultiByte(CP_UTF8, 0, errorInfo.Description, -1, nullptr, 0, nullptr, nullptr);
                        std::string desc(size, 0);
                        WideCharToMultiByte(CP_UTF8, 0, errorInfo.Description, -1, &desc[0], size, nullptr, nullptr);
                        if (!desc.empty() && desc.back() == '\0') desc.pop_back();
                        errorMsg += "Description: " + desc;
                    }
                } else {
                    errorMsg += "No additional error details available.";
                }
                
                if (showUI) {
                    ShowMessage(errorMsg.c_str(), "PythonFar Error", MB_OK | MB_ICONERROR);
                }
                LOG_TRACE("OpenW: Failed to create plugin instance");
            }
            
            return (HANDLE)1;
        }
        else if (command.substr(0, 7) == "unload ") {
            LOG_TRACE("OpenW: Handling unload command");
             std::string pluginName = command.substr(7);
             LOG_TRACE("OpenW: Unloading plugin: " << pluginName);
            
            // Check if plugin is loaded
            auto it = g_LoadedPlugins.find(pluginName);
            if (it == g_LoadedPlugins.end()) {
                if (showUI) {
                    ShowMessage(("Plugin '" + pluginName + "' is not loaded!").c_str(), "PythonFar", MB_OK | MB_ICONWARNING);
                }
                return (HANDLE)1;
            }
            
            // Try to destroy plugin instance
            if (g_Adapter.DestroyInstance(it->second)) {
                g_LoadedPlugins.erase(it);
                if (showUI) {
                    ShowMessage(("Plugin '" + pluginName + "' unloaded successfully!").c_str(), "PythonFar", MB_OK | MB_ICONINFORMATION);
                }
                LOG_TRACE("OpenW: Plugin unloaded successfully");
            } else {
                if (showUI) {
                    ShowMessage(("Failed to unload plugin '" + pluginName + "'").c_str(), "PythonFar", MB_OK | MB_ICONERROR);
                }
                LOG_TRACE("OpenW: Failed to destroy plugin instance");
            }
            
            return (HANDLE)1;
        }
        
        // Default: treat the command as a Python script name and execute it.
        {
            std::string scriptName = command;
            LOG_TRACE("OpenW: Running script: " << scriptName);

            if (scriptName.empty() ||
                scriptName.find("..") != std::string::npos) {
                LOG_TRACE("OpenW: Invalid script name: " << scriptName);
                return nullptr;
            }

            // Build script path: absolute path or relative to CWD
            std::wstring wideScriptName = PythonFar::UTF8ToWide(scriptName);
            std::wstring scriptPath;
            if (wideScriptName.find(L':') != std::wstring::npos ||
                wideScriptName[0] == L'\\' || wideScriptName[0] == L'/') {
                // Absolute path (C:\... or \\...)
                scriptPath = wideScriptName;
            } else if (wideScriptName.find(L'\\') != std::wstring::npos ||
                       wideScriptName.find(L'/') != std::wstring::npos) {
                // Relative path with subdirectories — resolve against CWD
                WCHAR cwd[MAX_PATH];
                GetCurrentDirectoryW(MAX_PATH, cwd);
                scriptPath = std::wstring(cwd) + L"\\" + wideScriptName;
            } else {
                // Bare name — look in CWD
                WCHAR cwd[MAX_PATH];
                GetCurrentDirectoryW(MAX_PATH, cwd);
                scriptPath = std::wstring(cwd) + L"\\" + wideScriptName;
            }
            if (scriptPath.find(L".py") == std::wstring::npos) {
                scriptPath += L".py";
            }

            LOG_TRACE("OpenW: Script path: " << PythonFar::WideToUTF8(scriptPath.c_str()));

            // Ensure adapter is initialised
            if (!g_Adapter.IsInitialized()) {
                LOG_TRACE("OpenW: Adapter not initialized, trying to initialize...");
                GlobalInfo gi = { sizeof(GlobalInfo) };
                if (!g_Adapter.ModuleInit() || !g_Adapter.Initialize(&gi)) {
                    LOG_TRACE("OpenW: Failed to initialize adapter");
                    return nullptr;
                }
                LOG_TRACE("OpenW: Adapter initialized successfully");
            }

            // Call RunScript directly via GetProcAddress on the adapter DLL
            // (GetFunctionAddress requires a non-null plugin instance).
            HMODULE hAdapter = GetModuleHandleW(PythonFar::ADAPTER_DLL_NAME);
            if (!hAdapter) {
                LOG_TRACE("OpenW: Adapter DLL not loaded");
                return nullptr;
            }
            typedef BOOL (WINAPI *RunScriptFunc)(const wchar_t*);
            RunScriptFunc runScript =
                reinterpret_cast<RunScriptFunc>(GetProcAddress(hAdapter, "RunScript"));
            if (!runScript) {
                LOG_TRACE("OpenW: RunScript export not found on adapter");
                return nullptr;
            }
            if (runScript(scriptPath.c_str())) {
                return (HANDLE)1;
            }
            LOG_TRACE("OpenW: Script execution failed");
            return nullptr;
        }
    }
    
    LOG_TRACE("OpenW: Not a command line call");
    return nullptr;
}

// ===== Adapter Loader API =====

BOOL WINAPI loader_Initialize(GlobalInfo* Info) noexcept {
    LOG_TRACE("loader_Initialize called - Far Manager is checking for adapter support");
    return try_call(
        [&]() -> BOOL {
            bool result = g_Adapter.ModuleInit() && g_Adapter.Initialize(Info);
            
            // We MUST populate Info so Far Manager registers us as a valid adapter
            if (Info) {
                PythonFar::InitializeGlobalInfo(Info, PythonFar::LOADER_GUID,
                                                 PythonFar::LOADER_TITLE,
                                                 PythonFar::LOADER_DESCRIPTION,
                                                 PythonFar::LOADER_AUTHOR);
                LOG_TRACE("Adapter::Initialize Info->StructSize=" << Info->StructSize);
            }
            
            LOG_TRACE(result ? "loader_Initialize succeeded" : "loader_Initialize failed");
            return result ? TRUE : FALSE;
        },
        []() -> BOOL {
            LOG_TRACE("loader_Initialize exception caught");
            return FALSE;
        });
}

BOOL WINAPI loader_IsPlugin(const wchar_t* FileName) noexcept {
     LOG_TRACE("loader_IsPlugin called for file: " << WideToUTF8(FileName));
    return try_call(
        [&]() -> BOOL {
            bool result = g_Adapter.IsPlugin(FileName);
            LOG_TRACE(result ? "  -> IsPlugin: TRUE" : "  -> IsPlugin: FALSE");
            return result ? TRUE : FALSE;
        },
        []() -> BOOL {
            LOG_TRACE("  -> IsPlugin: EXCEPTION");
            return FALSE;
        });
}

HANDLE WINAPI loader_CreateInstance(const wchar_t* FileName) noexcept {
     LOG_TRACE("loader_CreateInstance called for: " << WideToUTF8(FileName));
    return try_call(
        [&]() -> HANDLE {
            HANDLE result = g_Adapter.CreateInstance(FileName);
            LOG_TRACE(result ? "  -> CreateInstance: SUCCESS" : "  -> CreateInstance: FAILED");
            return result;
        },
        []() -> HANDLE {
            LOG_TRACE("  -> CreateInstance: EXCEPTION");
            return HANDLE{};
        });
}

FARPROC WINAPI loader_GetFunctionAddress(HANDLE Instance, const wchar_t* FunctionName) noexcept {
    return try_call(
        [&]() -> FARPROC {
            return g_Adapter.GetFunctionAddress(Instance, FunctionName);
        },
        []() -> FARPROC {
            return FARPROC{};
        });
}

BOOL WINAPI loader_GetError(ErrorInfo* Info) noexcept {
    return try_call(
        [&]() -> BOOL {
            return g_Adapter.GetError(Info) ? TRUE : FALSE;
        },
        []() -> BOOL {
            return FALSE;
        });
}

BOOL WINAPI loader_DestroyInstance(HANDLE Instance) noexcept {
    return try_call(
        [&]() -> BOOL {
            return g_Adapter.DestroyInstance(Instance) ? TRUE : FALSE;
        },
        []() -> BOOL {
            return FALSE;
        });
}

void WINAPI loader_Free(const ExitInfo* Info) noexcept {
    return try_call(
        [&] {
            g_Adapter.Free(Info);
            g_Adapter.ModuleFree();
        },
        [] {
        });
}

} // extern "C"