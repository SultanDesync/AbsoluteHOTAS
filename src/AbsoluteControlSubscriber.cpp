#include "AbsoluteControlSubscriber.h"

#include "AbsoluteControlSettings.h"
#include "Plugin.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <format>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace {
using namespace AbsoluteControlPanelApi;

constexpr std::string_view kHotasModuleId = "absolute.hotas";

std::atomic<const ApiV1*> g_hostApi{};
std::atomic_bool g_registered{};
std::atomic_bool g_terminalRejection{};
std::atomic_bool g_forceReadException{};
std::mutex g_registrationMutex;

std::atomic_bool g_throttleHookInstalled{};
std::atomic_bool g_nativeControlsInitialized{};
std::atomic_bool g_controllerStarted{};
std::atomic_bool g_legacyWorkbenchConfigured{};
std::atomic_bool g_legacyWorkbenchInstalled{};

struct SettingsSession {
    bool loaded{};
    bool dirty{};
    AbsoluteControlSettings::ScalarState saved{};
    AbsoluteControlSettings::ScalarState draft{};
    AbsoluteControlSettings::Revision revision{};
    std::string lastError;
};

std::mutex g_settingsMutex;
SettingsSession g_settings;

const ApiV1 g_incompatibleHostApi = [] {
    ApiV1 api;
    api.structSize = 0;
    api.abiVersion = 0;
    return api;
}();

template <std::size_t N>
void Copy(char (&target)[N], std::string_view source) noexcept
{
    const auto count = (std::min)(source.size(), N - 1);
    std::memcpy(target, source.data(), count);
    target[count] = '\0';
}

template <std::size_t N>
bool Terminated(const char (&value)[N]) noexcept
{
    return std::memchr(value, '\0', N) != nullptr;
}

ValueV1 BooleanValue(bool value) noexcept
{
    ValueV1 result;
    result.kind = ValueKind::Boolean;
    result.booleanValue = value ? 1U : 0U;
    return result;
}

ValueV1 IntegerValue(std::int64_t value) noexcept
{
    ValueV1 result;
    result.kind = ValueKind::Integer;
    result.integerValue = value;
    return result;
}

ValueV1 FloatValue(double value) noexcept
{
    ValueV1 result;
    result.kind = ValueKind::Float;
    result.floatValue = value;
    return result;
}

ValueV1 StringValue(std::string_view value) noexcept
{
    ValueV1 result;
    result.kind = ValueKind::String;
    Copy(result.stringValue, value);
    return result;
}

ControlDescriptorV1 ReadOnlyStatus(std::string_view id, std::string_view label,
                                   std::string_view description) noexcept
{
    ControlDescriptorV1 control;
    control.kind = ControlKind::InputBinding;
    control.flags = kControlReadOnly;
    Copy(control.controlId, id);
    Copy(control.label, label);
    Copy(control.description, description);
    return control;
}

ControlDescriptorV1 Toggle(std::string_view id, std::string_view label,
                           std::string_view description) noexcept
{
    ControlDescriptorV1 control;
    control.kind = ControlKind::Toggle;
    Copy(control.controlId, id);
    Copy(control.label, label);
    Copy(control.description, description);
    return control;
}

ControlDescriptorV1 FloatSlider(std::string_view id, std::string_view label,
                                std::string_view description, double minimum,
                                double maximum, double step) noexcept
{
    ControlDescriptorV1 control;
    control.kind = ControlKind::FloatSlider;
    Copy(control.controlId, id);
    Copy(control.label, label);
    Copy(control.description, description);
    control.minimumValue = minimum;
    control.maximumValue = maximum;
    control.stepValue = step;
    return control;
}

ControlDescriptorV1 IntegerSlider(std::string_view id, std::string_view label,
                                  std::string_view description, double minimum,
                                  double maximum, double step) noexcept
{
    auto control = FloatSlider(id, label, description, minimum, maximum, step);
    control.kind = ControlKind::IntegerSlider;
    return control;
}

ControlDescriptorV1 Choice(std::string_view id, std::string_view label,
                           std::string_view description, double minimum,
                           double maximum, double step) noexcept
{
    ControlDescriptorV1 control;
    control.kind = ControlKind::Choice;
    Copy(control.controlId, id);
    Copy(control.label, label);
    Copy(control.description, description);
    control.minimumValue = minimum;
    control.maximumValue = maximum;
    control.stepValue = step;
    return control;
}

const ModuleDescriptorV1 g_module = [] {
    ModuleDescriptorV1 module;
    Copy(module.moduleId, kHotasModuleId);
    Copy(module.displayName, "AbsoluteHOTAS");
    Copy(module.description,
         "Standalone HOTAS/HOSAS flight controls, bindings, profiles, and runtime status.");
    return module;
}();

const std::array g_setupControls{
    ReadOnlyStatus("setup-control-host", "Absolute Control",
        "Optional native menu connection; flight controls do not depend on the host."),
    ReadOnlyStatus("setup-flight-runtime", "Flight runtime",
        "Startup state of the standalone injection hook and controller poller."),
    ReadOnlyStatus("setup-configuration", "Configuration owner",
        "AbsoluteHOTAS owns defaults, custom settings, profiles, validation, and reload."),
    ReadOnlyStatus("setup-legacy-workbench", "Legacy workbench",
        "The embedded workbench remains an optional transition and fallback frontend."),
    ReadOnlyStatus("setup-suite-modules", "Optional suite modules",
        "Head Tracking and Mouse Alignment remain separately installed, separately owned modules."),
};

const std::array g_flightAxisControls{
    Toggle("flight-controls-enabled", "Flight controls enabled",
        "Enable AbsoluteHOTAS flight-axis injection for the Main controls configuration."),
    Toggle("pitch-inverted", "Invert pitch",
        "Reverse the sign of the physical pitch axis before flight-axis injection."),
    FloatSlider("pitch-sensitivity", "Pitch sensitivity",
        "Scale pitch response before curves and game-side flight processing.", 0.1, 3.0, 0.05),
    ReadOnlyStatus("flight-axes-scope", "H1 migration scope",
        "Axis binding capture, response graphs, and the remaining axes stay in the legacy workbench until their provider components land."),
};

const std::array g_diagnosticControls{
    Choice("pilot-context-mode", "Outside-pilot-seat behavior",
        "Choose how much HOTAS output is parked when automatic pilot context reports that the player is not flying.",
        0.0, 2.0, 1.0),
    Toggle("automatic-pilot-detection", "Automatic pilot detection",
        "Use the exact-gated native pilot signal instead of treating the pilot context as manually managed."),
    IntegerSlider("pilot-latch-ms", "Pilot signal latch",
        "Keep the last valid pilot signal for this many milliseconds during transient game-state changes.", 500.0, 30000.0, 500.0),
    ReadOnlyStatus("diagnostics-control-session", "Control editing session",
        "Provider-owned draft, persistence, reload, and stale-revision status."),
    ReadOnlyStatus("diagnostics-compatibility", "Compatibility",
        "AbsoluteHOTAS version, Control ABI, and exact-gated Starfield runtime family."),
    ReadOnlyStatus("diagnostics-native-controls", "Native control seams",
        "Startup result for exact-gated ship-control and flight-writer seams."),
    ReadOnlyStatus("diagnostics-controller", "Controller service",
        "Standalone DirectInput polling and flight-control execution state."),
    ReadOnlyStatus("diagnostics-frontends", "Frontends",
        "Absolute Control connection and embedded-workbench fallback state."),
    ReadOnlyStatus("diagnostics-coordination", "Suite coordination",
        "Current standalone ownership and the deferred optional headless runtime boundary."),
};

bool Equivalent(const AbsoluteControlSettings::ScalarState& left,
                const AbsoluteControlSettings::ScalarState& right) noexcept
{
    return left.flightControlsEnabled == right.flightControlsEnabled &&
           left.pitchInverted == right.pitchInverted &&
           std::abs(left.pitchSensitivity - right.pitchSensitivity) <= 0.0001 &&
           left.pilotContextMode == right.pilotContextMode &&
           left.automaticPilotDetection == right.automaticPilotDetection &&
           left.pilotLatchMilliseconds == right.pilotLatchMilliseconds;
}

bool LoadSessionLocked(bool refreshClean) noexcept
{
    if (g_settings.loaded && (!refreshClean || g_settings.dirty ||
        AbsoluteControlSettings::CurrentRevision() == g_settings.revision)) {
        return true;
    }

    AbsoluteControlSettings::ScalarState state;
    AbsoluteControlSettings::Revision revision;
    std::string error;
    if (!AbsoluteControlSettings::Load(state, revision, error)) {
        g_settings.loaded = false;
        g_settings.lastError = error.empty() ?
            "AbsoluteHOTAS could not read its settings." : std::move(error);
        return false;
    }
    g_settings.loaded = true;
    g_settings.dirty = false;
    g_settings.saved = state;
    g_settings.draft = state;
    g_settings.revision = revision;
    g_settings.lastError.clear();
    return true;
}

Result ReadSettingsValue(std::string_view id, ValueV1& output) noexcept
{
    if (id != "flight-controls-enabled" && id != "pitch-inverted" &&
        id != "pitch-sensitivity" && id != "pilot-context-mode" &&
        id != "automatic-pilot-detection" && id != "pilot-latch-ms") {
        return Result::NotFound;
    }
    std::scoped_lock lock(g_settingsMutex);
    if (!LoadSessionLocked(true)) return Result::Rejected;

    if (id == "flight-controls-enabled") {
        output = BooleanValue(g_settings.draft.flightControlsEnabled);
    } else if (id == "pitch-inverted") {
        output = BooleanValue(g_settings.draft.pitchInverted);
    } else if (id == "pitch-sensitivity") {
        output = FloatValue(g_settings.draft.pitchSensitivity);
    } else if (id == "pilot-context-mode") {
        output = IntegerValue(g_settings.draft.pilotContextMode);
    } else if (id == "automatic-pilot-detection") {
        output = BooleanValue(g_settings.draft.automaticPilotDetection);
    } else if (id == "pilot-latch-ms") {
        output = IntegerValue(g_settings.draft.pilotLatchMilliseconds);
    } else {
        return Result::NotFound;
    }
    return Result::Ok;
}

Result ReadStatusValue(std::string_view id, ValueV1& output) noexcept
{
    if (id == "setup-control-host") {
        output = StringValue(
            "Connected through ABI 1; HOTAS gameplay remains independently initialized.");
    } else if (id == "setup-flight-runtime") {
        output = StringValue(std::format(
            "Flight hook {} | controller {}",
            g_throttleHookInstalled.load(std::memory_order_acquire) ? "ready" : "unavailable",
            g_controllerStarted.load(std::memory_order_acquire) ? "running" : "not running"));
    } else if (id == "setup-configuration") {
        output = StringValue(
            "AbsoluteHOTAS.ini -> AbsoluteHOTAS_Custom.ini -> active profile overlay");
    } else if (id == "setup-legacy-workbench") {
        const bool configured =
            g_legacyWorkbenchConfigured.load(std::memory_order_acquire);
        const bool installed =
            g_legacyWorkbenchInstalled.load(std::memory_order_acquire);
        output = StringValue(!configured ?
            "Disabled by [UI] bEnableWorkbench; manual configuration remains available." :
            installed ? "Available as the supported transition/fallback frontend." :
                        "Configured but renderer hooks were unavailable; gameplay is unaffected.");
    } else if (id == "setup-suite-modules") {
        output = StringValue(
            "Camera pose: Absolute Head Tracking | mouse centering: AbsoluteZero");
    } else if (id == "flight-axes-scope") {
        output = StringValue(
            "Pitch scalar settings are native here; DirectInput capture and live visuals remain in the transition workbench.");
    } else if (id == "diagnostics-control-session") {
        std::scoped_lock lock(g_settingsMutex);
        if (!g_settings.lastError.empty()) {
            output = StringValue(g_settings.lastError);
        } else if (g_settings.dirty) {
            output = StringValue("Draft has unapplied HOTAS changes.");
        } else {
            output = StringValue("Settings read-back is synchronized with the HOTAS-owned files.");
        }
    } else if (id == "diagnostics-compatibility") {
        output = StringValue(std::format(
            "AbsoluteHOTAS {} | Control ABI 1 | Starfield 1.16.242/1.16.244 exact gates",
            Plugin::VersionString));
    } else if (id == "diagnostics-native-controls") {
        output = StringValue(
            g_nativeControlsInitialized.load(std::memory_order_acquire) ?
                "At least one exact-gated native control seam initialized." :
                "Native control seams unavailable; affected paths remain fail-closed.");
    } else if (id == "diagnostics-controller") {
        output = StringValue(
            g_controllerStarted.load(std::memory_order_acquire) ?
                "DirectInput poller running under AbsoluteHOTAS ownership." :
                "Controller poller not running; configuration and diagnostics remain available.");
    } else if (id == "diagnostics-frontends") {
        output = StringValue(std::format(
            "Absolute Control connected | embedded workbench {}",
            g_legacyWorkbenchInstalled.load(std::memory_order_acquire) ?
                "available" : "unavailable or disabled"));
    } else if (id == "diagnostics-coordination") {
        output = StringValue(
            "Standalone HOTAS seam ownership; optional Absolute Flight Runtime is deferred.");
    } else {
        return Result::NotFound;
    }
    return Result::Ok;
}

Result __cdecl ReadValue(void*, const char* rawId, ValueV1* output) noexcept
{
    if (!rawId || !output || output->structSize < sizeof(ValueV1)) {
        return Result::InvalidArgument;
    }
    try {
        if (g_forceReadException.load(std::memory_order_acquire)) {
            throw std::runtime_error("forced provider callback failure");
        }
        const std::string_view id{rawId};
        const auto settingsResult = ReadSettingsValue(id, *output);
        return settingsResult == Result::NotFound ?
            ReadStatusValue(id, *output) : settingsResult;
    } catch (...) {
        return Result::Rejected;
    }
}

Result __cdecl WriteDraft(void*, const char* rawId,
                          const ValueV1* value) noexcept
{
    if (!rawId || !value || value->structSize < sizeof(ValueV1)) {
        return Result::InvalidArgument;
    }
    try {
        if (!AbsoluteControlSettings::CanEdit()) return Result::Rejected;
        std::scoped_lock lock(g_settingsMutex);
        if (!LoadSessionLocked(true)) return Result::Rejected;

        const std::string_view id{rawId};
        if (id == "flight-controls-enabled") {
            if (value->kind != ValueKind::Boolean || value->booleanValue > 1) {
                return Result::InvalidArgument;
            }
            g_settings.draft.flightControlsEnabled = value->booleanValue != 0;
        } else if (id == "pitch-inverted") {
            if (value->kind != ValueKind::Boolean || value->booleanValue > 1) {
                return Result::InvalidArgument;
            }
            g_settings.draft.pitchInverted = value->booleanValue != 0;
        } else if (id == "pitch-sensitivity") {
            if (value->kind != ValueKind::Float || !std::isfinite(value->floatValue) ||
                value->floatValue < 0.1 || value->floatValue > 3.0) {
                return Result::InvalidArgument;
            }
            g_settings.draft.pitchSensitivity = value->floatValue;
        } else if (id == "pilot-context-mode") {
            if (value->kind != ValueKind::Integer || value->integerValue < 0 ||
                value->integerValue > 2) {
                return Result::InvalidArgument;
            }
            g_settings.draft.pilotContextMode =
                static_cast<int>(value->integerValue);
        } else if (id == "automatic-pilot-detection") {
            if (value->kind != ValueKind::Boolean || value->booleanValue > 1) {
                return Result::InvalidArgument;
            }
            g_settings.draft.automaticPilotDetection = value->booleanValue != 0;
        } else if (id == "pilot-latch-ms") {
            if (value->kind != ValueKind::Integer || value->integerValue < 500 ||
                value->integerValue > 30000 || value->integerValue % 500 != 0) {
                return Result::InvalidArgument;
            }
            g_settings.draft.pilotLatchMilliseconds =
                static_cast<int>(value->integerValue);
        } else {
            return Result::NotFound;
        }

        g_settings.dirty = !Equivalent(g_settings.draft, g_settings.saved);
        g_settings.lastError.clear();
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

Result __cdecl ApplyDraft(void*) noexcept
{
    try {
        if (!AbsoluteControlSettings::CanEdit()) return Result::Rejected;
        std::scoped_lock lock(g_settingsMutex);
        if (!LoadSessionLocked(true)) return Result::Rejected;
        if (!g_settings.dirty) return Result::Ok;
        if (AbsoluteControlSettings::CurrentRevision().sourceFingerprint !=
            g_settings.revision.sourceFingerprint) {
            g_settings.lastError =
                "The HOTAS configuration changed after this draft was opened; discard it and try again.";
            return Result::Rejected;
        }

        AbsoluteControlSettings::ScalarState readBack;
        AbsoluteControlSettings::Revision revision;
        std::string error;
        if (!AbsoluteControlSettings::Apply(
                g_settings.draft, g_settings.revision, readBack, revision, error)) {
            g_settings.lastError = error.empty() ?
                "AbsoluteHOTAS could not persist this draft." : std::move(error);
            return Result::WriteFailure;
        }

        g_settings.saved = readBack;
        g_settings.draft = readBack;
        g_settings.revision = revision;
        g_settings.dirty = false;
        g_settings.lastError.clear();
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

void __cdecl CancelDraft(void*) noexcept
{
    try {
        std::scoped_lock lock(g_settingsMutex);
        g_settings.loaded = false;
        g_settings.dirty = false;
        (void)LoadSessionLocked(false);
    } catch (...) {
        std::scoped_lock lock(g_settingsMutex);
        g_settings = {};
        g_settings.lastError = "AbsoluteHOTAS could not restore the persisted settings.";
    }
}

Result __cdecl ReadChoiceOptions(void*, const char* rawId,
                                 ChoiceOptionV1* options,
                                 std::uint32_t capacity,
                                 std::uint32_t* outputCount) noexcept
{
    if (!rawId || !outputCount) return Result::InvalidArgument;
    try {
        if (std::string_view(rawId) != "pilot-context-mode") {
            *outputCount = 0;
            return Result::NotFound;
        }
        constexpr std::array<std::pair<std::int64_t, std::string_view>, 3> values{{
            {0, "Do not park automatically"},
            {1, "Park flight controls"},
            {2, "Park all plugin output"},
        }};
        *outputCount = static_cast<std::uint32_t>(values.size());
        if (!options || capacity < values.size()) return Result::CapacityExceeded;
        for (std::size_t index = 0; index < values.size(); ++index) {
            options[index] = {};
            options[index].value = values[index].first;
            Copy(options[index].label, values[index].second);
        }
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

PageDescriptorV1 Page(std::string_view id, std::string_view name,
                      std::string_view description,
                      const ControlDescriptorV1* controls,
                      std::uint32_t controlCount, bool editable,
                      bool labeledChoices = false) noexcept
{
    PageDescriptorV1 page;
    Copy(page.moduleId, kHotasModuleId);
    Copy(page.pageId, id);
    Copy(page.displayName, name);
    Copy(page.description, description);
    page.controlCount = controlCount;
    page.controls = controls;
    page.readValue = &ReadValue;
    if (editable) {
        page.writeDraft = &WriteDraft;
        page.apply = &ApplyDraft;
        page.cancel = &CancelDraft;
    }
    if (labeledChoices) page.readChoiceOptions = &ReadChoiceOptions;
    return page;
}

const std::array g_pages{
    Page("hotas-setup", "Setup Overview",
         "Read-only readiness and ownership summary for the standalone HOTAS runtime.",
         g_setupControls.data(), static_cast<std::uint32_t>(g_setupControls.size()), false),
    Page("hotas-flight-axes", "Flight Axes",
         "First native Control editing slice for HOTAS-owned flight-axis settings.",
         g_flightAxisControls.data(),
         static_cast<std::uint32_t>(g_flightAxisControls.size()), true),
    Page("hotas-diagnostics", "Plugin & Compatibility",
         "Pilot-context controls plus compatibility, frontend, and suite diagnostics.",
         g_diagnosticControls.data(),
         static_cast<std::uint32_t>(g_diagnosticControls.size()), true, true),
};

bool ValidApi(const ApiV1* api) noexcept
{
    constexpr auto required = offsetof(ApiV1, isInputCaptureActive) +
                              sizeof(((ApiV1*)nullptr)->isInputCaptureActive);
    return api && api->structSize >= required && api->abiVersion == kAbiVersion &&
           api->moduleId && std::string_view(api->moduleId) == kModuleId &&
           api->registerPage && api->unregisterModule && api->requestRefresh &&
           api->registerModule && api->isOpen && api->isInputCaptureActive;
}

using QueryApi = const ApiV1*(__cdecl*)(std::uint32_t) noexcept;

const ApiV1* __cdecl ResolveLoadedHost(const wchar_t* moduleName) noexcept
{
    if (!moduleName) return nullptr;
    const auto module = GetModuleHandleW(moduleName);
    if (!module) return nullptr;
    const auto address = GetProcAddress(module, "AbsoluteControlPanel_QueryApi");
    if (!address) return &g_incompatibleHostApi;
    const auto query = reinterpret_cast<QueryApi>(address);
    const auto* api = query(kAbiVersion);
    return api ? api : &g_incompatibleHostApi;
}

bool ValidIdentifier(std::string_view value) noexcept
{
    if (value.empty()) return false;
    return std::ranges::all_of(value, [](unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
               ch == '.' || ch == '_' || ch == '-';
    });
}

bool ValidControl(const ControlDescriptorV1& control,
                  bool hasChoiceReader) noexcept
{
    if (control.structSize < sizeof(ControlDescriptorV1) ||
        !Terminated(control.controlId) || !Terminated(control.label) ||
        !Terminated(control.description) ||
        !ValidIdentifier(control.controlId) || control.label[0] == '\0') {
        return false;
    }
    constexpr std::uint32_t allowedFlags = kControlReadOnly;
    if ((control.flags & ~allowedFlags) != 0) return false;
    const bool readOnly = (control.flags & kControlReadOnly) != 0;
    switch (control.kind) {
    case ControlKind::Toggle:
        return !readOnly;
    case ControlKind::IntegerSlider:
    case ControlKind::FloatSlider:
        return !readOnly && std::isfinite(control.minimumValue) &&
               std::isfinite(control.maximumValue) &&
               std::isfinite(control.stepValue) &&
               control.minimumValue <= control.maximumValue &&
               control.stepValue > 0.0;
    case ControlKind::Choice:
        return !readOnly && hasChoiceReader;
    case ControlKind::InputBinding:
        return readOnly;
    default:
        return false;
    }
}

Result RegistrationFailure(Result result) noexcept
{
    if (result == Result::NotReady) return result;
    g_terminalRejection.store(true, std::memory_order_release);
    return Result::Rejected;
}
} // namespace

namespace AbsoluteControlSubscriber {

void SetRuntimeStatus(const RuntimeStatus& status) noexcept
{
    g_throttleHookInstalled.store(status.throttleHookInstalled, std::memory_order_release);
    g_nativeControlsInitialized.store(
        status.nativeControlsInitialized, std::memory_order_release);
    g_controllerStarted.store(status.controllerStarted, std::memory_order_release);
    g_legacyWorkbenchConfigured.store(
        status.legacyWorkbenchConfigured, std::memory_order_release);
    g_legacyWorkbenchInstalled.store(
        status.legacyWorkbenchInstalled, std::memory_order_release);
}

AbsoluteControlPanelApi::Result RegisterDiscoveredHost() noexcept
{
    return Testing::RegisterWithResolver(&ResolveLoadedHost);
}

bool IsHosted() noexcept
{
    return g_registered.load(std::memory_order_acquire);
}

bool IsHostOpen() noexcept
{
    try {
        const auto* api = g_hostApi.load(std::memory_order_acquire);
        return g_registered.load(std::memory_order_acquire) && api && api->isOpen &&
               api->isOpen() != 0;
    } catch (...) {
        return false;
    }
}

bool IsHostInputCaptureActive() noexcept
{
    try {
        const auto* api = g_hostApi.load(std::memory_order_acquire);
        return g_registered.load(std::memory_order_acquire) && api &&
               api->isInputCaptureActive && api->isInputCaptureActive() != 0;
    } catch (...) {
        return false;
    }
}

namespace Testing {

AbsoluteControlPanelApi::Result ValidateDescriptors(
    const PageDescriptorV1* pages, std::size_t pageCount) noexcept
{
    if (!pages || pageCount == 0) return Result::InvalidArgument;
    try {
        std::unordered_set<std::string_view> pageIds;
        std::unordered_set<std::string_view> controlIds;
        for (std::size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
            const auto& page = pages[pageIndex];
            if (page.structSize < sizeof(PageDescriptorV1) ||
                !Terminated(page.moduleId) || !Terminated(page.pageId) ||
                !Terminated(page.displayName) || !Terminated(page.description) ||
                std::string_view(page.moduleId) != kHotasModuleId ||
                !ValidIdentifier(page.pageId) || page.displayName[0] == '\0' ||
                !page.controls || page.controlCount == 0 || !page.readValue ||
                page.invokeAction) {
                return Result::InvalidArgument;
            }
            if (!pageIds.insert(page.pageId).second) return Result::Duplicate;

            bool hasEditable{};
            bool hasChoice{};
            for (std::uint32_t controlIndex = 0;
                 controlIndex < page.controlCount; ++controlIndex) {
                const auto& control = page.controls[controlIndex];
                if (!ValidControl(control, page.readChoiceOptions != nullptr)) {
                    return Result::InvalidArgument;
                }
                hasEditable |= (control.flags & kControlReadOnly) == 0;
                hasChoice |= control.kind == ControlKind::Choice;
                if (!controlIds.insert(control.controlId).second) {
                    return Result::Duplicate;
                }
            }
            const bool hasTransaction = page.writeDraft && page.apply && page.cancel;
            if (hasEditable != hasTransaction ||
                (!hasEditable && (page.writeDraft || page.apply || page.cancel)) ||
                hasChoice != (page.readChoiceOptions != nullptr)) {
                return Result::InvalidArgument;
            }
        }
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

AbsoluteControlPanelApi::Result RegisterWithResolver(
    ResolveLoadedHostCallback resolver) noexcept
{
    if (!resolver) return Result::InvalidArgument;
    if (g_registered.load(std::memory_order_acquire)) return Result::Ok;
    if (g_terminalRejection.load(std::memory_order_acquire)) return Result::Rejected;

    try {
        std::scoped_lock lock(g_registrationMutex);
        if (g_registered.load(std::memory_order_relaxed)) return Result::Ok;
        if (g_terminalRejection.load(std::memory_order_relaxed)) return Result::Rejected;

        const auto descriptorResult = ValidateDescriptors(g_pages.data(), g_pages.size());
        if (descriptorResult != Result::Ok) {
            g_terminalRejection.store(true, std::memory_order_release);
            return Result::Rejected;
        }

        for (const wchar_t* moduleName : {
                 L"AbsoluteControlPanel.dll",
                 L"AbsoluteControlPanelResearchDev.dll",
             }) {
            const auto* api = resolver(moduleName);
            if (!api) continue;
            if (!ValidApi(api)) {
                g_terminalRejection.store(true, std::memory_order_release);
                return Result::Rejected;
            }

            const auto moduleResult = api->registerModule(&g_module);
            if (moduleResult != Result::Ok && moduleResult != Result::Duplicate) {
                return RegistrationFailure(moduleResult);
            }
            const bool ownsModuleRegistration = moduleResult == Result::Ok;
            for (const auto& page : g_pages) {
                const auto pageResult = api->registerPage(&page);
                if (pageResult != Result::Ok && pageResult != Result::Duplicate) {
                    if (ownsModuleRegistration) {
                        (void)api->unregisterModule(kHotasModuleId.data());
                    }
                    return RegistrationFailure(pageResult);
                }
            }

            g_hostApi.store(api, std::memory_order_release);
            g_registered.store(true, std::memory_order_release);
            return Result::Ok;
        }
        return Result::NotFound;
    } catch (...) {
        g_terminalRejection.store(true, std::memory_order_release);
        return Result::Rejected;
    }
}

const ModuleDescriptorV1& Module() noexcept
{
    return g_module;
}

const PageDescriptorV1* Pages(std::size_t& pageCount) noexcept
{
    pageCount = g_pages.size();
    return g_pages.data();
}

void ForceReadException(bool enabled) noexcept
{
    g_forceReadException.store(enabled, std::memory_order_release);
}

void Reset() noexcept
{
    {
        std::scoped_lock lock(g_registrationMutex);
        g_hostApi.store(nullptr, std::memory_order_release);
        g_registered.store(false, std::memory_order_release);
        g_terminalRejection.store(false, std::memory_order_release);
        g_forceReadException.store(false, std::memory_order_release);
        SetRuntimeStatus({});
    }
    {
        std::scoped_lock lock(g_settingsMutex);
        g_settings = {};
    }
}

} // namespace Testing
} // namespace AbsoluteControlSubscriber
