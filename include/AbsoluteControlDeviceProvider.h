#pragma once

#include "AbsoluteControlPanelAPI.h"
#include "AbsoluteInputBusAPI.h"

#include <cstddef>

namespace AbsoluteControlDeviceProvider {

[[nodiscard]] const AbsoluteControlPanelApi::ControlDescriptorV1* Controls(
    std::size_t& count) noexcept;

AbsoluteControlPanelApi::Result __cdecl ReadValue(
    void*, const char*, AbsoluteControlPanelApi::ValueV1*) noexcept;
AbsoluteControlPanelApi::Result __cdecl WriteSelection(
    void*, const char*, const AbsoluteControlPanelApi::ValueV1*) noexcept;
AbsoluteControlPanelApi::Result __cdecl InvokeAction(
    void*, const char*) noexcept;
AbsoluteControlPanelApi::Result __cdecl ReadRecordItems(
    void*, const char*, AbsoluteControlPanelApi::RecordItemV1*,
    std::uint32_t, std::uint32_t*) noexcept;

// Called by the controller immediately after Input Bus publishes a frame. The
// provider copies the bounded POD inputs; host callbacks never enumerate HID,
// traverse game state, touch disk, or call back into Input Bus.
void PublishRuntime(
    const AbsoluteInputBusApi::DeviceInfoV1* infos,
    const AbsoluteInputBusApi::DeviceSnapshotV1* snapshots,
    std::size_t count) noexcept;

namespace Testing {
void Reset() noexcept;
} // namespace Testing

} // namespace AbsoluteControlDeviceProvider
