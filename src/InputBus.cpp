#include "PCH.h"

#define ABSOLUTE_HOTAS_EXPORTS
#include "AbsoluteInputBusAPI.h"
#include "AbsoluteControlDeviceProvider.h"
#include "AbsoluteControlDevices.h"
#include "DeviceManager.h"
#include "InputBus.h"
#include "InputBusCapturePolicy.h"
#include "InputBusContextPolicy.h"
#include "InputBusState.h"
#include "Plugin.h"
#include "RuntimePaths.h"

namespace {
using BusResult = AbsoluteInputBusApi::Result;
using CaptureState = AbsoluteInputBusApi::CaptureState;
using ControlKind = AbsoluteInputBusApi::ControlKind;

constexpr long kAxisCaptureThreshold = 8000;
constexpr int kAxisCaptureFrames = 5;
constexpr int kDigitalCaptureFrames = 2;

template <std::size_t Size>
void Copy(char (&destination)[Size], std::string_view value) noexcept
{
    std::fill(std::begin(destination), std::end(destination), '\0');
    if (!value.empty()) {
        std::memcpy(destination, value.data(), std::min(value.size(), Size - 1));
    }
}

std::string FormatGuid(const GUID& guid)
{
    return std::format(
        "{{{:08X}-{:04X}-{:04X}-{:02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}}}",
        guid.Data1, guid.Data2, guid.Data3,
        static_cast<unsigned>(guid.Data4[0]),
        static_cast<unsigned>(guid.Data4[1]),
        static_cast<unsigned>(guid.Data4[2]),
        static_cast<unsigned>(guid.Data4[3]),
        static_cast<unsigned>(guid.Data4[4]),
        static_cast<unsigned>(guid.Data4[5]),
        static_cast<unsigned>(guid.Data4[6]),
        static_cast<unsigned>(guid.Data4[7]));
}

struct PublishedDevice {
    AbsoluteInputBusApi::DeviceInfoV1 info{};
    AbsoluteInputBusApi::DeviceSnapshotV1 snapshot{};
    InputBusState::EdgeState edges{};
    bool snapshotAvailable{};
};

struct CaptureBaseline {
    std::uint32_t deviceIndex{};
    std::array<std::int32_t, AbsoluteInputBusApi::kAxisCount> axes{};
    std::array<std::uint32_t, AbsoluteInputBusApi::kDigitalControlCount>
        seenPressCount{};
    bool connected{};
};

struct CaptureSession {
    CaptureState state{CaptureState::Idle};
    std::uint64_t sessionId{};
    std::uint32_t allowedControls{};
    std::chrono::milliseconds settle{50};
    std::chrono::milliseconds timeout{8000};
    std::string consumerId;
    AbsoluteInputBusApi::BindingV1 binding{};
    std::string detail;
    std::vector<CaptureBaseline> baselines;
    InputBusCapturePolicy::AxisDebounce axisDebounce{};
    InputBusCapturePolicy::DigitalDebounce digitalDebounce{};
    int targetDevice{-1};
    int targetChannel{-1};
    std::chrono::steady_clock::time_point started{};
    std::chrono::steady_clock::time_point lastTarget{};
    bool baselineNeeded{};
};

std::mutex g_mutex;
std::vector<PublishedDevice> g_devices;
AbsoluteInputBusApi::ProfileStateV1 g_profile{};
AbsoluteInputBusApi::RuntimeContextV1 g_context{};
CaptureSession g_capture;
std::uint64_t g_nextSession{};
std::uint64_t g_snapshotSequence{};
std::uint64_t g_contextSequence{};
std::uint64_t g_producerGeneration{};
bool g_ready{};
bool g_contextPublished{};

void BuildDeviceTableLocked()
{
    g_devices.clear();
    g_devices.reserve(static_cast<std::size_t>(DeviceManager::GetDeviceCount()));
    for (int index = 0; index < DeviceManager::GetDeviceCount(); ++index) {
        const auto& source = DeviceManager::GetDevice(index);
        PublishedDevice device;
        device.info.deviceIndex = static_cast<std::uint32_t>(index);
        std::memcpy(device.info.instanceGuid, &source.guidInstance,
                    sizeof(device.info.instanceGuid));
        std::memcpy(device.info.productGuid, &source.guidProduct,
                    sizeof(device.info.productGuid));
        device.info.vendorId = source.vid;
        device.info.productId = source.pid;
        device.info.axisCount = static_cast<std::uint32_t>(
            std::clamp(source.axisCount, 0,
                       static_cast<int>(AbsoluteInputBusApi::kAxisCount)));
        device.info.buttonCount = static_cast<std::uint32_t>(
            std::clamp(source.buttonCount, 0,
                       static_cast<int>(AbsoluteInputBusApi::kButtonCount)));
        device.info.povCount =
            static_cast<std::uint32_t>(AbsoluteInputBusApi::kPovCount);
        Copy(device.info.persistentId, FormatGuid(source.guidInstance));
        Copy(device.info.instanceName, source.instanceName);
        Copy(device.info.productName, source.productName);
        device.snapshot.deviceIndex = static_cast<std::uint32_t>(index);
        device.snapshot.producerGeneration = g_producerGeneration;
        std::fill(std::begin(device.snapshot.povHundredths),
                  std::end(device.snapshot.povHundredths), -1);
        g_devices.push_back(std::move(device));
    }
}

InputBusState::DigitalArray DigitalState(const DIJOYSTATE2& state) noexcept
{
    InputBusState::DigitalArray result{};
    for (std::size_t button = 0; button < AbsoluteInputBusApi::kButtonCount;
         ++button) {
        result[button] = (state.rgbButtons[button] & 0x80U) != 0;
    }
    for (std::size_t pov = 0; pov < AbsoluteInputBusApi::kPovCount; ++pov) {
        for (std::size_t direction = 0; direction < 4; ++direction) {
            result[AbsoluteInputBusApi::kButtonCount + pov * 4 + direction] =
                InputBusState::PovDirectionActive(
                    static_cast<std::int32_t>(state.rgdwPOV[pov]), direction);
        }
    }
    return result;
}

void PublishDeviceSnapshotsLocked(const InputBus::AxisCalibrationMap& calibration)
{
    const auto sequence = ++g_snapshotSequence;
    for (auto& device : g_devices) {
        const auto index = static_cast<int>(device.info.deviceIndex);
        const DIJOYSTATE2* state = DeviceManager::GetCachedState(index);
        const bool connected = state != nullptr;
        const auto digital = connected ? DigitalState(*state) : InputBusState::DigitalArray{};
        InputBusState::UpdateEdges(device.edges, connected, digital);

        auto& output = device.snapshot;
        output.sequence = sequence;
        output.producerGeneration = g_producerGeneration;
        output.connected = connected ? 1U : 0U;
        std::fill(std::begin(output.digitalDown), std::end(output.digitalDown), 0ULL);

        for (std::size_t channel = 0;
             channel < AbsoluteInputBusApi::kDigitalControlCount; ++channel) {
            if (digital[channel]) {
                output.digitalDown[channel / 64] |= 1ULL << (channel % 64);
            }
            output.pressCount[channel] = device.edges.pressCount[channel];
            output.releaseCount[channel] = device.edges.releaseCount[channel];
        }

        for (std::size_t axis = 0; axis < AbsoluteInputBusApi::kAxisCount; ++axis) {
            const int usage = 0x30 + static_cast<int>(axis);
            const int calibrationKey = (index << 8) | usage;
            long minimum = 0;
            long maximum = 65535;
            if (const auto found = calibration.find(calibrationKey);
                found != calibration.end() && found->second.second > found->second.first) {
                minimum = found->second.first;
                maximum = found->second.second;
            }
            const long raw = connected
                ? DeviceManager::GetAxisFromState(state, usage) : 0;
            output.rawAxes[axis] = static_cast<std::int32_t>(raw);
            output.axisMinimum[axis] = static_cast<std::int32_t>(minimum);
            output.axisMaximum[axis] = static_cast<std::int32_t>(maximum);
            if (connected) {
                const double unit = static_cast<double>(raw - minimum) /
                    static_cast<double>(maximum - minimum);
                output.normalizedAxes[axis] = static_cast<float>(
                    std::clamp(unit * 2.0 - 1.0, -1.0, 1.0));
            } else {
                output.normalizedAxes[axis] = 0.0F;
            }
        }
        for (std::size_t pov = 0; pov < AbsoluteInputBusApi::kPovCount; ++pov) {
            output.povHundredths[pov] = connected && LOWORD(state->rgdwPOV[pov]) != 0xFFFF
                ? static_cast<std::int32_t>(state->rgdwPOV[pov]) : -1;
        }
        device.snapshotAvailable = true;
    }

    // Prepare one bounded device-page frame on the controller thread. Stable
    // Control callbacks consume only this copy and never enumerate DirectInput
    // or call back into Input Bus while the host renders.
    std::array<AbsoluteInputBusApi::DeviceInfoV1,
               AbsoluteControlDevices::kMaximumDevices> infos{};
    std::array<AbsoluteInputBusApi::DeviceSnapshotV1,
               AbsoluteControlDevices::kMaximumDevices> snapshots{};
    const auto count = (std::min)(g_devices.size(), infos.size());
    for (std::size_t index = 0; index < count; ++index) {
        infos[index] = g_devices[index].info;
        snapshots[index] = g_devices[index].snapshot;
    }
    AbsoluteControlDeviceProvider::PublishRuntime(
        infos.data(), snapshots.data(), count);
}

CaptureBaseline MakeBaseline(const PublishedDevice& device)
{
    CaptureBaseline baseline;
    baseline.deviceIndex = device.info.deviceIndex;
    std::copy(std::begin(device.snapshot.rawAxes),
              std::end(device.snapshot.rawAxes), baseline.axes.begin());
    std::copy(std::begin(device.snapshot.pressCount),
              std::end(device.snapshot.pressCount),
              baseline.seenPressCount.begin());
    baseline.connected = device.snapshot.connected != 0;
    return baseline;
}

void TakeCaptureBaselineLocked()
{
    g_capture.baselines.clear();
    for (const auto& device : g_devices) {
        if (device.snapshotAvailable && device.snapshot.connected) {
            g_capture.baselines.push_back(MakeBaseline(device));
        }
    }
    g_capture.baselineNeeded = false;
}

CaptureBaseline* FindBaseline(std::uint32_t deviceIndex)
{
    const auto found = std::ranges::find(
        g_capture.baselines, deviceIndex, &CaptureBaseline::deviceIndex);
    return found == g_capture.baselines.end() ? nullptr : &*found;
}

void ReconcileCaptureBaselinesLocked()
{
    for (const auto& device : g_devices) {
        if (!device.snapshotAvailable) continue;
        auto* baseline = FindBaseline(device.info.deviceIndex);
        if (!baseline) {
            if (device.snapshot.connected) {
                g_capture.baselines.push_back(MakeBaseline(device));
            }
            continue;
        }
        if (!device.snapshot.connected) {
            baseline->connected = false;
        } else if (!baseline->connected) {
            *baseline = MakeBaseline(device);
        }
    }
}

bool ChannelAllowed(std::size_t channel) noexcept
{
    return channel < AbsoluteInputBusApi::kButtonCount
        ? (g_capture.allowedControls & AbsoluteInputBusApi::kCaptureButtons) != 0
        : (g_capture.allowedControls & AbsoluteInputBusApi::kCapturePovDirections) != 0;
}

bool ChannelHeld(int deviceIndex, int channel) noexcept
{
    if (deviceIndex < 0 || deviceIndex >= static_cast<int>(g_devices.size()) ||
        channel < 0) return false;
    const auto& snapshot = g_devices[static_cast<std::size_t>(deviceIndex)].snapshot;
    return snapshot.connected && InputBusState::Down(
        snapshot.digitalDown, static_cast<std::size_t>(channel));
}

const char* PovDirectionName(int direction) noexcept
{
    constexpr std::array<const char*, 4> names{"up", "right", "down", "left"};
    return direction >= 0 && direction < static_cast<int>(names.size())
        ? names[static_cast<std::size_t>(direction)] : "unknown";
}

void CommitCaptureLocked(int deviceIndex, ControlKind kind,
                         std::uint32_t controlId)
{
    if (deviceIndex < 0 || deviceIndex >= static_cast<int>(g_devices.size())) {
        g_capture.state = CaptureState::Error;
        g_capture.detail = "The captured DirectInput device is no longer available.";
        return;
    }
    const auto& device = g_devices[static_cast<std::size_t>(deviceIndex)];
    AbsoluteInputBusApi::BindingV1 binding;
    binding.kind = kind;
    binding.deviceIndex = static_cast<std::uint32_t>(deviceIndex);
    binding.controlId = controlId;
    std::memcpy(binding.instanceGuid, device.info.instanceGuid,
                sizeof(binding.instanceGuid));
    Copy(binding.persistentId, device.info.persistentId);
    Copy(binding.productName, device.info.productName);

    std::string text;
    switch (kind) {
    case ControlKind::Button:
        text = std::format("{}@button:{}", device.info.persistentId, controlId);
        break;
    case ControlKind::PovDirection: {
        const auto ordinal = static_cast<int>(controlId) - 129;
        text = std::format("{}@pov:{}:{}", device.info.persistentId,
                           ordinal / 4, PovDirectionName(ordinal % 4));
        break;
    }
    case ControlKind::Axis:
        text = std::format("{}@axis:0x{:02X}", device.info.persistentId,
                           controlId);
        break;
    default:
        g_capture.state = CaptureState::Error;
        g_capture.detail = "The captured control kind was invalid.";
        return;
    }
    Copy(binding.bindingText, text);
    g_capture.binding = binding;
    g_capture.state = CaptureState::Captured;
    g_capture.detail = "DirectInput control captured by AbsoluteHOTAS.";
    RuntimePaths::Log("[InputBus]", std::format(
        "Capture {} for '{}' -> {}", g_capture.sessionId,
        g_capture.consumerId.empty() ? "anonymous" : g_capture.consumerId, text));
}

void PollCaptureLocked(std::chrono::steady_clock::time_point now)
{
    if (g_capture.state != CaptureState::Capturing) return;
    if (g_capture.baselineNeeded) {
        TakeCaptureBaselineLocked();
        return;
    }
    ReconcileCaptureBaselinesLocked();

    int edgeDevice = -1;
    int edgeChannel = -1;
    for (auto& baseline : g_capture.baselines) {
        if (baseline.deviceIndex >= g_devices.size()) continue;
        const auto& snapshot = g_devices[baseline.deviceIndex].snapshot;
        if (!snapshot.connected || !baseline.connected) continue;
        for (std::size_t channel = 0;
             channel < AbsoluteInputBusApi::kDigitalControlCount; ++channel) {
            if (snapshot.pressCount[channel] != baseline.seenPressCount[channel]) {
                baseline.seenPressCount[channel] = snapshot.pressCount[channel];
                if (edgeChannel < 0 && ChannelAllowed(channel)) {
                    edgeDevice = static_cast<int>(baseline.deviceIndex);
                    edgeChannel = static_cast<int>(channel);
                }
            }
        }
    }

    const bool candidateHeld = ChannelHeld(
        g_capture.digitalDebounce.deviceIndex,
        g_capture.digitalDebounce.channel);
    const auto confirmed = InputBusCapturePolicy::UpdateDigital(
        g_capture.digitalDebounce, edgeDevice, edgeChannel, candidateHeld,
        kDigitalCaptureFrames);
    if (confirmed.confirmed) {
        g_capture.targetDevice = confirmed.deviceIndex;
        g_capture.targetChannel = confirmed.channel;
        g_capture.lastTarget = now;
    }

    if ((g_capture.allowedControls & AbsoluteInputBusApi::kCaptureAxes) != 0 &&
        g_capture.targetChannel < 0 &&
        g_capture.digitalDebounce.channel < 0) {
        int bestDevice = -1;
        int bestAxis = -1;
        long bestMovement{};
        for (const auto& baseline : g_capture.baselines) {
            if (baseline.deviceIndex >= g_devices.size()) continue;
            const auto& snapshot = g_devices[baseline.deviceIndex].snapshot;
            if (!snapshot.connected || !baseline.connected) continue;
            for (std::size_t axis = 0; axis < AbsoluteInputBusApi::kAxisCount; ++axis) {
                const auto movement = static_cast<long>(std::llabs(
                    static_cast<long long>(snapshot.rawAxes[axis]) -
                    static_cast<long long>(baseline.axes[axis])));
                if (movement > bestMovement) {
                    bestMovement = movement;
                    bestDevice = static_cast<int>(baseline.deviceIndex);
                    bestAxis = static_cast<int>(axis);
                }
            }
        }
        if (InputBusCapturePolicy::UpdateAxis(
                g_capture.axisDebounce, bestDevice, bestAxis, bestMovement,
                kAxisCaptureThreshold, kAxisCaptureFrames)) {
            CommitCaptureLocked(bestDevice, ControlKind::Axis,
                                0x30U + static_cast<std::uint32_t>(bestAxis));
            return;
        }
    } else {
        g_capture.axisDebounce = {};
    }

    if (g_capture.targetChannel >= 0 &&
        g_capture.digitalDebounce.channel < 0 &&
        now - g_capture.lastTarget >= g_capture.settle) {
        const auto controlId = static_cast<std::uint32_t>(g_capture.targetChannel + 1);
        const auto kind = g_capture.targetChannel <
            static_cast<int>(AbsoluteInputBusApi::kButtonCount)
            ? ControlKind::Button : ControlKind::PovDirection;
        CommitCaptureLocked(g_capture.targetDevice, kind, controlId);
        return;
    }

    if (now - g_capture.started >= g_capture.timeout) {
        if (g_capture.targetChannel >= 0) {
            const auto controlId =
                static_cast<std::uint32_t>(g_capture.targetChannel + 1);
            const auto kind = g_capture.targetChannel <
                static_cast<int>(AbsoluteInputBusApi::kButtonCount)
                ? ControlKind::Button : ControlKind::PovDirection;
            CommitCaptureLocked(g_capture.targetDevice, kind, controlId);
        } else {
            g_capture.state = CaptureState::TimedOut;
            g_capture.detail = "No eligible DirectInput control was detected before timeout.";
        }
    }
}

std::uint32_t __cdecl GetDeviceCount() noexcept
{
    try {
        std::scoped_lock lock(g_mutex);
        return g_ready ? static_cast<std::uint32_t>(g_devices.size()) : 0U;
    } catch (...) { return 0U; }
}

BusResult __cdecl GetDevice(std::uint32_t index,
                            AbsoluteInputBusApi::DeviceInfoV1* output) noexcept
{
    try {
        if (!output || output->structSize < sizeof(*output))
            return BusResult::InvalidArgument;
        std::scoped_lock lock(g_mutex);
        if (!g_ready) return BusResult::NotReady;
        if (index >= g_devices.size()) return BusResult::NotFound;
        *output = g_devices[index].info;
        return BusResult::Ok;
    } catch (...) { return BusResult::NotReady; }
}

BusResult __cdecl GetSnapshot(
    std::uint32_t index, AbsoluteInputBusApi::DeviceSnapshotV1* output) noexcept
{
    try {
        if (!output || output->structSize < sizeof(*output))
            return BusResult::InvalidArgument;
        std::scoped_lock lock(g_mutex);
        if (!g_ready) return BusResult::NotReady;
        if (index >= g_devices.size()) return BusResult::NotFound;
        if (!g_devices[index].snapshotAvailable) return BusResult::NotReady;
        *output = g_devices[index].snapshot;
        return BusResult::Ok;
    } catch (...) { return BusResult::NotReady; }
}

BusResult __cdecl GetProfileState(
    AbsoluteInputBusApi::ProfileStateV1* output) noexcept
{
    try {
        if (!output || output->structSize < sizeof(*output))
            return BusResult::InvalidArgument;
        std::scoped_lock lock(g_mutex);
        if (!g_ready || g_profile.generation == 0) return BusResult::NotReady;
        *output = g_profile;
        return BusResult::Ok;
    } catch (...) { return BusResult::NotReady; }
}

BusResult __cdecl GetRuntimeContext(
    AbsoluteInputBusApi::RuntimeContextV1* output) noexcept
{
    try {
        if (!output || output->structSize < sizeof(*output))
            return BusResult::InvalidArgument;
        std::scoped_lock lock(g_mutex);
        if (!g_ready || !g_contextPublished) return BusResult::NotReady;
        *output = g_context;
        return BusResult::Ok;
    } catch (...) { return BusResult::NotReady; }
}

BusResult __cdecl BeginCapture(
    const AbsoluteInputBusApi::CaptureRequestV1* request,
    std::uint64_t* sessionId) noexcept
{
    try {
        if (!request || request->structSize < sizeof(*request) || !sessionId ||
            request->allowedControls == 0 ||
            (request->allowedControls & ~AbsoluteInputBusApi::kCaptureAll) != 0) {
            return BusResult::InvalidArgument;
        }
        std::scoped_lock lock(g_mutex);
        if (!g_ready) return BusResult::NotReady;
        if (g_capture.state == CaptureState::Capturing) return BusResult::Busy;

        g_capture = {};
        g_capture.state = CaptureState::Capturing;
        if (++g_nextSession == 0) ++g_nextSession;
        g_capture.sessionId = g_nextSession;
        g_capture.allowedControls = request->allowedControls;
        g_capture.settle = std::chrono::milliseconds(
            std::clamp(request->settleMilliseconds, 10U, 1000U));
        g_capture.timeout = std::chrono::milliseconds(
            std::clamp(request->timeoutMilliseconds, 500U, 30000U));
        g_capture.consumerId.assign(
            request->consumerId,
            strnlen_s(request->consumerId,
                      AbsoluteInputBusApi::kConsumerIdCapacity));
        g_capture.detail = "Move or press the DirectInput control to bind.";
        g_capture.started = std::chrono::steady_clock::now();
        const bool havePublishedSnapshot = std::ranges::any_of(
            g_devices, &PublishedDevice::snapshotAvailable);
        if (havePublishedSnapshot) TakeCaptureBaselineLocked();
        else g_capture.baselineNeeded = true;
        *sessionId = g_capture.sessionId;
        return BusResult::Ok;
    } catch (...) { return BusResult::NotReady; }
}

BusResult __cdecl PollCapture(
    std::uint64_t sessionId,
    AbsoluteInputBusApi::CaptureResultV1* output) noexcept
{
    try {
        if (!output || output->structSize < sizeof(*output))
            return BusResult::InvalidArgument;
        std::scoped_lock lock(g_mutex);
        if (!g_ready) return BusResult::NotReady;
        if (sessionId == 0 || sessionId != g_capture.sessionId)
            return BusResult::StaleSession;
        AbsoluteInputBusApi::CaptureResultV1 result;
        result.state = g_capture.state;
        result.sessionId = g_capture.sessionId;
        result.binding = g_capture.binding;
        Copy(result.detail, g_capture.detail);
        *output = result;
        return BusResult::Ok;
    } catch (...) { return BusResult::NotReady; }
}

BusResult __cdecl CancelCapture(std::uint64_t sessionId) noexcept
{
    try {
        std::scoped_lock lock(g_mutex);
        if (!g_ready) return BusResult::NotReady;
        if (sessionId == 0 || sessionId != g_capture.sessionId)
            return BusResult::StaleSession;
        if (g_capture.state == CaptureState::Capturing) {
            g_capture.state = CaptureState::Cancelled;
            g_capture.detail = "DirectInput capture cancelled.";
        }
        return BusResult::Ok;
    } catch (...) { return BusResult::NotReady; }
}

const AbsoluteInputBusApi::ApiV1 g_api{
    .providerId = "absolute.hotas.input-bus",
    .displayName = "Absolute Input Bus",
    .version = Plugin::VersionString.data(),
    .capabilities = AbsoluteInputBusApi::kCapabilitiesV1,
    .getDeviceCount = &GetDeviceCount,
    .getDevice = &GetDevice,
    .getSnapshot = &GetSnapshot,
    .getProfileState = &GetProfileState,
    .getRuntimeContext = &GetRuntimeContext,
    .beginCapture = &BeginCapture,
    .pollCapture = &PollCapture,
    .cancelCapture = &CancelCapture,
};
} // namespace

namespace InputBus {

void Initialize()
{
    DeviceManager::OpenAllDevices();
    std::scoped_lock lock(g_mutex);
    ++g_producerGeneration;
    if (g_producerGeneration == 0) ++g_producerGeneration;
    BuildDeviceTableLocked();
    g_ready = true;
    g_contextPublished = false;
    g_capture = {};
    RuntimePaths::Log("[InputBus]", std::format(
        "Input Bus ABI {} ready with {} DirectInput device(s).",
        AbsoluteInputBusApi::kAbiVersion, g_devices.size()));
}

void Poll(const AxisCalibrationMap& calibration)
{
    std::scoped_lock lock(g_mutex);
    if (!g_ready) return;
    if (DeviceManager::GetDeviceCount() != static_cast<int>(g_devices.size())) {
        DeviceManager::OpenAllDevices();
        ++g_producerGeneration;
        BuildDeviceTableLocked();
        if (g_capture.state == CaptureState::Capturing) {
            g_capture.state = CaptureState::Error;
            g_capture.detail = "The DirectInput device table changed during capture.";
        }
    }
    PublishDeviceSnapshotsLocked(calibration);
    PollCaptureLocked(std::chrono::steady_clock::now());
}

void SetActiveProfile(std::uint32_t slot, std::string_view profileId)
{
    std::scoped_lock lock(g_mutex);
    if (g_capture.state == CaptureState::Capturing) {
        g_capture.state = CaptureState::Cancelled;
        g_capture.detail = "DirectInput capture cancelled because the HOTAS profile changed.";
    }
    g_profile.activeSlot = slot;
    ++g_profile.generation;
    if (g_profile.generation == 0) ++g_profile.generation;
    Copy(g_profile.profileId, profileId.empty() ? std::string_view{"base"} : profileId);
}

void PublishRuntimeContext(const PilotState::Snapshot& snapshot,
                           bool automaticPilotSource)
{
    auto next = InputBusContextPolicy::Build(snapshot, automaticPilotSource);

    std::scoped_lock lock(g_mutex);
    next.sequence = ++g_contextSequence;
    next.producerGeneration = g_producerGeneration;
    const bool changed = !g_contextPublished || next.context != g_context.context ||
        next.validSignals != g_context.validSignals ||
        next.activeSignals != g_context.activeSignals ||
        next.sourceFlags != g_context.sourceFlags;
    next.contextGeneration = g_context.contextGeneration + (changed ? 1U : 0U);
    if (next.contextGeneration == 0) next.contextGeneration = 1;
    g_context = next;
    g_contextPublished = true;
}

void Shutdown()
{
    std::scoped_lock lock(g_mutex);
    g_ready = false;
    g_contextPublished = false;
    g_devices.clear();
    g_profile = {};
    g_context = {};
    g_capture = {};
}

const AbsoluteInputBusApi::ApiV1* GetApi() noexcept { return &g_api; }

} // namespace InputBus

extern "C" ABSOLUTE_INPUT_BUS_API const AbsoluteInputBusApi::ApiV1*
AbsoluteHOTAS_QueryInputBusApi(std::uint32_t requestedAbiVersion) noexcept
{
    return requestedAbiVersion == AbsoluteInputBusApi::kAbiVersion ? &g_api : nullptr;
}
