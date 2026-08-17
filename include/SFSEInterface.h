#pragma once

// ============================================================================
// Minimal, self-contained SFSE plugin ABI — vendored so AbsoluteHOTAS carries no
// CommonLibSF dependency. The plugin uses only the SFSE load/version contract
// and the documented lifecycle messaging boundary (it does its own HID input,
// signature scanning, and manual trampoline hooks), never RE:: types, REL::ID,
// or the Address Library. Namespaces and macro names mirror CommonLibSF exactly
// (SFSE::, REL::Version, SFSE_PLUGIN_LOAD/_VERSION) so call sites are unchanged.
//
// The PluginVersionData layout is byte-exact against the SFSE ABI and locked by
// static_asserts below — do not reorder or resize its members.
// ============================================================================

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>

namespace REL
{
    // Starfield version packing: major<<24 | minor<<16 | patch<<4 | build.
    class Version
    {
    public:
        using value_type = std::uint16_t;

        constexpr Version() noexcept = default;

        constexpr Version(value_type a_major, value_type a_minor = 0,
                          value_type a_patch = 0, value_type a_build = 0) noexcept :
            _impl{ a_major, a_minor, a_patch, a_build }
        {}

        [[nodiscard]] constexpr value_type& operator[](std::size_t i) noexcept { return _impl[i]; }
        [[nodiscard]] constexpr value_type  operator[](std::size_t i) const noexcept { return _impl[i]; }

        [[nodiscard]] constexpr std::uint32_t pack() const noexcept
        {
            return static_cast<std::uint32_t>(
                (_impl[0] & 0x0FFu) << 24 |
                (_impl[1] & 0x0FFu) << 16 |
                (_impl[2] & 0xFFFu) << 4  |
                (_impl[3] & 0x00Fu));
        }

        [[nodiscard]] static constexpr Version unpack(std::uint32_t v) noexcept
        {
            return Version{
                static_cast<value_type>((v >> 24) & 0x0FFu),
                static_cast<value_type>((v >> 16) & 0x0FFu),
                static_cast<value_type>((v >> 4)  & 0xFFFu),
                static_cast<value_type>(v & 0x00Fu)
            };
        }

        [[nodiscard]] std::string string(std::string_view sep = ".") const
        {
            std::string out;
            for (std::size_t i = 0; i < _impl.size(); ++i) {
                out += std::to_string(_impl[i]);
                if (i + 1 < _impl.size()) out.append(sep.data(), sep.size());
            }
            return out;
        }

    private:
        std::array<value_type, 4> _impl{ 0, 0, 0, 0 };
    };
}

namespace SFSE
{
    using PluginHandle = std::uint32_t;

    namespace Impl
    {
        struct SFSEInterface
        {
            std::uint32_t sfseVersion;
            std::uint32_t runtimeVersion;
            std::uint32_t interfaceVersion;
            void* (*queryInterface)(std::uint32_t);
            PluginHandle (*getPluginHandle)();
            const void* (*getPluginInfo)(const char*);
        };

        struct SFSEMessagingInterface
        {
            std::uint32_t interfaceVersion;
            bool (*registerListener)(PluginHandle, const char*, void*);
            bool (*dispatch)(PluginHandle, std::uint32_t, void*, std::uint32_t,
                             const char*);
        };
    }

    class MessagingInterface
    {
    public:
        enum MessageType : std::uint32_t
        {
            kPostLoad,
            kPostPostLoad,
            kPostDataLoad,
            kPostPostDataLoad,
        };

        struct Message
        {
            const char* sender;
            std::uint32_t type;
            std::uint32_t dataLength;
            void* data;
        };

        using EventCallback = void (*)(Message*);

        [[nodiscard]] std::uint32_t Version() const noexcept
        {
            return GetProxy().interfaceVersion;
        }

        [[nodiscard]] bool RegisterListener(PluginHandle handle,
                                            EventCallback callback) const noexcept
        {
            return callback && GetProxy().registerListener &&
                   GetProxy().registerListener(
                       handle, "SFSE", reinterpret_cast<void*>(callback));
        }

    private:
        [[nodiscard]] const Impl::SFSEMessagingInterface& GetProxy() const noexcept
        {
            return reinterpret_cast<const Impl::SFSEMessagingInterface&>(*this);
        }
    };

    class LoadInterface
    {
    public:
        [[nodiscard]] PluginHandle GetPluginHandle() const noexcept
        {
            return GetProxy().getPluginHandle ? GetProxy().getPluginHandle() : 0;
        }

        [[nodiscard]] const MessagingInterface* GetMessagingInterface() const noexcept
        {
            return GetProxy().queryInterface ?
                static_cast<const MessagingInterface*>(GetProxy().queryInterface(1)) :
                nullptr;
        }

    private:
        [[nodiscard]] const Impl::SFSEInterface& GetProxy() const noexcept
        {
            return reinterpret_cast<const Impl::SFSEInterface&>(*this);
        }
    };

    struct PluginVersionData
    {
    public:
        enum Version : std::uint32_t
        {
            kVersion = 1
        };

        constexpr void PluginVersion(REL::Version a_version) noexcept { pluginVersion = a_version.pack(); }
        constexpr void PluginName(std::string_view a_plugin) noexcept { SetCharBuffer(a_plugin, std::span{ pluginName }); }
        constexpr void AuthorName(std::string_view a_name) noexcept { SetCharBuffer(a_name, std::span{ author }); }

        constexpr void UsesSigScanning(bool a_value) noexcept { SetOrClearBit(addressIndependence, 1 << 0, a_value); }
        // 1 << 2 is Address Library v2.
        constexpr void UsesAddressLibrary(bool a_value) noexcept { SetOrClearBit(addressIndependence, 1 << 2, a_value); }
        constexpr void HasNoStructUse(bool a_value) noexcept { SetOrClearBit(structureCompatibility, 1 << 0, a_value); }
        // 1 << 3 is runtime 1.14.70 and later.
        constexpr void IsLayoutDependent(bool a_value) noexcept { SetOrClearBit(structureCompatibility, 1 << 3, a_value); }

        constexpr void CompatibleVersions(std::initializer_list<REL::Version> a_versions) noexcept
        {
            assert(a_versions.size() < std::size(compatibleVersions) - 1);  // must stay zero-terminated
            std::ranges::transform(a_versions, std::begin(compatibleVersions),
                [](const REL::Version& v) noexcept { return v.pack(); });
        }

        constexpr void MinimumRequiredXSEVersion(REL::Version a_version) noexcept { xseMinimum = a_version.pack(); }

        const std::uint32_t dataVersion{ kVersion };
        std::uint32_t       pluginVersion = 0;
        char                pluginName[256] = {};
        char                author[256] = {};
        std::uint32_t       addressIndependence = 0;
        std::uint32_t       structureCompatibility = 0;
        std::uint32_t       compatibleVersions[16] = {};
        std::uint32_t       xseMinimum = 0;
        const std::uint32_t reservedNonBreaking = 0;
        const std::uint32_t reservedBreaking = 0;

    private:
        static constexpr void SetCharBuffer(std::string_view a_src, std::span<char> a_dst) noexcept
        {
            assert(a_src.size() < a_dst.size());
            std::ranges::fill(a_dst, '\0');
            std::ranges::copy(a_src, a_dst.begin());
        }

        static constexpr void SetOrClearBit(std::uint32_t& a_data, std::uint32_t a_bit, bool a_set) noexcept
        {
            if (a_set) a_data |= a_bit;
            else       a_data &= ~a_bit;
        }
    };

    static_assert(offsetof(PluginVersionData, dataVersion) == 0x000);
    static_assert(offsetof(PluginVersionData, pluginVersion) == 0x004);
    static_assert(offsetof(PluginVersionData, pluginName) == 0x008);
    static_assert(offsetof(PluginVersionData, author) == 0x108);
    static_assert(offsetof(PluginVersionData, addressIndependence) == 0x208);
    static_assert(offsetof(PluginVersionData, structureCompatibility) == 0x20C);
    static_assert(offsetof(PluginVersionData, compatibleVersions) == 0x210);
    static_assert(offsetof(PluginVersionData, xseMinimum) == 0x250);
    static_assert(offsetof(PluginVersionData, reservedNonBreaking) == 0x254);
    static_assert(offsetof(PluginVersionData, reservedBreaking) == 0x258);
    static_assert(sizeof(PluginVersionData) == 0x25C);
}

#define SFSE_EXPORT extern "C" [[maybe_unused]] __declspec(dllexport)
#define SFSE_PLUGIN_LOAD(...) SFSE_EXPORT bool SFSEPlugin_Load(__VA_ARGS__)
#define SFSE_PLUGIN_VERSION SFSE_EXPORT constinit SFSE::PluginVersionData SFSEPlugin_Version
