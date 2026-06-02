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

TEST_F(LoaderBridgesTest, BridgesDeepCopyStruct) {
    // Setup dummy structs
    FarStandardFunctions fsf = { sizeof(FarStandardFunctions) };
    PluginStartupInfo psi = { sizeof(PluginStartupInfo) };
    psi.FSF = &fsf;
    psi.PanelControl = reinterpret_cast<FARAPIPANELCONTROL>(DummyPanelControl);
    psi.AdvControl = reinterpret_cast<FARAPIADVCONTROL>(DummyAdvControl);
    psi.DialogInit = reinterpret_cast<FARAPIDIALOGINIT>(DummyDialogInit);

    // Load it
    setStartupInfoW(&psi);

    // Destroy local memory
    psi.PanelControl = nullptr;
    psi.AdvControl = nullptr;
    psi.DialogInit = nullptr;
    psi.FSF = nullptr;

    // Call the bridges
    intptr_t result1 = panelControlBridge(nullptr, 0, 0, nullptr);
    EXPECT_TRUE(g_DummyPanelControlCalled) << "PanelControl bridge failed";
    EXPECT_EQ(result1, 42);

    intptr_t result2 = advControlBridge(nullptr, 0, 0, nullptr);
    EXPECT_TRUE(g_DummyAdvControlCalled) << "AdvControl bridge failed";
    EXPECT_EQ(result2, 43);

    HANDLE result3 = dialogInitBridge(nullptr, nullptr, 0, 0, 0, 0, nullptr, nullptr, 0, 0, 0, nullptr, nullptr);
    EXPECT_TRUE(g_DummyDialogInitCalled) << "DialogInit bridge failed";
    EXPECT_EQ(reinterpret_cast<intptr_t>(result3), 44);
}

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

class LoaderDialogTimeoutTest : public ::testing::Test {
protected:
    HMODULE hLoader = nullptr;
    void (WINAPI *setStartupInfoW)(const PluginStartupInfo*) = nullptr;
    intptr_t (WINAPI *dialogRunWithTimeout)(const GUID*, HANDLE, unsigned long) = nullptr;
    intptr_t (WINAPI *processSynchroEventW)(const ProcessSynchroEventInfo*) = nullptr;

    void SetUp() override {
        hLoader = LoadLibraryW(L"PythonFar.dll");
        ASSERT_NE(hLoader, nullptr) << "Failed to load PythonFar.dll";

        setStartupInfoW = reinterpret_cast<void (WINAPI*)(const PluginStartupInfo*)>(
            GetProcAddress(hLoader, "SetStartupInfoW"));
        dialogRunWithTimeout = reinterpret_cast<intptr_t (WINAPI*)(const GUID*, HANDLE, unsigned long)>(
            GetProcAddress(hLoader, "PythonFar_DialogRunWithTimeout"));
        processSynchroEventW = reinterpret_cast<intptr_t (WINAPI*)(const ProcessSynchroEventInfo*)>(
            GetProcAddress(hLoader, "ProcessSynchroEventW"));

        ASSERT_NE(setStartupInfoW, nullptr);
        ASSERT_NE(dialogRunWithTimeout, nullptr);
        ASSERT_NE(processSynchroEventW, nullptr);

        g_SynchroRequested = false;
        g_DialogRunCalls = 0;
        {
            std::lock_guard<std::mutex> lock(g_ClosedMutex);
            g_ClosedDialogs.clear();
        }

        // Install dummy Far API pointers.
        static FarStandardFunctions fsf = { sizeof(FarStandardFunctions) };
        PluginStartupInfo psi = { sizeof(PluginStartupInfo) };
        psi.FSF = &fsf;
        psi.DialogRun = reinterpret_cast<FARAPIDIALOGRUN>(TimeoutDialogRun);
        psi.AdvControl = reinterpret_cast<FARAPIADVCONTROL>(TimeoutAdvControl);
        psi.SendDlgMessage = reinterpret_cast<FARAPISENDDLGMESSAGE>(TimeoutSendDlgMessage);
        setStartupInfoW(&psi);
    }

    void TearDown() override {
        if (hLoader) FreeLibrary(hLoader);
    }
};

TEST_F(LoaderDialogTimeoutTest, TimeoutRequestsSynchroAndClosesDialog) {
    HANDLE fakeDlg = reinterpret_cast<HANDLE>(0xABCD);
    GUID pluginId = PythonFar::LOADER_GUID;

    // Short timeout so the worker fires quickly.
    intptr_t runResult = dialogRunWithTimeout(&pluginId, fakeDlg, 50 /* ms */);
    EXPECT_EQ(runResult, 0) << "DialogRun should have been invoked";
    EXPECT_GE(g_DialogRunCalls.load(), 1);

    // Wait for the worker thread to fire (timeout + margin).
    for (int i = 0; i < 100 && !g_SynchroRequested.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(g_SynchroRequested.load()) << "Worker thread did not request ACTL_SYNCHRO";

    // Simulate Far dispatching the synchro event back to the plugin.
    ProcessSynchroEventInfo info = { sizeof(ProcessSynchroEventInfo) };
    info.Event = SE_COMMONSYNCHRO;
    info.Param = nullptr;
    processSynchroEventW(&info);

    // The pending dialog should have been closed via DM_CLOSE.
    std::lock_guard<std::mutex> lock(g_ClosedMutex);
    ASSERT_EQ(g_ClosedDialogs.size(), static_cast<size_t>(1));
    EXPECT_EQ(g_ClosedDialogs[0], fakeDlg);
}

TEST_F(LoaderDialogTimeoutTest, SynchroWithNoPendingDialogsClosesNothing) {
    // No DialogRunWithTimeout call -> the pending set is empty.
    ProcessSynchroEventInfo info = { sizeof(ProcessSynchroEventInfo) };
    info.Event = SE_COMMONSYNCHRO;
    info.Param = nullptr;
    processSynchroEventW(&info);

    std::lock_guard<std::mutex> lock(g_ClosedMutex);
    EXPECT_TRUE(g_ClosedDialogs.empty()) << "Nothing should be closed when no dialogs pend";
}

TEST_F(LoaderDialogTimeoutTest, ZeroTimeoutDoesNotScheduleClose) {
    HANDLE fakeDlg = reinterpret_cast<HANDLE>(0x1234);
    GUID pluginId = PythonFar::LOADER_GUID;

    // TimeoutMs == 0 means "no auto-close worker" per the implementation.
    dialogRunWithTimeout(&pluginId, fakeDlg, 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(g_SynchroRequested.load()) << "Zero timeout must not request synchro";

    ProcessSynchroEventInfo info = { sizeof(ProcessSynchroEventInfo) };
    info.Event = SE_COMMONSYNCHRO;
    processSynchroEventW(&info);

    std::lock_guard<std::mutex> lock(g_ClosedMutex);
    EXPECT_TRUE(g_ClosedDialogs.empty());
}
