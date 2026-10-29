#include "SupportReport.h"
#include "TestCheck.h"

#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

namespace RuntimePaths {
std::filesystem::path PluginDirectory() { return std::filesystem::temp_directory_path(); }
}

static std::string Read(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

int main()
{
    const auto directory = std::filesystem::temp_directory_path() /
        ("absolutehotas-report-test-" + std::to_string(GetCurrentProcessId()));
    REQUIRE(std::filesystem::create_directory(directory));
    const auto report = directory / "AbsoluteHOTAS-support.txt";
    SupportReport::SetStartup("AbsoluteHOTAS: test\nStarfield: 1.16.244.0\nSFSE: 0.2.20.0\n");
    REQUIRE(SupportReport::ExportTo(directory, "Control ABI: 1\n"));
    REQUIRE(Read(report).contains("No controller snapshot"));
    REQUIRE(Read(report).contains("Starfield: 1.16.244.0"));
    SupportReport::PublishRuntime("Active profile: precision\nThrottle: waiting for neutral\n");
    REQUIRE(SupportReport::ExportTo(directory, "Control connected\n"));
    const auto saved = Read(report);
    REQUIRE(saved.contains("Active profile: precision"));
    REQUIRE(saved.contains("waiting for neutral"));
    REQUIRE(!saved.contains("No controller snapshot"));
    REQUIRE(!std::filesystem::exists(directory / "AbsoluteHOTAS-support.txt.tmp"));

    // Failed replacement keeps the preceding report intact and reports failure.
    const HANDLE locked = CreateFileW(report.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(locked != INVALID_HANDLE_VALUE);
    SupportReport::PublishRuntime("Replacement snapshot\n");
    REQUIRE(!SupportReport::ExportTo(directory, ""));
    REQUIRE(SupportReport::Status().contains("Report failed"));
    REQUIRE(CloseHandle(locked));
    REQUIRE(Read(report) == saved);
    REQUIRE(!SupportReport::ExportTo(directory / "missing", ""));

    // Publication and export share a snapshot boundary, never a partial string.
    std::thread publisher([] {
        for (int i = 0; i < 50; ++i) SupportReport::PublishRuntime(std::string(2048, 'X') + "\n");
    });
    REQUIRE(SupportReport::ExportTo(directory, "Concurrent export\n"));
    publisher.join();
    REQUIRE(SupportReport::ExportTo(directory, ""));
    REQUIRE(Read(report).contains(std::string(2048, 'X')));
    REQUIRE(std::filesystem::remove(report));
    REQUIRE(std::filesystem::remove(directory));
}
