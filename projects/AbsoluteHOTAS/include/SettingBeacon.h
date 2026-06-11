#pragma once
#include <cstdint>

namespace SettingBeacon {
    // Plant the magic number beacon AND zero game deadzones.
    // Returns true if the beacon was planted successfully.
    bool PlantBeacon();

    // Returns true if the beacon is currently active.
    bool IsActive();
}
