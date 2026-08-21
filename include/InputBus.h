#pragma once

#include "AbsoluteInputBusAPI.h"
#include "PilotState.h"

#include <string_view>
#include <unordered_map>
#include <utility>

namespace InputBus {

using AxisCalibrationMap = std::unordered_map<int, std::pair<long, long>>;

// All mutation occurs on the HOTAS controller thread. Exported API callbacks
// copy already-published POD snapshots under the service lock.
void Initialize();
void Poll(const AxisCalibrationMap& calibration);
void SetActiveProfile(std::uint32_t slot, std::string_view profileId);
void PublishRuntimeContext(const PilotState::Snapshot& snapshot,
                           bool automaticPilotSource);
void Shutdown();

const AbsoluteInputBusApi::ApiV1* GetApi() noexcept;

} // namespace InputBus
