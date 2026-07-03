#pragma once
#include <string_view>
#include "SFSEInterface.h"

// Version components are injected from xmake (add_defines PLUGIN_VERSION_* in
// xmake.lua, kept in step with set_version) so the runtime banner and the SFSE
// plugin version data move together. The fallback values apply only to tooling
// without the defines (e.g. clangd) and are intentionally an obvious 0.0.0.
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
