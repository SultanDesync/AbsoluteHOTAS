#include "Plugin.h"
#include <SFSE/Interfaces.h>

SFSEPluginVersion = []() noexcept {
    SFSE::PluginVersionData data{};

    data.PluginVersion(Plugin::Version);
    data.PluginName(Plugin::Name);
    data.AuthorName(Plugin::Author);
    data.UsesAddressLibrary(false);
    data.UsesSigScanning(true);
    data.IsLayoutDependent(false);
    data.HasNoStructUse(true);
    
    // Explicitly target current tested Starfield/SFSE runtime pair.
    data.CompatibleVersions({ REL::Version{ 1, 16, 242, 0 } });
    data.MinimumRequiredXSEVersion(REL::Version{ 0, 2, 20, 0 });

    return data;
}();
