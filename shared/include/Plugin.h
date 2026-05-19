#pragma once
#include <string_view>
#include <REL/Relocation.h>

namespace Plugin
{
    using namespace std::string_view_literals;

    static constexpr auto Name{ "AbsoluteHOTAS"sv };
    static constexpr auto Author{ "Antigravity"sv };
    static constexpr auto Version{
        REL::Version{ 1, 6, 0, 0 }
    };
}
