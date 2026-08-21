#include "AbsoluteControlDeviceProvider.h"
#include "AbsoluteControlDevices.h"
#include "AbsoluteControlSettings.h"
#include "AbsoluteControlTelemetry.h"

#include <array>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
HotasBindingCatalog::BindingState g_bindings{};
AbsoluteControlDevices::CalibrationMap g_calibration;
AbsoluteControlSettings::Revision g_revision{1, 11};
std::size_t g_applyCalls{};
}

namespace AbsoluteControlSettings {
bool LoadDeviceState(HotasBindingCatalog::BindingState& bindings,
                     AbsoluteControlDevices::CalibrationMap& calibration,
                     Revision& revision, std::string& error) noexcept
{
    bindings = g_bindings;
    calibration = g_calibration;
    revision = g_revision;
    error.clear();
    return true;
}

bool ApplyDeviceState(
    const HotasBindingCatalog::BindingState& bindings,
    const AbsoluteControlDevices::CalibrationMap& calibration,
    const Revision&, HotasBindingCatalog::BindingState& bindingReadBack,
    AbsoluteControlDevices::CalibrationMap& calibrationReadBack,
    Revision& revision, std::string& error) noexcept
{
    g_bindings = bindingReadBack = bindings;
    g_calibration = calibrationReadBack = calibration;
    ++g_revision.runtimeGeneration;
    revision = g_revision;
    error.clear();
    ++g_applyCalls;
    return true;
}
} // namespace AbsoluteControlSettings

namespace {
using namespace AbsoluteControlPanelApi;

ValueV1 String(std::string_view value)
{
    ValueV1 result;
    result.kind = ValueKind::String;
    std::memcpy(result.stringValue, value.data(), value.size());
    result.stringValue[value.size()] = '\0';
    return result;
}

void Publish(std::array<AbsoluteInputBusApi::DeviceInfoV1, 3>& infos,
             std::array<AbsoluteInputBusApi::DeviceSnapshotV1, 3>& snapshots,
             std::int32_t axisOne = 32768)
{
    for (std::size_t index = 0; index < infos.size(); ++index) {
        infos[index].deviceIndex = static_cast<std::uint32_t>(index);
        infos[index].axisCount = 8;
        infos[index].buttonCount = 32;
        infos[index].productGuid[0] = index == 1 ? 9 : 7;
        std::snprintf(infos[index].persistentId,
            sizeof(infos[index].persistentId),
            "{%08zu-0000-0000-0000-000000000000}", index + 1);
        std::snprintf(infos[index].productName,
            sizeof(infos[index].productName), "%s",
            index == 1 ? "Throttle" : "Twin Stick");
        snapshots[index].deviceIndex = static_cast<std::uint32_t>(index);
        snapshots[index].connected = 1;
        std::fill(std::begin(snapshots[index].rawAxes),
                  std::end(snapshots[index].rawAxes), 32768);
        snapshots[index].rawAxes[1] = index == 0 ? axisOne : 32768;
        snapshots[index].normalizedAxes[1] =
            static_cast<float>(axisOne - 32768) / 32768.0F;
    }
    AbsoluteControlDeviceProvider::PublishRuntime(
        infos.data(), snapshots.data(), infos.size());
}
} // namespace

int main()
{
    using namespace AbsoluteControlPanelApi;
    AbsoluteControlDeviceProvider::Testing::Reset();

    std::size_t controlCount{};
    const auto* controls = AbsoluteControlDeviceProvider::Controls(controlCount);
    assert(controls && controlCount == 11);
    assert(controls[0].kind == ControlKind::RecordCollection);
    assert((controls[0].flags & kControlTransientSelection) != 0);
    assert(controls[7].kind == ControlKind::Action);
    assert((controls[7].flags & kControlRequiresConfirmation) != 0);
    assert((controls[10].flags & kControlRequiresConfirmation) != 0);

    std::array<AbsoluteInputBusApi::DeviceInfoV1, 3> infos{};
    std::array<AbsoluteInputBusApi::DeviceSnapshotV1, 3> snapshots{};
    Publish(infos, snapshots);

    std::array<RecordItemV1, kMaximumRecordItems> records{};
    std::uint32_t recordCount{};
    assert(AbsoluteControlDeviceProvider::ReadRecordItems(
        nullptr, "device-selection", records.data(),
        static_cast<std::uint32_t>(records.size()),
        &recordCount) == Result::Ok);
    assert(recordCount == 3);
    assert(std::strcmp(records[0].label, "Twin Stick") == 0);
    assert(std::strstr(records[0].detail, "duplicate product"));

    ValueV1 selected;
    assert(AbsoluteControlDeviceProvider::ReadValue(
        nullptr, "device-selection", &selected) == Result::Ok);
    assert(std::strcmp(selected.stringValue, records[0].recordId) == 0);

    // Target collection contains the matching non-adjacent duplicate only.
    assert(AbsoluteControlDeviceProvider::ReadRecordItems(
        nullptr, "reassignment-target", records.data(),
        static_cast<std::uint32_t>(records.size()),
        &recordCount) == Result::Ok);
    assert(recordCount == 1);
    const auto target = String(records[0].recordId);
    assert(AbsoluteControlDeviceProvider::WriteSelection(
        nullptr, "reassignment-target", &target) == Result::Ok);

    for (std::size_t index = 0; index < g_bindings.size(); ++index) {
        g_bindings[index] = HotasBindingCatalog::kTargets[index].captureKind ==
            HotasBindingCatalog::CaptureKind::Axis ? "#0@0x30" : "#0@1";
    }
    assert(AbsoluteControlDeviceProvider::InvokeAction(
        nullptr, "reassign-duplicate-device") == Result::Ok);
    assert(g_applyCalls == 1);
    assert(g_bindings[0] == "#2@0x30");

    assert(AbsoluteControlDeviceProvider::InvokeAction(
        nullptr, "begin-device-calibration") == Result::Ok);
    Publish(infos, snapshots, 40000);
    std::size_t channelCount{};
    const auto* channels = AbsoluteControlTelemetry::Testing::Channels(channelCount);
    const AbsoluteControlPanelExperimental::LiveChannelDescriptorV1* deviceChannel{};
    for (std::size_t index = 0; index < channelCount; ++index) {
        if (std::strcmp(channels[index].channelId, "device-axes") == 0) {
            deviceChannel = &channels[index];
            break;
        }
    }
    assert(deviceChannel);
    AbsoluteControlPanelExperimental::LiveFrameV1 frame;
    assert(deviceChannel->readLiveFrame(deviceChannel->context, &frame) ==
           AbsoluteControlPanelExperimental::Result::Ok);
    assert(frame.telemetryPlot.availableMask == 0xFFU);
    assert(frame.telemetryPlot.values[1] > 0.2);
    assert(AbsoluteControlDeviceProvider::InvokeAction(
        nullptr, "commit-device-calibration") == Result::Ok);
    assert(g_applyCalls == 2);
    assert((g_calibration.at((0 << 8) | 0x31) ==
            std::pair<long, long>(32768, 40000)));

    assert(AbsoluteControlDeviceProvider::InvokeAction(
        nullptr, "clear-device-calibration") == Result::Ok);
    assert(g_applyCalls == 3);
    assert(g_calibration.empty());

    assert(AbsoluteControlDeviceProvider::InvokeAction(
        nullptr, "begin-device-calibration") == Result::Ok);
    assert(AbsoluteControlDeviceProvider::InvokeAction(
        nullptr, "cancel-device-calibration") == Result::Ok);
    ValueV1 status;
    assert(AbsoluteControlDeviceProvider::ReadValue(
        nullptr, "device-calibration-status", &status) == Result::Ok);
    assert(std::strstr(status.stringValue, "cancelled"));
    return 0;
}
