#include "PCH.h"

#define ABSOLUTE_HOTAS_EXPORTS
#include "AbsoluteHOTASAPI.h"
#include "AbsolutePowerAPI.h"
#include "BindingRef.h"
#include "DeviceManager.h"
#include "Plugin.h"
#include "RuntimePaths.h"
#include "SuiteCommandBindings.h"

namespace {
using HotasResult = AbsoluteHOTASApi::Result;
using CaptureState = AbsoluteHOTASApi::CaptureState;

constexpr std::string_view kPowerModule{"absolute.power"};
constexpr const char* kPowerSection = "Suite.AbsolutePower";
constexpr auto kCaptureTimeout = std::chrono::seconds(8);
constexpr auto kCaptureSettle = std::chrono::milliseconds(50);

template <std::size_t Size>
void Copy(char (&destination)[Size], std::string_view value) {
    std::ranges::fill(destination, '\0');
    std::memcpy(destination, value.data(), std::min(value.size(), Size - 1));
}

std::string Bounded(const char* value, std::size_t capacity) {
    return value ? std::string(value, strnlen_s(value, capacity)) : std::string{};
}

struct Entry {
    std::string commandId;
    std::string label;
    std::string bindingText;
    BindingRef binding{"", -1, -1};
    bool previousDown{};
};

struct DeviceButtons {
    int deviceIndex{-1};
    std::array<BYTE, 128> buttons{};
    std::array<DWORD, 4> povs{};
};

struct Capture {
    CaptureState state{CaptureState::Idle};
    std::string commandId;
    std::string binding;
    std::string detail;
    std::vector<DeviceButtons> previous;
    int candidateDevice{-1};
    int candidateValue{-1};
    int candidateFrames{};
    int targetDevice{-1};
    int targetValue{-1};
    std::chrono::steady_clock::time_point started{};
    std::chrono::steady_clock::time_point lastTarget{};
    bool snapshotNeeded{};
};

std::mutex g_mutex;
std::vector<Entry> g_entries;
std::unordered_map<std::string, std::string> g_commands;
const AbsolutePowerApi::ApiV1* g_powerApi{};
Capture g_capture;
bool g_initialized{};
std::chrono::steady_clock::time_point g_nextPowerRefresh{};

bool IsPovActive(DWORD pov, int direction) {
    if (LOWORD(pov) == 0xFFFF) return false;
    constexpr std::array<DWORD, 4> angles{0, 9000, 18000, 27000};
    const DWORD target = angles[static_cast<std::size_t>(direction)];
    DWORD difference = pov > target ? pov - target : target - pov;
    if (difference > 18000) difference = 36000 - difference;
    return difference <= 4500;
}

bool IsHeld(int deviceIndex, int value) {
    const auto* state = DeviceManager::GetCachedState(deviceIndex);
    if (!state) return false;
    if (value >= 1 && value <= 128) return (state->rgbButtons[value - 1] & 0x80) != 0;
    if (value >= 129 && value <= 144) {
        const int pov = (value - 129) / 4;
        const int direction = (value - 129) % 4;
        return IsPovActive(state->rgdwPOV[pov], direction);
    }
    return false;
}

void ResolveBinding(Entry& entry) {
    auto& ref = entry.binding;
    if (!ref.IsValid() || ref.value < 1 || ref.value > 144) return;
    int deviceIndex = -1;
    if (ref.HasIndex()) {
        deviceIndex = ref.deviceIndex < DeviceManager::GetDeviceCount() ? ref.deviceIndex : -1;
    } else if (ref.HasDevice()) {
        deviceIndex = DeviceManager::ResolveByName(ref.deviceName);
    } else if (DeviceManager::GetDeviceCount() > 0) {
        deviceIndex = 0;
    }
    if (deviceIndex >= 0) {
        ref.deviceIndex = deviceIndex;
        DeviceManager::OpenDevice(deviceIndex);
    }
}

bool HasDuplicateName(int deviceIndex) {
    const auto& name = DeviceManager::GetDevice(deviceIndex).productName;
    for (int index = 0; index < DeviceManager::GetDeviceCount(); ++index) {
        if (index != deviceIndex && DeviceManager::GetDevice(index).productName == name) return true;
    }
    return false;
}

std::string FormatCaptured(int deviceIndex, int value) {
    const auto& device = DeviceManager::GetDevice(deviceIndex);
    if (HasDuplicateName(deviceIndex)) return std::format("#{}@{}", deviceIndex, value);
    return std::format("{}@{}", device.productName, value);
}

Entry* FindEntry(std::string_view commandId) {
    const auto found = std::ranges::find(g_entries, commandId, &Entry::commandId);
    return found == g_entries.end() ? nullptr : &*found;
}

bool Persist(std::string_view commandId, const char* binding) {
    const auto path = RuntimePaths::CustomIniPath().string();
    return WritePrivateProfileStringA(kPowerSection, std::string(commandId).c_str(), binding,
                                      path.c_str()) != FALSE;
}

void LoadPersisted() {
    g_entries.clear();
    std::vector<char> section(64 * 1024, '\0');
    const auto path = RuntimePaths::CustomIniPath().string();
    GetPrivateProfileSectionA(kPowerSection, section.data(),
                              static_cast<DWORD>(section.size()), path.c_str());
    for (const char* record = section.data(); *record; record += std::strlen(record) + 1) {
        const std::string_view line(record);
        const auto separator = line.find('=');
        if (separator == std::string_view::npos || separator == 0) continue;
        Entry entry;
        entry.commandId = line.substr(0, separator);
        entry.bindingText = line.substr(separator + 1);
        entry.binding = ParseBindingRef(entry.bindingText.c_str(), -1);
        if (!entry.binding.IsValid() || entry.binding.value < 1 || entry.binding.value > 144) continue;
        ResolveBinding(entry);
        g_entries.push_back(std::move(entry));
    }
}

bool PowerApiValid(const AbsolutePowerApi::ApiV1* api) {
    constexpr std::size_t minimum =
        offsetof(AbsolutePowerApi::ApiV1, invokeCommand) +
        sizeof(api->invokeCommand);
    return api && api->structSize >= minimum &&
           api->abiVersion == AbsolutePowerApi::kAbiVersion && api->moduleId &&
           std::string_view(api->moduleId) == kPowerModule && api->getCommandCount &&
           api->getCommand && api->invokeCommand;
}

void RefreshPowerCommands() {
    const auto now = std::chrono::steady_clock::now();
    if (now < g_nextPowerRefresh) return;
    g_nextPowerRefresh = now + std::chrono::seconds(1);

    if (!g_powerApi) {
        using Query = const AbsolutePowerApi::ApiV1*(__cdecl*)(std::uint32_t) noexcept;
        if (const HMODULE module = GetModuleHandleW(L"AbsolutePower.dll")) {
            if (const FARPROC address = GetProcAddress(module, "AbsolutePower_QueryApi")) {
                const auto candidate = reinterpret_cast<Query>(address)(AbsolutePowerApi::kAbiVersion);
                if (PowerApiValid(candidate)) g_powerApi = candidate;
            }
        }
    }
    if (!g_powerApi) return;

    std::unordered_map<std::string, std::string> commands;
    const auto count = std::min<std::uint32_t>(g_powerApi->getCommandCount(), 512);
    for (std::uint32_t index = 0; index < count; ++index) {
        AbsolutePowerApi::CommandV1 command{};
        if (g_powerApi->getCommand(index, &command) != AbsolutePowerApi::Result::Ok) continue;
        commands.emplace(Bounded(command.id, AbsolutePowerApi::kIdCapacity),
                         Bounded(command.label, AbsolutePowerApi::kLabelCapacity));
    }
    g_commands = std::move(commands);
    for (auto& entry : g_entries) {
        if (const auto found = g_commands.find(entry.commandId); found != g_commands.end()) {
            entry.label = found->second;
        }
    }
}

void TakeCaptureSnapshot() {
    g_capture.previous.clear();
    DeviceManager::OpenAllDevices();
    for (int index = 0; index < DeviceManager::GetDeviceCount(); ++index) {
        const auto* state = DeviceManager::GetCachedState(index);
        if (!state) continue;
        DeviceButtons snapshot;
        snapshot.deviceIndex = index;
        std::memcpy(snapshot.buttons.data(), state->rgbButtons, snapshot.buttons.size());
        std::memcpy(snapshot.povs.data(), state->rgdwPOV, sizeof(state->rgdwPOV));
        g_capture.previous.push_back(snapshot);
    }
    g_capture.snapshotNeeded = false;
}

void UpdatePrevious() {
    for (auto& previous : g_capture.previous) {
        const auto* state = DeviceManager::GetCachedState(previous.deviceIndex);
        if (!state) continue;
        std::memcpy(previous.buttons.data(), state->rgbButtons, previous.buttons.size());
        std::memcpy(previous.povs.data(), state->rgdwPOV, sizeof(state->rgdwPOV));
    }
}

void CommitCapture() {
    const std::string binding = FormatCaptured(g_capture.targetDevice, g_capture.targetValue);
    const auto conflict = std::ranges::find_if(g_entries, [&](const Entry& entry) {
        return entry.commandId != g_capture.commandId && entry.bindingText == binding;
    });
    if (conflict != g_entries.end()) {
        g_capture.state = CaptureState::Error;
        g_capture.detail = std::format("That control is already assigned to '{}'.", conflict->label.empty()
            ? conflict->commandId : conflict->label);
        return;
    }
    if (!Persist(g_capture.commandId, binding.c_str())) {
        g_capture.state = CaptureState::Error;
        g_capture.detail = "AbsoluteHOTAS_Custom.ini could not be updated.";
        return;
    }
    Entry* entry = FindEntry(g_capture.commandId);
    if (!entry) {
        Entry added;
        added.commandId = g_capture.commandId;
        g_entries.push_back(std::move(added));
        entry = &g_entries.back();
    }
    entry->bindingText = binding;
    entry->binding = ParseBindingRef(binding.c_str(), -1);
    entry->previousDown = true;
    ResolveBinding(*entry);
    if (const auto found = g_commands.find(entry->commandId); found != g_commands.end()) {
        entry->label = found->second;
    }
    g_capture.binding = binding;
    g_capture.detail = "HOTAS button saved by AbsoluteHOTAS.";
    g_capture.state = CaptureState::Captured;
    RuntimePaths::Log("[SuiteBindings]", "Bound " + g_capture.commandId + " to " + binding);
}

void PollCapture() {
    if (g_capture.state != CaptureState::Capturing) return;
    if (g_capture.snapshotNeeded) {
        TakeCaptureSnapshot();
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    int edgeDevice = -1;
    int edgeValue = -1;
    for (const auto& previous : g_capture.previous) {
        if (edgeValue >= 0) break;
        const auto* state = DeviceManager::GetCachedState(previous.deviceIndex);
        if (!state) continue;
        for (int button = 0; button < 128 && edgeValue < 0; ++button) {
            if ((state->rgbButtons[button] & 0x80) && !(previous.buttons[button] & 0x80)) {
                edgeDevice = previous.deviceIndex;
                edgeValue = button + 1;
            }
        }
        for (int pov = 0; pov < 4 && edgeValue < 0; ++pov) {
            for (int direction = 0; direction < 4 && edgeValue < 0; ++direction) {
                if (IsPovActive(state->rgdwPOV[pov], direction) &&
                    !IsPovActive(previous.povs[pov], direction)) {
                    edgeDevice = previous.deviceIndex;
                    edgeValue = 129 + pov * 4 + direction;
                }
            }
        }
    }
    if (edgeValue >= 0) {
        g_capture.candidateDevice = edgeDevice;
        g_capture.candidateValue = edgeValue;
        g_capture.candidateFrames = 1;
    } else if (g_capture.candidateValue >= 0) {
        if (IsHeld(g_capture.candidateDevice, g_capture.candidateValue)) {
            if (++g_capture.candidateFrames >= 2) {
                g_capture.targetDevice = g_capture.candidateDevice;
                g_capture.targetValue = g_capture.candidateValue;
                g_capture.lastTarget = now;
                g_capture.candidateDevice = -1;
                g_capture.candidateValue = -1;
                g_capture.candidateFrames = 0;
            }
        } else {
            g_capture.candidateDevice = -1;
            g_capture.candidateValue = -1;
            g_capture.candidateFrames = 0;
        }
    }
    UpdatePrevious();
    if (g_capture.targetValue >= 0 && g_capture.candidateValue < 0 &&
        now - g_capture.lastTarget >= kCaptureSettle) {
        CommitCapture();
    } else if (now - g_capture.started >= kCaptureTimeout) {
        g_capture.state = CaptureState::TimedOut;
        g_capture.detail = "No new HOTAS button press was detected within eight seconds.";
    }
}

HotasResult GetCommandBinding(const char* moduleId, const char* commandId,
                              AbsoluteHOTASApi::CommandBindingV1* output) noexcept {
    try {
        if (!moduleId || !commandId || !output || output->structSize < sizeof(*output) ||
            std::string_view(moduleId) != kPowerModule) return HotasResult::InvalidArgument;
        std::scoped_lock lock(g_mutex);
        if (!g_initialized) return HotasResult::NotReady;
        const Entry* entry = FindEntry(commandId);
        if (!entry) return HotasResult::NotFound;
        AbsoluteHOTASApi::CommandBindingV1 value{};
        Copy(value.moduleId, moduleId);
        Copy(value.commandId, entry->commandId);
        Copy(value.label, entry->label);
        Copy(value.binding, entry->bindingText);
        *output = value;
        return HotasResult::Ok;
    } catch (...) { return HotasResult::NotReady; }
}

HotasResult ClearCommandBinding(const char* moduleId, const char* commandId) noexcept {
    try {
        if (!moduleId || !commandId || std::string_view(moduleId) != kPowerModule)
            return HotasResult::InvalidArgument;
        std::scoped_lock lock(g_mutex);
        if (!g_initialized) return HotasResult::NotReady;
        if (!Persist(commandId, nullptr)) return HotasResult::WriteFailure;
        std::erase_if(g_entries, [commandId](const Entry& entry) { return entry.commandId == commandId; });
        return HotasResult::Ok;
    } catch (...) { return HotasResult::NotReady; }
}

HotasResult BeginButtonCapture(const char* moduleId, const char* commandId) noexcept {
    try {
        if (!moduleId || !commandId || std::string_view(moduleId) != kPowerModule)
            return HotasResult::InvalidArgument;
        std::scoped_lock lock(g_mutex);
        if (!g_initialized) return HotasResult::NotReady;
        if (g_capture.state == CaptureState::Capturing) return HotasResult::Busy;
        // A plan may have been created and saved from this same Power panel.
        // Refresh here so it can be bound immediately instead of waiting for
        // the controller's one-second background discovery interval.
        g_nextPowerRefresh = {};
        RefreshPowerCommands();
        if (!g_commands.contains(commandId)) return HotasResult::NotFound;
        g_capture = {};
        g_capture.state = CaptureState::Capturing;
        g_capture.commandId = commandId;
        g_capture.detail = "Press a HOTAS button or POV direction.";
        g_capture.started = std::chrono::steady_clock::now();
        g_capture.snapshotNeeded = true;
        return HotasResult::Ok;
    } catch (...) { return HotasResult::NotReady; }
}

HotasResult PollButtonCapture(AbsoluteHOTASApi::CaptureV1* output) noexcept {
    try {
        if (!output || output->structSize < sizeof(*output)) return HotasResult::InvalidArgument;
        std::scoped_lock lock(g_mutex);
        if (!g_initialized) return HotasResult::NotReady;
        AbsoluteHOTASApi::CaptureV1 value{};
        value.state = g_capture.state;
        Copy(value.moduleId, kPowerModule);
        Copy(value.commandId, g_capture.commandId);
        Copy(value.binding, g_capture.binding);
        Copy(value.detail, g_capture.detail);
        *output = value;
        return HotasResult::Ok;
    } catch (...) { return HotasResult::NotReady; }
}

HotasResult CancelButtonCapture() noexcept {
    try {
        std::scoped_lock lock(g_mutex);
        if (!g_initialized) return HotasResult::NotReady;
        if (g_capture.state == CaptureState::Capturing) {
            g_capture.state = CaptureState::Cancelled;
            g_capture.detail = "HOTAS capture cancelled.";
        }
        return HotasResult::Ok;
    } catch (...) { return HotasResult::NotReady; }
}

const AbsoluteHOTASApi::ApiV1 g_api{
    .moduleId = "absolute.hotas",
    .displayName = "AbsoluteHOTAS",
    .version = Plugin::VersionString.data(),
    .getCommandBinding = &GetCommandBinding,
    .clearCommandBinding = &ClearCommandBinding,
    .beginButtonCapture = &BeginButtonCapture,
    .pollButtonCapture = &PollButtonCapture,
    .cancelButtonCapture = &CancelButtonCapture,
};
} // namespace

namespace SuiteCommandBindings {
void Initialize() {
    std::scoped_lock lock(g_mutex);
    DeviceManager::OpenAllDevices();
    LoadPersisted();
    g_initialized = true;
    g_nextPowerRefresh = {};
    RefreshPowerCommands();
    RuntimePaths::Log("[SuiteBindings]", "Suite command binding service ready.");
}

void Reload() {
    std::scoped_lock lock(g_mutex);
    if (!g_initialized) return;
    LoadPersisted();
    g_nextPowerRefresh = {};
}

void Poll() {
    std::scoped_lock lock(g_mutex);
    if (!g_initialized) return;
    RefreshPowerCommands();
    PollCapture();
    const bool suppress = g_capture.state == CaptureState::Capturing;
    for (auto& entry : g_entries) {
        const bool down = entry.binding.IsValid() && IsHeld(entry.binding.deviceIndex, entry.binding.value);
        if (!suppress && down && !entry.previousDown && g_powerApi && g_commands.contains(entry.commandId)) {
            const auto result = g_powerApi->invokeCommand(entry.commandId.c_str());
            RuntimePaths::Log("[SuiteBindings]", std::format("{} -> {}", entry.commandId,
                result == AbsolutePowerApi::Result::Ok ? "invoked" : "rejected"));
        }
        entry.previousDown = down;
    }
}

void Shutdown() {
    std::scoped_lock lock(g_mutex);
    g_initialized = false;
    g_entries.clear();
    g_commands.clear();
    g_powerApi = nullptr;
    g_capture = {};
}

const AbsoluteHOTASApi::ApiV1* GetApi() noexcept { return &g_api; }
} // namespace SuiteCommandBindings

extern "C" ABSOLUTE_HOTAS_API const AbsoluteHOTASApi::ApiV1*
AbsoluteHOTAS_QueryApi(std::uint32_t requestedAbiVersion) noexcept {
    return requestedAbiVersion == AbsoluteHOTASApi::kAbiVersion ? &g_api : nullptr;
}
