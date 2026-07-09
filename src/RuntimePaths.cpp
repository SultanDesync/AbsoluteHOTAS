#include "PCH.h"

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

    std::filesystem::path UserIniPath()
    {
        return PluginDirectory() / L"AbsoluteHOTAS_User.ini";
    }

    std::filesystem::path MacrosIniPath()
    {
        return PluginDirectory() / L"AbsoluteHOTAS_Macros.ini";
    }

    std::filesystem::path ProfilesDir()
    {
        return PluginDirectory() / L"Profiles";
    }

    std::filesystem::path LogPath()
    {
        return PluginDirectory() / L"AbsoluteHOTAS.log";
    }

    static bool g_loggingEnabled = false;

    bool IsLoggingEnabled()
    {
        return g_loggingEnabled;
    }

    void InitLogging()
    {
        // Deliberately uses the Win32 GetPrivateProfile* API rather than SimpleIni:
        // this runs at plugin-load bootstrap, before the controller/config subsystem
        // (which owns the CSimpleIniA instance) exists. Reading one bool here with the
        // OS INI reader keeps logging available from the very first line of startup.
        // Do not "unify" this onto SimpleIni — there is no config object yet.
        wchar_t value[32]{};
        GetPrivateProfileStringW(L"Injection", L"bEnableLog", L"0", value, static_cast<DWORD>(std::size(value)), IniPath().c_str());

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

        g_loggingEnabled = (normalized == L"1" || normalized == L"true" || normalized == L"yes" || normalized == L"on");
    }

    // Rotate a stale log once per session, before the first line is appended, so a
    // long-lived install does not grow the file without bound.
    static void RotateIfLarge()
    {
        static bool checked = false;
        if (checked) return;
        checked = true;

        const auto logPath    = LogPath();
        const auto oldLogPath = PluginDirectory() / L"AbsoluteHOTAS.log.old";
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (GetFileAttributesExW(logPath.c_str(), GetFileExInfoStandard, &data)) {
            ULARGE_INTEGER size{};
            size.HighPart = data.nFileSizeHigh;
            size.LowPart  = data.nFileSizeLow;
            if (size.QuadPart > 1024ull * 1024ull) {
                DeleteFileW(oldLogPath.c_str());
                MoveFileExW(logPath.c_str(), oldLogPath.c_str(), MOVEFILE_REPLACE_EXISTING);
            }
        }
    }

    void Log(const char* a_tag, const std::string& a_message)
    {
        if (!g_loggingEnabled) return;

        RotateIfLarge();

        std::ofstream log(LogPath(), std::ios::app);
        if (log.is_open()) {
            log << a_tag << ' ' << a_message << '\n';
            log.flush();
        }
    }
}
