#include "RuntimePaths.h"

#include <windows.h>
#include <fstream>
#include <cwctype>

namespace {
    std::filesystem::path ModulePath()
    {
        HMODULE module = nullptr;
        GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&ModulePath),
            &module);

        wchar_t path[MAX_PATH]{};
        if (module && GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)))) {
            return std::filesystem::path(path);
        }

        return std::filesystem::current_path() / L"Data" / L"SFSE" / L"Plugins" / L"AbsoluteHOTAS.dll";
    }
}

namespace RuntimePaths {
    std::filesystem::path PluginDirectory()
    {
        return ModulePath().parent_path();
    }

    std::filesystem::path IniPath()
    {
        return PluginDirectory() / L"AbsoluteHOTAS.ini";
    }

    std::filesystem::path LogPath()
    {
        return PluginDirectory() / L"AbsoluteHOTAS.log";
    }

    static bool g_fileLoggingEnabled = false;

    bool IsFileLoggingEnabled()
    {
        return g_fileLoggingEnabled;
    }

    void EnableFileLogging()
    {
        wchar_t value[32]{};
        GetPrivateProfileStringW(L"Injection", L"bLogThrottle", L"0", value, static_cast<DWORD>(std::size(value)), IniPath().c_str());

        std::wstring_view text{ value };
        while (!text.empty() && std::iswspace(text.front())) {
            text.remove_prefix(1);
        }
        while (!text.empty() && std::iswspace(text.back())) {
            text.remove_suffix(1);
        }

        std::wstring normalized;
        normalized.reserve(text.size());
        for (const auto ch : text) {
            normalized.push_back(static_cast<wchar_t>(std::towlower(ch)));
        }

        g_fileLoggingEnabled = (normalized == L"1" || normalized == L"true" || normalized == L"yes" || normalized == L"on");
    }

    void AppendLogAlways(const char* a_prefix, const std::string& a_message)
    {
        std::ofstream log(LogPath(), std::ios::app);
        if (log.is_open()) {
            log << a_prefix << ' ' << a_message << '\n';
            log.flush();
        }
    }

    void AppendLog(const char* a_prefix, const std::string& a_message)
    {
        if (!g_fileLoggingEnabled) return;
        AppendLogAlways(a_prefix, a_message);
    }
}
