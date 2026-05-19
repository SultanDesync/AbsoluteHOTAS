#pragma once

#include <filesystem>
#include <string>

namespace RuntimePaths {
    std::filesystem::path PluginDirectory();
    std::filesystem::path IniPath();
    std::filesystem::path LogPath();

    bool IsFileLoggingEnabled();
    void AppendLog(const char* a_prefix, const std::string& a_message);
}
