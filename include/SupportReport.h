#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace SupportReport {

// Startup and controller owners publish copies. Export never reads live HID,
// configuration objects, or game memory from a frontend callback.
void SetStartup(std::string text);
void PublishRuntime(std::string text);
[[nodiscard]] bool Export(std::string_view frontendStatus) noexcept;
[[nodiscard]] std::string Status();

// Shared persistence boundary, also exercised with a temporary test directory.
[[nodiscard]] bool ExportTo(const std::filesystem::path& directory,
                            std::string_view frontendStatus) noexcept;

} // namespace SupportReport
