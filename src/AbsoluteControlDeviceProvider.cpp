#include "AbsoluteControlDeviceProvider.h"

#include "AbsoluteControlDevices.h"
#include "AbsoluteControlSettings.h"
#include "AbsoluteControlTelemetry.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <format>
#include <mutex>
#include <string>
#include <string_view>

namespace {
using namespace AbsoluteControlPanelApi;

std::mutex g_mutex;
AbsoluteControlDevices::Session g_session;
std::string g_status{"Waiting for the first DirectInput snapshot."};
bool g_hasOperationStatus{};

template <std::size_t Size>
void Copy(char (&destination)[Size], std::string_view value) noexcept
{
    const auto count = (std::min)(value.size(), Size - 1);
    std::memcpy(destination, value.data(), count);
    destination[count] = '\0';
}

ControlDescriptorV1 Control(ControlKind kind, std::uint32_t flags,
                            std::string_view id, std::string_view label,
                            std::string_view description) noexcept
{
    ControlDescriptorV1 result;
    result.kind = kind;
    result.flags = flags;
    Copy(result.controlId, id);
    Copy(result.label, label);
    Copy(result.description, description);
    return result;
}

const std::array g_controls{
    Control(ControlKind::RecordCollection, kControlTransientSelection,
        "device-selection", "DirectInput devices",
        "Select a bounded stable device record to inspect or calibrate."),
    Control(ControlKind::InputBinding, kControlReadOnly,
        "device-identity-status", "Selected device",
        "Persistent identity, connection state, and control counts."),
    Control(ControlKind::InputBinding, kControlReadOnly,
        "device-calibration-status", "Calibration session",
        "Eight-axis sweep progress and the last device operation result."),
    Control(ControlKind::GroupHeader, kControlNone,
        "device-calibration-section", "Sweep calibration",
        "Move all real axes through their full travel. Motion of 5000 raw units or less is ignored as ghost input."),
    Control(ControlKind::Action, kControlLayoutInline,
        "begin-device-calibration", "Begin sweep",
        "Start an eight-axis calibration sweep for the selected connected device."),
    Control(ControlKind::Action, kControlLayoutInline,
        "commit-device-calibration", "Commit sweep",
        "Persist every detected axis range, reload the runtime, and leave undetected axes unchanged."),
    Control(ControlKind::Action, kControlLayoutInline,
        "cancel-device-calibration", "Cancel sweep",
        "Discard the in-memory sweep without changing saved calibration."),
    Control(ControlKind::Action, kControlRequiresConfirmation,
        "clear-device-calibration", "Clear saved calibration",
        "Remove all eight saved calibration ranges for the selected device and reload the runtime."),
    Control(ControlKind::GroupHeader, kControlNone,
        "device-reassignment-section", "Duplicate-device reassignment",
        "Choose another instance of the same product. Reassignment swaps explicit device indices in the fixed HOTAS binding catalog and calibration map."),
    Control(ControlKind::RecordCollection, kControlTransientSelection,
        "reassignment-target", "Duplicate target",
        "Select a non-adjacent duplicate by stable instance identity."),
    Control(ControlKind::Action, kControlRequiresConfirmation,
        "reassign-duplicate-device", "Reassign duplicate devices",
        "Atomically swap fixed HOTAS bindings and saved calibration between the selected duplicate instances."),
};

ValueV1 StringValue(std::string_view value) noexcept
{
    ValueV1 result;
    result.kind = ValueKind::String;
    Copy(result.stringValue, value);
    return result;
}

std::string CalibrationStatus() noexcept
{
    const auto& sweep = g_session.Calibration();
    if (!sweep.active) return g_status;
    const auto detected = std::popcount(sweep.activeAxisMask);
    return std::format(
        "Sweep active: {} of 8 axes exceed the {}-unit ghost threshold.",
        detected, AbsoluteControlDevices::kGhostThreshold);
}

bool LoadCurrentDeviceState(HotasBindingCatalog::BindingState& bindings,
                            AbsoluteControlDevices::CalibrationMap& calibration,
                            AbsoluteControlSettings::Revision& revision,
                            std::string& error) noexcept
{
    return AbsoluteControlSettings::LoadDeviceState(
        bindings, calibration, revision, error);
}

bool PersistDeviceState(
    const HotasBindingCatalog::BindingState& bindings,
    const AbsoluteControlDevices::CalibrationMap& calibration,
    const AbsoluteControlSettings::Revision& revision,
    std::string& error) noexcept
{
    HotasBindingCatalog::BindingState bindingReadBack;
    AbsoluteControlDevices::CalibrationMap calibrationReadBack;
    AbsoluteControlSettings::Revision next;
    return AbsoluteControlSettings::ApplyDeviceState(
        bindings, calibration, revision, bindingReadBack,
        calibrationReadBack, next, error);
}

} // namespace

namespace AbsoluteControlDeviceProvider {

const ControlDescriptorV1* Controls(std::size_t& count) noexcept
{
    count = g_controls.size();
    return g_controls.data();
}

Result __cdecl ReadValue(void*, const char* rawId, ValueV1* output) noexcept
{
    if (!rawId || !output || output->structSize < sizeof(ValueV1)) {
        return Result::InvalidArgument;
    }
    try {
        std::scoped_lock lock(g_mutex);
        const std::string_view id{rawId};
        if (id == "device-selection") {
            *output = StringValue(g_session.SelectedRecordId());
        } else if (id == "reassignment-target") {
            *output = StringValue(g_session.ReassignmentTargetId());
        } else if (id == "device-identity-status") {
            const auto* device = g_session.SelectedDevice();
            *output = StringValue(device ? std::format(
                "#{} {} | {} axes | {} buttons | {} | {}",
                device->deviceIndex,
                device->productName.empty() ? device->instanceName : device->productName,
                device->axisCount, device->buttonCount,
                device->connected ? "connected" : "unavailable",
                device->persistentId) : "No DirectInput device is available.");
        } else if (id == "device-calibration-status") {
            *output = StringValue(CalibrationStatus());
        } else {
            return Result::NotFound;
        }
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

Result __cdecl WriteSelection(void*, const char* rawId,
                              const ValueV1* value) noexcept
{
    if (!rawId || !value || value->structSize < sizeof(ValueV1) ||
        value->kind != ValueKind::String ||
        std::memchr(value->stringValue, '\0', sizeof(value->stringValue)) == nullptr) {
        return Result::InvalidArgument;
    }
    try {
        std::scoped_lock lock(g_mutex);
        const std::string_view id{rawId};
        const std::string_view selection{value->stringValue};
        const bool accepted = id == "device-selection" ?
            g_session.Select(selection) : id == "reassignment-target" ?
            g_session.SelectReassignmentTarget(selection) : false;
        return accepted ? Result::Ok :
            (id == "device-selection" || id == "reassignment-target" ?
                Result::InvalidArgument : Result::NotFound);
    } catch (...) {
        return Result::Rejected;
    }
}

Result __cdecl InvokeAction(void*, const char* rawId) noexcept
{
    if (!rawId) return Result::InvalidArgument;
    try {
        std::scoped_lock lock(g_mutex);
        const std::string_view id{rawId};
        if (id == "begin-device-calibration") {
            if (!g_session.BeginCalibration()) return Result::Rejected;
            g_status = "Calibration sweep started.";
            g_hasOperationStatus = true;
            return Result::Ok;
        }
        if (id == "cancel-device-calibration") {
            g_session.CancelCalibration();
            g_status = "Calibration sweep cancelled; saved ranges were not changed.";
            g_hasOperationStatus = true;
            return Result::Ok;
        }

        HotasBindingCatalog::BindingState bindings;
        AbsoluteControlDevices::CalibrationMap calibration;
        AbsoluteControlSettings::Revision revision;
        std::string error;
        if (!LoadCurrentDeviceState(bindings, calibration, revision, error)) {
            g_status = error;
            g_hasOperationStatus = true;
            return Result::Rejected;
        }

        auto candidate = g_session;
        if (id == "commit-device-calibration") {
            if (!candidate.Calibration().active) return Result::Rejected;
            const auto count = candidate.CommitCalibration(calibration);
            if (count == 0) {
                g_status = "No axis exceeded the ghost threshold; nothing was saved.";
                g_hasOperationStatus = true;
                return Result::Rejected;
            }
            if (!PersistDeviceState(bindings, calibration, revision, error)) {
                g_status = error;
                g_hasOperationStatus = true;
                return Result::WriteFailure;
            }
            g_session = std::move(candidate);
            g_status = std::format("Saved {} calibrated axis ranges.", count);
            g_hasOperationStatus = true;
            return Result::Ok;
        }
        if (id == "clear-device-calibration") {
            const auto count = candidate.ClearCalibration(calibration);
            if (!PersistDeviceState(bindings, calibration, revision, error)) {
                g_status = error;
                g_hasOperationStatus = true;
                return Result::WriteFailure;
            }
            g_session = std::move(candidate);
            g_status = std::format("Cleared {} saved axis ranges.", count);
            g_hasOperationStatus = true;
            return Result::Ok;
        }
        if (id == "reassign-duplicate-device") {
            const auto result = candidate.ReassignDuplicate(bindings, calibration);
            if (!result.accepted) {
                g_status = result.detail;
                g_hasOperationStatus = true;
                return Result::InvalidArgument;
            }
            if (!PersistDeviceState(bindings, calibration, revision, error)) {
                g_status = error;
                g_hasOperationStatus = true;
                return Result::WriteFailure;
            }
            g_status = result.detail;
            g_hasOperationStatus = true;
            return Result::Ok;
        }
        return Result::NotFound;
    } catch (...) {
        return Result::Rejected;
    }
}

Result __cdecl ReadRecordItems(void*, const char* rawId,
                               RecordItemV1* items, std::uint32_t capacity,
                               std::uint32_t* outputCount) noexcept
{
    if (!rawId || !outputCount) return Result::InvalidArgument;
    try {
        std::scoped_lock lock(g_mutex);
        const std::string_view id{rawId};
        if (id != "device-selection" && id != "reassignment-target") {
            *outputCount = 0;
            return Result::NotFound;
        }
        std::array<const AbsoluteControlDevices::DeviceRecord*,
                   AbsoluteControlDevices::kMaximumDevices> selected{};
        std::size_t count{};
        for (const auto& record : g_session.Records()) {
            if (id == "reassignment-target") {
                auto candidate = g_session;
                if (!candidate.SelectReassignmentTarget(record.recordId)) continue;
            }
            selected[count++] = &record;
        }
        *outputCount = static_cast<std::uint32_t>(count);
        if (count != 0 && (!items || capacity < count)) {
            return Result::CapacityExceeded;
        }
        for (std::size_t index = 0; index < count; ++index) {
            items[index] = {};
            items[index].flags = selected[index]->flags;
            Copy(items[index].recordId, selected[index]->recordId);
            Copy(items[index].label, selected[index]->label);
            Copy(items[index].summary, selected[index]->summary);
            Copy(items[index].detail, selected[index]->detail);
        }
        return Result::Ok;
    } catch (...) {
        *outputCount = 0;
        return Result::Rejected;
    }
}

void PublishRuntime(const AbsoluteInputBusApi::DeviceInfoV1* infos,
                    const AbsoluteInputBusApi::DeviceSnapshotV1* snapshots,
                    std::size_t count) noexcept
{
    try {
        std::array<AbsoluteControlDevices::RuntimeDevice,
                   AbsoluteControlDevices::kMaximumDevices> devices{};
        const auto bounded = infos && snapshots ?
            (std::min)(count, devices.size()) : std::size_t{};
        for (std::size_t index = 0; index < bounded; ++index) {
            auto& output = devices[index];
            const auto& info = infos[index];
            const auto& snapshot = snapshots[index];
            output.deviceIndex = info.deviceIndex;
            std::copy(std::begin(info.instanceGuid), std::end(info.instanceGuid),
                      output.instanceGuid.begin());
            std::copy(std::begin(info.productGuid), std::end(info.productGuid),
                      output.productGuid.begin());
            output.vendorId = info.vendorId;
            output.productId = info.productId;
            output.axisCount = (std::min<std::uint32_t>)(
                info.axisCount, static_cast<std::uint32_t>(output.rawAxes.size()));
            output.buttonCount = info.buttonCount;
            output.persistentId = info.persistentId;
            output.instanceName = info.instanceName;
            output.productName = info.productName;
            output.connected = snapshot.connected != 0;
            std::copy(std::begin(snapshot.rawAxes), std::end(snapshot.rawAxes),
                      output.rawAxes.begin());
            std::copy(std::begin(snapshot.normalizedAxes),
                      std::end(snapshot.normalizedAxes),
                      output.normalizedAxes.begin());
        }

        AbsoluteControlTelemetry::DeviceSample telemetry;
        {
            std::scoped_lock lock(g_mutex);
            g_session.Publish(devices.data(), bounded);
            if (!g_hasOperationStatus) {
                g_status = bounded == 0 ? "No DirectInput devices detected." :
                    std::format("{} DirectInput device{} available.", bounded,
                        bounded == 1 ? "" : "s");
            }
            if (const auto* selected = g_session.SelectedDevice();
                selected && selected->connected) {
                telemetry.values = selected->normalizedAxes;
                telemetry.availableMask = selected->axisCount >= 8 ? 0xFFU :
                    ((1U << selected->axisCount) - 1U);
            }
        }
        AbsoluteControlTelemetry::PublishDevice(telemetry);
    } catch (...) {
        AbsoluteControlTelemetry::PublishDevice({});
    }
}

namespace Testing {
void Reset() noexcept
{
    std::scoped_lock lock(g_mutex);
    g_session = {};
    g_status = "Waiting for the first DirectInput snapshot.";
    g_hasOperationStatus = false;
}
} // namespace Testing

} // namespace AbsoluteControlDeviceProvider
