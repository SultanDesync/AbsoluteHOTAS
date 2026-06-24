#pragma once
#include <string_view>
#include <REL/Relocation.h>

// Version components are injected from CMake PROJECT_VERSION (CMakeLists.txt) so
// the runtime banner, the SFSE plugin version data, and the DLL file-version
// resource all move together from the single source: project(... VERSION x.y.z).
// The fallback values apply only to non-CMake tooling (e.g. clangd without the
// defines) and are intentionally an obviously-wrong 0.0.0.
#ifndef PLUGIN_VERSION_MAJOR
#define PLUGIN_VERSION_MAJOR 0
#endif
#ifndef PLUGIN_VERSION_MINOR
#define PLUGIN_VERSION_MINOR 0
#endif
#ifndef PLUGIN_VERSION_PATCH
#define PLUGIN_VERSION_PATCH 0
#endif

namespace Plugin
{
    using namespace std::string_view_literals;

    static constexpr auto Name{ "AbsoluteHOTAS"sv };
    static constexpr auto Author{ "Antigravity"sv };
    static constexpr auto Version{
        REL::Version{ PLUGIN_VERSION_MAJOR, PLUGIN_VERSION_MINOR, PLUGIN_VERSION_PATCH, 0 }
    };
}
