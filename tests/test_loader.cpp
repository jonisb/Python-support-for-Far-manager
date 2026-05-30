#include <gtest/gtest.h>
#include <windows.h>
#include <string>
#include "plugin.hpp"

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
