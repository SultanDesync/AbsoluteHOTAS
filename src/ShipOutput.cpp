#include "PCH.h"
#include "ShipOutput.h"
#include "ControlMapReader.h"
#include "DeviceManager.h"
#include "RuntimePaths.h"
#include "StringUtils.h"
#include "UniversalContextInput.h"
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <shlobj.h>
#include <array>
#include <cstdio>
#include <filesystem>

// ============================================================================
// Internal helpers
// ============================================================================

static void ShipLog(const char* msg) {
    RuntimePaths::Log("[ShipOutput]", msg);
}

// ============================================================================
// Output parsing
// ============================================================================

static bool IsExtendedKeyboardScanCode(uint16_t sc) {
    return sc == 0x48 || sc == 0x50 || sc == 0x4B || sc == 0x4D;
}

static ShipOutput ParseShipOutput(std::string_view value, ShipOutput fallback) {
    const std::string normalized = TrimLower(value);
    if (normalized.empty()) return fallback;
    if (normalized == "none") return NoOutput;

    auto parseNumber = [](std::string_view text, uint16_t& result) {
        const std::string number(text);
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(number.c_str(), &end, 0);
        if (end == number.c_str() || *end != '\0' || parsed > 0xFFFFul) return false;
        result = static_cast<uint16_t>(parsed);
        return true;
    };

    uint16_t code = 0;
    if (normalized.rfind("key:", 0) == 0 && parseNumber(std::string_view(normalized).substr(4), code) && code > 0)
        return { ShipOutputKind::Keyboard, code, IsExtendedKeyboardScanCode(code) };

    if (normalized.rfind("mouse:", 0) == 0 && parseNumber(std::string_view(normalized).substr(6), code) && code >= 1 && code <= 4)
        return { ShipOutputKind::Mouse, code, false };

    ShipLog(("Warning: invalid [ShipButtonOutputs] value '" + std::string(value) + "'; using reconciled/default output.").c_str());
    return fallback;
}

static ShipOutput UniversalContextOutput(std::string_view actionId) {
    const auto* mapping = UniversalContextInput::Find(actionId);
    return mapping
        ? ShipOutput{ ShipOutputKind::Keyboard, mapping->scanCode, mapping->extended }
        : NoOutput;
}

static ShipOutput ShipOutputFromSpec(const ShipActionOutputSpec& spec) {
    switch (spec.kind) {
        case ShipActionOutputKind::Keyboard:
            return { ShipOutputKind::Keyboard, spec.code, spec.extended };
        case ShipActionOutputKind::Mouse:
            return { ShipOutputKind::Mouse, spec.code, false };
        case ShipActionOutputKind::None:
            return NoOutput;
    }
    return NoOutput;
}

static int ParseExpansionButtonKey(std::string_view key) {
    const std::string normalized = TrimLower(key);
    std::string_view numberText;
    if (normalized.rfind("ibutton", 0) == 0)       numberText = std::string_view(normalized).substr(7);
    else if (normalized.rfind("button", 0) == 0)   numberText = std::string_view(normalized).substr(6);
    else return -1;
    if (numberText.empty()) return -1;
    const std::string number(numberText);
    char* end = nullptr;
    const long parsed = std::strtol(number.c_str(), &end, 10);
    if (end == number.c_str() || *end != '\0' || parsed < 1 || parsed > 144) return -1;
    return static_cast<int>(parsed);
}

// ============================================================================
// Internal state
// ============================================================================

struct HeldShipOutput {
    ShipOutput output;
    std::vector<uint32_t> owners;
};

static std::vector<ShipButtonBinding> s_shipButtonBindings;
static std::vector<HeldShipOutput>    s_heldShipOutputs;
static std::vector<ControlMap::Record> s_controlMapRecords;
static std::array<ShipControlMethodResolution, kShipActionCatalog.size()>
    s_methodResolutions{};
static bool s_routingInputsReady = false;

static bool SameOutput(const ShipOutput& lhs, const ShipOutput& rhs) {
    return lhs.kind == rhs.kind && lhs.code == rhs.code && lhs.extended == rhs.extended;
}

static uint32_t ShipOwnerIdForIndex(size_t index) {
    return OwnerShipButtonBase + static_cast<uint32_t>(index);
}

// ============================================================================
// Legacy 4.x ControlMap output reconciliation
// ============================================================================
// Kept so existing configuration/profile snapshots round-trip their former
// outputs. Recognized 5.0 ship actions never emit these values.

static std::filesystem::path ControlMapCustomPath() {
    PWSTR docs = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)) && docs)
        result = std::filesystem::path(docs) / L"My Games" / L"Starfield" / L"ControlMap_Custom.txt";
    if (docs) CoTaskMemFree(docs);
    return result;
}

static ShipOutput ShipOutputFromControlMap(const ControlMap::Output& o) {
    switch (o.kind) {
        case ControlMap::OutputKind::Keyboard: return { ShipOutputKind::Keyboard, o.code, o.extended };
        case ControlMap::OutputKind::Mouse:    return { ShipOutputKind::Mouse,    o.code, false };
        default:                               return NoOutput;
    }
}

static ControlMap::Output ControlMapFromShipOutput(const ShipOutput& s) {
    switch (s.kind) {
        case ShipOutputKind::Keyboard: return { ControlMap::OutputKind::Keyboard, s.code, s.extended };
        case ShipOutputKind::Mouse:    return { ControlMap::OutputKind::Mouse,    s.code, false };
        default:                       return {};
    }
}

static ControlMap::ControlMapFile LoadControlMapOverrides() {
    const std::filesystem::path path = ControlMapCustomPath();
    if (path.empty()) {
        ShipLog("ControlMap sync: could not resolve Documents path; using vanilla defaults.");
        return {};
    }
    ControlMap::ControlMapFile file = ControlMap::ReadFile(path);
    if (!file.valid) {
        ShipLog("ControlMap sync: no readable ControlMap_Custom.txt; using vanilla defaults.");
        return file;
    }
    return file;
}

// Resolve the effective default for one action: the player's in-game binding if
// they remapped it, otherwise the vanilla default passed in.
static ShipOutput ControlMapDefaultForAction(const std::vector<ControlMap::Record>& records,
                                             const ShipActionDefinition& def) {
    const ControlMap::Output resolved = ControlMap::ResolveBinding(
        records, def.controlMapContext, def.controlMapAction,
        ControlMapFromShipOutput(ShipOutputFromSpec(def.vanillaOutput)));
    return ShipOutputFromControlMap(resolved);
}

static bool HasControlMapPrimary(const std::vector<ControlMap::Record>& records,
                                 const ShipActionDefinition& def) {
    return std::any_of(records.begin(), records.end(), [&](const auto& record) {
        return record.context == def.controlMapContext &&
            record.action == def.controlMapAction && record.IsPrimary();
    });
}

// ============================================================================
// SendInput helpers
// ============================================================================

static void SendKeyboardScanCode(uint16_t scanCode, bool extended, bool keyUp) {
    if (scanCode == 0) return;
    INPUT input = {};
    input.type      = INPUT_KEYBOARD;
    input.ki.wScan  = scanCode;
    input.ki.dwFlags = KEYEVENTF_SCANCODE | (extended ? KEYEVENTF_EXTENDEDKEY : 0) | (keyUp ? KEYEVENTF_KEYUP : 0);
    SendInput(1, &input, sizeof(INPUT));
}

static void SendMouseButton(uint16_t mouseButton, bool keyUp) {
    INPUT input = {};
    input.type = INPUT_MOUSE;
    switch (mouseButton) {
        case 1: input.mi.dwFlags = keyUp ? MOUSEEVENTF_LEFTUP   : MOUSEEVENTF_LEFTDOWN;   break;
        case 2: input.mi.dwFlags = keyUp ? MOUSEEVENTF_RIGHTUP  : MOUSEEVENTF_RIGHTDOWN;  break;
        case 3: input.mi.dwFlags = keyUp ? MOUSEEVENTF_MIDDLEUP : MOUSEEVENTF_MIDDLEDOWN; break;
        case 4:
            input.mi.dwFlags   = keyUp ? MOUSEEVENTF_XUP : MOUSEEVENTF_XDOWN;
            input.mi.mouseData = XBUTTON1;
            break;
        default: return;
    }
    SendInput(1, &input, sizeof(INPUT));
}

static void EmitShipOutput(const ShipOutput& output, bool keyUp) {
    switch (output.kind) {
        case ShipOutputKind::Keyboard: SendKeyboardScanCode(output.code, output.extended, keyUp); break;
        case ShipOutputKind::Mouse:    SendMouseButton(output.code, keyUp);                       break;
        case ShipOutputKind::None:     break;
    }
}

static void PulseOutput(const ShipOutput& output) {
    if (output.kind == ShipOutputKind::None) return;
    EmitShipOutput(output, false);
    EmitShipOutput(output, true);
}

// ============================================================================
// Public API implementation
// ============================================================================

namespace ShipOutputSystem {

void RefreshRoutingInputs() {
    const ControlMap::ControlMapFile controlMap = LoadControlMapOverrides();
    s_controlMapRecords = controlMap.DeviceRecords();

    CSimpleIniA preferences;
    preferences.SetUnicode(false);
    const bool preferencesLoaded =
        preferences.LoadFile(RuntimePaths::CustomIniPath().string().c_str()) == SI_OK &&
        preferences.GetLongValue("Meta", "iConfigVersion", -1) >= 1;

    for (std::size_t index = 0; index < kShipActionCatalog.size(); ++index) {
        const auto& definition = kShipActionCatalog[index];
        std::string normalized;
        if (preferencesLoaded) {
            if (const char* value = preferences.GetValue(
                    "ShipControlMethods", definition.actionId.data(), nullptr)) {
                normalized = TrimLower(value);
            }
        }
        s_methodResolutions[index] = ResolveShipControlMethod(definition, normalized);
        if (s_methodResolutions[index].overridePresent &&
            !s_methodResolutions[index].overrideAccepted) {
            ShipLog(("Ignored unsupported [ShipControlMethods] " +
                std::string(definition.actionId) + "=" + normalized +
                "; using the catalog recommendation.").c_str());
        }
    }
    s_routingInputsReady = true;
}

void SetOutputHeld(const ShipOutput& output, uint32_t ownerId, bool held) {
    if (output.kind == ShipOutputKind::None) return;

    auto it = std::find_if(s_heldShipOutputs.begin(), s_heldShipOutputs.end(),
        [&](const HeldShipOutput& h) { return SameOutput(h.output, output); });

    if (held) {
        if (it == s_heldShipOutputs.end()) {
            s_heldShipOutputs.push_back({ output, { ownerId } });
            EmitShipOutput(output, false);
            return;
        }
        if (std::find(it->owners.begin(), it->owners.end(), ownerId) == it->owners.end())
            it->owners.push_back(ownerId);
        return;
    }

    if (it == s_heldShipOutputs.end()) return;
    std::erase(it->owners, ownerId);
    if (it->owners.empty()) {
        EmitShipOutput(it->output, true);
        s_heldShipOutputs.erase(it);
    }
}

void ReleaseOwnerOutputs(uint32_t ownerId) {
    NativeShipControl::ReleaseOwner(ownerId);
    for (auto it = s_heldShipOutputs.begin(); it != s_heldShipOutputs.end();) {
        std::erase(it->owners, ownerId);
        if (it->owners.empty()) {
            EmitShipOutput(it->output, true);
            it = s_heldShipOutputs.erase(it);
        } else {
            ++it;
        }
    }
}

void ReleaseAllShipButtonOutputs() {
    NativeShipControl::ReleaseAll();
    for (const auto& h : s_heldShipOutputs) EmitShipOutput(h.output, true);
    s_heldShipOutputs.clear();
    for (auto& b : s_shipButtonBindings) b.previousPressed = false;
}

void ReleaseShipButtonBindingOutputs() {
    for (size_t i = 0; i < s_shipButtonBindings.size(); ++i)
        ReleaseOwnerOutputs(ShipOwnerIdForIndex(i));
    for (auto& b : s_shipButtonBindings) b.previousPressed = false;
}

// ---- Profile snapshot / restore (see profile-switching.md) ----

std::vector<ShipButtonBinding> SnapshotBindings() {
    // Safe to copy: actionId/sourceIniKey/outputIniKey point at static string
    // literals in the BindingDef table, so the copies keep valid pointers.
    return s_shipButtonBindings;
}

void RestoreBindings(const std::vector<ShipButtonBinding>& bindings) {
    s_shipButtonBindings = bindings;
    // previousPressed rides along in the copy; SeedDownButtonsConsumed fixes up
    // whatever is physically held after the caller has released held outputs.
}

void SeedDownButtonsConsumed() {
    // After a swap, a button still physically held must NOT re-fire as its meaning
    // in the newly-active profile. Mark every currently-down button as already
    // seen, so its output only triggers on the next genuine press edge.
    for (auto& b : s_shipButtonBindings)
        b.previousPressed = DeviceManager::IsButtonPressed(b.buttonRef);
}

bool IsBoostRequested() {
    return NativeShipControl::IsActionHeld(NativeShipControl::Action::FireBoosters);
}

ShipButtonBinding* GetShipButtonBindings() {
    return s_shipButtonBindings.data();
}

int GetShipButtonCount() {
    return static_cast<int>(s_shipButtonBindings.size());
}

void SetUniversalContextHeld(std::string_view actionId, uint32_t ownerId, bool held) {
    const auto* mapping = UniversalContextInput::Find(actionId);
    if (!mapping) return;

    const auto route = UniversalContextInput::ResolveRoute(
        actionId, NativeShipControl::TargetingModeActive());

    SetOutputHeld(
        { ShipOutputKind::Keyboard, mapping->scanCode, mapping->extended },
        ownerId, held && route.vanillaKey);
    if (mapping->targetingSelector) {
        const auto nativeAction = NativeShipControl::ActionFromId(actionId);
        if (nativeAction != NativeShipControl::Action::Invalid) {
            NativeShipControl::SetActionHeld(nativeAction, ownerId,
                held && route.targetingSelector);
        }
    }
}

const ShipButtonBinding* FindShipButtonBinding(std::string_view actionId) {
    const auto it = std::find_if(s_shipButtonBindings.begin(), s_shipButtonBindings.end(),
        [&](const ShipButtonBinding& binding) { return binding.actionId == actionId; });
    return it == s_shipButtonBindings.end() ? nullptr : &*it;
}

static ShipControlTarget MakeControlTarget(ShipControlMethod method,
                                           std::string_view actionId,
                                           const ShipOutput& output) {
    switch (method) {
        case ShipControlMethod::Direct: {
            const auto action = NativeShipControl::ActionFromId(actionId);
            if (action == NativeShipControl::Action::Invalid) return {};
            return { ShipControlTargetKind::Native, action, NoOutput, actionId };
        }
        case ShipControlMethod::Context:
            if (!UniversalContextInput::Find(actionId)) return {};
            return { ShipControlTargetKind::Context,
                NativeShipControl::Action::Invalid, NoOutput, actionId };
        case ShipControlMethod::KeyboardCompatibility:
            if (output.kind == ShipOutputKind::None) return {};
            return { ShipControlTargetKind::RawOutput,
                NativeShipControl::Action::Invalid, output, actionId };
    }
    return {};
}

ShipControlTarget ResolveControlTarget(std::string_view token) {
    const std::string lowered = TrimLower(token);
    // Raw key/mouse outputs (and "none") carry a ':' or are the literal "none".
    if (lowered == "none") return {};
    if (lowered.find(':') != std::string::npos) {
        const ShipOutput output = ParseShipOutput(token, NoOutput);
        return output.kind == ShipOutputKind::None ? ShipControlTarget{}
            : ShipControlTarget{ ShipControlTargetKind::RawOutput,
                NativeShipControl::Action::Invalid, output, {} };
    }

    if (const auto* binding = FindShipButtonBinding(token))
        return MakeControlTarget(binding->method, binding->actionId, binding->output);

    // This fallback is used only before the binding snapshots have been loaded.
    // It still consumes the catalog recommendation; an unknown action fails closed.
    const auto* definition = FindShipAction(token);
    if (!definition) return {};
    return MakeControlTarget(definition->recommendedMethod, definition->actionId,
                             ShipOutputFromSpec(definition->vanillaOutput));
}

void SetControlTargetHeld(const ShipControlTarget& target, uint32_t ownerId, bool held) {
    switch (target.kind) {
        case ShipControlTargetKind::Native:
            NativeShipControl::SetActionHeld(target.nativeAction, ownerId, held);
            break;
        case ShipControlTargetKind::Context:
            SetUniversalContextHeld(target.actionId, ownerId, held);
            break;
        case ShipControlTargetKind::RawOutput:
            SetOutputHeld(target.output, ownerId, held);
            break;
        case ShipControlTargetKind::None:
            break;
    }
}

ShipActionRouteInfo GetShipActionRouteInfo(std::string_view actionId) {
    const auto* definition = FindShipAction(actionId);
    if (!definition) return {};

    const auto* binding = FindShipButtonBinding(actionId);
    const ShipControlMethod method = binding
        ? binding->method : definition->recommendedMethod;
    const ShipOutput keyboardOutput = binding
        ? binding->output : ShipOutputFromSpec(definition->vanillaOutput);

    ShipActionAvailability availability = ShipActionAvailability::AvailableNow;
    if (method == ShipControlMethod::Direct) {
        availability = NativeShipControl::GetActionAvailability(
            NativeShipControl::ActionFromId(actionId));
    } else if (method == ShipControlMethod::KeyboardCompatibility &&
               keyboardOutput.kind == ShipOutputKind::None) {
        availability = ShipActionAvailability::UnavailableInContext;
    }

    return {
        definition->actionId,
        definition->displayLabel,
        definition->group,
        method,
        binding && binding->methodOverridden,
        keyboardOutput,
        binding ? binding->resolutionSource : KeyboardResolutionSource::VanillaFallback,
        availability,
    };
}

std::vector<ShipActionRouteInfo> GetShipActionRouteInfos() {
    std::vector<ShipActionRouteInfo> result;
    result.reserve(kShipActionCatalog.size());
    for (const auto& definition : kShipActionCatalog)
        result.push_back(GetShipActionRouteInfo(definition.actionId));
    return result;
}

void LoadShipButtonBindings(CSimpleIniA& ini) {
    if (!s_routingInputsReady) RefreshRoutingInputs();
    s_shipButtonBindings.clear();
    s_shipButtonBindings.reserve(kShipActionCatalog.size());

    // Resolve and retain the keyboard-compatible route even when Direct is the
    // selected method. This keeps method changes a dispatch decision, not a
    // second binding model. Compatibility precedence per action:
    //   vanilla default -> control-map-derived (in-game rebind) -> explicit [ShipButtonOutputs].
    const bool syncFromControlMap = ini.GetBoolValue("General", "bSyncShipOutputsFromControlMap", true);

    for (std::size_t index = 0; index < kShipActionCatalog.size(); ++index) {
        const auto& def = kShipActionCatalog[index];
        ShipOutput cmDefault = ShipOutputFromSpec(def.vanillaOutput);
        const bool controlMapPrimary = syncFromControlMap &&
            HasControlMapPrimary(s_controlMapRecords, def);
        if (syncFromControlMap)
            cmDefault = ControlMapDefaultForAction(s_controlMapRecords, def);

        const char* outputValue = ini.GetValue(
            "ShipButtonOutputs", def.legacyOutputIniKey.data(), nullptr);
        ShipOutput finalOut = outputValue ? ParseShipOutput(outputValue, cmDefault) : cmDefault;
        KeyboardResolutionSource resolutionSource = ResolveKeyboardResolutionSource(
            controlMapPrimary, outputValue != nullptr);
        if (def.recommendedMethod == ShipControlMethod::Context) {
            const ShipOutput universal = UniversalContextOutput(def.actionId);
            // Context actions deliberately represent fixed vanilla navigation,
            // not the rebound ship-only action or a legacy output override.
            finalOut = universal;
            resolutionSource = KeyboardResolutionSource::FixedContext;
        }

        BindingRef bRef = ParseBindingRef(
            ini.GetValue("ShipButtons", def.sourceIniKey.data(), ""), -1);
        const auto methodResolution = s_methodResolutions[index];

        ShipButtonBinding binding{
            def.actionId.data(),
            def.sourceIniKey.data(),
            def.legacyOutputIniKey.data(),
            bRef,
            finalOut,
            methodResolution.method,
            resolutionSource,
            methodResolution.overrideAccepted &&
                methodResolution.method != def.recommendedMethod,
            ShipBindingMode::Hold,  // All actions default to Hold; future: per-action overrides
            false
        };

        if (binding.buttonRef.value > 144) {
            ShipLog(("Warning: [ShipButtons] " + std::string(def.sourceIniKey) +
                     " is outside 1-144 range; disabling.").c_str());
            binding.buttonRef.value = -1;
        }

        s_shipButtonBindings.push_back(binding);
    }

    // [ButtonExpansion] — extra button → output mappings
    CSimpleIniA::TNamesDepend keys;
    if (!ini.GetAllKeys("ButtonExpansion", keys)) return;

    for (const auto& key : keys) {
        if (!key.pItem) continue;
        const char* outputValue = ini.GetValue("ButtonExpansion", key.pItem, nullptr);
        if (!outputValue) continue;
        const std::string normalizedOutput = TrimLower(outputValue);
        if (normalizedOutput.empty() || normalizedOutput == "none") continue;

        const ShipOutput output = ParseShipOutput(outputValue, NoOutput);
        if (output.kind == ShipOutputKind::None) continue;

        std::string deviceName;
        std::string_view svKey(key.pItem);
        auto atPos = svKey.rfind('@');
        if (atPos != std::string_view::npos) {
            deviceName = std::string(svKey.substr(0, atPos));
            svKey = svKey.substr(atPos + 1);
        }

        int buttonId = ParseExpansionButtonKey(svKey);
        if (buttonId < 1 || buttonId > 144) {
            ShipLog(("Warning: [ButtonExpansion] key '" + std::string(key.pItem) +
                     "' is not iButton1..iButton144; ignoring.").c_str());
            continue;
        }

        BindingRef finalRef;
        finalRef.deviceName  = deviceName;
        finalRef.value       = buttonId;
        finalRef.deviceIndex = -1;

        // #N@ syntax written by BindingWizard for duplicate devices
        if (!deviceName.empty() && deviceName.front() == '#') {
            char* endPtr = nullptr;
            long idx = std::strtol(deviceName.c_str() + 1, &endPtr, 10);
            if (endPtr != deviceName.c_str() + 1 && idx >= 0) {
                finalRef.deviceIndex = static_cast<int>(idx);
                finalRef.deviceName.clear();
            }
        }

        s_shipButtonBindings.push_back({
            "ButtonExpansion",
            key.pItem,
            key.pItem,
            finalRef,
            output,
            ShipControlMethod::KeyboardCompatibility,
            KeyboardResolutionSource::LegacyManualOverride,
            false,
            ShipBindingMode::Hold,
            false
        });
    }
}

void UpdateShipButtonBindings() {
    for (int i = 0; i < static_cast<int>(s_shipButtonBindings.size()); ++i) {
        auto& binding = s_shipButtonBindings[i];
        bool pressed  = DeviceManager::IsButtonPressed(binding.buttonRef);
        const uint32_t ownerId = ShipOwnerIdForIndex(i);

        const auto nativeAction = NativeShipControl::ActionFromId(binding.actionId);
        if (binding.method == ShipControlMethod::Context) {
            const auto* universalMapping = UniversalContextInput::Find(binding.actionId);
            // Targeting Mode consumes dedicated SelectLeft/SelectRight semantic
            // events rather than the ShipHUD arrow bindings. Switch lanes under
            // the exact targeting camera gate so power management never receives
            // the same press. Outside targeting, raw output remains edge-seeded:
            // a button held while targeting closes cannot resume power control
            // until it has been released and genuinely pressed again.
            const bool targetingSelector = universalMapping &&
                universalMapping->targetingSelector &&
                NativeShipControl::TargetingModeActive();
            if (targetingSelector) {
                SetUniversalContextHeld(binding.actionId, ownerId, pressed);
            } else {
                if (universalMapping && universalMapping->targetingSelector &&
                    nativeAction != NativeShipControl::Action::Invalid) {
                    NativeShipControl::SetActionHeld(nativeAction, ownerId, false);
                }
                if (pressed != binding.previousPressed)
                    SetOutputHeld(binding.output, ownerId, pressed);
            }
        } else if (binding.method == ShipControlMethod::Direct &&
                   nativeAction != NativeShipControl::Action::Invalid) {
            if (pressed != binding.previousPressed)
                NativeShipControl::SetActionHeld(nativeAction, ownerId, pressed);
        } else if (binding.method == ShipControlMethod::KeyboardCompatibility &&
                   binding.mode == ShipBindingMode::Hold) {
            if (pressed != binding.previousPressed)
                SetOutputHeld(binding.output, ownerId, pressed);
        } else if (binding.method == ShipControlMethod::KeyboardCompatibility &&
                   pressed && !binding.previousPressed) {
            PulseOutput(binding.output);
        }

        if (!pressed && binding.previousPressed)
            ReleaseOwnerOutputs(ownerId);

        binding.previousPressed = pressed;
    }
}

} // namespace ShipOutputSystem
