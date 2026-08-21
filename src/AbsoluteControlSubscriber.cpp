#include "AbsoluteControlSubscriber.h"

#include "AbsoluteControlSettings.h"
#include "AbsoluteControlThrottleActions.h"
#include "AbsoluteControlDeviceProvider.h"
#include "AbsoluteControlFlightAxesComposition.h"
#include "AbsoluteControlShipButtonsComposition.h"
#include "AbsoluteControlMacros.h"
#include "AbsoluteControlProfiles.h"
#include "AbsoluteControlTelemetry.h"
#include "BindingRef.h"
#include "HotasBindingCapture.h"
#include "HotasBindingCatalog.h"
#include "Plugin.h"
#include "ShipOutput.h"
#include "WizardCapture.h"
#include "WizardDefs.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
using namespace AbsoluteControlPanelApi;

constexpr std::string_view kHotasModuleId = "absolute.hotas";

std::atomic<const ApiV1*> g_hostApi{};
std::atomic_bool g_registered{};
std::atomic_bool g_terminalRejection{};
std::atomic_bool g_compositionRegistered{};
std::atomic_bool g_shipCompositionRegistered{};
std::atomic_bool g_forceReadException{};
std::mutex g_registrationMutex;

std::atomic_bool g_throttleHookInstalled{};
std::atomic_bool g_nativeControlsInitialized{};
std::atomic_bool g_externalMouseSteeringOwner{};
std::atomic_bool g_externalCameraOwner{};
std::atomic_bool g_controllerStarted{};
std::atomic_bool g_legacyWorkbenchConfigured{};
std::atomic_bool g_legacyWorkbenchInstalled{};

struct SettingsSession {
    bool loaded{};
    bool dirty{};
    AbsoluteControlSettings::ScalarState saved{};
    AbsoluteControlSettings::ScalarState draft{};
    HotasBindingCatalog::BindingState savedBindings{};
    HotasBindingCatalog::BindingState draftBindings{};
    AbsoluteControlSettings::ShipRouteState savedRoutes{
        AbsoluteControlSettings::DefaultShipRoutes()};
    AbsoluteControlSettings::ShipRouteState draftRoutes{
        AbsoluteControlSettings::DefaultShipRoutes()};
    AbsoluteControlSettings::Revision revision{};
    std::string editTarget;
    std::uint64_t generation{};
    std::string lastError;
};

std::mutex g_settingsMutex;
SettingsSession g_settings;

struct PageContext {
    std::string_view pageId;
};

struct CaptureSession {
    bool started{};
    bool finished{};
    std::string_view pageId;
    std::string_view controlId;
    std::uint64_t draftGeneration{};
    BindingCaptureState terminalState{BindingCaptureState::Idle};
    std::string binding;
    std::string detail;
};

CaptureSession g_capture;

std::mutex g_profileMutex;
std::unique_ptr<AbsoluteControlProfiles::Session> g_profileSession;
CaptureSession g_profileCapture;
std::string g_profileError;

std::mutex g_macroMutex;
std::unique_ptr<AbsoluteControlMacros::Session> g_macroSession;
CaptureSession g_macroCapture;
std::string g_macroError;
std::int64_t g_targetCatalogSelection{};

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

ControlDescriptorV1 GroupHeader(std::string_view id, std::string_view label,
                                std::string_view description) noexcept
{
    ControlDescriptorV1 control;
    control.kind = ControlKind::GroupHeader;
    Copy(control.controlId, id);
    Copy(control.label, label);
    Copy(control.description, description);
    return control;
}

ControlDescriptorV1 BindingControl(
    const HotasBindingCatalog::Target& target) noexcept
{
    ControlDescriptorV1 control;
    control.kind = ControlKind::InputBinding;
    control.flags = kBindingController | kBindingClearable;
    Copy(control.controlId, target.controlId);
    Copy(control.label, target.displayLabel);
    if (target.family == HotasBindingCatalog::TargetFamily::FlightAssist ||
        target.family == HotasBindingCatalog::TargetFamily::TurnAssist) {
        Copy(control.description,
            "Press a DirectInput button or POV direction. AbsoluteHOTAS handles this function internally; it does not emit a keypress.");
    } else if (target.family == HotasBindingCatalog::TargetFamily::ShipAction) {
        Copy(control.description,
            "Press a DirectInput button or POV direction, then choose how AbsoluteHOTAS dispatches the action below.");
    } else {
        Copy(control.description,
            target.captureKind == HotasBindingCatalog::CaptureKind::Axis
                ? "Move one DirectInput axis through a clear range to capture it."
                : "Press a DirectInput button or POV direction to capture it.");
    }
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

ControlDescriptorV1 TransientChoice(std::string_view id,
                                    std::string_view label,
                                    std::string_view description,
                                    double minimum, double maximum) noexcept
{
    auto control = Choice(id, label, description, minimum, maximum, 1.0);
    control.flags = kControlTransientChoice;
    return control;
}

ControlDescriptorV1 TextInput(std::string_view id, std::string_view label,
                              std::string_view description) noexcept
{
    ControlDescriptorV1 control;
    control.kind = ControlKind::TextInput;
    Copy(control.controlId, id);
    Copy(control.label, label);
    Copy(control.description, description);
    // The stable host interprets these numeric fields as the accepted text
    // length contract and rejects zero/default bounds.
    control.minimumValue = 0.0;
    control.maximumValue = static_cast<double>(kStringValueCapacity - 1);
    control.stepValue = 1.0;
    return control;
}

ControlDescriptorV1 Action(std::string_view id, std::string_view label,
                           std::string_view description,
                           std::uint32_t flags = kControlNone) noexcept
{
    ControlDescriptorV1 control;
    control.kind = ControlKind::Action;
    control.flags = flags;
    Copy(control.controlId, id);
    Copy(control.label, label);
    Copy(control.description, description);
    return control;
}

ControlDescriptorV1 RecordCollection(std::string_view id,
                                     std::string_view label,
                                     std::string_view description) noexcept
{
    ControlDescriptorV1 control;
    control.kind = ControlKind::RecordCollection;
    control.flags = kControlTransientSelection;
    Copy(control.controlId, id);
    Copy(control.label, label);
    Copy(control.description, description);
    return control;
}

ControlDescriptorV1 ControllerBinding(std::string_view id,
                                      std::string_view label,
                                      std::string_view description) noexcept
{
    ControlDescriptorV1 control;
    control.kind = ControlKind::InputBinding;
    control.flags = kBindingController | kBindingClearable;
    Copy(control.controlId, id);
    Copy(control.label, label);
    Copy(control.description, description);
    return control;
}

ControlDescriptorV1 ScalarControl(AbsoluteControlSettings::ScalarField field,
                                  std::string_view label,
                                  std::string_view description) noexcept
{
    using namespace AbsoluteControlSettings;
    const auto& definition = Definition(field);
    switch (definition.type) {
    case ScalarType::Boolean:
        return Toggle(definition.controlId, label, description);
    case ScalarType::Integer:
        return IntegerSlider(definition.controlId, label, description,
            definition.minimum, definition.maximum, definition.step);
    case ScalarType::Float:
        return FloatSlider(definition.controlId, label, description,
            definition.minimum, definition.maximum, definition.step);
    case ScalarType::Choice:
        return Choice(definition.controlId, label, description,
            definition.minimum, definition.maximum, definition.step);
    }
    return {};
}

ControlDescriptorV1 AdvancedScalarControl(
    AbsoluteControlSettings::ScalarField field, std::string_view label,
    std::string_view description) noexcept
{
    auto control = ScalarControl(field, label, description);
    control.flags |= kControlAdvanced;
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
    ScalarControl(AbsoluteControlSettings::ScalarField::FlightControlsEnabled,
        "Flight controls enabled",
        "Enable AbsoluteHOTAS flight-axis injection for the Main controls configuration."),
    ScalarControl(AbsoluteControlSettings::ScalarField::ThrottleInvert, "Invert throttle", "Reverse logical throttle travel without changing hardware calibration."),
    ScalarControl(AbsoluteControlSettings::ScalarField::PitchInvert, "Invert pitch", "Reverse the sign of the physical pitch axis before injection."),
    ScalarControl(AbsoluteControlSettings::ScalarField::PitchSensitivity, "Pitch sensitivity", "Scale pitch response before flight processing."),
    ScalarControl(AbsoluteControlSettings::ScalarField::PitchSaturation, "Pitch saturation", "Set the normalized input that reaches full pitch authority."),
    ScalarControl(AbsoluteControlSettings::ScalarField::PitchDeadzone, "Pitch deadzone", "Ignore pitch input inside this normalized center range."),
    ScalarControl(AbsoluteControlSettings::ScalarField::YawInvert, "Invert yaw", "Reverse the sign of the physical yaw axis before injection."),
    ScalarControl(AbsoluteControlSettings::ScalarField::YawSensitivity, "Yaw sensitivity", "Scale yaw response before flight processing."),
    ScalarControl(AbsoluteControlSettings::ScalarField::YawSaturation, "Yaw saturation", "Set the normalized input that reaches full yaw authority."),
    ScalarControl(AbsoluteControlSettings::ScalarField::YawDeadzone, "Yaw deadzone", "Ignore yaw input inside this normalized center range."),
    ScalarControl(AbsoluteControlSettings::ScalarField::RollInvert, "Invert roll", "Reverse the sign of the physical roll axis before injection."),
    ScalarControl(AbsoluteControlSettings::ScalarField::RollSensitivity, "Roll sensitivity", "Scale roll response before flight processing."),
    ScalarControl(AbsoluteControlSettings::ScalarField::RollSaturation, "Roll saturation", "Set the normalized input that reaches full roll authority."),
    ScalarControl(AbsoluteControlSettings::ScalarField::RollDeadzone, "Roll deadzone", "Ignore roll input inside this normalized center range."),
    ScalarControl(AbsoluteControlSettings::ScalarField::StrafeLateralInvert, "Invert lateral strafe", "Reverse lateral strafe direction."),
    ScalarControl(AbsoluteControlSettings::ScalarField::StrafeLateralSensitivity, "Lateral strafe sensitivity", "Scale lateral translation response."),
    ScalarControl(AbsoluteControlSettings::ScalarField::StrafeLateralSaturation, "Lateral strafe saturation", "Set the input that reaches full lateral authority."),
    ScalarControl(AbsoluteControlSettings::ScalarField::StrafeLateralDeadzone, "Lateral strafe deadzone", "Ignore lateral strafe input inside this center range."),
    ScalarControl(AbsoluteControlSettings::ScalarField::StrafeVerticalInvert, "Invert vertical strafe", "Reverse vertical strafe direction."),
    ScalarControl(AbsoluteControlSettings::ScalarField::StrafeVerticalSaturation, "Vertical strafe saturation", "Set the input that reaches full vertical authority."),
    ScalarControl(AbsoluteControlSettings::ScalarField::StrafeVerticalDeadzone, "Vertical strafe deadzone", "Ignore vertical strafe input inside this center range."),
    ScalarControl(AbsoluteControlSettings::ScalarField::ReverseInvert, "Invert reverse axis", "Reverse the dedicated analog reverse-axis direction."),
    ScalarControl(AbsoluteControlSettings::ScalarField::ReverseSensitivity, "Reverse-axis sensitivity", "Scale dedicated analog reverse input."),
    ScalarControl(AbsoluteControlSettings::ScalarField::ReverseSaturation, "Reverse-axis saturation", "Set the input that reaches full dedicated reverse authority."),
    ScalarControl(AbsoluteControlSettings::ScalarField::DigitalRollStrength, "Digital roll strength", "Set roll deflection while a digital roll control is held."),
    ScalarControl(AbsoluteControlSettings::ScalarField::DigitalStrafeStrength, "Digital strafe strength", "Set strafe deflection while a digital strafe control is held."),
};

const std::array g_shipButtonControls{
    ScalarControl(AbsoluteControlSettings::ScalarField::HoldForBoost, "Boost temporarily owns throttle", "Pause throttle injection while boost is held, then resume at maximum throttle."),
    ScalarControl(AbsoluteControlSettings::ScalarField::MenuUsePitch, "Pitch navigates up and down", "Reuse the configured pitch axis for vertical menu navigation after neutral arming."),
    ScalarControl(AbsoluteControlSettings::ScalarField::MenuUseYaw, "Yaw navigates left and right", "Reuse the configured yaw axis for horizontal navigation after neutral arming."),
    ScalarControl(AbsoluteControlSettings::ScalarField::MenuUsePrimaryWeapon, "Primary weapon selects", "Reuse Primary Weapon as Select or Accept after release arming."),
    ScalarControl(AbsoluteControlSettings::ScalarField::MenuInvertVertical, "Invert vertical navigation", "Reverse pitch-axis menu navigation direction."),
    ScalarControl(AbsoluteControlSettings::ScalarField::MenuInvertHorizontal, "Invert horizontal navigation", "Reverse yaw-axis menu navigation direction."),
    ScalarControl(AbsoluteControlSettings::ScalarField::MenuEngageThreshold, "Menu axis engage threshold", "Normalized deflection required to engage a navigation direction."),
    ScalarControl(AbsoluteControlSettings::ScalarField::MenuReleaseThreshold, "Menu axis release threshold", "Normalized return threshold; must remain below engage for hysteresis."),
};

const std::array g_throttleControls{
    GroupHeader("throttle-landmark-guide", "LANDMARKS - DIRECT OR BY FEEL",
        "Drag a coloured landmark on the pinned graph, or track the physical throttle with one of the three actions below."),
    ReadOnlyStatus("throttle-scope", "Graph editing",
        "Landmarks use ordinary draft edits. Width sliders redraw the graph immediately; Apply persists and Cancel restores everything."),
    Action("throttle-capture-detent", "Set cruise from throttle",
        "Start live cruise tracking; move the throttle by feel, then press again or Apply to keep the current position.",
        kControlMutatesDraft | kControlLayoutInline),
    Action("throttle-capture-reverse", "Set zero thrust from throttle",
        "Start live zero-thrust tracking; move the throttle by feel, then press again or Apply to keep the current position.",
        kControlMutatesDraft | kControlLayoutInline),
    Action("throttle-capture-boost", "Set boost from throttle",
        "Start live boost tracking; move the throttle by feel, then press again or Apply to keep the current position.",
        kControlMutatesDraft | kControlLayoutInline),
    GroupHeader("throttle-positional-zones", "POSITIONAL ZONES",
        "Enable the zones you use, then tune their widths while watching the live lever marker."),
    ScalarControl(AbsoluteControlSettings::ScalarField::IdlePlateau, "Idle zone width", "Bottom fraction of positional throttle travel treated as idle."),
    ScalarControl(AbsoluteControlSettings::ScalarField::ThrottleSaturation, "Full-thrust saturation", "Positional input fraction that reaches full forward authority when boost and reverse zones are not defining the top end."),
    ScalarControl(AbsoluteControlSettings::ScalarField::DetentDeadzone, "Cruise zone width", "Logical raw half-width around the cruise landmark."),
    ScalarControl(AbsoluteControlSettings::ScalarField::ReverseZoneEnabled, "Reverse zone enabled", "Use the low end of the primary throttle as a binary reverse zone."),
    ScalarControl(AbsoluteControlSettings::ScalarField::ReverseZoneDeadzone, "Zero-thrust zone width", "Logical raw half-width around the reverse zero-thrust landmark."),
    ScalarControl(AbsoluteControlSettings::ScalarField::BoostZoneEnabled, "Boost zone enabled", "Use the high end of positional throttle travel to request boost."),
    ScalarControl(AbsoluteControlSettings::ScalarField::BoostZoneDeadzone, "Full-thrust zone width", "Logical raw half-width of the full-thrust plateau before boost."),
    GroupHeader("throttle-rate-mode", "RATE / SELF-CENTERING THROTTLE",
        "Alternative mode for a spring-centered axis; these controls do not define positional landmark zones."),
    ScalarControl(AbsoluteControlSettings::ScalarField::RateThrottleEnabled, "Rate throttle enabled", "Interpret a self-centering axis as throttle change rate rather than position."),
    ScalarControl(AbsoluteControlSettings::ScalarField::ThrottleDeadzone, "Rate input center deadzone", "Ignore centered input inside this range while using rate throttle."),
    ScalarControl(AbsoluteControlSettings::ScalarField::ThrottleSensitivity, "Rate input sensitivity", "Scale the signed rate request made by a self-centering throttle axis."),
    ScalarControl(AbsoluteControlSettings::ScalarField::AccumulatorRate, "Rate throttle ramp", "Throttle units per second at full axis deflection."),
    ScalarControl(AbsoluteControlSettings::ScalarField::AccumulatorDecay, "Rate throttle decay", "Throttle units per second returned toward idle at neutral; zero holds."),
    ScalarControl(AbsoluteControlSettings::ScalarField::ReverseGateVelocity, "Reverse velocity gate", "Allow rate-throttle reverse only below this HUD velocity in meters per second."),
    GroupHeader("throttle-turn-assist", "PILOT TURN ASSIST",
        "Optional runtime assist layered on either throttle mode."),
    ScalarControl(AbsoluteControlSettings::ScalarField::TurnAssistEnabled, "Pilot Turn Assist", "Let the native turn-rate assist temporarily reduce throttle during hard turns."),
    ScalarControl(AbsoluteControlSettings::ScalarField::TurnAssistMode, "Turn Assist activation", "Choose Always, While held, or Toggle; assign the assist binding on Ship Buttons."),
    GroupHeader("throttle-utility", "ONE-SHOT UTILITY",
        "Optional convenience operation; it does not create a persistent link between controls."),
    Action("throttle-link-idle-saturation", "Link idle and top deadzones once",
        "Set throttle saturation to 1 minus the current idle plateau once; later edits remain independent.",
        kControlMutatesDraft),
    GroupHeader("throttle-precise-landmarks", "PRECISE LANDMARK VALUES",
        "Advanced numeric fallbacks for keyboard/controller adjustment; pointer users can drag the graph handles instead."),
    AdvancedScalarControl(AbsoluteControlSettings::ScalarField::DetentCenter, "Cruise position", "Precise logical raw position of the cruise detent."),
    AdvancedScalarControl(AbsoluteControlSettings::ScalarField::ReverseZoneCenter, "Zero-thrust position", "Precise logical raw position of the reverse-zone zero-thrust landmark."),
    AdvancedScalarControl(AbsoluteControlSettings::ScalarField::BoostZoneCenter, "Boost position", "Precise logical raw position at the center of the full-thrust plateau before boost."),
};

const std::array g_aimingControls{
    ScalarControl(AbsoluteControlSettings::ScalarField::AimEnabled, "Aim system enabled", "Enable HOTAS-owned reticle and aim injection."),
    ScalarControl(AbsoluteControlSettings::ScalarField::AimSensitivity, "Aim-driven steering sensitivity", "Scale flight-axis input when the aim system drives steering."),
    ScalarControl(AbsoluteControlSettings::ScalarField::AimSmoothing, "Aim smoothing", "Apply bounded EMA smoothing to low-resolution aiming input."),
    ScalarControl(AbsoluteControlSettings::ScalarField::AimYawInvert, "Invert aim yaw", "Reverse independent analog aim-yaw direction."),
    ScalarControl(AbsoluteControlSettings::ScalarField::AimYawSensitivity, "Aim yaw sensitivity", "Scale independent analog aim-yaw input."),
    ScalarControl(AbsoluteControlSettings::ScalarField::AimPitchInvert, "Invert aim pitch", "Reverse independent analog aim-pitch direction."),
    ScalarControl(AbsoluteControlSettings::ScalarField::AimPitchSensitivity, "Aim pitch sensitivity", "Scale independent analog aim-pitch input."),
    ScalarControl(AbsoluteControlSettings::ScalarField::DigitalAimSpeed, "Digital aim speed", "Set reticle travel speed while a digital aim direction is held."),
};

const std::array g_profileControls{
    RecordCollection("profile-records", "Main controls and layers",
        "Select Main controls, an inheriting sparse overlay, or an independent full profile."),
    ReadOnlyStatus("profile-selected-status", "Selected edit target",
        "Profile kind, inheritance, override count, file, and activation route."),
    ReadOnlyStatus("profile-switch-status", "Pending profile switch",
        "Resolve unsaved changes before moving the legacy Wizard edit target."),
    Choice("profile-activation-mode", "Activation mode",
        "Momentary applies while held; Toggle switches on/off; Selector maps a stable rotary position.",
        0.0, 2.0, 1.0),
    ControllerBinding("profile-activation-trigger", "Controller activation",
        "Bind the controller button or selector position that activates this profile."),
    TextInput("profile-operation-name", "New profile name",
        "Name used by Create overlay or Export Main as full profile."),
    Action("profile-create-overlay", "Create overlay",
        "Create a sparse layer that inherits Main controls and select it for editing.",
        kControlAppliesDraftBeforeInvoke),
    Action("profile-export-full", "Export Main as full profile",
        "Materialize the current Main controls as an independent full profile.",
        kControlAppliesDraftBeforeInvoke),
    Action("profile-import-full", "Import selected full profile",
        "Replace Main controls with the selected independent full profile after an automatic backup.",
        kControlAppliesDraftBeforeInvoke | kControlRequiresConfirmation),
    Action("profile-reset-main", "Reset Main controls",
        "Back up and reset Main controls to mod defaults.",
        kControlAppliesDraftBeforeInvoke | kControlRequiresConfirmation),
    Action("profile-switch-save", "Save and switch",
        "Save the current Wizard edit target and activation draft, then select the pending profile.",
        kControlLayoutInline),
    Action("profile-switch-discard", "Discard and switch",
        "Discard current Wizard and activation changes, then select the pending profile.",
        kControlLayoutInline | kControlRequiresConfirmation),
    Action("profile-switch-stay", "Stay here",
        "Cancel the pending profile switch without changing the current draft.",
        kControlLayoutInline),
};

const std::array g_profileFallbackControls{
    ReadOnlyStatus("profiles-scope", "Legacy host fallback",
        "Profiles continue to load and switch under HOTAS ownership; this Control host lacks the record, confirmation, or provider-capture contract required by the native editor."),
};

const std::array g_shortcutControls{
    RecordCollection("shortcut-records", "Keyboard & mouse shortcuts",
        "Select a controller-to-keyboard or controller-to-mouse shortcut."),
    ControllerBinding("shortcut-trigger", "Controller trigger",
        "Capture or clear the selected shortcut's controller button or POV direction."),
    Choice("shortcut-output", "Raw output",
        "Choose one explicit keyboard key or mouse button; unknown stored tokens remain visible until replaced.",
        -1.0, static_cast<double>(AbsoluteControlMacros::Session::OutputCatalogSize() - 1), 1.0),
    Action("shortcut-add", "Add shortcut", "Add an incomplete shortcut row to this page draft.",
        kControlMutatesDraft | kControlLayoutInline),
    Action("shortcut-delete", "Remove shortcut", "Remove the selected shortcut.",
        kControlMutatesDraft | kControlLayoutInline | kControlRequiresConfirmation),
    Action("shortcut-menu-preset", "Add menu-navigation preset",
        "Add unbound W, A, S, D, Tab, E, and Escape shortcut rows.",
        kControlMutatesDraft),
    ReadOnlyStatus("shortcut-macro-link", "Need a chord or sequence?",
        "Open the Macros page in Absolute Control. This ABI has no provider-driven page-navigation callback."),
    ReadOnlyStatus("shortcut-status", "Shortcut draft", "Persistence and validation status for this page transaction."),
};

const std::array g_macroControls{
    RecordCollection("macro-records", "Macros", "Select one HOTAS-owned macro to edit."),
    Action("macro-add", "Add macro", "Add a one-step macro draft without any power-system preset.",
        kControlMutatesDraft | kControlLayoutInline),
    Action("macro-delete", "Delete macro", "Delete the selected macro and all of its ordered steps.",
        kControlMutatesDraft | kControlLayoutInline | kControlRequiresConfirmation),
    TextInput("macro-name", "Name", "Friendly macro name; duplicate names remain distinct and ordered."),
    ControllerBinding("macro-trigger", "Trigger", "Capture or clear the selected macro trigger."),
    Toggle("macro-turbo", "Turbo repeat", "Repeat the complete sequence while its trigger remains held."),
    RecordCollection("macro-step-records", "Ordered steps", "Select a step; list order is execution order."),
    Action("macro-step-add", "Add step", "Append a default Next System tap step.",
        kControlMutatesDraft | kControlLayoutInline),
    Action("macro-step-delete", "Delete step", "Remove the selected step.",
        kControlMutatesDraft | kControlLayoutInline | kControlRequiresConfirmation),
    Action("macro-step-up", "Move up", "Move the selected step earlier.",
        kControlMutatesDraft | kControlLayoutInline),
    Action("macro-step-down", "Move down", "Move the selected step later.",
        kControlMutatesDraft | kControlLayoutInline),
    Choice("macro-step-action", "Step action", "Tap repeats; Hold uses a duration in milliseconds.", 0.0, 1.0, 1.0),
    IntegerSlider("macro-step-amount", "Tap count / hold milliseconds",
        "Tap: repetition count. Hold: duration in milliseconds.", 0.0, 60000.0, 1.0),
    IntegerSlider("macro-step-gap", "Gap after step", "Delay before the next step, in milliseconds.", 0.0, 60000.0, 1.0),
    RecordCollection("macro-target-records", "Chord targets", "Targets in one step are pressed simultaneously."),
    TransientChoice("macro-target-catalog", "Target to add",
        "Choose a named ship action or explicit raw keyboard/mouse output.", 0.0,
        static_cast<double>(AbsoluteControlMacros::Session::TargetCatalogSize() - 1)),
    Action("macro-target-add", "Add chord target", "Add the selected target to this step.",
        kControlMutatesDraft | kControlLayoutInline),
    Action("macro-target-delete", "Remove chord target", "Remove the selected target from this step.",
        kControlMutatesDraft | kControlLayoutInline),
    ReadOnlyStatus("macro-status", "Macro draft", "Persistence and validation status for this page transaction."),
};

const std::array g_macroFallbackControls{
    ReadOnlyStatus("macros-scope", "Legacy host fallback",
        "Macros remain editable in the embedded workbench because this Control host lacks selected records, confirmations, or provider capture."),
};

const std::array g_deviceFallbackControls{
    ReadOnlyStatus("devices-scope", "Legacy host fallback",
        "Device inventory and calibration remain in the embedded workbench because this Control host lacks selected records and confirmations."),
};

const std::array g_diagnosticControls{
    ScalarControl(AbsoluteControlSettings::ScalarField::PilotContextMode,
        "Outside-pilot-seat behavior",
        "Choose how much HOTAS output is parked when automatic pilot context reports that the player is not flying."),
    ScalarControl(AbsoluteControlSettings::ScalarField::AutomaticPilotDetection,
        "Automatic pilot detection",
        "Use the exact-gated native pilot signal instead of treating the pilot context as manually managed."),
    ScalarControl(AbsoluteControlSettings::ScalarField::PilotLatchMilliseconds,
        "Pilot signal latch",
        "Keep the last valid pilot signal for this many milliseconds during transient game-state changes."),
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

template <std::size_t N>
std::vector<ControlDescriptorV1> WithBindings(
    const std::array<ControlDescriptorV1, N>& base,
    std::string_view pageId)
{
    std::vector<ControlDescriptorV1> controls(base.begin(), base.end());
    for (const auto& target : HotasBindingCatalog::kTargets) {
        if (target.pageId != pageId) continue;
        controls.push_back(BindingControl(target));
        if (target.family == HotasBindingCatalog::TargetFamily::ShipAction) {
            controls.push_back(ReadOnlyStatus(
                std::format("{}-route", target.controlId),
                std::format("{} route", target.displayLabel),
                "Selected dispatch method, route source, and current runtime availability."));
        }
    }
    return controls;
}

const auto g_flightAxisControlsWithBindings =
    WithBindings(g_flightAxisControls, HotasBindingCatalog::kFlightAxesPageId);

std::vector<ControlDescriptorV1> WithThrottleNavigation(
    const std::vector<ControlDescriptorV1>& base)
{
    std::vector<ControlDescriptorV1> controls = base;
    controls.push_back(Action(
        "flight-open-bindings", "Open Bindings",
        "Return to the landing page to bind the core analog flight axes."));
    controls.push_back(ReadOnlyStatus(
        "flight-throttle-summary", "Throttle behavior",
        "Positional and rate-throttle behavior share the Throttle Setup draft; Flight Axes does not duplicate those fields."));
    controls.push_back(Action(
        "flight-open-throttle", "Open Throttle Setup",
        "Open the HOTAS-owned positional and rate-throttle configuration page."));
    return controls;
}

const auto g_flightAxisControlsWithNavigation =
    WithThrottleNavigation(g_flightAxisControlsWithBindings);

const HotasBindingCatalog::Target* FindShipActionTarget(
    std::string_view actionId) noexcept
{
    for (const auto& target : HotasBindingCatalog::kTargets) {
        if (target.family == HotasBindingCatalog::TargetFamily::ShipAction &&
            target.actionId == actionId) return &target;
    }
    return nullptr;
}

ControlDescriptorV1 ShipRouteControl(
    const HotasBindingCatalog::Target& target)
{
    const auto* action = FindShipAction(target.actionId);
    const auto id = std::format("{}-route", target.controlId);
    const auto label = std::format("{} output method", target.displayLabel);
    if (action && action->allowedMethods == kDirectOrKeyboard) {
        return Choice(id, label,
            "Direct calls the native ship function. SendInput emits the current Starfield key or mouse binding, with the catalog fallback if none is mapped.",
            0.0, 1.0, 1.0);
    }
    return ReadOnlyStatus(id, label,
        "This universal context action automatically follows menu, targeting, and ship state; it has no manual output-method switch.");
}

std::vector<ControlDescriptorV1> ShipButtonControls()
{
    std::vector<ControlDescriptorV1> controls;
    controls.reserve(80);
    const auto appendAction = [&](std::string_view actionId) {
        if (const auto* target = FindShipActionTarget(actionId)) {
            controls.push_back(BindingControl(*target));
            const auto* action = FindShipAction(target->actionId);
            if (action && action->allowedMethods == kDirectOrKeyboard) {
                controls.push_back(ShipRouteControl(*target));
            }
        }
    };

    controls.push_back(GroupHeader(
        "ship-axis-bindings", "FLIGHT AXES",
        "Bind the seven analog HOTAS lanes here. Response graphs, inversion, deadzones, and saturation remain on Flight Axes."));
    for (const auto& target : HotasBindingCatalog::kTargets) {
        if (target.pageId == HotasBindingCatalog::kShipButtonsPageId &&
            (target.family == HotasBindingCatalog::TargetFamily::CoreAxis ||
             target.family == HotasBindingCatalog::TargetFamily::ReverseAxis)) {
            controls.push_back(BindingControl(target));
        }
    }

    controls.push_back(GroupHeader(
        "ship-primary-controls", "PRIMARY FLIGHT & COMBAT",
        "Bind the controls used most often in flight. Where offered, Direct calls the ship function while SendInput emits Starfield's current keyboard or mouse binding."));
    for (const auto actionId : {
             "FireWeapon0", "FireWeapon1", "FireWeapon2", "FireBoosters",
             "SwitchFlightModes", "ShipAction1", "OpenScanner", "Repair",
             "ShipAlternateControlHold", "Cruise", "AutopilotOnOff" }) {
        appendAction(actionId);
        if (actionId == std::string_view{"FireBoosters"}) {
            controls.push_back(g_shipButtonControls[0]);
        }
    }

    controls.push_back(GroupHeader(
        "ship-hotas-functions", "ABSOLUTEHOTAS THROTTLE FUNCTIONS",
        "These are plugin-owned throttle commands. They operate on HOTAS state directly and never simulate a keyboard or mouse input."));
    for (const auto& target : HotasBindingCatalog::kTargets) {
        if (target.pageId == HotasBindingCatalog::kShipButtonsPageId &&
            (target.family == HotasBindingCatalog::TargetFamily::FlightAssist ||
             target.family == HotasBindingCatalog::TargetFamily::TurnAssist)) {
            controls.push_back(BindingControl(target));
        }
    }

    controls.push_back(GroupHeader(
        "ship-navigation-controls", "CONTEXT & NAVIGATION",
        "These universal controls automatically choose the appropriate menu, targeting, or cockpit path and therefore use a fixed context route."));
    for (const auto actionId : {
             "SelectTarget", "IncreaseSystemPower", "DecreaseSystemPower",
             "PreviousSystem", "NextSystem", "Cancel" }) {
        appendAction(actionId);
    }

    controls.push_back(GroupHeader(
        "ship-camera-cockpit", "CAMERA & COCKPIT",
        "Less-frequent camera, docking, and cockpit controls. Compatibility defaults are selected where Starfield requires its ordinary input path."));
    for (const auto actionId : {
             "TogglePov", "ZoomCameraIn", "ZoomCameraOut", "UndockTakeOff",
             "GetUp", "ExitShipFromCockpit" }) {
        appendAction(actionId);
    }

    controls.push_back(GroupHeader(
        "ship-menu-axis-reuse", "MENU CONTROL REUSE",
        "Optionally reuse flight axes and Primary Weapon for menu navigation with neutral arming and hysteresis."));
    controls.insert(controls.end(), g_shipButtonControls.begin() + 1,
                    g_shipButtonControls.end());
    return controls;
}

const auto g_shipButtonControlsWithBindings = ShipButtonControls();

std::vector<ControlDescriptorV1> WithShortcutControls(
    const std::vector<ControlDescriptorV1>& base, bool pageNavigation)
{
    std::vector<ControlDescriptorV1> controls = base;
    controls.push_back(GroupHeader(
        "ship-custom-shortcuts", "CUSTOM KEYBOARD & MOUSE SHORTCUTS",
        "Use these for raw compatibility outputs or ordinary UI shortcuts. Chords and ordered sequences belong on the Macros page."));
    for (const auto& shortcutControl : g_shortcutControls) {
        if (pageNavigation &&
            std::string_view{ shortcutControl.controlId } == "shortcut-macro-link") {
            controls.push_back(Action(
                "shortcut-macro-link", "Need a chord or sequence?",
                "Open the Macros page to build ordered steps, chords, tap/hold timing, gaps, and turbo."));
        } else {
            controls.push_back(shortcutControl);
        }
    }
    return controls;
}

const auto g_shipButtonControlsWithShortcuts =
    WithShortcutControls(g_shipButtonControlsWithBindings, true);
const auto g_shipButtonControlsWithShortcutFallback =
    WithShortcutControls(g_shipButtonControlsWithBindings, false);
const auto g_aimingControlsWithBindings =
    WithBindings(g_aimingControls, HotasBindingCatalog::kAimingPageId);
const auto g_diagnosticControlsWithBindings =
    WithBindings(g_diagnosticControls, HotasBindingCatalog::kDiagnosticsPageId);

void PublishThrottlePreviewLocked() noexcept
{
    using namespace AbsoluteControlSettings;
    AbsoluteControlTelemetry::SetThrottlePreview({
        .inverted = GetBoolean(g_settings.draft,
            ScalarField::ThrottleInvert),
        .reverseZoneEnabled = GetBoolean(g_settings.draft,
            ScalarField::ReverseZoneEnabled),
        .boostZoneEnabled = GetBoolean(g_settings.draft,
            ScalarField::BoostZoneEnabled),
        .idlePlateau = GetFloat(g_settings.draft,
            ScalarField::IdlePlateau),
        .saturation = GetFloat(g_settings.draft,
            ScalarField::ThrottleSaturation),
        .detentCenter = GetInteger(g_settings.draft,
            ScalarField::DetentCenter),
        .detentDeadzone = GetInteger(g_settings.draft,
            ScalarField::DetentDeadzone),
        .reverseZoneCenter = GetInteger(g_settings.draft,
            ScalarField::ReverseZoneCenter),
        .reverseZoneDeadzone = GetInteger(g_settings.draft,
            ScalarField::ReverseZoneDeadzone),
        .boostZoneCenter = GetInteger(g_settings.draft,
            ScalarField::BoostZoneCenter),
        .boostZoneDeadzone = GetInteger(g_settings.draft,
            ScalarField::BoostZoneDeadzone),
    });
    AbsoluteControlTelemetry::SetAxisTuningPreviews({{
        { GetBoolean(g_settings.draft, ScalarField::ThrottleInvert),
          GetFloat(g_settings.draft, ScalarField::ThrottleSensitivity),
          GetFloat(g_settings.draft, ScalarField::ThrottleSaturation),
          GetFloat(g_settings.draft, ScalarField::ThrottleDeadzone) },
        { GetBoolean(g_settings.draft, ScalarField::PitchInvert),
          GetFloat(g_settings.draft, ScalarField::PitchSensitivity),
          GetFloat(g_settings.draft, ScalarField::PitchSaturation),
          GetFloat(g_settings.draft, ScalarField::PitchDeadzone) },
        { GetBoolean(g_settings.draft, ScalarField::YawInvert),
          GetFloat(g_settings.draft, ScalarField::YawSensitivity),
          GetFloat(g_settings.draft, ScalarField::YawSaturation),
          GetFloat(g_settings.draft, ScalarField::YawDeadzone) },
        { GetBoolean(g_settings.draft, ScalarField::RollInvert),
          GetFloat(g_settings.draft, ScalarField::RollSensitivity),
          GetFloat(g_settings.draft, ScalarField::RollSaturation),
          GetFloat(g_settings.draft, ScalarField::RollDeadzone) },
        { GetBoolean(g_settings.draft, ScalarField::StrafeLateralInvert),
          GetFloat(g_settings.draft, ScalarField::StrafeLateralSensitivity),
          GetFloat(g_settings.draft, ScalarField::StrafeLateralSaturation),
          GetFloat(g_settings.draft, ScalarField::StrafeLateralDeadzone) },
        { GetBoolean(g_settings.draft, ScalarField::StrafeVerticalInvert),
          GetFloat(g_settings.draft, ScalarField::StrafeLateralSensitivity),
          GetFloat(g_settings.draft, ScalarField::StrafeVerticalSaturation),
          GetFloat(g_settings.draft, ScalarField::StrafeVerticalDeadzone) },
    }});
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
    HotasBindingCatalog::BindingState bindings;
    AbsoluteControlSettings::ShipRouteState routes;
    std::string editTarget;
    if (!AbsoluteControlSettings::LoadEditTargetWithBindingsAndRoutes(
            state, bindings, routes, editTarget, revision, error)) {
        g_settings.loaded = false;
        g_settings.lastError = error.empty() ?
            "AbsoluteHOTAS could not read its settings." : std::move(error);
        return false;
    }
    g_settings.loaded = true;
    g_settings.dirty = false;
    g_settings.saved = state;
    g_settings.draft = state;
    g_settings.savedBindings = bindings;
    g_settings.draftBindings = std::move(bindings);
    g_settings.savedRoutes = routes;
    g_settings.draftRoutes = routes;
    g_settings.revision = revision;
    g_settings.editTarget = std::move(editTarget);
    ++g_settings.generation;
    g_settings.lastError.clear();
    PublishThrottlePreviewLocked();
    return true;
}

bool SettingsDirtyLocked() noexcept
{
    return !AbsoluteControlSettings::Equivalent(
               g_settings.draft, g_settings.saved) ||
           g_settings.draftBindings != g_settings.savedBindings ||
           g_settings.draftRoutes != g_settings.savedRoutes;
}

bool IsBound(std::string_view binding) noexcept
{
    return !binding.empty() && binding != "(unbound)" && binding != "-1";
}

Result __cdecl ReadFlightAxesCompositionStates(
    void*, const char* moduleId, const char* pageId,
    AbsoluteControlCompositionExperimental::NodeStateV1* states,
    std::uint32_t capacity, std::uint32_t* count) noexcept
{
    namespace Composition = AbsoluteControlCompositionExperimental;
    if (!moduleId || !pageId || !states || !count ||
        std::string_view{moduleId} != kHotasModuleId ||
        std::string_view{pageId} != HotasBindingCatalog::kFlightAxesPageId ||
        capacity < 9) {
        return Result::InvalidArgument;
    }

    try {
        std::scoped_lock lock(g_settingsMutex);
        if (!LoadSessionLocked(true)) return Result::Rejected;
        std::uint32_t output{};
        const auto append = [&](std::string_view nodeId,
                                std::string_view value,
                                std::string_view detail,
                                std::string_view source,
                                Composition::StatusSeverity severity,
                                std::uint32_t flags) {
            auto& state = states[output++];
            state = {};
            state.flags = flags;
            state.severity = severity;
            state.sequence = g_settings.generation;
            Copy(state.nodeId, nodeId);
            Copy(state.value, value);
            Copy(state.detail, detail);
            Copy(state.sourceLabel, source);
        };

        constexpr std::array<std::string_view, 6> cardIds{
            "axis-throttle-card", "axis-pitch-card", "axis-yaw-card",
            "axis-roll-card", "axis-strafe-lateral-card",
            "axis-strafe-vertical-card"};
        constexpr auto stateFlags = Composition::kNodeStateVisible |
            Composition::kNodeStateEnabled | Composition::kNodeStateRequired;
        std::size_t ready{};
        for (std::size_t index = 0; index < cardIds.size(); ++index) {
            const auto& binding = g_settings.draftBindings[index];
            if (IsBound(binding)) ++ready;
        }
        append("flight-summary", std::format("{} / 6 AXES BOUND", ready),
            "Flight-axis injection is profile-owned. Buttons and macros remain independent.",
            g_settings.dirty ? "UNAPPLIED DRAFT" : "MAIN CONTROLS",
            ready == cardIds.size() ? Composition::StatusSeverity::Normal :
                Composition::StatusSeverity::Information,
            Composition::kNodeStateVisible | Composition::kNodeStateEnabled);

        for (std::size_t index = 0; index < cardIds.size(); ++index) {
            const auto& binding = g_settings.draftBindings[index];
            const bool bound = IsBound(binding);
            append(cardIds[index], bound ? "BOUND" : "NEEDS BINDING",
                bound ? "The selected DirectInput axis is ready for live verification."
                      : "Bind a DirectInput axis; tuning remains editable before capture.",
                bound ? binding : "NO INPUT SOURCE",
                bound ? Composition::StatusSeverity::Normal :
                    Composition::StatusSeverity::Warning,
                stateFlags);
        }

        const auto& analogReverse = g_settings.draftBindings[6];
        const auto& digitalReverse = g_settings.draftBindings[7];
        const bool reverseBound = IsBound(analogReverse) || IsBound(digitalReverse);
        append("reverse-card", reverseBound ? "AVAILABLE" : "OPTIONAL",
            "A held digital reverse binding suppresses the dedicated reverse axis while active.",
            IsBound(digitalReverse) ? "DIGITAL REVERSE" :
                IsBound(analogReverse) ? "ANALOG REVERSE" : "NO REVERSE BINDING",
            reverseBound ? Composition::StatusSeverity::Information :
                Composition::StatusSeverity::Normal,
            Composition::kNodeStateVisible | Composition::kNodeStateEnabled);

        std::size_t digitalReady{};
        for (std::size_t index = 8; index < 14; ++index) {
            if (IsBound(g_settings.draftBindings[index])) ++digitalReady;
        }
        append("digital-fallback-card",
            std::format("{} / 6 BOUND", digitalReady),
            "Digital roll and strafe controls are optional fallbacks for unbound analog lanes.",
            "BUTTON / POV FALLBACKS",
            digitalReady == 0 ? Composition::StatusSeverity::Normal :
                Composition::StatusSeverity::Information,
            Composition::kNodeStateVisible | Composition::kNodeStateEnabled);
        *count = output;
        return Result::Ok;
    } catch (...) {
        *count = 0;
        return Result::Rejected;
    }
}

Result RegisterFlightAxesComposition(
    const AbsoluteControlCompositionExperimental::ApiV1* api) noexcept
{
    namespace Composition = AbsoluteControlCompositionExperimental;
    if (!api || api->structSize < sizeof(Composition::ApiV1) ||
        api->abiVersion != Composition::kAbiVersion ||
        (api->capabilities & Composition::kC2Capabilities) !=
            Composition::kC2Capabilities ||
        !api->registerPageComposition || !api->unregisterModule ||
        !api->requestRefresh) {
        return Result::InvalidArgument;
    }
    const auto descriptor =
        AbsoluteControlFlightAxesComposition::Descriptor(
            nullptr, &ReadFlightAxesCompositionStates);
    const auto result = api->registerPageComposition(&descriptor);
    if (result == Result::Ok || result == Result::Duplicate) {
        g_compositionRegistered.store(true, std::memory_order_release);
        return Result::Ok;
    }
    return result;
}

Result RegisterShipButtonsComposition(
    const AbsoluteControlCompositionExperimental::ApiV1* api) noexcept
{
    namespace Composition = AbsoluteControlCompositionExperimental;
    if (!api || api->structSize < sizeof(Composition::ApiV1) ||
        api->abiVersion != Composition::kAbiVersion ||
        (api->capabilities & Composition::kC1Capabilities) !=
            Composition::kC1Capabilities ||
        !api->registerPageComposition || !api->unregisterModule ||
        !api->requestRefresh) {
        return Result::InvalidArgument;
    }
    const auto descriptor =
        AbsoluteControlShipButtonsComposition::Descriptor();
    const auto result = api->registerPageComposition(&descriptor);
    if (result == Result::Ok || result == Result::Duplicate) {
        g_shipCompositionRegistered.store(true, std::memory_order_release);
        return Result::Ok;
    }
    return result;
}

Result ReadSettingsValue(std::string_view id, ValueV1& output) noexcept
{
    using namespace AbsoluteControlSettings;
    const auto* definition = FindDefinition(id);
    const auto* binding = HotasBindingCatalog::Find(id);
    if (!definition && !binding) return Result::NotFound;
    std::scoped_lock lock(g_settingsMutex);
    if (!LoadSessionLocked(true)) return Result::Rejected;

    if (binding) {
        output = StringValue(
            g_settings.draftBindings[HotasBindingCatalog::IndexOf(*binding)]);
        return Result::Ok;
    }

    switch (definition->type) {
    case ScalarType::Boolean:
        output = BooleanValue(GetBoolean(g_settings.draft, definition->field));
        break;
    case ScalarType::Integer:
    case ScalarType::Choice:
        output = IntegerValue(GetInteger(g_settings.draft, definition->field));
        break;
    case ScalarType::Float:
        output = FloatValue(GetFloat(g_settings.draft, definition->field));
        break;
    }
    return Result::Ok;
}

std::string_view AvailabilityLabel(ShipActionAvailability availability) noexcept
{
    switch (availability) {
    case ShipActionAvailability::SupportedWaitingForContext:
        return "supported; waiting for ship context";
    case ShipActionAvailability::AvailableNow:
        return "available now";
    case ShipActionAvailability::UnavailableForBuild:
        return "unavailable for this game build";
    case ShipActionAvailability::UnavailableInContext:
        return "unavailable in the current context";
    }
    return "availability unknown";
}

std::string_view KeyboardSourceLabel(KeyboardResolutionSource source) noexcept
{
    switch (source) {
    case KeyboardResolutionSource::NotApplicable: return "not applicable";
    case KeyboardResolutionSource::FixedContext: return "fixed context route";
    case KeyboardResolutionSource::VanillaFallback: return "vanilla fallback";
    case KeyboardResolutionSource::ControlMapCustom: return "current ControlMap binding";
    case KeyboardResolutionSource::LegacyManualOverride: return "manual compatibility override";
    }
    return "unknown source";
}

std::string RouteDetail(const ShipActionRouteInfo& route)
{
    const auto selection = route.methodOverridden
        ? "configured method override" : "catalog method";
    switch (route.method) {
    case ShipControlMethod::Direct:
        return std::format("Direct native action | {} | {}", selection,
                           AvailabilityLabel(route.availability));
    case ShipControlMethod::Context:
        return std::format("Context navigation route | {} | {}", selection,
                           AvailabilityLabel(route.availability));
    case ShipControlMethod::KeyboardCompatibility: {
        std::string output = "no resolved output";
        if (route.resolvedKeyboardOutput.kind == ShipOutputKind::Keyboard) {
            output = std::format("keyboard scan 0x{:02X}",
                route.resolvedKeyboardOutput.code);
        } else if (route.resolvedKeyboardOutput.kind == ShipOutputKind::Mouse) {
            output = std::format("mouse button {}",
                route.resolvedKeyboardOutput.code);
        }
        return std::format("Keyboard compatibility ({}, {}) | {} | {}",
            output, KeyboardSourceLabel(route.keyboardSource), selection,
            AvailabilityLabel(route.availability));
    }
    }
    return "Unknown route";
}

const HotasBindingCatalog::Target* FindShipRouteTarget(
    std::string_view id) noexcept
{
    for (const auto& target : HotasBindingCatalog::kTargets) {
        if (target.family != HotasBindingCatalog::TargetFamily::ShipAction ||
            id != std::format("{}-route", target.controlId)) {
            continue;
        }
        return &target;
    }
    return nullptr;
}

Result ReadShipRouteStatus(std::string_view id, ValueV1& output)
{
    const auto* target = FindShipRouteTarget(id);
    if (!target) return Result::NotFound;
    const auto* action = FindShipAction(target->actionId);
    if (!action) return Result::NotFound;

    if (action->allowedMethods == kDirectOrKeyboard) {
        std::scoped_lock lock(g_settingsMutex);
        if (!LoadSessionLocked(true)) return Result::Rejected;
        const auto index = static_cast<std::size_t>(
            action - kShipActionCatalog.data());
        output = IntegerValue(
            g_settings.draftRoutes[index] ==
                ShipControlMethod::KeyboardCompatibility ? 1 : 0);
    } else {
        output = StringValue(RouteDetail(
            ShipOutputSystem::GetShipActionRouteInfo(target->actionId)));
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
        output = StringValue(AbsoluteControlTelemetry::IsRegistered() ?
            "Static axis settings, DirectInput capture, and host-native input/output telemetry are available." :
            "Static axis settings and DirectInput capture are available; this Control build does not expose the optional live-component API.");
    } else if (id == "flight-throttle-summary") {
        output = StringValue(
            "Positional and rate-throttle behavior are edited once on Throttle Setup from the same HOTAS-owned settings draft.");
    } else if (id == "ship-buttons-scope") {
        output = StringValue(
            "Named actions, routes, flight assists, menu reuse, and custom shortcuts are published here.");
    } else if (id == "throttle-scope") {
        using Target = AbsoluteControlTelemetry::ThrottleCaptureTarget;
        switch (AbsoluteControlTelemetry::GetThrottleCaptureTarget()) {
        case Target::Detent:
            output = StringValue(
                "LIVE SET: move the throttle to the cruise detent. The cyan handle follows it; press Set cruise from throttle again or Apply to keep it.");
            break;
        case Target::Reverse:
            output = StringValue(
                "LIVE SET: move the throttle to physical zero thrust. The cyan handle follows it; press Set zero thrust from throttle again or Apply to keep it.");
            break;
        case Target::Boost:
            output = StringValue(
                "LIVE SET: move the throttle to the boost boundary. The cyan handle follows it; press Set boost from throttle again or Apply to keep it.");
            break;
        case Target::None:
            output = StringValue(AbsoluteControlTelemetry::IsRegistered() ?
                "The pinned axis range previews this draft live. Response history is a secondary diagnostic and starts collapsed." :
                "Throttle settings and landmark capture are available; optional live range telemetry is unavailable on this Control build.");
            break;
        }
    } else if (id == "aiming-scope") {
        output = StringValue(AbsoluteControlTelemetry::IsRegistered() ?
            "Static HOTAS aiming settings, bindings, and host-native aim telemetry are available. Camera and native mouse steering remain external." :
            "Static HOTAS aiming settings and bindings are available; optional live telemetry is unavailable. Camera and native mouse steering remain external.");
    } else if (id == "profiles-scope") {
        output = StringValue(
            "Profiles remain editable in the embedded workbench because this Control host lacks the required record and confirmation capabilities.");
    } else if (id == "macros-scope") {
        output = StringValue(
            "Macros remain editable in the embedded workbench because this Control host lacks the required record and capture capabilities.");
    } else if (id == "devices-scope") {
        output = StringValue(
            "Device inventory and calibration remain in the embedded workbench because this Control host lacks the required record and confirmation capabilities.");
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
        const bool zero = g_externalMouseSteeringOwner.load(std::memory_order_acquire);
        const bool head = g_externalCameraOwner.load(std::memory_order_acquire);
        if (zero && head) {
            output = StringValue(
                "Absolute Head Tracking owns camera; AbsoluteZero owns native mouse pitch/yaw; HOTAS retains the flight observer, writer, and other lanes.");
        } else if (head) {
            output = StringValue(
                "Absolute Head Tracking owns camera; HOTAS retains the shared flight observer and flight-control lanes.");
        } else if (zero) {
            output = StringValue(
                "AbsoluteZero compatibility active: native mouse owns pitch/yaw; HOTAS retains roll, strafe, and the shared writer hook.");
        } else {
            output = StringValue(
                "Standalone HOTAS steering and legacy camera ownership; optional Absolute Flight Runtime is deferred.");
        }
    } else {
        return ReadShipRouteStatus(id, output);
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

bool ContextOwns(void* rawContext,
                 const HotasBindingCatalog::Target& target) noexcept
{
    const auto* context = static_cast<const PageContext*>(rawContext);
    return context && context->pageId == target.pageId;
}

bool DecodeBindingDraft(const HotasBindingCatalog::Target& target,
                        const ValueV1& value, std::string& binding) noexcept
{
    if (value.kind != ValueKind::String ||
        std::memchr(value.stringValue, '\0', sizeof(value.stringValue)) == nullptr) {
        return false;
    }
    std::string_view raw{value.stringValue};
    if (raw.empty() || raw == "-1" || raw == "(unbound)") {
        binding = "(unbound)";
        return true;
    }
    const auto parsed = ParseBindingRef(value.stringValue, -1);
    const bool valid = target.captureKind == HotasBindingCatalog::CaptureKind::Axis
        ? parsed.value >= 0x30 && parsed.value <= 0x37
        : parsed.value >= 1 && parsed.value <= 144;
    if (!valid) return false;
    binding = FormatBindingRef(
        parsed, target.captureKind == HotasBindingCatalog::CaptureKind::Axis);
    return true;
}

Result __cdecl WriteDraft(void* rawContext, const char* rawId,
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
        const auto* definition = AbsoluteControlSettings::FindDefinition(id);
        const auto* bindingTarget = HotasBindingCatalog::Find(id);
        if (!definition && !bindingTarget) return Result::NotFound;

        if (bindingTarget) {
            if (!ContextOwns(rawContext, *bindingTarget)) {
                return Result::InvalidArgument;
            }
            std::string binding;
            if (!DecodeBindingDraft(*bindingTarget, *value, binding)) {
                return Result::InvalidArgument;
            }
            const auto targetIndex = HotasBindingCatalog::IndexOf(*bindingTarget);
            if (binding != "(unbound)") {
                for (std::size_t index = 0;
                     index < g_settings.draftBindings.size(); ++index) {
                    if (index != targetIndex &&
                        g_settings.draftBindings[index] == binding) {
                        return Result::Duplicate;
                    }
                }
            }
            g_settings.draftBindings[targetIndex] = std::move(binding);
            g_settings.dirty = SettingsDirtyLocked();
            g_settings.lastError.clear();
            return Result::Ok;
        }

        auto candidate = g_settings.draft;
        using AbsoluteControlSettings::ScalarType;
        switch (definition->type) {
        case ScalarType::Boolean:
            if (value->kind != ValueKind::Boolean || value->booleanValue > 1) {
                return Result::InvalidArgument;
            }
            AbsoluteControlSettings::SetBoolean(
                candidate, definition->field, value->booleanValue != 0);
            break;
        case ScalarType::Integer:
        case ScalarType::Choice:
            if (value->kind != ValueKind::Integer ||
                value->integerValue < definition->minimum ||
                value->integerValue > definition->maximum ||
                ((definition->type == ScalarType::Choice ||
                  definition->field ==
                      AbsoluteControlSettings::ScalarField::PilotLatchMilliseconds) &&
                 definition->step >= 1.0 &&
                    (value->integerValue - static_cast<std::int64_t>(definition->minimum)) %
                        static_cast<std::int64_t>(definition->step) != 0)) {
                return Result::InvalidArgument;
            }
            AbsoluteControlSettings::SetInteger(
                candidate, definition->field, value->integerValue);
            break;
        case ScalarType::Float:
            if (value->kind != ValueKind::Float ||
                !std::isfinite(value->floatValue) ||
                value->floatValue < definition->minimum ||
                value->floatValue > definition->maximum) {
                return Result::InvalidArgument;
            }
            AbsoluteControlSettings::SetFloat(
                candidate, definition->field, value->floatValue);
            break;
        }

        std::string validationError;
        if (!AbsoluteControlSettings::Validate(candidate, validationError)) {
            g_settings.lastError = std::move(validationError);
            return Result::InvalidArgument;
        }
        using ScalarField = AbsoluteControlSettings::ScalarField;
        if (definition->field == ScalarField::DetentCenter ||
            definition->field == ScalarField::ReverseZoneCenter ||
            definition->field == ScalarField::BoostZoneCenter) {
            // Direct graph manipulation takes ownership from an in-progress
            // physical set-by-feel gesture so the two input sources cannot
            // fight over the same landmark.
            AbsoluteControlTelemetry::SetThrottleCaptureTarget(
                AbsoluteControlTelemetry::ThrottleCaptureTarget::None);
        }
        g_settings.draft = candidate;
        PublishThrottlePreviewLocked();

        g_settings.dirty = SettingsDirtyLocked();
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
        using CaptureTarget = AbsoluteControlTelemetry::ThrottleCaptureTarget;
        const auto liveCapture =
            AbsoluteControlTelemetry::GetThrottleCaptureTarget();
        if (liveCapture != CaptureTarget::None) {
            std::int64_t current{};
            if (!AbsoluteControlTelemetry::ReadPrimaryThrottleRaw(current)) {
                g_settings.lastError =
                    "The primary throttle has no live sample to finish landmark setup.";
                return Result::NotReady;
            }
            const auto action = liveCapture == CaptureTarget::Detent ?
                AbsoluteControlThrottleActions::Action::CaptureDetent :
                liveCapture == CaptureTarget::Reverse ?
                    AbsoluteControlThrottleActions::Action::CaptureReverse :
                    AbsoluteControlThrottleActions::Action::CaptureBoost;
            std::string captureError;
            if (AbsoluteControlThrottleActions::Apply(
                    g_settings.draft, action, current, captureError) !=
                AbsoluteControlThrottleActions::Result::Applied) {
                g_settings.lastError = std::move(captureError);
                return Result::InvalidArgument;
            }
            AbsoluteControlTelemetry::SetThrottleCaptureTarget(
                CaptureTarget::None);
            PublishThrottlePreviewLocked();
            g_settings.dirty = SettingsDirtyLocked();
        }
        if (!g_settings.dirty) return Result::Ok;
        if (AbsoluteControlSettings::CurrentRevision().sourceFingerprint !=
            g_settings.revision.sourceFingerprint) {
            g_settings.lastError =
                "The HOTAS configuration changed after this draft was opened; discard it and try again.";
            return Result::Rejected;
        }

        AbsoluteControlSettings::ScalarState readBack;
        HotasBindingCatalog::BindingState bindingReadBack;
        AbsoluteControlSettings::Revision revision;
        std::string error;
        AbsoluteControlSettings::ShipRouteState routeReadBack;
        if (!AbsoluteControlSettings::ApplyEditTargetWithBindingsAndRoutes(
                g_settings.draft, g_settings.draftBindings,
                g_settings.draftRoutes, g_settings.editTarget,
                g_settings.revision, readBack,
                bindingReadBack, routeReadBack, revision, error)) {
            g_settings.lastError = error.empty() ?
                "AbsoluteHOTAS could not persist this draft." : std::move(error);
            return Result::WriteFailure;
        }

        g_settings.saved = readBack;
        g_settings.draft = readBack;
        g_settings.savedBindings = bindingReadBack;
        g_settings.draftBindings = std::move(bindingReadBack);
        g_settings.savedRoutes = routeReadBack;
        g_settings.draftRoutes = routeReadBack;
        g_settings.revision = revision;
        g_settings.dirty = false;
        ++g_settings.generation;
        g_settings.lastError.clear();
        PublishThrottlePreviewLocked();
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

void __cdecl CancelDraft(void*) noexcept
{
    try {
        std::scoped_lock lock(g_settingsMutex);
        HotasBindingCapture::Cancel();
        g_capture = {};
        AbsoluteControlTelemetry::SetThrottleCaptureTarget(
            AbsoluteControlTelemetry::ThrottleCaptureTarget::None);
        g_settings.loaded = false;
        g_settings.dirty = false;
        (void)LoadSessionLocked(false);
    } catch (...) {
        std::scoped_lock lock(g_settingsMutex);
        g_settings = {};
        g_settings.lastError = "AbsoluteHOTAS could not restore the persisted settings.";
    }
}

Result __cdecl InvokeThrottleAction(void*, const char* rawId) noexcept
{
    if (!rawId) return Result::InvalidArgument;
    try {
        if (!AbsoluteControlSettings::CanEdit()) return Result::Rejected;
        const std::string_view id{rawId};
        AbsoluteControlThrottleActions::Action action{};
        AbsoluteControlTelemetry::ThrottleCaptureTarget captureTarget{
            AbsoluteControlTelemetry::ThrottleCaptureTarget::None};
        bool needsRaw{};
        if (id == "throttle-capture-detent") {
            action = AbsoluteControlThrottleActions::Action::CaptureDetent;
            needsRaw = true;
            captureTarget =
                AbsoluteControlTelemetry::ThrottleCaptureTarget::Detent;
        } else if (id == "throttle-capture-reverse") {
            action = AbsoluteControlThrottleActions::Action::CaptureReverse;
            needsRaw = true;
            captureTarget =
                AbsoluteControlTelemetry::ThrottleCaptureTarget::Reverse;
        } else if (id == "throttle-capture-boost") {
            action = AbsoluteControlThrottleActions::Action::CaptureBoost;
            needsRaw = true;
            captureTarget =
                AbsoluteControlTelemetry::ThrottleCaptureTarget::Boost;
        } else if (id == "throttle-link-idle-saturation") {
            action = AbsoluteControlThrottleActions::Action::LinkIdleAndSaturation;
        } else {
            return Result::NotFound;
        }

        if (needsRaw && AbsoluteControlTelemetry::GetThrottleCaptureTarget() !=
                captureTarget) {
            AbsoluteControlTelemetry::SetThrottleCaptureTarget(captureTarget);
            return Result::Ok;
        }

        std::optional<std::int64_t> logicalRaw;
        if (needsRaw) {
            std::int64_t current{};
            if (AbsoluteControlTelemetry::ReadPrimaryThrottleRaw(current)) {
                logicalRaw = current;
            }
        }
        std::scoped_lock lock(g_settingsMutex);
        if (!LoadSessionLocked(true)) return Result::Rejected;
        std::string error;
        switch (AbsoluteControlThrottleActions::Apply(
            g_settings.draft, action, logicalRaw, error)) {
        case AbsoluteControlThrottleActions::Result::Applied:
            if (needsRaw) {
                AbsoluteControlTelemetry::SetThrottleCaptureTarget(
                    AbsoluteControlTelemetry::ThrottleCaptureTarget::None);
            }
            PublishThrottlePreviewLocked();
            g_settings.dirty = SettingsDirtyLocked();
            g_settings.lastError.clear();
            return Result::Ok;
        case AbsoluteControlThrottleActions::Result::InputUnavailable:
            g_settings.lastError = std::move(error);
            return Result::NotReady;
        case AbsoluteControlThrottleActions::Result::InvalidDraft:
            g_settings.lastError = std::move(error);
            return Result::InvalidArgument;
        }
        return Result::Rejected;
    } catch (...) {
        return Result::Rejected;
    }
}

Result __cdecl ReadChoiceOptions(void*, const char* rawId,
                                 ChoiceOptionV1* options,
                                 std::uint32_t capacity,
                                 std::uint32_t* outputCount) noexcept
{
    if (!rawId || !outputCount) return Result::InvalidArgument;
    try {
        const std::string_view id{rawId};
        const bool pilotMode = id == "pilot-context-mode";
        const bool turnAssistMode = id == "turn-assist-mode";
        if (!pilotMode && !turnAssistMode) {
            *outputCount = 0;
            return Result::NotFound;
        }
        constexpr std::array<std::pair<std::int64_t, std::string_view>, 3> pilotValues{{
            {0, "Do not park automatically"},
            {1, "Park flight controls"},
            {2, "Park all plugin output"},
        }};
        constexpr std::array<std::pair<std::int64_t, std::string_view>, 3> turnValues{{
            {0, "Always"},
            {1, "While held"},
            {2, "Toggle on/off"},
        }};
        const auto& values = pilotMode ? pilotValues : turnValues;
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

bool ReadStringValue(const ValueV1& value, std::string_view& output) noexcept;
bool OtherProfileContextDraftDirty() noexcept;
void InvalidateSelectedProfileSessions() noexcept;

bool EnsureMacroSessionLocked() noexcept
{
    if (g_macroSession) return true;
    auto session = std::make_unique<AbsoluteControlMacros::Session>(
        AbsoluteControlMacros::WizardRepository());
    std::string error;
    if (!session->Open(error)) {
        g_macroError = error.empty()
            ? "AbsoluteHOTAS could not open its macro and shortcut configuration."
            : std::move(error);
        return false;
    }
    g_macroSession = std::move(session);
    g_macroError.clear();
    return true;
}

Result ReadDynamicValue(std::string_view id, ValueV1& output) noexcept
{
    std::scoped_lock lock(g_macroMutex);
    if (!EnsureMacroSessionLocked()) return Result::Rejected;
    const auto* macro = g_macroSession->SelectedMacro();
    const auto* step = g_macroSession->SelectedStep();
    const auto* shortcut = g_macroSession->SelectedShortcut();
    if (id == "macro-records") output = StringValue(g_macroSession->SelectedMacroId());
    else if (id == "macro-name") output = StringValue(macro ? macro->name : "");
    else if (id == "macro-trigger") output = StringValue(macro ? macro->buttonBinding : "(unbound)");
    else if (id == "macro-turbo") output = BooleanValue(macro && macro->turbo);
    else if (id == "macro-step-records") output = StringValue(g_macroSession->SelectedStepId());
    else if (id == "macro-step-action") output = IntegerValue(step && step->hold ? 1 : 0);
    else if (id == "macro-step-amount") output = IntegerValue(step ? step->amount : 0);
    else if (id == "macro-step-gap") output = IntegerValue(step ? step->gapMs : 0);
    else if (id == "macro-target-records") output = StringValue(g_macroSession->SelectedTargetId());
    else if (id == "macro-target-catalog") output = IntegerValue(g_targetCatalogSelection);
    else if (id == "shortcut-records") output = StringValue(g_macroSession->SelectedShortcutId());
    else if (id == "shortcut-trigger") output = StringValue(shortcut ? shortcut->buttonBinding : "(unbound)");
    else if (id == "shortcut-output") output = IntegerValue(shortcut
        ? AbsoluteControlMacros::Session::OutputCatalogIndex(shortcut->output) : -1);
    else if (id == "shortcut-macro-link") output = StringValue(
        "Use the Macros page for chords, ordered sequences, holds, gaps, and turbo.");
    else if (id == "macro-status" || id == "shortcut-status") output = StringValue(
        !g_macroError.empty() ? g_macroError : g_macroSession->Dirty()
            ? "This page has unapplied HOTAS configuration changes."
            : "This page is synchronized with HOTAS-owned configuration.");
    else return Result::NotFound;
    return Result::Ok;
}

Result __cdecl ReadMacroValue(void*, const char* rawId, ValueV1* output) noexcept
{
    if (!rawId || !output || output->structSize < sizeof(ValueV1)) return Result::InvalidArgument;
    try { return ReadDynamicValue(rawId, *output); }
    catch (...) { return Result::Rejected; }
}

Result __cdecl ReadShipValue(void* context, const char* rawId, ValueV1* output) noexcept
{
    if (!rawId || !output || output->structSize < sizeof(ValueV1)) return Result::InvalidArgument;
    try {
        const std::string_view id{rawId};
        if (id.starts_with("shortcut-")) return ReadDynamicValue(id, *output);
        return ReadValue(context, rawId, output);
    } catch (...) { return Result::Rejected; }
}

Result WriteDynamicDraft(std::string_view id, const ValueV1& value) noexcept
{
    std::scoped_lock lock(g_macroMutex);
    if (!EnsureMacroSessionLocked()) return Result::Rejected;
    std::string_view text;
    bool accepted{};
    if (id == "macro-records" || id == "macro-step-records" ||
        id == "macro-target-records" || id == "macro-name" ||
        id == "macro-trigger" || id == "shortcut-records" ||
        id == "shortcut-trigger") {
        if (!ReadStringValue(value, text)) return Result::InvalidArgument;
    }
    if (id == "macro-records") accepted = g_macroSession->SelectMacro(text);
    else if (id == "macro-name") accepted = g_macroSession->SetMacroName(std::string{text});
    else if (id == "macro-trigger") accepted = g_macroSession->SetMacroTrigger(text);
    else if (id == "macro-turbo") {
        if (value.kind != ValueKind::Boolean || value.booleanValue > 1) return Result::InvalidArgument;
        accepted = g_macroSession->SetMacroTurbo(value.booleanValue != 0);
    } else if (id == "macro-step-records") accepted = g_macroSession->SelectStep(text);
    else if (id == "macro-step-action") {
        if (value.kind != ValueKind::Integer || value.integerValue < 0 || value.integerValue > 1) return Result::InvalidArgument;
        accepted = g_macroSession->SetStepHold(value.integerValue == 1);
    } else if (id == "macro-step-amount") {
        if (value.kind != ValueKind::Integer || value.integerValue < 0 || value.integerValue > 60000) return Result::InvalidArgument;
        accepted = g_macroSession->SetStepAmount(static_cast<int>(value.integerValue));
    } else if (id == "macro-step-gap") {
        if (value.kind != ValueKind::Integer || value.integerValue < 0 || value.integerValue > 60000) return Result::InvalidArgument;
        accepted = g_macroSession->SetStepGap(static_cast<int>(value.integerValue));
    } else if (id == "macro-target-records") accepted = g_macroSession->SelectTarget(text);
    else if (id == "macro-target-catalog") {
        if (value.kind != ValueKind::Integer || value.integerValue < 0 ||
            value.integerValue >= static_cast<std::int64_t>(AbsoluteControlMacros::Session::TargetCatalogSize()))
            return Result::InvalidArgument;
        g_targetCatalogSelection = value.integerValue;
        return Result::Ok;
    } else if (id == "shortcut-records") accepted = g_macroSession->SelectShortcut(text);
    else if (id == "shortcut-trigger") accepted = g_macroSession->SetShortcutTrigger(text);
    else if (id == "shortcut-output") {
        if (value.kind != ValueKind::Integer || value.integerValue < 0 ||
            value.integerValue >= static_cast<std::int64_t>(AbsoluteControlMacros::Session::OutputCatalogSize()))
            return Result::InvalidArgument;
        accepted = g_macroSession->SetShortcutOutput(static_cast<std::size_t>(value.integerValue));
    } else return Result::NotFound;
    return accepted ? Result::Ok : Result::NotFound;
}

Result __cdecl WriteMacroDraft(void*, const char* rawId, const ValueV1* value) noexcept
{
    if (!rawId || !value || value->structSize < sizeof(ValueV1)) return Result::InvalidArgument;
    try {
        if (!AbsoluteControlSettings::CanEdit()) return Result::Rejected;
        return WriteDynamicDraft(rawId, *value);
    } catch (...) { return Result::Rejected; }
}

Result __cdecl WriteShipDraft(void* context, const char* rawId, const ValueV1* value) noexcept
{
    if (!rawId || !value || value->structSize < sizeof(ValueV1)) return Result::InvalidArgument;
    try {
        const std::string_view id{rawId};
        if (id.starts_with("shortcut-"))
            return WriteMacroDraft(context, rawId, value);
        if (const auto* target = FindShipRouteTarget(id)) {
            if (!AbsoluteControlSettings::CanEdit() ||
                !ContextOwns(context, *target) ||
                value->kind != ValueKind::Integer ||
                value->integerValue < 0 || value->integerValue > 1) {
                return Result::InvalidArgument;
            }
            const auto* action = FindShipAction(target->actionId);
            if (!action || action->allowedMethods != kDirectOrKeyboard) {
                return Result::Rejected;
            }
            const auto method = value->integerValue == 0
                ? ShipControlMethod::Direct
                : ShipControlMethod::KeyboardCompatibility;
            if (!AllowsShipControlMethod(*action, method)) {
                return Result::InvalidArgument;
            }
            std::scoped_lock lock(g_settingsMutex);
            if (!LoadSessionLocked(true)) return Result::Rejected;
            const auto index = static_cast<std::size_t>(
                action - kShipActionCatalog.data());
            g_settings.draftRoutes[index] = method;
            g_settings.dirty = SettingsDirtyLocked();
            g_settings.lastError.clear();
            return Result::Ok;
        }
        return WriteDraft(context, rawId, value);
    } catch (...) { return Result::Rejected; }
}

Result __cdecl ApplyMacroDraft(void*) noexcept
{
    try {
        if (!AbsoluteControlSettings::CanEdit()) return Result::Rejected;
        std::scoped_lock lock(g_macroMutex);
        if (!EnsureMacroSessionLocked()) return Result::Rejected;
        std::string error;
        if (!g_macroSession->Apply(error)) {
            g_macroError = std::move(error);
            return Result::WriteFailure;
        }
        g_macroError.clear();
        return Result::Ok;
    } catch (...) { return Result::Rejected; }
}

Result __cdecl ApplyShipDraft(void* context) noexcept
{
    const auto settingsResult = ApplyDraft(context);
    return settingsResult == Result::Ok ? ApplyMacroDraft(context) : settingsResult;
}

void __cdecl CancelMacroDraft(void*) noexcept
{
    try {
        std::scoped_lock lock(g_macroMutex);
        HotasBindingCapture::Cancel();
        g_macroCapture = {};
        if (EnsureMacroSessionLocked()) g_macroSession->Cancel();
        g_macroError.clear();
    } catch (...) { g_macroError = "AbsoluteHOTAS could not restore the macro draft."; }
}

void __cdecl CancelShipDraft(void* context) noexcept
{
    CancelDraft(context);
    CancelMacroDraft(context);
}

Result __cdecl ReadMacroChoices(void*, const char* rawId, ChoiceOptionV1* options,
                                 std::uint32_t capacity, std::uint32_t* outputCount) noexcept
{
    if (!rawId || !outputCount) return Result::InvalidArgument;
    const std::string_view id{rawId};
    std::size_t count{};
    if (id == "macro-step-action") count = 2;
    else if (id == "macro-target-catalog") count = AbsoluteControlMacros::Session::TargetCatalogSize();
    else if (id == "shortcut-output") count = AbsoluteControlMacros::Session::OutputCatalogSize() + 1;
    else { *outputCount = 0; return Result::NotFound; }
    *outputCount = static_cast<std::uint32_t>(count);
    if (!options || capacity < count) return Result::CapacityExceeded;
    for (std::size_t i = 0; i < count; ++i) {
        options[i] = {};
        if (id == "macro-step-action") {
            options[i].value = static_cast<std::int64_t>(i);
            Copy(options[i].label, i == 0 ? "Tap" : "Hold");
        } else if (id == "macro-target-catalog") {
            options[i].value = static_cast<std::int64_t>(i);
            Copy(options[i].label, AbsoluteControlMacros::Session::TargetCatalogLabel(i));
        } else {
            options[i].value = static_cast<std::int64_t>(i) - 1;
            Copy(options[i].label, i == 0 ? "Choose an output" :
                AbsoluteControlMacros::Session::OutputCatalogLabel(i - 1));
        }
    }
    return Result::Ok;
}

Result __cdecl ReadShipChoices(void* context, const char* rawId, ChoiceOptionV1* options,
                                std::uint32_t capacity, std::uint32_t* outputCount) noexcept
{
    if (!rawId || !outputCount) return Result::InvalidArgument;
    const std::string_view id{rawId};
    if (id == "shortcut-output")
        return ReadMacroChoices(context, rawId, options, capacity, outputCount);
    if (const auto* target = FindShipRouteTarget(id)) {
        const auto* action = FindShipAction(target->actionId);
        if (!action || action->allowedMethods != kDirectOrKeyboard) {
            *outputCount = 0;
            return Result::NotFound;
        }
        *outputCount = 2;
        if (!options || capacity < 2) return Result::CapacityExceeded;
        options[0] = {};
        options[0].value = 0;
        Copy(options[0].label, "Direct function");
        options[1] = {};
        options[1].value = 1;
        Copy(options[1].label, "SendInput key / mouse");
        return Result::Ok;
    }
    return ReadChoiceOptions(context, rawId, options, capacity, outputCount);
}

Result CopyRecords(const std::vector<AbsoluteControlMacros::Record>& records,
                   RecordItemV1* items, std::uint32_t capacity,
                   std::uint32_t* outputCount) noexcept
{
    *outputCount = static_cast<std::uint32_t>(records.size());
    if (!items || capacity < records.size()) return Result::CapacityExceeded;
    for (std::size_t i = 0; i < records.size(); ++i) {
        items[i] = {};
        items[i].flags = records[i].flags ? kRecordItemWarning : kRecordItemNone;
        Copy(items[i].recordId, records[i].recordId);
        Copy(items[i].label, records[i].label);
        Copy(items[i].summary, records[i].summary);
        Copy(items[i].detail, records[i].detail);
    }
    return Result::Ok;
}

Result __cdecl ReadMacroRecords(void*, const char* rawId, RecordItemV1* items,
                                 std::uint32_t capacity, std::uint32_t* outputCount) noexcept
{
    if (!rawId || !outputCount) return Result::InvalidArgument;
    try {
        std::scoped_lock lock(g_macroMutex);
        if (!EnsureMacroSessionLocked()) return Result::Rejected;
        const std::string_view id{rawId};
        if (id == "macro-records") return CopyRecords(g_macroSession->MacroRecords(), items, capacity, outputCount);
        if (id == "macro-step-records") return CopyRecords(g_macroSession->StepRecords(), items, capacity, outputCount);
        if (id == "macro-target-records") return CopyRecords(g_macroSession->TargetRecords(), items, capacity, outputCount);
        if (id == "shortcut-records") return CopyRecords(g_macroSession->ShortcutRecords(), items, capacity, outputCount);
        *outputCount = 0; return Result::NotFound;
    } catch (...) { return Result::Rejected; }
}

Result __cdecl InvokeMacroAction(void*, const char* rawId) noexcept
{
    if (!rawId) return Result::InvalidArgument;
    try {
        if (!AbsoluteControlSettings::CanEdit()) return Result::Rejected;
        std::scoped_lock lock(g_macroMutex);
        if (!EnsureMacroSessionLocked()) return Result::Rejected;
        const std::string_view id{rawId};
        bool result{};
        if (id == "macro-add") result = g_macroSession->AddMacro();
        else if (id == "macro-delete") result = g_macroSession->DeleteMacro();
        else if (id == "macro-step-add") result = g_macroSession->AddStep();
        else if (id == "macro-step-delete") result = g_macroSession->DeleteStep();
        else if (id == "macro-step-up") result = g_macroSession->MoveStep(-1);
        else if (id == "macro-step-down") result = g_macroSession->MoveStep(1);
        else if (id == "macro-target-add") result = g_macroSession->AddTarget(
            static_cast<std::size_t>(g_targetCatalogSelection));
        else if (id == "macro-target-delete") result = g_macroSession->DeleteTarget();
        else if (id == "shortcut-add") result = g_macroSession->AddShortcut();
        else if (id == "shortcut-delete") result = g_macroSession->DeleteShortcut();
        else if (id == "shortcut-menu-preset") result = g_macroSession->AddMenuNavigationPreset();
        else return Result::NotFound;
        return result ? Result::Ok : Result::NotReady;
    } catch (...) { return Result::Rejected; }
}

Result __cdecl InvokePageLinkAction(void*, const char* rawId) noexcept
{
    if (!rawId) return Result::InvalidArgument;
    const std::string_view id{ rawId };
    const char* pageId{};
    if (id == "flight-open-throttle") pageId = "hotas-throttle";
    else if (id == "flight-open-bindings") pageId = "hotas-ship-buttons";
    else if (id == "shortcut-macro-link") pageId = "hotas-macros";
    else return Result::NotFound;
    return AbsoluteControlSubscriber::RequestHostPage(pageId)
        ? Result::Ok : Result::NotReady;
}

Result __cdecl InvokeShipAction(void* context, const char* rawId) noexcept
{
    return rawId && std::string_view{ rawId } == "shortcut-macro-link"
        ? InvokePageLinkAction(context, rawId)
        : InvokeMacroAction(context, rawId);
}

bool EnsureProfileSessionLocked() noexcept
{
    if (g_profileSession) return true;
    auto session = std::make_unique<AbsoluteControlProfiles::Session>(
        AbsoluteControlProfiles::WizardRepository());
    std::string error;
    if (!session->Open(error)) {
        g_profileError = error.empty()
            ? "AbsoluteHOTAS could not open the profile repository."
            : std::move(error);
        return false;
    }
    g_profileSession = std::move(session);
    g_profileError.clear();
    return true;
}

std::int64_t ActivationModeValue(std::string_view mode) noexcept
{
    return mode == "toggle" ? 1 : mode == "selector" ? 2 : 0;
}

std::string_view ActivationModeName(std::int64_t mode) noexcept
{
    switch (mode) {
    case 0: return "momentary";
    case 1: return "toggle";
    case 2: return "selector";
    default: return {};
    }
}

Result __cdecl ReadProfileValue(void*, const char* rawId,
                                ValueV1* output) noexcept
{
    if (!rawId || !output || output->structSize < sizeof(ValueV1)) {
        return Result::InvalidArgument;
    }
    try {
        std::scoped_lock lock(g_profileMutex);
        if (!EnsureProfileSessionLocked()) return Result::Rejected;
        const std::string_view id{rawId};
        const auto* selected = g_profileSession->Selected();
        if (!selected) return Result::NotReady;
        if (id == "profile-records") {
            *output = StringValue(g_profileSession->SelectedRecordId());
        } else if (id == "profile-selected-status") {
            const auto inheritance = selected->inheritsMain
                ? std::format("inherits Main · {} overrides", selected->overrideCount)
                : selected->kind == "full" ? "independent full profile"
                                            : "authoritative Main controls";
            *output = StringValue(std::format(
                "{} | {} | {} | activation {} ({})",
                selected->label, inheritance, selected->filename,
                selected->activationTrigger, selected->activationMode));
        } else if (id == "profile-switch-status") {
            if (g_profileSession->HasPendingSwitch()) {
                *output = StringValue(
                    "Unsaved changes are holding the current edit target. Choose Save and switch, Discard and switch, or Stay here.");
            } else if (!g_profileError.empty()) {
                *output = StringValue(g_profileError);
            } else if (g_profileSession->Dirty()) {
                *output = StringValue(
                    "This profile page has unapplied HOTAS changes.");
            } else {
                *output = StringValue(
                    "The selected profile is synchronized with HOTAS-owned configuration.");
            }
        } else if (id == "profile-activation-mode") {
            *output = IntegerValue(ActivationModeValue(selected->activationMode));
        } else if (id == "profile-activation-trigger") {
            *output = StringValue(selected->activationTrigger);
        } else if (id == "profile-operation-name") {
            *output = StringValue(g_profileSession->OperationName());
        } else {
            return Result::NotFound;
        }
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

bool ReadStringValue(const ValueV1& value, std::string_view& output) noexcept
{
    if (value.kind != ValueKind::String) return false;
    const auto* end = static_cast<const char*>(
        std::memchr(value.stringValue, '\0', sizeof(value.stringValue)));
    if (!end) return false;
    output = std::string_view{
        value.stringValue, static_cast<std::size_t>(end - value.stringValue)};
    return true;
}

Result __cdecl WriteProfileDraft(void*, const char* rawId,
                                 const ValueV1* value) noexcept
{
    if (!rawId || !value || value->structSize < sizeof(ValueV1)) {
        return Result::InvalidArgument;
    }
    try {
        if (!AbsoluteControlSettings::CanEdit()) return Result::Rejected;
        std::scoped_lock lock(g_profileMutex);
        if (!EnsureProfileSessionLocked()) return Result::Rejected;
        const std::string_view id{rawId};
        std::string error;
        if (id == "profile-records") {
            if (OtherProfileContextDraftDirty()) {
                g_profileError =
                    "Apply or Cancel the current HOTAS page before changing profiles.";
                return Result::Rejected;
            }
            std::string_view recordId;
            if (!ReadStringValue(*value, recordId)) {
                return Result::InvalidArgument;
            }
            const std::string previous = g_profileSession->Selected()
                ? g_profileSession->Selected()->configurationName : "";
            switch (g_profileSession->Select(recordId, error)) {
            case AbsoluteControlProfiles::SelectResult::Selected:
                if (g_profileSession->Selected() &&
                    g_profileSession->Selected()->configurationName != previous) {
                    InvalidateSelectedProfileSessions();
                }
                [[fallthrough]];
            case AbsoluteControlProfiles::SelectResult::NeedsResolution:
                g_profileError.clear();
                return Result::Ok;
            case AbsoluteControlProfiles::SelectResult::NotFound:
                return Result::NotFound;
            case AbsoluteControlProfiles::SelectResult::Failed:
                g_profileError = std::move(error);
                return Result::Rejected;
            }
        } else if (id == "profile-activation-mode") {
            if (value->kind != ValueKind::Integer) {
                return Result::InvalidArgument;
            }
            const auto mode = ActivationModeName(value->integerValue);
            if (mode.empty() || !g_profileSession->SetActivationMode(mode)) {
                return Result::InvalidArgument;
            }
        } else if (id == "profile-activation-trigger") {
            std::string_view binding;
            if (!ReadStringValue(*value, binding)) {
                return Result::InvalidArgument;
            }
            switch (g_profileSession->SetActivationBinding(binding)) {
            case AbsoluteControlProfiles::BindingResult::Ok:
                break;
            case AbsoluteControlProfiles::BindingResult::Conflict:
                return Result::Duplicate;
            case AbsoluteControlProfiles::BindingResult::NotFound:
                return Result::NotFound;
            case AbsoluteControlProfiles::BindingResult::Invalid:
                return Result::InvalidArgument;
            }
        } else if (id == "profile-operation-name") {
            std::string_view name;
            if (!ReadStringValue(*value, name)) {
                return Result::InvalidArgument;
            }
            g_profileSession->SetOperationName(std::string{name});
        } else {
            return Result::NotFound;
        }
        g_profileError.clear();
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

Result __cdecl ApplyProfileDraft(void*) noexcept
{
    try {
        if (!AbsoluteControlSettings::CanEdit()) return Result::Rejected;
        std::scoped_lock lock(g_profileMutex);
        if (!EnsureProfileSessionLocked()) return Result::Rejected;
        std::string error;
        if (!g_profileSession->Apply(error)) {
            g_profileError = std::move(error);
            return Result::WriteFailure;
        }
        g_profileError.clear();
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

void __cdecl CancelProfileDraft(void*) noexcept
{
    try {
        std::scoped_lock lock(g_profileMutex);
        HotasBindingCapture::Cancel();
        g_profileCapture = {};
        if (!EnsureProfileSessionLocked()) return;
        std::string error;
        if (!g_profileSession->Cancel(error)) g_profileError = std::move(error);
        else g_profileError.clear();
    } catch (...) {
        g_profileError = "AbsoluteHOTAS could not restore the profile draft.";
    }
}

Result __cdecl ReadProfileChoices(void*, const char* rawId,
                                  ChoiceOptionV1* options,
                                  std::uint32_t capacity,
                                  std::uint32_t* outputCount) noexcept
{
    if (!rawId || !outputCount) return Result::InvalidArgument;
    if (std::string_view{rawId} != "profile-activation-mode") {
        *outputCount = 0;
        return Result::NotFound;
    }
    constexpr std::array<std::pair<std::int64_t, std::string_view>, 3> values{{
        {0, "Momentary (while held)"},
        {1, "Toggle on/off"},
        {2, "Selector position"},
    }};
    *outputCount = static_cast<std::uint32_t>(values.size());
    if (!options || capacity < values.size()) return Result::CapacityExceeded;
    for (std::size_t index = 0; index < values.size(); ++index) {
        options[index] = {};
        options[index].value = values[index].first;
        Copy(options[index].label, values[index].second);
    }
    return Result::Ok;
}

Result __cdecl ReadProfileRecords(void*, const char* rawId,
                                  RecordItemV1* items,
                                  std::uint32_t capacity,
                                  std::uint32_t* outputCount) noexcept
{
    if (!rawId || !outputCount) return Result::InvalidArgument;
    if (std::string_view{rawId} != "profile-records") {
        *outputCount = 0;
        return Result::NotFound;
    }
    try {
        std::scoped_lock lock(g_profileMutex);
        if (!EnsureProfileSessionLocked()) return Result::Rejected;
        const auto& records = g_profileSession->Records();
        *outputCount = static_cast<std::uint32_t>(records.size());
        if (!items || capacity < records.size()) return Result::CapacityExceeded;
        for (std::size_t index = 0; index < records.size(); ++index) {
            const auto& record = records[index];
            items[index] = {};
            Copy(items[index].recordId, record.recordId);
            Copy(items[index].label, record.label);
            Copy(items[index].summary,
                record.inheritsMain
                    ? std::format("Sparse overlay · inherits Main · {} overrides",
                                  record.overrideCount)
                    : record.kind == "full" ? "Independent full profile"
                                            : "Main controls");
            Copy(items[index].detail, std::format(
                "{} | keyboard {} | controller {} ({})",
                record.filename, record.keyboardShortcut,
                record.activationTrigger, record.activationMode));
        }
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

Result __cdecl InvokeProfileAction(void*, const char* rawId) noexcept
{
    if (!rawId) return Result::InvalidArgument;
    try {
        if (!AbsoluteControlSettings::CanEdit()) return Result::Rejected;
        std::scoped_lock lock(g_profileMutex);
        if (!EnsureProfileSessionLocked()) return Result::Rejected;
        const std::string_view id{rawId};
        const std::string previous = g_profileSession->Selected()
            ? g_profileSession->Selected()->configurationName : "";
        std::string error;
        bool result{};
        if (id == "profile-create-overlay") {
            result = g_profileSession->CreateOverlay(error);
        } else if (id == "profile-export-full") {
            result = g_profileSession->ExportMainFull(error);
        } else if (id == "profile-import-full") {
            result = g_profileSession->ImportSelectedFull(error);
        } else if (id == "profile-reset-main") {
            result = g_profileSession->ResetMain(error);
        } else if (id == "profile-switch-save") {
            if (!g_profileSession->HasPendingSwitch()) return Result::NotReady;
            result = g_profileSession->ResolveSwitch(
                AbsoluteControlProfiles::SwitchChoice::Save, error);
        } else if (id == "profile-switch-discard") {
            if (!g_profileSession->HasPendingSwitch()) return Result::NotReady;
            result = g_profileSession->ResolveSwitch(
                AbsoluteControlProfiles::SwitchChoice::Discard, error);
        } else if (id == "profile-switch-stay") {
            if (!g_profileSession->HasPendingSwitch()) return Result::NotReady;
            result = g_profileSession->ResolveSwitch(
                AbsoluteControlProfiles::SwitchChoice::Cancel, error);
        } else {
            return Result::NotFound;
        }
        if (!result) {
            g_profileError = std::move(error);
            return Result::Rejected;
        }
        if (g_profileSession->Selected() &&
            g_profileSession->Selected()->configurationName != previous) {
            InvalidateSelectedProfileSessions();
        }
        g_profileError.clear();
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

void WriteCaptureResult(BindingCaptureV1& output,
                        BindingCaptureState state,
                        std::string_view binding,
                        std::string_view detail) noexcept
{
    output.state = state;
    Copy(output.binding, binding);
    Copy(output.detail, detail);
}

Result __cdecl BeginBindingCapture(void* rawContext,
                                   const char* rawId) noexcept
{
    if (!rawContext || !rawId) return Result::InvalidArgument;
    try {
        const auto* target = HotasBindingCatalog::Find(rawId);
        if (!target) return Result::NotFound;
        if (!ContextOwns(rawContext, *target)) return Result::InvalidArgument;
        if (!AbsoluteControlSettings::CanEdit()) return Result::Rejected;

        std::scoped_lock lock(g_settingsMutex);
        if (!LoadSessionLocked(true)) return Result::Rejected;
        HotasBindingCapture::Cancel();
        g_capture = {
            .started = true,
            .finished = false,
            .pageId = target->pageId,
            .controlId = target->controlId,
            .draftGeneration = g_settings.generation,
            .terminalState = BindingCaptureState::Capturing,
        };
        HotasBindingCapture::Begin(*target);
        return Result::Ok;
    } catch (...) {
        HotasBindingCapture::Cancel();
        g_capture = {};
        return Result::Rejected;
    }
}

Result __cdecl PollBindingCapture(void* rawContext, const char* rawId,
                                  BindingCaptureV1* output) noexcept
{
    if (!rawContext || !rawId || !output ||
        output->structSize < sizeof(BindingCaptureV1)) {
        return Result::InvalidArgument;
    }
    try {
        const auto* target = HotasBindingCatalog::Find(rawId);
        if (!target) return Result::NotFound;
        if (!ContextOwns(rawContext, *target)) return Result::InvalidArgument;

        std::scoped_lock lock(g_settingsMutex);
        if (!g_capture.started || g_capture.controlId != target->controlId ||
            g_capture.pageId != target->pageId) {
            WriteCaptureResult(*output, BindingCaptureState::Idle, {},
                               "No capture is active for this binding.");
            return Result::Ok;
        }
        if (g_capture.draftGeneration != g_settings.generation) {
            HotasBindingCapture::Cancel();
            g_capture.finished = true;
            g_capture.terminalState = BindingCaptureState::Cancelled;
            g_capture.detail =
                "The HOTAS draft changed; start this capture again.";
        }
        if (!g_capture.finished) {
            std::string captured;
            switch (HotasBindingCapture::Poll(captured)) {
            case HotasBindingCapture::PollState::Capturing:
                WriteCaptureResult(*output, BindingCaptureState::Capturing, {},
                                   "Waiting for DirectInput movement.");
                return Result::Ok;
            case HotasBindingCapture::PollState::Captured:
                g_capture.finished = true;
                g_capture.terminalState = BindingCaptureState::Captured;
                g_capture.binding = std::move(captured);
                g_capture.detail = "Input captured; Apply saves the page draft.";
                break;
            case HotasBindingCapture::PollState::TimedOut:
                g_capture.finished = true;
                g_capture.terminalState = BindingCaptureState::TimedOut;
                g_capture.detail = "No input settled before capture timed out.";
                break;
            }
        }
        WriteCaptureResult(*output, g_capture.terminalState,
                           g_capture.binding, g_capture.detail);
        return Result::Ok;
    } catch (...) {
        WriteCaptureResult(*output, BindingCaptureState::Error, {},
                           "AbsoluteHOTAS could not poll this binding capture.");
        return Result::Rejected;
    }
}

Result __cdecl CancelBindingCapture(void* rawContext,
                                    const char* rawId) noexcept
{
    if (!rawContext || !rawId) return Result::InvalidArgument;
    try {
        const auto* target = HotasBindingCatalog::Find(rawId);
        if (!target) return Result::NotFound;
        if (!ContextOwns(rawContext, *target)) return Result::InvalidArgument;
        std::scoped_lock lock(g_settingsMutex);
        if (g_capture.started && g_capture.controlId == target->controlId) {
            HotasBindingCapture::Cancel();
            g_capture = {};
        }
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

Result __cdecl ReassignBinding(void* rawContext, const char* rawId,
                               const char* rawBinding) noexcept
{
    if (!rawContext || !rawId || !rawBinding) {
        return Result::InvalidArgument;
    }
    try {
        const auto* target = HotasBindingCatalog::Find(rawId);
        if (!target) return Result::NotFound;
        if (!ContextOwns(rawContext, *target)) return Result::InvalidArgument;
        if (!AbsoluteControlSettings::CanEdit()) return Result::Rejected;

        const auto* bindingEnd = static_cast<const char*>(
            std::memchr(rawBinding, '\0', kStringValueCapacity));
        if (!bindingEnd) return Result::InvalidArgument;
        ValueV1 value;
        value.kind = ValueKind::String;
        Copy(value.stringValue, std::string_view{
            rawBinding, static_cast<std::size_t>(bindingEnd - rawBinding)});
        std::string binding;
        if (!DecodeBindingDraft(*target, value, binding) ||
            binding == "(unbound)") {
            return Result::InvalidArgument;
        }

        std::scoped_lock lock(g_settingsMutex);
        if (!LoadSessionLocked(true)) return Result::Rejected;
        const auto targetIndex = HotasBindingCatalog::IndexOf(*target);
        bool foundPrevious{};
        for (std::size_t index = 0;
             index < g_settings.draftBindings.size(); ++index) {
            if (index != targetIndex &&
                g_settings.draftBindings[index] == binding) {
                g_settings.draftBindings[index] = "(unbound)";
                foundPrevious = true;
            }
        }
        if (!foundPrevious) return Result::NotFound;
        g_settings.draftBindings[targetIndex] = std::move(binding);
        g_settings.dirty = SettingsDirtyLocked();
        g_settings.lastError.clear();
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

bool IsProfileBinding(void* rawContext, const char* rawId) noexcept
{
    const auto* context = static_cast<const PageContext*>(rawContext);
    return context && context->pageId == "hotas-profiles" && rawId &&
           std::string_view{rawId} == "profile-activation-trigger";
}

bool IsMacroBinding(void* rawContext, const char* rawId) noexcept
{
    const auto* context = static_cast<const PageContext*>(rawContext);
    if (!context || !rawId) return false;
    const std::string_view id{rawId};
    return (context->pageId == "hotas-macros" && id == "macro-trigger") ||
           (context->pageId == "hotas-ship-buttons" && id == "shortcut-trigger");
}

Result __cdecl BeginMacroBindingCapture(void* rawContext, const char* rawId) noexcept
{
    if (!IsMacroBinding(rawContext, rawId)) return Result::InvalidArgument;
    try {
        if (!AbsoluteControlSettings::CanEdit()) return Result::Rejected;
        std::scoped_lock lock(g_macroMutex);
        if (!EnsureMacroSessionLocked()) return Result::Rejected;
        const bool macro = std::string_view{rawId} == "macro-trigger";
        if ((macro && !g_macroSession->SelectedMacro()) ||
            (!macro && !g_macroSession->SelectedShortcut())) return Result::NotReady;
        const auto* context = static_cast<const PageContext*>(rawContext);
        HotasBindingCapture::Cancel();
        g_macroCapture = {
            .started = true,
            .finished = false,
            .pageId = context->pageId,
            .controlId = macro ? std::string_view{"macro-trigger"} : std::string_view{"shortcut-trigger"},
            .terminalState = BindingCaptureState::Capturing,
        };
        HotasBindingCapture::BeginButton(
            macro ? CaptureSlot::kMacroBase : CaptureSlot::kCustomBase,
            macro ? "Macro trigger" : "Shortcut trigger",
            WizardCapture::kButtonCaptureMs);
        return Result::Ok;
    } catch (...) {
        HotasBindingCapture::Cancel();
        g_macroCapture = {};
        return Result::Rejected;
    }
}

Result __cdecl PollMacroBindingCapture(void* rawContext, const char* rawId,
                                       BindingCaptureV1* output) noexcept
{
    if (!IsMacroBinding(rawContext, rawId) || !output ||
        output->structSize < sizeof(BindingCaptureV1)) return Result::InvalidArgument;
    try {
        std::scoped_lock lock(g_macroMutex);
        if (!g_macroCapture.started || g_macroCapture.controlId != rawId) {
            WriteCaptureResult(*output, BindingCaptureState::Idle, {},
                               "No macro or shortcut capture is active.");
            return Result::Ok;
        }
        if (!g_macroCapture.finished) {
            std::string captured;
            switch (HotasBindingCapture::Poll(captured)) {
            case HotasBindingCapture::PollState::Capturing:
                WriteCaptureResult(*output, BindingCaptureState::Capturing, {},
                    "Waiting for a DirectInput button or POV direction.");
                return Result::Ok;
            case HotasBindingCapture::PollState::Captured:
                g_macroCapture.finished = true;
                g_macroCapture.terminalState = BindingCaptureState::Captured;
                g_macroCapture.binding = std::move(captured);
                g_macroCapture.detail = "Trigger captured; Apply saves the page draft.";
                break;
            case HotasBindingCapture::PollState::TimedOut:
                g_macroCapture.finished = true;
                g_macroCapture.terminalState = BindingCaptureState::TimedOut;
                g_macroCapture.detail = "No controller input settled before capture timed out.";
                break;
            }
        }
        WriteCaptureResult(*output, g_macroCapture.terminalState,
                           g_macroCapture.binding, g_macroCapture.detail);
        return Result::Ok;
    } catch (...) {
        WriteCaptureResult(*output, BindingCaptureState::Error, {},
                           "AbsoluteHOTAS could not poll trigger capture.");
        return Result::Rejected;
    }
}

Result __cdecl CancelMacroBindingCapture(void* rawContext, const char* rawId) noexcept
{
    if (!IsMacroBinding(rawContext, rawId)) return Result::InvalidArgument;
    try {
        std::scoped_lock lock(g_macroMutex);
        HotasBindingCapture::Cancel();
        g_macroCapture = {};
        return Result::Ok;
    } catch (...) { return Result::Rejected; }
}

Result __cdecl BeginShipBindingCapture(void* context, const char* rawId) noexcept
{
    return rawId && std::string_view{rawId} == "shortcut-trigger"
        ? BeginMacroBindingCapture(context, rawId)
        : BeginBindingCapture(context, rawId);
}

Result __cdecl PollShipBindingCapture(void* context, const char* rawId,
                                      BindingCaptureV1* output) noexcept
{
    return rawId && std::string_view{rawId} == "shortcut-trigger"
        ? PollMacroBindingCapture(context, rawId, output)
        : PollBindingCapture(context, rawId, output);
}

Result __cdecl CancelShipBindingCapture(void* context, const char* rawId) noexcept
{
    return rawId && std::string_view{rawId} == "shortcut-trigger"
        ? CancelMacroBindingCapture(context, rawId)
        : CancelBindingCapture(context, rawId);
}

Result __cdecl BeginProfileBindingCapture(void* rawContext,
                                          const char* rawId) noexcept
{
    if (!IsProfileBinding(rawContext, rawId)) return Result::InvalidArgument;
    try {
        if (!AbsoluteControlSettings::CanEdit()) return Result::Rejected;
        std::scoped_lock lock(g_profileMutex);
        if (!EnsureProfileSessionLocked()) return Result::Rejected;
        const auto* selected = g_profileSession->Selected();
        if (!selected) return Result::NotReady;
        HotasBindingCapture::Cancel();
        g_profileCapture = {
            .started = true,
            .finished = false,
            .pageId = "hotas-profiles",
            .controlId = "profile-activation-trigger",
            .draftGeneration = g_profileSession->Generation(),
            .terminalState = BindingCaptureState::Capturing,
        };
        HotasBindingCapture::BeginButton(
            CaptureSlot::kProfileTrigger, "Profile activation",
            selected->activationMode == "selector"
                ? WizardCapture::kSelectorCaptureMs
                : WizardCapture::kButtonCaptureMs);
        return Result::Ok;
    } catch (...) {
        HotasBindingCapture::Cancel();
        g_profileCapture = {};
        return Result::Rejected;
    }
}

Result __cdecl PollProfileBindingCapture(void* rawContext, const char* rawId,
                                         BindingCaptureV1* output) noexcept
{
    if (!IsProfileBinding(rawContext, rawId) || !output ||
        output->structSize < sizeof(BindingCaptureV1)) {
        return Result::InvalidArgument;
    }
    try {
        std::scoped_lock lock(g_profileMutex);
        if (!EnsureProfileSessionLocked()) return Result::Rejected;
        if (!g_profileCapture.started) {
            WriteCaptureResult(*output, BindingCaptureState::Idle, {},
                               "No profile activation capture is active.");
            return Result::Ok;
        }
        if (g_profileCapture.draftGeneration !=
            g_profileSession->Generation()) {
            HotasBindingCapture::Cancel();
            g_profileCapture.finished = true;
            g_profileCapture.terminalState = BindingCaptureState::Cancelled;
            g_profileCapture.detail =
                "The selected profile changed; start this capture again.";
        }
        if (!g_profileCapture.finished) {
            std::string captured;
            switch (HotasBindingCapture::Poll(captured)) {
            case HotasBindingCapture::PollState::Capturing:
                WriteCaptureResult(*output, BindingCaptureState::Capturing, {},
                    "Waiting for a DirectInput button or selector position.");
                return Result::Ok;
            case HotasBindingCapture::PollState::Captured:
                g_profileCapture.finished = true;
                g_profileCapture.terminalState = BindingCaptureState::Captured;
                g_profileCapture.binding = std::move(captured);
                g_profileCapture.detail =
                    "Activation captured; Apply saves the profile draft.";
                break;
            case HotasBindingCapture::PollState::TimedOut:
                g_profileCapture.finished = true;
                g_profileCapture.terminalState = BindingCaptureState::TimedOut;
                g_profileCapture.detail =
                    "No controller input settled before capture timed out.";
                break;
            }
        }
        WriteCaptureResult(*output, g_profileCapture.terminalState,
                           g_profileCapture.binding, g_profileCapture.detail);
        return Result::Ok;
    } catch (...) {
        WriteCaptureResult(*output, BindingCaptureState::Error, {},
                           "AbsoluteHOTAS could not poll profile capture.");
        return Result::Rejected;
    }
}

Result __cdecl CancelProfileBindingCapture(void* rawContext,
                                           const char* rawId) noexcept
{
    if (!IsProfileBinding(rawContext, rawId)) return Result::InvalidArgument;
    try {
        std::scoped_lock lock(g_profileMutex);
        HotasBindingCapture::Cancel();
        g_profileCapture = {};
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

Result __cdecl ReassignProfileBinding(void* rawContext, const char* rawId,
                                      const char* rawBinding) noexcept
{
    if (!IsProfileBinding(rawContext, rawId) || !rawBinding) {
        return Result::InvalidArgument;
    }
    try {
        const auto* end = static_cast<const char*>(
            std::memchr(rawBinding, '\0', kStringValueCapacity));
        if (!end) return Result::InvalidArgument;
        std::scoped_lock lock(g_profileMutex);
        if (!EnsureProfileSessionLocked()) return Result::Rejected;
        const auto result = g_profileSession->ReassignActivationBinding(
            std::string_view{rawBinding,
                static_cast<std::size_t>(end - rawBinding)});
        switch (result) {
        case AbsoluteControlProfiles::BindingResult::Ok: return Result::Ok;
        case AbsoluteControlProfiles::BindingResult::Conflict:
            return Result::Duplicate;
        case AbsoluteControlProfiles::BindingResult::NotFound:
            return Result::NotFound;
        case AbsoluteControlProfiles::BindingResult::Invalid:
            return Result::InvalidArgument;
        }
        return Result::Rejected;
    } catch (...) {
        return Result::Rejected;
    }
}

constexpr bool IsPinnedProfileSelector(std::string_view id) noexcept
{
    return id == "profile-records" || id.ends_with("-edit-profile");
}

constexpr bool IsPinnedProfileMode(std::string_view id) noexcept
{
    return id == "profile-activation-mode" || id.ends_with("-layer-mode");
}

constexpr bool IsPinnedProfileBinding(std::string_view id) noexcept
{
    return id == "profile-activation-trigger" ||
           id.ends_with("-layer-modifier");
}

const char* CanonicalProfileControl(std::string_view id) noexcept
{
    if (IsPinnedProfileSelector(id)) return "profile-records";
    if (IsPinnedProfileMode(id)) return "profile-activation-mode";
    if (IsPinnedProfileBinding(id)) return "profile-activation-trigger";
    return nullptr;
}

bool OtherProfileContextDraftDirty() noexcept
{
    {
        std::scoped_lock lock(g_settingsMutex);
        if (g_settings.loaded && g_settings.dirty) return true;
    }
    {
        std::scoped_lock lock(g_macroMutex);
        if (g_macroSession && g_macroSession->Dirty()) return true;
    }
    return false;
}

void InvalidateSelectedProfileSessions() noexcept
{
    {
        std::scoped_lock lock(g_settingsMutex);
        HotasBindingCapture::Cancel();
        g_capture = {};
        g_settings = {};
    }
    {
        std::scoped_lock lock(g_macroMutex);
        g_macroCapture = {};
        g_macroSession.reset();
        g_macroError.clear();
    }
    AbsoluteControlTelemetry::SetThrottleCaptureTarget(
        AbsoluteControlTelemetry::ThrottleCaptureTarget::None);
}

std::string SelectedProfileConfiguration() noexcept
{
    try {
        std::scoped_lock lock(g_profileMutex);
        if (!EnsureProfileSessionLocked()) return {};
        const auto* selected = g_profileSession->Selected();
        return selected ? selected->configurationName : std::string{};
    } catch (...) {
        return {};
    }
}

struct PinnedPageContext {
    PageDescriptorV1 original{};
};

std::string_view PinnedControlStem(std::string_view pageId) noexcept;

bool PinnedContextOwns(const PinnedPageContext& context,
                       std::string_view id) noexcept
{
    if (CanonicalProfileControl(id)) {
        const auto stem = PinnedControlStem(context.original.pageId);
        return id == std::format("{}-edit-profile", stem) ||
               id == std::format("{}-layer-mode", stem) ||
               id == std::format("{}-layer-modifier", stem);
    }
    if (FindShipRouteTarget(id)) {
        return std::string_view{context.original.pageId} ==
               HotasBindingCatalog::kShipButtonsPageId;
    }
    for (std::uint32_t index = 0;
         index < context.original.controlCount; ++index) {
        if (std::string_view{context.original.controls[index].controlId} == id)
            return true;
    }
    return false;
}

PageContext g_canonicalProfileContext{"hotas-profiles"};

Result __cdecl ReadPinnedPageValue(void* rawContext, const char* rawId,
                                   ValueV1* output) noexcept
{
    if (!rawContext || !rawId) return Result::InvalidArgument;
    const auto* context = static_cast<const PinnedPageContext*>(rawContext);
    if (!PinnedContextOwns(*context, rawId)) return Result::InvalidArgument;
    if (const auto* canonical = CanonicalProfileControl(rawId)) {
        return ReadProfileValue(nullptr, canonical, output);
    }
    return context->original.readValue
        ? context->original.readValue(context->original.context, rawId, output)
        : Result::NotFound;
}

Result __cdecl WritePinnedPageDraft(void* rawContext, const char* rawId,
                                    const ValueV1* value) noexcept
{
    if (!rawContext || !rawId) return Result::InvalidArgument;
    const auto* context = static_cast<const PinnedPageContext*>(rawContext);
    if (!PinnedContextOwns(*context, rawId)) return Result::InvalidArgument;
    if (const auto* canonical = CanonicalProfileControl(rawId)) {
        if (IsPinnedProfileSelector(rawId) && OtherProfileContextDraftDirty()) {
            g_profileError =
                "Apply or Cancel this page before changing its editing profile.";
            return Result::Rejected;
        }
        const auto previous = SelectedProfileConfiguration();
        const auto result = WriteProfileDraft(nullptr, canonical, value);
        if (result == Result::Ok &&
            SelectedProfileConfiguration() != previous) {
            InvalidateSelectedProfileSessions();
        }
        return result;
    }
    return context->original.writeDraft
        ? context->original.writeDraft(context->original.context, rawId, value)
        : Result::NotFound;
}

Result __cdecl ReadPinnedPageChoices(void* rawContext, const char* rawId,
                                     ChoiceOptionV1* options,
                                     std::uint32_t capacity,
                                     std::uint32_t* outputCount) noexcept
{
    if (!rawContext || !rawId) return Result::InvalidArgument;
    const auto* context = static_cast<const PinnedPageContext*>(rawContext);
    if (!PinnedContextOwns(*context, rawId)) return Result::InvalidArgument;
    if (IsPinnedProfileMode(rawId)) {
        return ReadProfileChoices(nullptr, "profile-activation-mode", options,
                                  capacity, outputCount);
    }
    return context->original.readChoiceOptions
        ? context->original.readChoiceOptions(context->original.context, rawId,
                                              options, capacity, outputCount)
        : Result::NotFound;
}

Result __cdecl ReadPinnedPageRecords(void* rawContext, const char* rawId,
                                     RecordItemV1* items,
                                     std::uint32_t capacity,
                                     std::uint32_t* outputCount) noexcept
{
    if (!rawContext || !rawId) return Result::InvalidArgument;
    const auto* context = static_cast<const PinnedPageContext*>(rawContext);
    if (!PinnedContextOwns(*context, rawId)) return Result::InvalidArgument;
    if (IsPinnedProfileSelector(rawId)) {
        return ReadProfileRecords(nullptr, "profile-records", items, capacity,
                                  outputCount);
    }
    return context->original.readRecordItems
        ? context->original.readRecordItems(context->original.context, rawId,
                                            items, capacity, outputCount)
        : Result::NotFound;
}

Result __cdecl InvokePinnedPageAction(void* rawContext,
                                      const char* rawId) noexcept
{
    if (!rawContext || !rawId) return Result::InvalidArgument;
    const auto* context = static_cast<const PinnedPageContext*>(rawContext);
    if (!PinnedContextOwns(*context, rawId)) return Result::InvalidArgument;
    return context->original.invokeAction
        ? context->original.invokeAction(context->original.context, rawId)
        : Result::NotFound;
}

Result __cdecl ApplyPinnedPageDraft(void* rawContext) noexcept
{
    if (!rawContext) return Result::InvalidArgument;
    const auto* context = static_cast<const PinnedPageContext*>(rawContext);
    if (context->original.apply) {
        const auto result = context->original.apply(context->original.context);
        if (result != Result::Ok) return result;
    }
    return ApplyProfileDraft(nullptr);
}

void __cdecl CancelPinnedPageDraft(void* rawContext) noexcept
{
    if (!rawContext) return;
    const auto* context = static_cast<const PinnedPageContext*>(rawContext);
    if (context->original.cancel) {
        context->original.cancel(context->original.context);
    }
    CancelProfileDraft(nullptr);
}

Result __cdecl BeginPinnedPageCapture(void* rawContext,
                                      const char* rawId) noexcept
{
    if (!rawContext || !rawId) return Result::InvalidArgument;
    const auto* context = static_cast<const PinnedPageContext*>(rawContext);
    if (!PinnedContextOwns(*context, rawId)) return Result::InvalidArgument;
    if (IsPinnedProfileBinding(rawId)) {
        return BeginProfileBindingCapture(
            &g_canonicalProfileContext, "profile-activation-trigger");
    }
    return context->original.beginBindingCapture
        ? context->original.beginBindingCapture(
              context->original.context, rawId)
        : Result::NotFound;
}

Result __cdecl PollPinnedPageCapture(void* rawContext, const char* rawId,
                                     BindingCaptureV1* output) noexcept
{
    if (!rawContext || !rawId) return Result::InvalidArgument;
    const auto* context = static_cast<const PinnedPageContext*>(rawContext);
    if (!PinnedContextOwns(*context, rawId)) return Result::InvalidArgument;
    if (IsPinnedProfileBinding(rawId)) {
        return PollProfileBindingCapture(
            &g_canonicalProfileContext, "profile-activation-trigger", output);
    }
    return context->original.pollBindingCapture
        ? context->original.pollBindingCapture(
              context->original.context, rawId, output)
        : Result::NotFound;
}

Result __cdecl CancelPinnedPageCapture(void* rawContext,
                                       const char* rawId) noexcept
{
    if (!rawContext || !rawId) return Result::InvalidArgument;
    const auto* context = static_cast<const PinnedPageContext*>(rawContext);
    if (!PinnedContextOwns(*context, rawId)) return Result::InvalidArgument;
    if (IsPinnedProfileBinding(rawId)) {
        return CancelProfileBindingCapture(
            &g_canonicalProfileContext, "profile-activation-trigger");
    }
    return context->original.cancelBindingCapture
        ? context->original.cancelBindingCapture(
              context->original.context, rawId)
        : Result::NotFound;
}

Result __cdecl ReassignPinnedPageBinding(void* rawContext, const char* rawId,
                                         const char* rawBinding) noexcept
{
    if (!rawContext || !rawId) return Result::InvalidArgument;
    const auto* context = static_cast<const PinnedPageContext*>(rawContext);
    if (!PinnedContextOwns(*context, rawId)) return Result::InvalidArgument;
    if (IsPinnedProfileBinding(rawId)) {
        return ReassignProfileBinding(&g_canonicalProfileContext,
            "profile-activation-trigger", rawBinding);
    }
    return context->original.reassignBinding
        ? context->original.reassignBinding(
              context->original.context, rawId, rawBinding)
        : Result::NotFound;
}

PageDescriptorV1 ProfilePage() noexcept
{
    PageDescriptorV1 page;
    Copy(page.moduleId, kHotasModuleId);
    Copy(page.pageId, "hotas-profiles");
    Copy(page.displayName, "Profiles & Layers");
    Copy(page.description,
        "Main controls, sparse profile overlays, activation routing, and full-profile repository operations.");
    page.controlCount = static_cast<std::uint32_t>(g_profileControls.size());
    page.controls = g_profileControls.data();
    static PageContext context{"hotas-profiles"};
    page.context = &context;
    page.readValue = &ReadProfileValue;
    page.writeDraft = &WriteProfileDraft;
    page.invokeAction = &InvokeProfileAction;
    page.apply = &ApplyProfileDraft;
    page.cancel = &CancelProfileDraft;
    page.readChoiceOptions = &ReadProfileChoices;
    page.beginBindingCapture = &BeginProfileBindingCapture;
    page.pollBindingCapture = &PollProfileBindingCapture;
    page.cancelBindingCapture = &CancelProfileBindingCapture;
    page.reassignBinding = &ReassignProfileBinding;
    page.readRecordItems = &ReadProfileRecords;
    return page;
}

PageDescriptorV1 MacroPage() noexcept
{
    PageDescriptorV1 page;
    Copy(page.moduleId, kHotasModuleId);
    Copy(page.pageId, "hotas-macros");
    Copy(page.displayName, "Macros");
    Copy(page.description,
        "HOTAS-owned triggers, ordered steps, chords, tap/hold timing, gaps, and turbo.");
    page.controlCount = static_cast<std::uint32_t>(g_macroControls.size());
    page.controls = g_macroControls.data();
    static PageContext context{"hotas-macros"};
    page.context = &context;
    page.readValue = &ReadMacroValue;
    page.writeDraft = &WriteMacroDraft;
    page.invokeAction = &InvokeMacroAction;
    page.apply = &ApplyMacroDraft;
    page.cancel = &CancelMacroDraft;
    page.readChoiceOptions = &ReadMacroChoices;
    page.beginBindingCapture = &BeginMacroBindingCapture;
    page.pollBindingCapture = &PollMacroBindingCapture;
    page.cancelBindingCapture = &CancelMacroBindingCapture;
    page.readRecordItems = &ReadMacroRecords;
    return page;
}

PageDescriptorV1 ShipButtonsPage(bool pageNavigation = true) noexcept
{
    PageDescriptorV1 page;
    Copy(page.moduleId, kHotasModuleId);
    Copy(page.pageId, "hotas-ship-buttons");
    Copy(page.displayName, "Bindings");
    Copy(page.description,
        "Core flight axes, named ship actions, plugin functions, and custom keyboard/mouse shortcuts.");
    const auto& controls = pageNavigation
        ? g_shipButtonControlsWithShortcuts
        : g_shipButtonControlsWithShortcutFallback;
    page.controlCount = static_cast<std::uint32_t>(controls.size());
    page.controls = controls.data();
    static PageContext context{"hotas-ship-buttons"};
    page.context = &context;
    page.readValue = &ReadShipValue;
    page.writeDraft = &WriteShipDraft;
    page.invokeAction = pageNavigation ? &InvokeShipAction : &InvokeMacroAction;
    page.apply = &ApplyShipDraft;
    page.cancel = &CancelShipDraft;
    page.readChoiceOptions = &ReadShipChoices;
    page.beginBindingCapture = &BeginShipBindingCapture;
    page.pollBindingCapture = &PollShipBindingCapture;
    page.cancelBindingCapture = &CancelShipBindingCapture;
    page.reassignBinding = &ReassignBinding;
    page.readRecordItems = &ReadMacroRecords;
    return page;
}

PageDescriptorV1 ThrottlePage() noexcept
{
    PageDescriptorV1 page;
    Copy(page.moduleId, kHotasModuleId);
    Copy(page.pageId, "hotas-throttle");
    Copy(page.displayName, "Throttle Setup");
    Copy(page.description,
        "HOTAS-owned positional and rate throttle behavior, landmarks, and one-shot setup actions.");
    page.controlCount = static_cast<std::uint32_t>(g_throttleControls.size());
    page.controls = g_throttleControls.data();
    page.readValue = &ReadValue;
    page.writeDraft = &WriteDraft;
    page.invokeAction = &InvokeThrottleAction;
    page.apply = &ApplyDraft;
    page.cancel = &CancelDraft;
    page.readChoiceOptions = &ReadChoiceOptions;
    return page;
}

PageDescriptorV1 DevicePage() noexcept
{
    PageDescriptorV1 page;
    Copy(page.moduleId, kHotasModuleId);
    Copy(page.pageId, "hotas-devices");
    Copy(page.displayName, "Devices & Calibration");
    Copy(page.description,
        "Stable DirectInput identity, selected-device live state, duplicate reassignment, and eight-axis sweep calibration.");
    std::size_t controlCount{};
    page.controls = AbsoluteControlDeviceProvider::Controls(controlCount);
    page.controlCount = static_cast<std::uint32_t>(controlCount);
    page.readValue = &AbsoluteControlDeviceProvider::ReadValue;
    page.writeDraft = &AbsoluteControlDeviceProvider::WriteSelection;
    page.invokeAction = &AbsoluteControlDeviceProvider::InvokeAction;
    page.readRecordItems = &AbsoluteControlDeviceProvider::ReadRecordItems;
    return page;
}

PageDescriptorV1 Page(std::string_view id, std::string_view name,
                      std::string_view description,
                      const ControlDescriptorV1* controls,
                      std::uint32_t controlCount, bool editable,
                       bool labeledChoices = false,
                       PageContext* context = nullptr,
                       bool bindings = false,
                       InvokeActionCallback invokeAction = nullptr) noexcept
{
    PageDescriptorV1 page;
    Copy(page.moduleId, kHotasModuleId);
    Copy(page.pageId, id);
    Copy(page.displayName, name);
    Copy(page.description, description);
    page.controlCount = controlCount;
    page.controls = controls;
    page.context = context;
    page.readValue = &ReadValue;
    if (editable) {
        page.writeDraft = &WriteDraft;
        page.apply = &ApplyDraft;
        page.cancel = &CancelDraft;
    }
    if (labeledChoices) page.readChoiceOptions = &ReadChoiceOptions;
    page.invokeAction = invokeAction;
    if (bindings) {
        page.beginBindingCapture = &BeginBindingCapture;
        page.pollBindingCapture = &PollBindingCapture;
        page.cancelBindingCapture = &CancelBindingCapture;
        page.reassignBinding = &ReassignBinding;
    }
    return page;
}

const auto g_deviceFallbackPage = Page(
    "hotas-devices", "Devices & Calibration",
    "DirectInput device identity, state, and hardware calibration remain available in the embedded workbench on this host.",
    g_deviceFallbackControls.data(),
    static_cast<std::uint32_t>(g_deviceFallbackControls.size()), false);

PageContext g_flightAxesContext{HotasBindingCatalog::kFlightAxesPageId};
PageContext g_shipButtonsContext{HotasBindingCatalog::kShipButtonsPageId};
PageContext g_aimingContext{HotasBindingCatalog::kAimingPageId};
PageContext g_diagnosticsContext{HotasBindingCatalog::kDiagnosticsPageId};

const std::array g_unpinnedPages{
    ShipButtonsPage(),
    Page("hotas-flight-axes", "Flight Axes",
         "First native Control editing slice for HOTAS-owned flight-axis settings.",
          g_flightAxisControlsWithNavigation.data(),
          static_cast<std::uint32_t>(g_flightAxisControlsWithNavigation.size()),
          true, false, &g_flightAxesContext, true, &InvokePageLinkAction),
    ThrottlePage(),
    Page("hotas-aiming", "Aiming & Combat",
         "HOTAS-owned independent reticle and digital aiming behavior.",
          g_aimingControlsWithBindings.data(),
          static_cast<std::uint32_t>(g_aimingControlsWithBindings.size()),
          true, false, &g_aimingContext, true),
    ProfilePage(),
    MacroPage(),
    DevicePage(),
    Page("hotas-diagnostics", "Plugin & Compatibility",
         "Pilot-context controls plus compatibility, frontend, and suite diagnostics.",
          g_diagnosticControlsWithBindings.data(),
          static_cast<std::uint32_t>(g_diagnosticControlsWithBindings.size()),
          true, true, &g_diagnosticsContext, true),
    Page("hotas-setup", "Administration",
         "Read-only readiness, ownership, and fallback-frontend summary for the standalone HOTAS runtime.",
         g_setupControls.data(), static_cast<std::uint32_t>(g_setupControls.size()), false),
};

std::string_view PinnedControlStem(std::string_view pageId) noexcept
{
    if (pageId == "hotas-ship-buttons") return "bindings";
    if (pageId == "hotas-flight-axes") return "flight-axes";
    if (pageId == "hotas-throttle") return "throttle";
    if (pageId == "hotas-aiming") return "aiming";
    if (pageId == "hotas-macros") return "macros";
    if (pageId == "hotas-devices") return "devices";
    if (pageId == "hotas-diagnostics") return "diagnostics";
    return "administration";
}

struct PinnedPageSet {
    std::array<std::vector<ControlDescriptorV1>,
               g_unpinnedPages.size()> controls;
    std::array<PinnedPageContext, g_unpinnedPages.size()> contexts;
    std::array<PageDescriptorV1, g_unpinnedPages.size()> pages;

    PinnedPageSet()
    {
        for (std::size_t index = 0; index < pages.size(); ++index) {
            const auto& source = g_unpinnedPages[index];
            auto& owned = controls[index];
            owned.reserve(source.controlCount + 3);
            if (std::string_view{source.pageId} == "hotas-profiles") {
                owned.assign(source.controls,
                             source.controls + source.controlCount);
                for (auto& control : owned) {
                    const std::string_view id{control.controlId};
                    if (id == "profile-records" ||
                        id == "profile-activation-mode" ||
                        id == "profile-activation-trigger") {
                        control.flags |= kControlPinnedContext;
                    }
                }
                pages[index] = source;
                pages[index].controls = owned.data();
                continue;
            }

            const auto stem = PinnedControlStem(source.pageId);
            auto selector = RecordCollection(
                std::format("{}-edit-profile", stem), "Editing profile",
                "Choose Main controls or a layer. Every HOTAS tab follows this same edit target.");
            selector.flags |= kControlPinnedContext;
            owned.push_back(selector);

            auto mode = Choice(std::format("{}-layer-mode", stem),
                "Shift layer behavior",
                "While held behaves like a modifier; Toggle latches the layer; Selector maps a stable encoder position.",
                0.0, 2.0, 1.0);
            mode.flags |= kControlPinnedContext;
            owned.push_back(mode);

            auto binding = ControllerBinding(
                std::format("{}-layer-modifier", stem),
                "Shift / selector binding",
                "Bind the controller button, modifier, or encoder position that activates the selected layer.");
            binding.flags |= kControlPinnedContext;
            owned.push_back(binding);
            owned.insert(owned.end(), source.controls,
                         source.controls + source.controlCount);

            contexts[index].original = source;
            auto& page = pages[index];
            page = source;
            page.controlCount = static_cast<std::uint32_t>(owned.size());
            page.controls = owned.data();
            page.context = &contexts[index];
            page.readValue = &ReadPinnedPageValue;
            page.writeDraft = &WritePinnedPageDraft;
            page.invokeAction = source.invokeAction
                ? &InvokePinnedPageAction : nullptr;
            page.apply = &ApplyPinnedPageDraft;
            page.cancel = &CancelPinnedPageDraft;
            page.readChoiceOptions = &ReadPinnedPageChoices;
            page.beginBindingCapture = &BeginPinnedPageCapture;
            page.pollBindingCapture = &PollPinnedPageCapture;
            page.cancelBindingCapture = &CancelPinnedPageCapture;
            page.reassignBinding = &ReassignPinnedPageBinding;
            page.readRecordItems = &ReadPinnedPageRecords;
        }
    }
};

const PinnedPageSet& PinnedPages()
{
    static const PinnedPageSet pages;
    return pages;
}

// Captureless ABI-1 hosts retain the scalar/status provider and the embedded
// workbench remains the binding fallback. They never receive editable binding
// rows or callback pointers they do not understand.
const std::array g_capturelessPages{
    Page("hotas-ship-buttons", "Bindings",
         "HOTAS ship settings; bindings remain in the embedded workbench on this host.",
         g_shipButtonControls.data(),
         static_cast<std::uint32_t>(g_shipButtonControls.size()), true),
    Page("hotas-flight-axes", "Flight Axes",
         "HOTAS-owned flight-axis settings; bindings remain in the embedded workbench on this host.",
         g_flightAxisControls.data(),
         static_cast<std::uint32_t>(g_flightAxisControls.size()), true),
    ThrottlePage(),
    Page("hotas-aiming", "Aiming & Combat",
         "HOTAS aiming settings; bindings remain in the embedded workbench on this host.",
         g_aimingControls.data(),
         static_cast<std::uint32_t>(g_aimingControls.size()), true),
    Page("hotas-profiles", "Profiles & Layers",
         "Main controls, sparse profile overlays, and activation routing.",
         g_profileFallbackControls.data(),
         static_cast<std::uint32_t>(g_profileFallbackControls.size()), false),
    Page("hotas-macros", "Macros",
         "HOTAS macros remain available in the embedded workbench on this host.",
         g_macroFallbackControls.data(),
         static_cast<std::uint32_t>(g_macroFallbackControls.size()), false),
    DevicePage(),
    Page("hotas-diagnostics", "Plugin & Compatibility",
         "Pilot-context controls plus compatibility, frontend, and suite diagnostics.",
         g_diagnosticControls.data(),
         static_cast<std::uint32_t>(g_diagnosticControls.size()), true, true),
    Page("hotas-setup", "Administration",
         "Read-only readiness, ownership, and fallback-frontend summary for the standalone HOTAS runtime.",
         g_setupControls.data(), static_cast<std::uint32_t>(g_setupControls.size()), false),
};

constexpr std::uint64_t kRequiredHostCapabilities = kCapabilityLabeledChoices;

bool ValidApi(const ApiV1* api) noexcept
{
    constexpr auto required = offsetof(ApiV1, isInputCaptureActive) +
                              sizeof(((ApiV1*)nullptr)->isInputCaptureActive);
    return api && api->structSize >= required && api->abiVersion == kAbiVersion &&
           api->moduleId && std::string_view(api->moduleId) == kModuleId &&
           api->registerPage && api->unregisterModule && api->requestRefresh &&
           api->registerModule && api->isOpen && api->isInputCaptureActive;
}

bool SupportsRequiredHostCapabilities(const ApiV1* api) noexcept
{
    constexpr auto capabilitiesEnd = offsetof(ApiV1, capabilities) +
                                     sizeof(((ApiV1*)nullptr)->capabilities);
    return api && api->structSize >= capabilitiesEnd &&
           (api->capabilities & kRequiredHostCapabilities) ==
               kRequiredHostCapabilities;
}

using QueryApi = const ApiV1*(__cdecl*)(std::uint32_t) noexcept;
using QueryLiveApi = const AbsoluteControlPanelExperimental::ExperimentalApiV1*
    (__cdecl*)(std::uint32_t) noexcept;

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

const AbsoluteControlPanelExperimental::ExperimentalApiV1*
ResolveLoadedLiveHost() noexcept
{
    for (const wchar_t* moduleName : {
             L"AbsoluteControlPanel.dll",
             L"AbsoluteControlPanelResearchDev.dll",
         }) {
        const auto module = GetModuleHandleW(moduleName);
        if (!module) continue;
        const auto address = GetProcAddress(
            module, "AbsoluteControlPanel_QueryLiveComponentsExperimental");
        if (!address) return nullptr;
        const auto query = reinterpret_cast<QueryLiveApi>(address);
        return query(AbsoluteControlPanelExperimental::kAbiVersion);
    }
    return nullptr;
}

using QueryCompositionApi =
    const AbsoluteControlCompositionExperimental::ApiV1*(__cdecl*)(
        std::uint32_t) noexcept;

const AbsoluteControlCompositionExperimental::ApiV1*
ResolveLoadedCompositionHost() noexcept
{
    for (const wchar_t* moduleName : {
             L"AbsoluteControlPanel.dll",
             L"AbsoluteControlPanelResearchDev.dll",
         }) {
        const auto module = GetModuleHandleW(moduleName);
        if (!module) continue;
        const auto address = GetProcAddress(
            module, "AbsoluteControlPanel_QueryCompositionApi");
        if (!address) return nullptr;
        const auto query = reinterpret_cast<QueryCompositionApi>(address);
        return query(AbsoluteControlCompositionExperimental::kAbiVersion);
    }
    return nullptr;
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
                  bool hasChoiceReader, bool hasRecordReader) noexcept
{
    if (control.structSize < sizeof(ControlDescriptorV1) ||
        !Terminated(control.controlId) || !Terminated(control.label) ||
        !Terminated(control.description) ||
        !ValidIdentifier(control.controlId) || control.label[0] == '\0') {
        return false;
    }
    constexpr std::uint32_t allowedFlags =
        kControlReadOnly | kControlRequiresRestart | kControlAdvanced |
        kControlMutatesDraft | kControlAppliesDraftBeforeInvoke |
        kControlTransientChoice | kControlLayoutInline |
        kControlRequiresConfirmation | kControlPinnedContext |
        kBindingKeyboard | kBindingMouse |
        kBindingController | kBindingModifiers | kBindingClearable;
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
        return !readOnly && hasChoiceReader &&
               (control.flags & ~(kControlAdvanced | kControlRequiresRestart |
                   kControlTransientChoice | kControlPinnedContext)) == 0;
    case ControlKind::Action:
        return !readOnly &&
               (control.flags & ~(kControlAdvanced | kControlMutatesDraft |
                   kControlAppliesDraftBeforeInvoke | kControlLayoutInline |
                   kControlRequiresConfirmation)) == 0;
    case ControlKind::InputBinding:
        return readOnly ||
               ((control.flags & kBindingController) != 0 &&
                (control.flags & ~(kControlAdvanced | kControlRequiresRestart |
                    kBindingController | kBindingClearable |
                    kControlPinnedContext)) == 0);
    case ControlKind::TextInput:
        return !readOnly &&
               (control.flags & ~(kControlAdvanced | kControlRequiresRestart)) == 0 &&
               std::isfinite(control.minimumValue) &&
               std::isfinite(control.maximumValue) &&
               std::isfinite(control.stepValue) &&
               control.minimumValue == 0.0 && control.maximumValue >= 1.0 &&
               control.maximumValue < static_cast<double>(kStringValueCapacity) &&
               std::floor(control.maximumValue) == control.maximumValue &&
               control.stepValue == 1.0;
    case ControlKind::GroupHeader:
        return control.flags == kControlNone;
    case ControlKind::RecordCollection:
        return !readOnly && hasRecordReader &&
               (control.flags & kControlTransientSelection) != 0 &&
               (control.flags & ~(kControlAdvanced |
                   kControlTransientSelection | kControlPinnedContext)) == 0;
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

void SetExternalMouseSteeringOwner(bool active) noexcept
{
    g_externalMouseSteeringOwner.store(active, std::memory_order_release);
}

void SetExternalCameraOwner(bool active) noexcept
{
    g_externalCameraOwner.store(active, std::memory_order_release);
}

AbsoluteControlPanelApi::Result RegisterDiscoveredHost() noexcept
{
    const auto result = Testing::RegisterWithResolver(&ResolveLoadedHost);
    if (result == AbsoluteControlPanelApi::Result::Ok) {
        // Experimental live components are additive. Their absence or rejection
        // never rolls back the stable settings provider.
        if (const auto* liveApi = ResolveLoadedLiveHost()) {
            const auto liveResult = AbsoluteControlTelemetry::Register(liveApi);
            if (liveResult == AbsoluteControlPanelExperimental::Result::Ok) {
                if (const auto* compositionApi =
                        ResolveLoadedCompositionHost()) {
                    (void)RegisterFlightAxesComposition(compositionApi);
                    (void)RegisterShipButtonsComposition(compositionApi);
                }
            }
        }
    }
    return result;
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

bool RequestHostPage(const char* pageId) noexcept
{
    try {
        const auto* api = g_hostApi.load(std::memory_order_acquire);
        if (!g_registered.load(std::memory_order_acquire) || !api || !pageId ||
            api->structSize < kApiV1RequestOpenPageSize ||
            (api->capabilities & kCapabilityPageOpenRequests) == 0 ||
            !api->requestOpenPage) {
            return false;
        }
        return api->requestOpenPage(kHotasModuleId.data(), pageId) == Result::Ok;
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
                !page.controls || page.controlCount == 0 || !page.readValue) {
                return Result::InvalidArgument;
            }
            if (!pageIds.insert(page.pageId).second) return Result::Duplicate;

            bool hasTransactionalEditable{};
            bool hasTransientEditable{};
            bool hasChoice{};
            bool hasBindings{};
            bool hasRecords{};
            bool hasActions{};
            for (std::uint32_t controlIndex = 0;
                 controlIndex < page.controlCount; ++controlIndex) {
                const auto& control = page.controls[controlIndex];
                if (!ValidControl(control, page.readChoiceOptions != nullptr,
                                  page.readRecordItems != nullptr)) {
                    return Result::InvalidArgument;
                }
                const bool transient =
                    (control.flags & kControlTransientChoice) != 0;
                hasTransactionalEditable |=
                    (control.flags & kControlReadOnly) == 0 && !transient &&
                    control.kind != ControlKind::Action &&
                    control.kind != ControlKind::GroupHeader;
                hasTransientEditable |=
                    (control.flags & kControlReadOnly) == 0 && transient;
                hasChoice |= control.kind == ControlKind::Choice;
                hasBindings |= control.kind == ControlKind::InputBinding &&
                               (control.flags & kControlReadOnly) == 0;
                hasRecords |= control.kind == ControlKind::RecordCollection;
                hasActions |= control.kind == ControlKind::Action;
                if (!controlIds.insert(control.controlId).second) {
                    return Result::Duplicate;
                }
            }
            const bool hasTransaction = page.writeDraft && page.apply && page.cancel;
            const bool hasCapture = page.beginBindingCapture &&
                                    page.pollBindingCapture &&
                                    page.cancelBindingCapture;
            if (hasTransactionalEditable != hasTransaction ||
                (hasTransientEditable && !page.writeDraft) ||
                (!hasTransactionalEditable && (page.apply || page.cancel)) ||
                (!hasTransactionalEditable && !hasTransientEditable &&
                    page.writeDraft) ||
                hasChoice != (page.readChoiceOptions != nullptr) ||
                hasRecords != (page.readRecordItems != nullptr) ||
                hasActions != (page.invokeAction != nullptr) ||
                hasBindings != hasCapture ||
                (hasBindings && !page.context) ||
                (!hasBindings && (page.beginBindingCapture ||
                    page.pollBindingCapture || page.cancelBindingCapture ||
                    page.reassignBinding))) {
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

        const auto& pinnedPages = PinnedPages().pages;
        const auto descriptorResult = ValidateDescriptors(
            pinnedPages.data(), pinnedPages.size());
        const auto unpinnedDescriptorResult = ValidateDescriptors(
            g_unpinnedPages.data(), g_unpinnedPages.size());
        const auto fallbackDescriptorResult = ValidateDescriptors(
            g_capturelessPages.data(), g_capturelessPages.size());
        const auto deviceFallbackDescriptorResult = ValidateDescriptors(
            &g_deviceFallbackPage, 1);
        if (descriptorResult != Result::Ok ||
            unpinnedDescriptorResult != Result::Ok ||
            fallbackDescriptorResult != Result::Ok ||
            deviceFallbackDescriptorResult != Result::Ok) {
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
            if (!SupportsRequiredHostCapabilities(api)) {
                g_terminalRejection.store(true, std::memory_order_release);
                return Result::Rejected;
            }

            const auto moduleResult = api->registerModule(&g_module);
            if (moduleResult != Result::Ok && moduleResult != Result::Duplicate) {
                return RegistrationFailure(moduleResult);
            }
            const bool ownsModuleRegistration = moduleResult == Result::Ok;
            const bool captureHost =
                (api->capabilities & kCapabilityProviderBindingCapture) != 0;
            const bool compoundHost =
                (api->capabilities & kCapabilityRecordCollections) != 0 &&
                (api->capabilities & kCapabilityActionConfirmation) != 0;
            const bool dynamicHost = captureHost && compoundHost;
            const bool pageOpenHost =
                api->structSize >= kApiV1RequestOpenPageSize &&
                (api->capabilities & kCapabilityPageOpenRequests) != 0 &&
                api->requestOpenPage;
            const bool pinnedContextHost =
                (api->capabilities & kCapabilityPinnedContextControls) != 0 &&
                (api->capabilities & kCapabilityRecordCollections) != 0 &&
                captureHost;
            const auto registerPages = [&](const auto& pages) {
                for (std::size_t index = 0; index < pages.size(); ++index) {
                    const std::string_view pageId{pages[index].pageId};
                    const bool dynamicPage =
                        pageId == "hotas-ship-buttons" ||
                        pageId == "hotas-profiles" ||
                        pageId == "hotas-macros";
                    auto page = pageId == "hotas-devices" && !compoundHost
                        ? g_deviceFallbackPage
                        : dynamicPage && !dynamicHost
                            ? g_capturelessPages[index] : pages[index];
                    if (!pageOpenHost && pinnedContextHost &&
                        page.controls == PinnedPages().pages[index].controls) {
                        page = g_unpinnedPages[index];
                    }
                    if (!pageOpenHost && page.controls ==
                        g_flightAxisControlsWithNavigation.data()) {
                        page.controls = g_flightAxisControlsWithBindings.data();
                        page.controlCount = static_cast<std::uint32_t>(
                            g_flightAxisControlsWithBindings.size());
                        page.invokeAction = nullptr;
                    } else if (!pageOpenHost && page.controls ==
                        g_shipButtonControlsWithShortcuts.data()) {
                        page = ShipButtonsPage(false);
                    }
                    const auto pageResult = api->registerPage(&page);
                    if (pageResult != Result::Ok && pageResult != Result::Duplicate) {
                        return pageResult;
                    }
                }
                return Result::Ok;
            };
            const auto pageResult = captureHost
                ? (pinnedContextHost ? registerPages(pinnedPages)
                                     : registerPages(g_unpinnedPages))
                : registerPages(g_capturelessPages);
            if (pageResult != Result::Ok) {
                if (ownsModuleRegistration) {
                    (void)api->unregisterModule(kHotasModuleId.data());
                }
                return RegistrationFailure(pageResult);
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
    const auto& pages = PinnedPages().pages;
    pageCount = pages.size();
    return pages.data();
}

AbsoluteControlPanelApi::Result RegisterFlightAxesComposition(
    const AbsoluteControlCompositionExperimental::ApiV1* api) noexcept
{
    return ::RegisterFlightAxesComposition(api);
}

bool IsFlightAxesCompositionRegistered() noexcept
{
    return g_compositionRegistered.load(std::memory_order_acquire);
}

AbsoluteControlPanelApi::Result RegisterShipButtonsComposition(
    const AbsoluteControlCompositionExperimental::ApiV1* api) noexcept
{
    return ::RegisterShipButtonsComposition(api);
}

bool IsShipButtonsCompositionRegistered() noexcept
{
    return g_shipCompositionRegistered.load(std::memory_order_acquire);
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
        g_compositionRegistered.store(false, std::memory_order_release);
        g_shipCompositionRegistered.store(false, std::memory_order_release);
        g_forceReadException.store(false, std::memory_order_release);
        SetRuntimeStatus({});
        SetExternalMouseSteeringOwner(false);
        SetExternalCameraOwner(false);
        AbsoluteControlTelemetry::Testing::Reset();
        AbsoluteControlDeviceProvider::Testing::Reset();
    }
    {
        std::scoped_lock lock(g_settingsMutex);
        HotasBindingCapture::Cancel();
        g_capture = {};
        g_settings = {};
    }
    {
        std::scoped_lock lock(g_profileMutex);
        HotasBindingCapture::Cancel();
        g_profileCapture = {};
        g_profileSession.reset();
        g_profileError.clear();
    }
    {
        std::scoped_lock lock(g_macroMutex);
        HotasBindingCapture::Cancel();
        g_macroCapture = {};
        g_macroSession.reset();
        g_macroError.clear();
        g_targetCatalogSelection = 0;
    }
}

} // namespace Testing
} // namespace AbsoluteControlSubscriber
