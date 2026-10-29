#include "SupportReport.h"
#include "RuntimePaths.h"

#include <windows.h>
#include <chrono>
#include <fstream>
#include <format>
#include <mutex>

namespace {
std::mutex g_mutex;
std::mutex g_exportMutex;
std::string g_startup{"Startup versions unavailable.\n"};
std::string g_runtime{"No controller snapshot has been published.\n"};
std::string g_status{"No support report exported this session."};
std::chrono::steady_clock::time_point g_published{};

void SetStatus(std::string text)
{
    std::scoped_lock lock(g_mutex);
    g_status = std::move(text);
}
}

namespace SupportReport {

void SetStartup(std::string text)
{
    std::scoped_lock lock(g_mutex);
    g_startup = std::move(text);
}

void PublishRuntime(std::string text)
{
    std::scoped_lock lock(g_mutex);
    g_runtime = std::move(text);
    g_published = std::chrono::steady_clock::now();
}

std::string Status()
{
    std::scoped_lock lock(g_mutex);
    return g_status;
}

bool ExportTo(const std::filesystem::path& directory,
              std::string_view frontendStatus) noexcept
{
    std::scoped_lock exportLock(g_exportMutex);
    try {
        std::string startup, runtime;
        long long age;
        {
            std::scoped_lock lock(g_mutex);
            startup = g_startup;
            runtime = g_runtime;
            age = g_published == std::chrono::steady_clock::time_point{}
                ? -1LL : std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - g_published).count();
        }
        const auto target = directory / L"AbsoluteHOTAS-support.txt";
        const auto temporary = directory / L"AbsoluteHOTAS-support.txt.tmp";
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            SetStatus("Report failed: cannot write in the SFSE/Plugins directory.");
            return false;
        }
        const auto now = std::chrono::system_clock::now();
        stream << "AbsoluteHOTAS support report\n"
               << std::format("Exported (UTC): {:%Y-%m-%d %H:%M:%S}\n", now)
               << startup << '\n' << frontendStatus << '\n'
               << "Controller snapshot age (ms; -1 = unavailable): " << age << '\n'
               << runtime
               << "\nUser-triggered diagnostic snapshot. Configuration files and logs are not embedded.\n";
        stream.flush();
        const bool written = stream.good();
        stream.close();
        if (!written || stream.fail()) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            SetStatus("Report failed: the file could not be completely written.");
            return false;
        }
        if (!MoveFileExW(temporary.c_str(), target.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            const auto error = GetLastError();
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            SetStatus(std::format("Report failed: Windows could not replace the file (error {}).", error));
            return false;
        }
        SetStatus("Saved SFSE/Plugins/AbsoluteHOTAS-support.txt (MO2 may place it in Overwrite).");
        return true;
    } catch (...) {
        SetStatus("Report failed: the destination or snapshot could not be written.");
        return false;
    }
}

bool Export(std::string_view frontendStatus) noexcept
{
    try {
        return ExportTo(RuntimePaths::PluginDirectory(), frontendStatus);
    } catch (...) {
        std::scoped_lock lock(g_mutex);
        g_status = "Report failed: the plugin directory could not be located.";
        return false;
    }
}

} // namespace SupportReport
