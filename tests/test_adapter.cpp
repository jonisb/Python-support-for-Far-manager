#include <gtest/gtest.h>
#include <windows.h>
#include <string>
#include <vector>
#include <fstream>
#include "plugin.hpp"
#include "common_log.hpp"

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

static std::wstring GetProcessPathEnv() {
    DWORD needed = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    if (needed == 0) return std::wstring();
    std::wstring value(static_cast<size_t>(needed), L'\0');
    DWORD written = GetEnvironmentVariableW(L"PATH", &value[0], needed);
    if (written == 0) return std::wstring();
    value.resize(static_cast<size_t>(written));
    return value;
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

