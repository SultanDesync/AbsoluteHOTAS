#pragma once
#include <string_view>
#include "SFSEInterface.h"

// Version components are injected from xmake (add_defines PLUGIN_VERSION_* in
// xmake.lua, kept in step with set_version) so the runtime banner and the SFSE
// plugin version data move together. Stable builds define PLUGIN_VERSION_STABLE;
// prerelease builds provide PLUGIN_VERSION_PRERELEASE. The fallback values apply
// only to tooling without the defines (e.g. clangd) and remain an obvious dev build.
#ifndef PLUGIN_VERSION_MAJOR
#define PLUGIN_VERSION_MAJOR 0
#endif
#ifndef PLUGIN_VERSION_MINOR
#define PLUGIN_VERSION_MINOR 0
#endif
#ifndef PLUGIN_VERSION_PATCH
#define PLUGIN_VERSION_PATCH 0
#endif
#ifndef PLUGIN_VERSION_PRERELEASE
#define PLUGIN_VERSION_PRERELEASE dev
#endif

#define ABSOLUTEHOTAS_STRINGIZE_IMPL(value) #value
#define ABSOLUTEHOTAS_STRINGIZE(value) ABSOLUTEHOTAS_STRINGIZE_IMPL(value)

namespace Plugin
{
    using namespace std::string_view_literals;

    static constexpr auto Name{ "AbsoluteHOTAS"sv };
    static constexpr auto Author{ "Antigravity"sv };
    static constexpr auto Version{
        REL::Version{ PLUGIN_VERSION_MAJOR, PLUGIN_VERSION_MINOR, PLUGIN_VERSION_PATCH, 0 }
    };
    static constexpr std::string_view VersionCoreString{
        ABSOLUTEHOTAS_STRINGIZE(PLUGIN_VERSION_MAJOR) "."
        ABSOLUTEHOTAS_STRINGIZE(PLUGIN_VERSION_MINOR) "."
        ABSOLUTEHOTAS_STRINGIZE(PLUGIN_VERSION_PATCH)
    };
#ifdef PLUGIN_VERSION_STABLE
    static constexpr std::string_view VersionString{ VersionCoreString };
#else
    static constexpr std::string_view VersionString{
        ABSOLUTEHOTAS_STRINGIZE(PLUGIN_VERSION_MAJOR) "."
        ABSOLUTEHOTAS_STRINGIZE(PLUGIN_VERSION_MINOR) "."
        ABSOLUTEHOTAS_STRINGIZE(PLUGIN_VERSION_PATCH) "-"
        ABSOLUTEHOTAS_STRINGIZE(PLUGIN_VERSION_PRERELEASE)
    };
#endif
}
