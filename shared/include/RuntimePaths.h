#pragma once

#include <filesystem>
#include <string>

namespace RuntimePaths {
    std::filesystem::path PluginDirectory();
    std::filesystem::path IniPath();
    std::filesystem::path LogPath();

    bool IsFileLoggingEnabled();

    // Call once at startup to read bLogThrottle from INI and set the global flag.
    void EnableFileLogging();

    // Gated by bLogThrottle — use for all runtime/per-frame messages.
    void AppendLog(const char* a_prefix, const std::string& a_message);

    // Always writes regardless of bLogThrottle — use for startup/init only.
    void AppendLogAlways(const char* a_prefix, const std::string& a_message);
}
