#include <gtest/gtest.h>
#include <windows.h>
#include <string>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <mutex>
#include "plugin.hpp"
#include "GlobalInfo.hpp"

// We test that SetStartupInfoW deep copies the struct to avoid memory corruption
// if Far Manager unloads/reuses the original memory.

static bool g_DummyPanelControlCalled = false;
static intptr_t WINAPI DummyPanelControl(HANDLE hPanel, intptr_t Command, intptr_t Param1, void* Param2) {
    g_DummyPanelControlCalled = true;
    return 42;
}

static bool g_DummyAdvControlCalled = false;
static intptr_t WINAPI DummyAdvControl(const GUID* ModuleGuid, intptr_t Command, intptr_t Param1, void* Param2) {
    g_DummyAdvControlCalled = true;
    return 43;
}

static bool g_DummyDialogInitCalled = false;
static HANDLE WINAPI DummyDialogInit(const GUID* PluginId, const GUID* Id, intptr_t X1, intptr_t Y1, intptr_t X2, intptr_t Y2, const wchar_t* HelpTopic, const struct FarDialogItem* Item, size_t ItemsNumber, intptr_t Reserved, FARDIALOGFLAGS Flags, FARWINDOWPROC DlgProc, void* Param) {
    g_DummyDialogInitCalled = true;
    return reinterpret_cast<HANDLE>(44);
}

class LoaderBridgesTest : public ::testing::Test {
protected:
    HMODULE hLoader = nullptr;
    void (WINAPI *setStartupInfoW)(const PluginStartupInfo*) = nullptr;

    intptr_t (WINAPI *panelControlBridge)(HANDLE, intptr_t, intptr_t, void*) = nullptr;
    intptr_t (WINAPI *advControlBridge)(const GUID*, intptr_t, intptr_t, void*) = nullptr;
    HANDLE (WINAPI *dialogInitBridge)(const GUID*, const GUID*, intptr_t, intptr_t, intptr_t, intptr_t, const wchar_t*, const struct FarDialogItem*, size_t, intptr_t, FARDIALOGFLAGS, FARWINDOWPROC, void*) = nullptr;

    void SetUp() override {
        hLoader = LoadLibraryW(L"PythonFar.dll");
        ASSERT_NE(hLoader, nullptr) << "Failed to load PythonFar.dll";

        setStartupInfoW = reinterpret_cast<void (WINAPI*)(const PluginStartupInfo*)>(
            GetProcAddress(hLoader, "SetStartupInfoW"));
        ASSERT_NE(setStartupInfoW, nullptr);

        panelControlBridge = reinterpret_cast<intptr_t (WINAPI*)(HANDLE, intptr_t, intptr_t, void*)>(
            GetProcAddress(hLoader, "PythonFar_PanelControl"));
        ASSERT_NE(panelControlBridge, nullptr);

        advControlBridge = reinterpret_cast<intptr_t (WINAPI*)(const GUID*, intptr_t, intptr_t, void*)>(
            GetProcAddress(hLoader, "PythonFar_AdvControl"));
        ASSERT_NE(advControlBridge, nullptr);

        dialogInitBridge = reinterpret_cast<HANDLE (WINAPI*)(const GUID*, const GUID*, intptr_t, intptr_t, intptr_t, intptr_t, const wchar_t*, const struct FarDialogItem*, size_t, intptr_t, FARDIALOGFLAGS, FARWINDOWPROC, void*)>(
            GetProcAddress(hLoader, "PythonFar_DialogInit"));
        ASSERT_NE(dialogInitBridge, nullptr);

        // Clear globals
        g_DummyPanelControlCalled = false;
        g_DummyAdvControlCalled = false;
        g_DummyDialogInitCalled = false;
    }

    void TearDown() override {
        if (hLoader) {
            FreeLibrary(hLoader);
        }
    }
};

// ===========================================================================
// Dialog timeout + ProcessSynchroEvent flow.
//
// PythonFar_DialogRunWithTimeout spawns a worker thread that, after the
// timeout, records the dialog handle in a shared set and asks Far to dispatch
// a synchro event (ACTL_SYNCHRO). When Far later calls ProcessSynchroEventW,
// the loader drains the set and closes each dialog via SendDlgMessage(DM_CLOSE).
//
// This is the same set that was previously subject to a data race; the test
// exercises the producer (worker) -> consumer (synchro handler) contract.
// ===========================================================================

static std::atomic<bool> g_SynchroRequested{false};
static std::atomic<int>  g_DialogRunCalls{0};

// DialogRun returns immediately so the test does not block on a real modal loop.
static intptr_t WINAPI TimeoutDialogRun(HANDLE hDlg) {
    g_DialogRunCalls++;
    return 0;
}

// AdvControl records that a synchro dispatch was requested by the worker.
static intptr_t WINAPI TimeoutAdvControl(const GUID* ModuleGuid, intptr_t Command, intptr_t Param1, void* Param2) {
    if (Command == ACTL_SYNCHRO) {
        g_SynchroRequested = true;
    }
    return 0;
}

// SendDlgMessage records DM_CLOSE calls so we can verify the dialog was closed.
static std::mutex g_ClosedMutex;
static std::vector<HANDLE> g_ClosedDialogs;
static intptr_t WINAPI TimeoutSendDlgMessage(HANDLE hDlg, intptr_t Msg, intptr_t Param1, void* Param2) {
    if (Msg == DM_CLOSE) {
        std::lock_guard<std::mutex> lock(g_ClosedMutex);
        g_ClosedDialogs.push_back(hDlg);
    }
    return 0;
}

// ===========================================================================
// Command-line UI feedback.
//
// OpenW(OPEN_COMMANDLINE) handles "py:..." commands typed at Far's command
// line and is supposed to show a message box with the result. A previous bug
// computed `showUI = Info->OpenFrom != OPEN_COMMANDLINE` *inside* the
// OPEN_COMMANDLINE branch, so showUI was always false and every message box
// was dead code. These tests install a non-modal hook and assert the message
// is actually emitted.
// ===========================================================================

typedef void (*ShowMessageBoxFn)(const char*, const char*, unsigned int);

static std::mutex g_MsgMutex;
static int g_MsgCount = 0;
static std::string g_LastMsgText;
static std::string g_LastMsgCaption;

static void CapturingMessageHook(const char* text, const char* caption, unsigned int type) {
    std::lock_guard<std::mutex> lock(g_MsgMutex);
    g_MsgCount++;
    g_LastMsgText = text ? text : "";
    g_LastMsgCaption = caption ? caption : "";
}

