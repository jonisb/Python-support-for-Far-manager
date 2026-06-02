#include <gtest/gtest.h>
#include <windows.h>
#include <string>
#include "plugin.hpp"

// Function pointers for adapter.dll exports
typedef BOOL (WINAPI *InitializeFunc)(GlobalInfo* Info);
typedef BOOL (WINAPI *IsPluginFunc)(const wchar_t* FileName);
typedef HANDLE (WINAPI *CreateInstanceFunc)(const wchar_t* FileName);
typedef FARPROC (WINAPI *GetFunctionAddressFunc)(HANDLE Instance, const wchar_t* FunctionName);
typedef BOOL (WINAPI *DestroyInstanceFunc)(HANDLE Instance);
typedef void (WINAPI *FreeFunc)(const ExitInfo* Info);

// Returns the directory containing this test executable (no trailing slash).
static std::wstring GetExeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf);
    size_t slash = path.find_last_of(L"\\/");
    return (slash != std::wstring::npos) ? path.substr(0, slash) : L".";
}

class AdapterTestFixture : public ::testing::Test {
protected:
    HMODULE hAdapter = nullptr;
    InitializeFunc initFunc = nullptr;
    IsPluginFunc isPluginFunc = nullptr;
    CreateInstanceFunc createInstanceFunc = nullptr;
    GetFunctionAddressFunc getFunctionAddressFunc = nullptr;
    DestroyInstanceFunc destroyInstanceFunc = nullptr;
    FreeFunc freeFunc = nullptr;

    void SetUp() override {
        hAdapter = LoadLibraryW(L"PythonFar.adapter.dll");
        ASSERT_NE(hAdapter, nullptr) << "Failed to load PythonFar.adapter.dll";

        initFunc = reinterpret_cast<InitializeFunc>(GetProcAddress(hAdapter, "adapter_Initialize"));
        isPluginFunc = reinterpret_cast<IsPluginFunc>(GetProcAddress(hAdapter, "adapter_IsPlugin"));
        createInstanceFunc = reinterpret_cast<CreateInstanceFunc>(GetProcAddress(hAdapter, "adapter_CreateInstance"));
        getFunctionAddressFunc = reinterpret_cast<GetFunctionAddressFunc>(GetProcAddress(hAdapter, "adapter_GetFunctionAddress"));
        destroyInstanceFunc = reinterpret_cast<DestroyInstanceFunc>(GetProcAddress(hAdapter, "adapter_DestroyInstance"));
        freeFunc = reinterpret_cast<FreeFunc>(GetProcAddress(hAdapter, "adapter_Free"));

        ASSERT_NE(initFunc, nullptr);
        ASSERT_NE(isPluginFunc, nullptr);
        ASSERT_NE(createInstanceFunc, nullptr);
        ASSERT_NE(getFunctionAddressFunc, nullptr);
        ASSERT_NE(destroyInstanceFunc, nullptr);
        ASSERT_NE(freeFunc, nullptr);

        GlobalInfo gi = { sizeof(GlobalInfo) };
        ASSERT_TRUE(initFunc(&gi)) << "Adapter failed to initialize";
    }

    void TearDown() override {
        if (freeFunc) {
            ExitInfo ei = { sizeof(ExitInfo) };
            freeFunc(&ei);
        }
        if (hAdapter) {
            FreeLibrary(hAdapter);
        }
    }
};

TEST_F(AdapterTestFixture, IsPlugin_InvalidFile) {
    BOOL isPlugin = isPluginFunc(L"nonexistent_plugin.py");
    EXPECT_FALSE(isPlugin) << "IsPlugin should return false for nonexistent file";
}

TEST_F(AdapterTestFixture, CreateAndDestroyInstance) {
    // Build an absolute path to test_plugin.far.py next to the test executable
    // so the test is not sensitive to the current working directory.
    std::wstring pluginFile = GetExeDir() + L"\\test_plugin.far.py";

    BOOL isPlugin = isPluginFunc(pluginFile.c_str());
    EXPECT_TRUE(isPlugin) << "IsPlugin failed to recognize the test script";

    HANDLE instance = createInstanceFunc(pluginFile.c_str());
    ASSERT_NE(instance, nullptr) << "Failed to create plugin instance";
    ASSERT_NE(instance, INVALID_HANDLE_VALUE) << "Failed to create plugin instance (invalid handle)";

    // Get a known function address
    FARPROC getPluginInfo = getFunctionAddressFunc(instance, L"GetPluginInfoW");
    EXPECT_NE(getPluginInfo, nullptr) << "Failed to get GetPluginInfoW function address";

    FARPROC openW = getFunctionAddressFunc(instance, L"OpenW");
    EXPECT_NE(openW, nullptr) << "Failed to get OpenW function address";

    // Test a non-existent function
    FARPROC nonexistent = getFunctionAddressFunc(instance, L"NonExistentFunction");
    EXPECT_EQ(nonexistent, nullptr) << "Should return null for non-existent function";

    BOOL destroyed = destroyInstanceFunc(instance);
    EXPECT_TRUE(destroyed) << "Failed to destroy instance";
}
