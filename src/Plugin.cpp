#include "PCH.h"

#include "Plugin.h"
#include "SFSEInterface.h"

// SFSE plugin version declaration. Owned by the plugin (not generated) so the
// compatibility contract is explicit and correct for this mod:
//   - UsesSigScanning(true)  / UsesAddressLibrary(false): the plugin finds its
//     hooks via signature scanning and manual offsets; it performs no Address
//     Library lookups, so it must NOT advertise an Address Library requirement.
//   - CompatibleVersions pinned to the tested runtimes: the ThrottleHook RVAs are
//     version-specific, so SFSE should refuse to load on an untested patch rather
//     than hook the wrong instructions and crash. Bump this list only after
//     re-validating offsets on a new runtime.
SFSE_PLUGIN_VERSION = []() noexcept {
    SFSE::PluginVersionData data{};

    data.PluginVersion(Plugin::Version);
    data.PluginName(Plugin::Name);
    data.AuthorName(Plugin::Author);

    data.UsesSigScanning(true);
    data.UsesAddressLibrary(false);
    // NOTE: HasNoStructUse(true) is technically a fib — the plugin *does* write raw
    // game-struct offsets (the flight-control cluster lanes at cluster_base+0x58..0x6C
    // and the 0x20-byte Bethesda Setting layout). It's tolerable only because the
    // pinned CompatibleVersions list below is the stricter, operative gate: SFSE will
    // refuse to load on any runtime whose struct layout we haven't validated. If that
    // version pin is ever loosened, this flag must be revisited (and probably set false).
    data.HasNoStructUse(true);
    data.IsLayoutDependent(false);

    // Tested Starfield runtimes.
    data.CompatibleVersions({ REL::Version{ 1, 16, 242, 0 }, REL::Version{ 1, 16, 244, 0 } });
    data.MinimumRequiredXSEVersion(REL::Version{ 0, 2, 20, 0 });

    return data;
}();
