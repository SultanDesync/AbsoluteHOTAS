#pragma once

#include <string>
#include <string_view>
#include <algorithm>
#include <cctype>

// ============================================================================
// Shared string utilities used across multiple subsystems.
// ============================================================================

// Trim whitespace and lowercase an ASCII string.
inline std::string TrimLower(std::string_view value) {
    const auto begin = std::find_if_not(value.begin(), value.end(),
        [](unsigned char ch) { return std::isspace(ch) != 0; });
    const auto end = std::find_if_not(value.rbegin(), value.rend(),
        [](unsigned char ch) { return std::isspace(ch) != 0; }).base();
    if (begin >= end) return {};
    std::string lowered(begin, end);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lowered;
}
