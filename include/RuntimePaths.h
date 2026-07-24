#pragma once

#include <filesystem>
#include <string>

namespace RuntimePaths {
    std::filesystem::path PluginDirectory();

    // Mod-owned defaults + tunables. Shipped in the archive and (from 4.0 on)
    // overwritten freely on update, because no user data lives here. InitLogging()
    // and mod-owned sections ([General], [Injection], [Gate]) read from this file.
    std::filesystem::path IniPath();

    // User-owned bindings, tuning, calibration, macros, and profile routing. Written
    // by the wizard, never shipped, never overwritten. Overlays IniPath() at load.
    std::filesystem::path CustomIniPath();

    // Directory for user-initiated profile snapshots (Export/Import). Created on first
    // export; may not exist yet.
    std::filesystem::path ProfilesDir();

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
