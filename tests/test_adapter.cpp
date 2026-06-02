#include <gtest/gtest.h>
#include <windows.h>
#include <string>
#include <vector>
#include <fstream>
#include "plugin.hpp"

// Function pointers for adapter.dll exports
typedef BOOL (WINAPI *InitializeFunc)(GlobalInfo* Info);
typedef BOOL (WINAPI *IsPluginFunc)(const wchar_t* FileName);
typedef HANDLE (WINAPI *CreateInstanceFunc)(const wchar_t* FileName);
typedef FARPROC (WINAPI *GetFunctionAddressFunc)(HANDLE Instance, const wchar_t* FunctionName);
typedef BOOL (WINAPI *DestroyInstanceFunc)(HANDLE Instance);
typedef void (WINAPI *FreeFunc)(const ExitInfo* Info);
typedef BOOL (WINAPI *GetErrorFunc)(ErrorInfo* Info);

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
        for (const auto& f : m_tempFiles) {
            DeleteFileW(f.c_str());
        }
        if (freeFunc) {
            ExitInfo ei = { sizeof(ExitInfo) };
            freeFunc(&ei);
        }
        if (hAdapter) {
            FreeLibrary(hAdapter);
        }
    }

    // Write a UTF-8 plugin source to a file next to the test exe. Returns the
    // full path. The file is registered for cleanup.
    std::wstring WritePlugin(const std::wstring& fileName, const std::string& utf8Source) {
        std::wstring path = GetExeDir() + L"\\" + fileName;
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(h, utf8Source.data(), static_cast<DWORD>(utf8Source.size()), &written, nullptr);
            CloseHandle(h);
            m_tempFiles.push_back(path);
        }
        return path;
    }

    std::vector<std::wstring> m_tempFiles;
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

// A valid plugin (one that implements get_plugin_info returning a title) must
// produce a populated PluginInfo: the plugin menu entry uses the title.
TEST_F(AdapterTestFixture, GetPluginInfoW_ValidPlugin_PopulatesMenu) {
    std::wstring pluginFile = GetExeDir() + L"\\test_plugin.far.py";

    HANDLE instance = createInstanceFunc(pluginFile.c_str());
    ASSERT_NE(instance, nullptr) << "Failed to create plugin instance";
    ASSERT_NE(instance, INVALID_HANDLE_VALUE) << "Failed to create plugin instance (invalid handle)";

    typedef void (WINAPI *GetPluginInfoWFunc)(PluginInfo*);
    GetPluginInfoWFunc getPluginInfoW =
        reinterpret_cast<GetPluginInfoWFunc>(getFunctionAddressFunc(instance, L"GetPluginInfoW"));
    ASSERT_NE(getPluginInfoW, nullptr) << "Failed to get GetPluginInfoW function address";

    PluginInfo info;
    memset(&info, 0xCD, sizeof(info));
    info.Instance = instance;

    getPluginInfoW(&info);

    EXPECT_EQ(info.StructSize, sizeof(PluginInfo));
    // test_plugin.far.py provides a title, so the plugin menu must have one entry.
    EXPECT_EQ(info.PluginMenu.Count, static_cast<size_t>(1)) << "Expected a populated plugin menu";
    ASSERT_NE(info.PluginMenu.Strings, nullptr);
    EXPECT_NE(info.PluginMenu.Strings[0], nullptr);

    BOOL destroyed = destroyInstanceFunc(instance);
    EXPECT_TRUE(destroyed) << "Failed to destroy instance";
}

// A plugin that does NOT implement get_plugin_info() must fail gracefully:
// GetPluginInfoW must not crash, must leave a safe default PluginInfo, and the
// failure must be retrievable through the adapter's GetError export.
TEST_F(AdapterTestFixture, GetPluginInfoW_MissingGetPluginInfo_ReportsError) {
    // Write a minimal plugin that lacks get_plugin_info() next to the test exe.
    std::wstring pluginFile = GetExeDir() + L"\\broken_no_info.far.py";
    {
        std::wofstream f(pluginFile);
        ASSERT_TRUE(f.is_open()) << "Could not create temp plugin file";
        f << L"class Plugin:\n"
          << L"    def __init__(self, psi_ptr=None):\n"
          << L"        pass\n"
          << L"    def OpenW(self, info_ptr):\n"
          << L"        return 1\n";
    }

    HANDLE instance = createInstanceFunc(pluginFile.c_str());
    ASSERT_NE(instance, nullptr) << "Failed to create plugin instance";
    ASSERT_NE(instance, INVALID_HANDLE_VALUE);

    typedef void (WINAPI *GetPluginInfoWFunc)(PluginInfo*);
    GetPluginInfoWFunc getPluginInfoW =
        reinterpret_cast<GetPluginInfoWFunc>(getFunctionAddressFunc(instance, L"GetPluginInfoW"));
    ASSERT_NE(getPluginInfoW, nullptr);

    PluginInfo info;
    memset(&info, 0xCD, sizeof(info));
    info.Instance = instance;

    // Must not crash, and must leave a safe (empty) default PluginInfo.
    getPluginInfoW(&info);
    EXPECT_EQ(info.StructSize, sizeof(PluginInfo));
    EXPECT_EQ(info.PluginMenu.Count, static_cast<size_t>(0));
    EXPECT_EQ(info.DiskMenu.Count, static_cast<size_t>(0));
    EXPECT_EQ(info.PluginConfig.Count, static_cast<size_t>(0));

    // The failure must be reported via GetError.
    GetErrorFunc getErrorFunc =
        reinterpret_cast<GetErrorFunc>(GetProcAddress(hAdapter, "adapter_GetError"));
    ASSERT_NE(getErrorFunc, nullptr) << "adapter_GetError export missing";

    ErrorInfo err = {};
    err.StructSize = sizeof(ErrorInfo);
    BOOL hasError = getErrorFunc(&err);
    EXPECT_TRUE(hasError) << "GetError should report the missing get_plugin_info failure";
    if (hasError) {
        ASSERT_NE(err.Summary, nullptr);
        EXPECT_NE(std::wstring(err.Summary).find(L"get_plugin_info"), std::wstring::npos)
            << "Error summary should mention get_plugin_info";
    }

    BOOL destroyed = destroyInstanceFunc(instance);
    EXPECT_TRUE(destroyed) << "Failed to destroy instance";

    DeleteFileW(pluginFile.c_str());
}

// A plugin whose get_plugin_info() raises must report the failure (with a
// Python traceback) via GetError, and must not crash.
TEST_F(AdapterTestFixture, GetPluginInfoW_RaisingGetPluginInfo_ReportsTraceback) {
    std::wstring pluginFile = GetExeDir() + L"\\broken_raises.far.py";
    {
        std::wofstream f(pluginFile);
        ASSERT_TRUE(f.is_open());
        f << L"class Plugin:\n"
          << L"    def __init__(self, psi_ptr=None):\n"
          << L"        pass\n"
          << L"    def get_plugin_info(self):\n"
          << L"        raise ValueError('boom in get_plugin_info')\n";
    }

    HANDLE instance = createInstanceFunc(pluginFile.c_str());
    ASSERT_NE(instance, nullptr);
    ASSERT_NE(instance, INVALID_HANDLE_VALUE);

    typedef void (WINAPI *GetPluginInfoWFunc)(PluginInfo*);
    GetPluginInfoWFunc getPluginInfoW =
        reinterpret_cast<GetPluginInfoWFunc>(getFunctionAddressFunc(instance, L"GetPluginInfoW"));
    ASSERT_NE(getPluginInfoW, nullptr);

    PluginInfo info;
    memset(&info, 0xCD, sizeof(info));
    info.Instance = instance;
    getPluginInfoW(&info);  // must not crash
    EXPECT_EQ(info.PluginMenu.Count, static_cast<size_t>(0));

    GetErrorFunc getErrorFunc =
        reinterpret_cast<GetErrorFunc>(GetProcAddress(hAdapter, "adapter_GetError"));
    ASSERT_NE(getErrorFunc, nullptr);

    ErrorInfo err = {};
    err.StructSize = sizeof(ErrorInfo);
    BOOL hasError = getErrorFunc(&err);
    EXPECT_TRUE(hasError) << "GetError should report the raised exception";
    if (hasError && err.Description) {
        std::wstring desc(err.Description);
        // The captured traceback should mention the exception text.
        EXPECT_NE(desc.find(L"boom in get_plugin_info"), std::wstring::npos)
            << "Error description should contain the Python traceback";
    }

    BOOL destroyed = destroyInstanceFunc(instance);
    EXPECT_TRUE(destroyed);

    DeleteFileW(pluginFile.c_str());
}

namespace {
// A complete, valid simple plugin source (UTF-8). Title is parameterized.
std::string ValidPluginSource(const char* title) {
    std::string t = title;
    return
        "class Plugin:\n"
        "    def __init__(self, psi_ptr=None):\n"
        "        pass\n"
        "    def get_plugin_info(self):\n"
        "        return {'title': '" + t + "', 'description': 'd', 'author': 'a', 'version': '1.0.0.0'}\n"
        "    def OpenW(self, info_ptr):\n"
        "        return 1\n";
}
} // namespace

// IsPlugin must accept a real .far.py file and reject a wrong extension.
TEST_F(AdapterTestFixture, IsPlugin_AcceptsFarPyRejectsOthers) {
    std::wstring good = WritePlugin(L"isplugin_ok.far.py", ValidPluginSource("Ok"));
    EXPECT_TRUE(isPluginFunc(good.c_str())) << ".far.py should be recognized";

    std::wstring wrongExt = WritePlugin(L"isplugin_wrong.txt", ValidPluginSource("Wrong"));
    EXPECT_FALSE(isPluginFunc(wrongExt.c_str())) << ".txt should not be a plugin";
}

// Creating a plugin from a path containing non-ASCII characters must work.
// This is a regression guard for the UTF-8 -> UTF-16 path conversion bug.
TEST_F(AdapterTestFixture, CreateInstance_NonAsciiPathLoads) {
    // File name contains Cyrillic "Привет" (UTF-16 literal -> on-disk name).
    const wchar_t fileName[] = {
        0x041F, 0x0440, 0x0438, 0x0432, 0x0435, 0x0442,
        L'.', L'f', L'a', L'r', L'.', L'p', L'y', 0
    };
    std::wstring path = WritePlugin(fileName, ValidPluginSource("Cyrillic Plugin"));

    EXPECT_TRUE(isPluginFunc(path.c_str()));
    HANDLE instance = createInstanceFunc(path.c_str());
    ASSERT_NE(instance, nullptr) << "Failed to load plugin from non-ASCII path";
    ASSERT_NE(instance, INVALID_HANDLE_VALUE);

    typedef void (WINAPI *GetPluginInfoWFunc)(PluginInfo*);
    GetPluginInfoWFunc getPluginInfoW =
        reinterpret_cast<GetPluginInfoWFunc>(getFunctionAddressFunc(instance, L"GetPluginInfoW"));
    ASSERT_NE(getPluginInfoW, nullptr);

    PluginInfo info;
    memset(&info, 0xCD, sizeof(info));
    info.Instance = instance;
    getPluginInfoW(&info);
    EXPECT_EQ(info.PluginMenu.Count, static_cast<size_t>(1)) << "Valid plugin should populate menu";

    EXPECT_TRUE(destroyInstanceFunc(instance));
}

// A get_plugin_info() that returns a dict without a usable title must be
// reported as an error (a plugin name is the minimum required metadata).
TEST_F(AdapterTestFixture, GetPluginInfoW_EmptyTitle_ReportsError) {
    std::wstring path = WritePlugin(L"empty_title.far.py",
        "class Plugin:\n"
        "    def __init__(self, psi_ptr=None):\n"
        "        pass\n"
        "    def get_plugin_info(self):\n"
        "        return {'title': '', 'description': 'no name'}\n");

    HANDLE instance = createInstanceFunc(path.c_str());
    ASSERT_NE(instance, nullptr);
    ASSERT_NE(instance, INVALID_HANDLE_VALUE);

    typedef void (WINAPI *GetPluginInfoWFunc)(PluginInfo*);
    GetPluginInfoWFunc getPluginInfoW =
        reinterpret_cast<GetPluginInfoWFunc>(getFunctionAddressFunc(instance, L"GetPluginInfoW"));
    ASSERT_NE(getPluginInfoW, nullptr);

    PluginInfo info;
    memset(&info, 0xCD, sizeof(info));
    info.Instance = instance;
    getPluginInfoW(&info);
    EXPECT_EQ(info.PluginMenu.Count, static_cast<size_t>(0)) << "Empty title must not populate menu";

    GetErrorFunc getErrorFunc =
        reinterpret_cast<GetErrorFunc>(GetProcAddress(hAdapter, "adapter_GetError"));
    ASSERT_NE(getErrorFunc, nullptr);
    ErrorInfo err = {};
    err.StructSize = sizeof(ErrorInfo);
    EXPECT_TRUE(getErrorFunc(&err)) << "Empty title should be reported via GetError";

    EXPECT_TRUE(destroyInstanceFunc(instance));
}

// GetError consumes the error: a second call (with no new error) returns FALSE.
TEST_F(AdapterTestFixture, GetError_ClearsAfterRead) {
    std::wstring path = WritePlugin(L"no_info_clear.far.py",
        "class Plugin:\n"
        "    def __init__(self, psi_ptr=None):\n"
        "        pass\n");

    HANDLE instance = createInstanceFunc(path.c_str());
    ASSERT_NE(instance, nullptr);
    ASSERT_NE(instance, INVALID_HANDLE_VALUE);

    typedef void (WINAPI *GetPluginInfoWFunc)(PluginInfo*);
    GetPluginInfoWFunc getPluginInfoW =
        reinterpret_cast<GetPluginInfoWFunc>(getFunctionAddressFunc(instance, L"GetPluginInfoW"));
    ASSERT_NE(getPluginInfoW, nullptr);

    PluginInfo info;
    memset(&info, 0xCD, sizeof(info));
    info.Instance = instance;
    getPluginInfoW(&info);  // triggers an error (no get_plugin_info)

    GetErrorFunc getErrorFunc =
        reinterpret_cast<GetErrorFunc>(GetProcAddress(hAdapter, "adapter_GetError"));
    ASSERT_NE(getErrorFunc, nullptr);

    ErrorInfo err1 = {}; err1.StructSize = sizeof(ErrorInfo);
    EXPECT_TRUE(getErrorFunc(&err1)) << "First GetError should report the error";

    ErrorInfo err2 = {}; err2.StructSize = sizeof(ErrorInfo);
    EXPECT_FALSE(getErrorFunc(&err2)) << "Second GetError should be cleared (no new error)";

    EXPECT_TRUE(destroyInstanceFunc(instance));
}

// Two independent instances can coexist and report distinct titles.
TEST_F(AdapterTestFixture, MultipleInstances_AreIndependent) {
    std::wstring pathA = WritePlugin(L"multi_a.far.py", ValidPluginSource("Plugin Alpha"));
    std::wstring pathB = WritePlugin(L"multi_b.far.py", ValidPluginSource("Plugin Beta"));

    HANDLE a = createInstanceFunc(pathA.c_str());
    HANDLE b = createInstanceFunc(pathB.c_str());
    ASSERT_NE(a, nullptr); ASSERT_NE(a, INVALID_HANDLE_VALUE);
    ASSERT_NE(b, nullptr); ASSERT_NE(b, INVALID_HANDLE_VALUE);
    EXPECT_NE(a, b) << "Distinct plugins should yield distinct instances";

    typedef void (WINAPI *GetPluginInfoWFunc)(PluginInfo*);
    auto callInfo = [&](HANDLE inst, PluginInfo& out) {
        GetPluginInfoWFunc fn = reinterpret_cast<GetPluginInfoWFunc>(
            getFunctionAddressFunc(inst, L"GetPluginInfoW"));
        ASSERT_NE(fn, nullptr);
        memset(&out, 0xCD, sizeof(out));
        out.Instance = inst;
        fn(&out);
    };

    PluginInfo infoA, infoB;
    callInfo(a, infoA);
    callInfo(b, infoB);

    ASSERT_EQ(infoA.PluginMenu.Count, static_cast<size_t>(1));
    ASSERT_EQ(infoB.PluginMenu.Count, static_cast<size_t>(1));
    EXPECT_STREQ(infoA.PluginMenu.Strings[0], L"Plugin Alpha");
    EXPECT_STREQ(infoB.PluginMenu.Strings[0], L"Plugin Beta");

    EXPECT_TRUE(destroyInstanceFunc(a));
    EXPECT_TRUE(destroyInstanceFunc(b));
}

// Destroying a null instance must be handled gracefully (no crash).
TEST_F(AdapterTestFixture, DestroyInstance_NullIsSafe) {
    destroyInstanceFunc(nullptr);
    SUCCEED();
}

// Regression: non-ASCII plugin metadata (title/description/author) read in the
// PluginModule constructor must be decoded from UTF-8 to UTF-16 correctly, not
// byte-widened. GetGlobalInfoW exposes m_Title etc., so we verify the exact
// wide string round-trips.
TEST_F(AdapterTestFixture, GetGlobalInfoW_NonAsciiMetadataDecodedCorrectly) {
    // The Python source declares title/description/author with Cyrillic text.
    // Source bytes are UTF-8 (Python defaults to UTF-8 for .py files).
    //   title       = "Заголовок"  (U+0417 0430 0433 043E 043B 043E 0432 043E 043A)
    //   author      = "Автор"      (U+0410 0432 0442 043E 0440)
    // The string literals below are the UTF-8 byte encodings of those.
    std::string source =
        "class Plugin:\n"
        "    title = '\xD0\x97\xD0\xB0\xD0\xB3\xD0\xBE\xD0\xBB\xD0\xBE\xD0\xB2\xD0\xBE\xD0\xBA'\n"  // Заголовок
        "    description = 'desc'\n"
        "    author = '\xD0\x90\xD0\xB2\xD1\x82\xD0\xBE\xD1\x80'\n"  // Автор
        "    version = (1, 2, 3, 4)\n"
        "    def __init__(self, psi_ptr=None):\n"
        "        pass\n"
        "    def get_plugin_info(self):\n"
        "        return {'title': self.title}\n";
    std::wstring path = WritePlugin(L"nonascii_meta.far.py", source);

    HANDLE instance = createInstanceFunc(path.c_str());
    ASSERT_NE(instance, nullptr);
    ASSERT_NE(instance, INVALID_HANDLE_VALUE);

    typedef void (WINAPI *GetGlobalInfoWFunc)(GlobalInfo*);
    GetGlobalInfoWFunc getGlobalInfoW =
        reinterpret_cast<GetGlobalInfoWFunc>(getFunctionAddressFunc(instance, L"GetGlobalInfoW"));
    ASSERT_NE(getGlobalInfoW, nullptr);

    GlobalInfo gi;
    memset(&gi, 0, sizeof(gi));
    gi.StructSize = sizeof(GlobalInfo);
    gi.Instance = instance;  // the wrapper resolves the PluginModule via Instance
    getGlobalInfoW(&gi);

    // Expected UTF-16 code points (NOT byte-widened UTF-8).
    const wchar_t expectedTitle[] = {
        0x0417, 0x0430, 0x0433, 0x043E, 0x043B, 0x043E, 0x0432, 0x043E, 0x043A, 0
    };
    const wchar_t expectedAuthor[] = { 0x0410, 0x0432, 0x0442, 0x043E, 0x0440, 0 };

    ASSERT_NE(gi.Title, nullptr);
    EXPECT_STREQ(gi.Title, expectedTitle) << "Title must be valid UTF-16, not byte-widened UTF-8";
    ASSERT_NE(gi.Author, nullptr);
    EXPECT_STREQ(gi.Author, expectedAuthor) << "Author must be valid UTF-16";

    // Length sanity: byte-widening would have produced 18 chars for the title
    // (9 code points * 2 UTF-8 bytes), the correct value is 9.
    EXPECT_EQ(wcslen(gi.Title), static_cast<size_t>(9));

    EXPECT_TRUE(destroyInstanceFunc(instance));
}

// Regression: when a plugin module fails to execute (raises at import time)
// with a non-ASCII error message, CreatePluginModule records the message in
// the adapter error description. That UTF-8 message must be decoded to UTF-16
// correctly (via MultiByteToWideChar), not byte-widened.
TEST_F(AdapterTestFixture, CreateInstance_NonAsciiImportError_DecodedCorrectly) {
    // Module-level `raise` with a Cyrillic message "Ошибка" (U+041E 0448 0438
    // 0431 043A 0430). Source bytes below are its UTF-8 encoding.
    std::string source =
        "raise RuntimeError('\xD0\x9E\xD1\x88\xD0\xB8\xD0\xB1\xD0\xBA\xD0\xB0')\n";  // Ошибка
    std::wstring path = WritePlugin(L"import_error_nonascii.far.py", source);

    HANDLE instance = createInstanceFunc(path.c_str());
    EXPECT_EQ(instance, nullptr) << "Module that raises at import must fail to load";

    GetErrorFunc getErrorFunc =
        reinterpret_cast<GetErrorFunc>(GetProcAddress(hAdapter, "adapter_GetError"));
    ASSERT_NE(getErrorFunc, nullptr);

    ErrorInfo err = {};
    err.StructSize = sizeof(ErrorInfo);
    ASSERT_TRUE(getErrorFunc(&err)) << "GetError should report the import failure";
    ASSERT_NE(err.Description, nullptr);

    // The description must contain the correctly-decoded Cyrillic substring.
    const wchar_t expected[] = { 0x041E, 0x0448, 0x0438, 0x0431, 0x043A, 0x0430, 0 };
    std::wstring desc(err.Description);
    EXPECT_NE(desc.find(expected), std::wstring::npos)
        << "Error description must contain valid UTF-16 'Ошибка', not byte-widened UTF-8";
}
