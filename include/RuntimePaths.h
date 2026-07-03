#pragma once

#include <filesystem>
#include <string>

namespace RuntimePaths {
    std::filesystem::path PluginDirectory();
    std::filesystem::path IniPath();
    std::filesystem::path LogPath();

    // Whether bEnableLog was set in the INI. Diagnostics are fully opt-in: when this
    // is false the plugin writes no log at all (not even crashes), so a normal run
    // never leaves a file on disk.
    bool IsLoggingEnabled();

    // Read bEnableLog from the INI once at startup and cache the result.
    void InitLogging();

    // Append one line to the log. No-op unless bEnableLog is true. There is a single
    // severity on purpose — with logging off nothing is written, and with it on we
    // want the full picture, so an errors-vs-info split would carry no behavior.
    void Log(const char* a_tag, const std::string& a_message);
}
