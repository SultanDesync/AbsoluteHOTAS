#pragma once

#include <cctype>
#include <span>
#include <string>
#include <string_view>

namespace DeviceIdentity {

// DirectInput instance GUIDs identify individual devices, including identical
// products. Keep the selector distinct from legacy product names and #N indices.
inline std::string CanonicalGuid(std::string_view guid)
{
    if (guid.size() != 38 || guid.front() != '{' || guid.back() != '}') return {};
    std::string result(guid);
    for (std::size_t i = 1; i < 37; ++i) {
        if (i == 9 || i == 14 || i == 19 || i == 24) {
            if (guid[i] != '-') return {};
        } else {
            const auto ch = static_cast<unsigned char>(guid[i]);
            if (!std::isxdigit(ch)) return {};
            result[i] = static_cast<char>(std::toupper(ch));
        }
    }
    return result;
}

inline std::string Selector(std::string_view guid)
{
    const auto canonical = CanonicalGuid(guid);
    return canonical.empty() ? std::string{} : "guid:" + canonical;
}

inline int Resolve(std::string_view selector, std::span<const std::string_view> ids)
{
    if (!selector.starts_with("guid:")) return -1;
    const auto guid = CanonicalGuid(selector.substr(5));
    if (guid.empty()) return -1;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (CanonicalGuid(ids[i]) == guid) return static_cast<int>(i);
    }
    return -1; // Never substitute another instance of the same product.
}

} // namespace DeviceIdentity
