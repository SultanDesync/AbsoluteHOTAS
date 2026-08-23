#include "PCH.h"

#include "WizardConfigInternal.h"

#include "ConfigOwnershipPolicy.h"
#include "ShipActionCatalog.h"

#include "RuntimePaths.h"

#include <cstdio>
#include <cstring>

namespace WizardConfig::Detail {


// --- INI write helpers ---
static void SetIniFloat(CSimpleIniA& ini, const char* section, const char* key, float val, const char* fmt = "%.2f") {
    char buf[32];
    std::snprintf(buf, sizeof(buf), fmt, val);
    ini.SetValue(section, key, buf);
}

static void SetIniLong(CSimpleIniA& ini, const char* section, const char* key, long val) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%ld", val);
    ini.SetValue(section, key, buf);
}

static void SetIniInt(CSimpleIniA& ini, const char* section, const char* key, int val) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", val);
    ini.SetValue(section, key, buf);
}


    static std::vector<std::string> SplitOn(std::string_view text, char delim) {
        std::vector<std::string> out;
        size_t start = 0;
        while (start <= text.size()) {
            const size_t end = text.find(delim, start);
            std::string_view piece = text.substr(start, (end == std::string_view::npos ? text.size() : end) - start);
            while (!piece.empty() && std::isspace((unsigned char)piece.front())) piece.remove_prefix(1);
            while (!piece.empty() && std::isspace((unsigned char)piece.back()))  piece.remove_suffix(1);
            if (!piece.empty()) out.emplace_back(piece);
            if (end == std::string_view::npos) break;
            start = end + 1;
        }
        return out;
    }

    static std::vector<std::string> SplitWhitespace(std::string_view text) {
        std::vector<std::string> out;
        size_t i = 0;
        while (i < text.size()) {
            while (i < text.size() && std::isspace((unsigned char)text[i])) ++i;
            const size_t start = i;
            while (i < text.size() && !std::isspace((unsigned char)text[i])) ++i;
            if (i > start) out.emplace_back(text.substr(start, i - start));
        }
        return out;
    }

    // "<targets> <tap|hold> <amount> [gapMs]" -> row. False if unrepresentable.
    static bool ParseStepRow(std::string_view line, MacroStepRow& out) {
        if (const size_t c = line.find(';'); c != std::string_view::npos)
            line = line.substr(0, c);

        const auto tok = SplitWhitespace(line);
        if (tok.size() < 3) return false;

        out.targets = SplitOn(tok[0], '+');
        if (out.targets.empty()) return false;

        std::string action = tok[1];
        std::transform(action.begin(), action.end(), action.begin(),
                       [](unsigned char ch) { return (char)std::tolower(ch); });
        out.hold   = (action == "hold");
        out.amount = std::atoi(tok[2].c_str());
        out.gapMs  = (tok.size() >= 4) ? std::atoi(tok[3].c_str()) : 50;

        if (out.amount < 0) out.amount = 0;
        if (out.gapMs  < 0) out.gapMs  = 0;
        return true;
    }

    // Trim a friendly macro name (the sName shown in the tab). Only strips the few
    // characters that would break the INI header/comment scanner if the raw name ever
    // leaked into a key; the display name itself keeps spaces and case.
    std::string SanitizeMacroName(const std::string& in) {
        std::string out;
        for (char c : in) {
            if (c == '[' || c == ']' || c == '=' || c == ';' || c == '\r' || c == '\n') continue;
            out.push_back(c);
        }
        const size_t a = out.find_first_not_of(' ');
        const size_t b = out.find_last_not_of(' ');
        if (a == std::string::npos) return "";
        return out.substr(a, b - a + 1);
    }

    // Slugify a friendly name into a stable, filename-safe INI section key:
    // "Power Grav to Shield" -> "power_grav_to_shield". Same name -> same key across
    // saves (no churn); MakeUniqueMacroKey appends _2/_3 only on a genuine collision,
    // so two macros that share a display name still get distinct sections.
    static std::string SlugifyMacroName(const std::string& in) {
        std::string out;
        for (char c : in) {
            if (std::isalnum((unsigned char)c)) out.push_back((char)std::tolower((unsigned char)c));
            else if (c == ' ' || c == '-' || c == '_') { if (!out.empty() && out.back() != '_') out.push_back('_'); }
        }
        while (!out.empty() && out.back() == '_') out.pop_back();
        return out.empty() ? "macro" : out;
    }

    static std::string MakeUniqueMacroKey(const std::string& sName, std::vector<std::string>& used) {
        const std::string base = SlugifyMacroName(sName);
        std::string key = base;
        for (int n = 2; std::find(used.begin(), used.end(), key) != used.end(); ++n)
            key = base + "_" + std::to_string(n);
        used.push_back(key);
        return key;
    }

    void LoadMacroRows(WizardState& s, const std::filesystem::path* profilePath) {
        s.macros.clear();

        CSimpleIniA ini;
        ini.SetUnicode(false);
        ini.LoadFile(RuntimePaths::CustomIniPath().string().c_str());
        if (profilePath) ini.LoadFile(profilePath->string().c_str());

        CSimpleIniA::TNamesDepend sections;
        ini.GetAllSections(sections);

        for (const auto& sec : sections) {
            if (!sec.pItem) continue;
            std::string_view name(sec.pItem);
            if (name.rfind("Macro:", 0) != 0) continue;

            MacroRow row;
            // Friendly display name from sName; a pasted chunk without one falls back
            // to the section key so it still shows up resolved and ready to bind.
            const char* disp = ini.GetValue(sec.pItem, "sName", nullptr);
            row.name = (disp && *disp) ? disp : std::string(name.substr(6));

            const char* btn = ini.GetValue(sec.pItem, "iButton", "");
            row.buttonBinding = (btn && *btn && std::strcmp(btn, "-1") != 0) ? btn : "(unbound)";
            row.turbo = ini.GetBoolValue(sec.pItem, "bTurbo", false);

            for (int i = 0;; ++i) {
                const std::string key = "Step" + std::to_string(i);
                const char* line = ini.GetValue(sec.pItem, key.c_str(), nullptr);
                if (!line) break;  // steps are contiguous Step0..StepN
                MacroStepRow step;
                if (ParseStepRow(line, step)) row.steps.push_back(std::move(step));
            }
            s.macros.push_back(std::move(row));
        }

        // Keep source-section order. The selected-record editor uses that order as
        // the user's macro order, and sorting by display name would make a no-op
        // Apply rewrite hand-authored sections in a different sequence.
    }


static std::string IniBinding(const CSimpleIniA& ini, const char* section,
                              const char* key, const std::string& fallback,
                              bool axis) {
    const char* value = ini.GetValue(section, key, nullptr);
    if (!value) return fallback;
    if (!*value || std::strcmp(value, "-1") == 0) return "(unbound)";
    return FormatBindingRef(ParseBindingRef(value, axis ? 0 : -1), axis);
}

void ApplyProfileScalars(const CSimpleIniA& ini, WizardState& s) {
#define APPLY_BOOL(sec, key, field) if (ini.GetValue(sec, key, nullptr)) s.field = ini.GetBoolValue(sec, key, s.field)
#define APPLY_LONG(sec, key, field) if (ini.GetValue(sec, key, nullptr)) s.field = (decltype(s.field))ini.GetLongValue(sec, key, s.field)
#define APPLY_FLOAT(sec, key, field) if (ini.GetValue(sec, key, nullptr)) s.field = (float)ini.GetDoubleValue(sec, key, s.field)
    APPLY_BOOL("Injection", "bEnableInjection", axisInjectionEnabled);
    for (int i = 0; i < kNumAxisSlots; ++i) {
        s.axisBindings[i] = IniBinding(ini, "Hardware", kAxisSlots[i].iniKey,
                                       s.axisBindings[i], true);
        if (kAxisSlots[i].invertIniKey && ini.GetValue("Hardware", kAxisSlots[i].invertIniKey, nullptr))
            s.axisInvert[i] = ini.GetBoolValue("Hardware", kAxisSlots[i].invertIniKey, s.axisInvert[i]);
        if (kAxisSlots[i].sensitivityKey && ini.GetValue("Hardware", kAxisSlots[i].sensitivityKey, nullptr))
            s.axisSensitivity[i] = (float)ini.GetDoubleValue("Hardware", kAxisSlots[i].sensitivityKey, s.axisSensitivity[i]);
        if (kAxisSlots[i].saturationKey && ini.GetValue("Hardware", kAxisSlots[i].saturationKey, nullptr))
            s.axisSaturation[i] = (float)ini.GetDoubleValue("Hardware", kAxisSlots[i].saturationKey, s.axisSaturation[i]);
        if (kAxisSlots[i].deadzoneKey && ini.GetValue("Hardware", kAxisSlots[i].deadzoneKey, nullptr))
            s.axisDeadzone[i] = (float)ini.GetDoubleValue("Hardware", kAxisSlots[i].deadzoneKey, s.axisDeadzone[i]);
    }

    APPLY_FLOAT("Normalization", "fIdlePlateau", idlePlateau);
    APPLY_LONG("Normalization", "iDetentCenter", detentCenter);
    APPLY_LONG("Normalization", "iDetentDeadzone", detentDeadzone);
    APPLY_BOOL("Normalization", "bUnipolarReverse", unipolarReverse);
    APPLY_LONG("Normalization", "iReverseZoneCenter", reverseZoneCenter);
    APPLY_LONG("Normalization", "iReverseZoneDeadzone", reverseZoneDeadzone);
    APPLY_BOOL("Normalization", "bBoostZone", boostZone);
    APPLY_LONG("Normalization", "iBoostZoneCenter", boostZoneCenter);
    APPLY_LONG("Normalization", "iBoostZoneDeadzone", boostZoneDeadzone);

    for (int i = 0; i < kNumButtonSlots; ++i)
        s.buttonBindings[i] = IniBinding(ini, "Buttons", kButtonSlots[i].iniKey, s.buttonBindings[i], false);
    if (const char* mode = ini.GetValue("Gate", "PilotGateMode", nullptr)) {
        s.pilotGateMode = _stricmp(mode, "Full") == 0 ? 2
            : (_stricmp(mode, "InjectionOnly") == 0 ? 1 : 0);
    }
    if (const char* signal = ini.GetValue("Gate", "PilotSignal", nullptr))
        s.automaticPilotSignal = _stricmp(signal, "Auto") == 0;
    APPLY_LONG("Gate", "iPilotLatchMilliseconds", pilotLatchMilliseconds);
    for (int i = 0; i < kNumControlExtensionSlots; ++i)
        s.controlExtensionBindings[i] = IniBinding(ini, "ControlExtensions",
            kControlExtensionSlots[i].iniKey, s.controlExtensionBindings[i], false);
    for (auto& action : s.shipActionSlots)
        action.binding = IniBinding(ini, "ShipButtons", action.iniKey.c_str(), action.binding, false);
    for (std::size_t index = 0; index < kMenuNavigationCatalog.size(); ++index) {
        s.menuNavigationBindings[index] = IniBinding(
            ini, "MenuControls", kMenuNavigationCatalog[index].iniKey.data(),
            s.menuNavigationBindings[index], false);
    }
    APPLY_BOOL("MenuControls", "bUsePitchAxisForNavigation", usePitchAxisForMenu);
    APPLY_BOOL("MenuControls", "bUseYawAxisForNavigation", useYawAxisForMenu);
    APPLY_BOOL("MenuControls", "bUsePrimaryWeaponForSelect", usePrimaryWeaponForMenuSelect);
    APPLY_BOOL("MenuControls", "bInvertVerticalNavigation", invertMenuVertical);
    APPLY_BOOL("MenuControls", "bInvertHorizontalNavigation", invertMenuHorizontal);
    APPLY_FLOAT("MenuControls", "fAxisEngageThreshold", menuAxisEngageThreshold);
    APPLY_FLOAT("MenuControls", "fAxisReleaseThreshold", menuAxisReleaseThreshold);
    for (int i = 0; i < kNumDigitalAxisSlots; ++i)
        s.digitalAxisBindings[i] = IniBinding(ini, "DigitalAxes", kDigitalAxisSlots[i].iniKey, s.digitalAxisBindings[i], false);
    APPLY_FLOAT("DigitalAxes", "fDigitalRollValue", digitalRollValue);
    APPLY_FLOAT("DigitalAxes", "fDigitalStrafeValue", digitalStrafeValue);

    APPLY_BOOL("Aim", "bSourceObjectAim", sourceObjectAim);
    APPLY_FLOAT("Aim", "fAimSensitivity", aimSensitivity);
    APPLY_FLOAT("Aim", "fAimSmoothing", aimSmoothing);
    for (int i = 0; i < kNumAimAxisSlots; ++i) {
        s.aimAxisBindings[i] = IniBinding(ini, "Aim", kAimAxisSlots[i].iniKey, s.aimAxisBindings[i], true);
        if (ini.GetValue("Aim", kAimAxisSlots[i].invertIniKey, nullptr))
            s.aimAxisInvert[i] = ini.GetBoolValue("Aim", kAimAxisSlots[i].invertIniKey, s.aimAxisInvert[i]);
        if (ini.GetValue("Aim", kAimAxisSlots[i].sensitivityKey, nullptr))
            s.aimAxisSensitivity[i] = (float)ini.GetDoubleValue("Aim", kAimAxisSlots[i].sensitivityKey, s.aimAxisSensitivity[i]);
    }
    for (int i = 0; i < kNumDigitalAimSlots; ++i)
        s.digitalAimBindings[i] = IniBinding(ini, "Aim", kDigitalAimSlots[i].iniKey, s.digitalAimBindings[i], false);
    APPLY_FLOAT("Aim", "fDigitalAimValue", digitalAimValue);
    s.toggleAimModeBinding = IniBinding(ini, "Aim", "iToggleAimModeButton", s.toggleAimModeBinding, false);
    APPLY_BOOL("Aim", "bHOSAMMode", hosamMode);
    APPLY_BOOL("Aim", "bAlignmentAssist", alignmentAssist);
    APPLY_FLOAT("Aim", "fAlignmentRadius", alignmentRadius);
    APPLY_LONG("Aim", "iAlignmentIdleMs", alignmentIdleMs);
    APPLY_FLOAT("Aim", "fAlignmentDecayRate", alignmentDecayRate);

    APPLY_BOOL("HeadTracking", "bEnabled", headLookEnabled);
    APPLY_BOOL("HeadTracking", "bOpenTrackEnabled", headLookOpenTrackEnabled);
    for (int i = 0; i < kNumHeadLookAxisSlots; ++i) {
        const auto& slot = kHeadLookAxisSlots[i];
        s.headLookAxisBindings[i] = IniBinding(
            ini, "HeadTracking", slot.iniKey, s.headLookAxisBindings[i], true);
        if (ini.GetValue("HeadTracking", slot.enabledIniKey, nullptr))
            s.headLookAxisEnabled[i] = ini.GetBoolValue(
                "HeadTracking", slot.enabledIniKey, s.headLookAxisEnabled[i]);
        if (ini.GetValue("HeadTracking", slot.invertIniKey, nullptr))
            s.headLookInvert[i] = ini.GetBoolValue(
                "HeadTracking", slot.invertIniKey, s.headLookInvert[i]);
        if (ini.GetValue("HeadTracking", slot.sensitivityKey, nullptr))
            s.headLookSensitivity[i] = static_cast<float>(ini.GetDoubleValue(
                "HeadTracking", slot.sensitivityKey, s.headLookSensitivity[i]));
        if (ini.GetValue("HeadTracking", slot.maximumKey, nullptr))
            s.headLookMaxDegrees[i] = static_cast<float>(ini.GetDoubleValue(
                "HeadTracking", slot.maximumKey, s.headLookMaxDegrees[i]));
    }
    APPLY_FLOAT("HeadTracking", "fDeadzoneDegrees", headLookDeadzoneDegrees);
    APPLY_FLOAT("HeadTracking", "fJoystickDeadzone", headLookJoystickDeadzone);
    APPLY_FLOAT("HeadTracking", "fSmoothing", headLookSmoothing);
    s.headLookRecenterBinding = IniBinding(
        ini, "HeadTracking", "iRecenterButton", s.headLookRecenterBinding, false);
    s.headLookToggleBinding = IniBinding(
        ini, "HeadTracking", "iToggleButton", s.headLookToggleBinding, false);

    APPLY_BOOL("DualStick", "bAccumulatorThrottle", accumulatorThrottle);
    APPLY_FLOAT("DualStick", "fAccumulatorRate", accumulatorRate);
    APPLY_FLOAT("DualStick", "fAccumulatorDecay", accumulatorDecay);
    APPLY_FLOAT("DualStick", "fReverseGateVelocity", reverseGateVelocity);
    APPLY_BOOL("DualStick", "bAccumulatorTurnAssist", accumulatorTurnAssist);
    APPLY_LONG("DualStick", "iTurnAssistMode", turnAssistMode);
    s.turnAssistBinding = IniBinding(ini, "DualStick", "iTurnAssistButton", s.turnAssistBinding, false);
    APPLY_BOOL("DualStick", "bHoldForBoost", holdForBoost);
#undef APPLY_BOOL
#undef APPLY_LONG
#undef APPLY_FLOAT
}

void LoadEffectiveCollections(const std::filesystem::path& profilePath,
                                     WizardState& s) {
    CSimpleIniA ini;
    ini.SetUnicode(false);
    ini.LoadFile(RuntimePaths::IniPath().string().c_str());
    ini.LoadFile(RuntimePaths::CustomIniPath().string().c_str());
    ini.LoadFile(profilePath.string().c_str());

    s.customBindings.clear();
    CSimpleIniA::TNamesDepend customKeys;
    ini.GetAllKeys("ButtonExpansion", customKeys);
    for (const auto& entry : customKeys) {
        std::string key = entry.pItem ? entry.pItem : "";
        const size_t marker = key.rfind("iButton");
        if (marker == std::string::npos) continue;
        key.erase(marker, 7);
        if (marker > 0 && key[marker - 1] == '@') {
            // Device@iButton42 -> Device@42 (the '@' remains in place).
        }
        const char* output = ini.GetValue("ButtonExpansion", entry.pItem, "none");
        if (marker == 0) key = key.empty() ? "(unbound)" : key;
        s.customBindings.push_back({ key, output ? output : "none" });
    }

    s.calibData.clear();
    CSimpleIniA::TNamesDepend calibKeys;
    ini.GetAllKeys("Calibration", calibKeys);
    for (const auto& entry : calibKeys) {
        int devIdx = -1, usage = -1;
        if (!entry.pItem || sscanf_s(entry.pItem, "iCalib_%d_0x%x", &devIdx, &usage) != 2)
            continue;
        long cmin = 0, cmax = 65535;
        const char* value = ini.GetValue("Calibration", entry.pItem, "");
        if (sscanf_s(value, "%ld,%ld", &cmin, &cmax) == 2 && cmin < cmax)
            s.calibData[(devIdx << 8) | usage] = { cmin, cmax };
    }
}


// Serialize every HOTAS-owned key from a WizardState into an ini object. Settings
// owned by separately installed modules may still be loaded for compatibility but
// are deliberately absent here. The full save and sparse overlay diff therefore
// author the same bounded key set.
void SerializeUserOwnedState(const WizardState& s, CSimpleIniA& ini) {
    ini.SetBoolValue("Injection", "bEnableInjection", s.axisInjectionEnabled);

    // Axes
    for (int i = 0; i < kNumAxisSlots; i++) {
        const char* val = (s.axisBindings[i] != "(unbound)") ? s.axisBindings[i].c_str() : "";
        ini.SetValue("Hardware", kAxisSlots[i].iniKey, val);
        if (kAxisSlots[i].invertIniKey)
            ini.SetBoolValue("Hardware", kAxisSlots[i].invertIniKey, s.axisInvert[i]);
        if (kAxisSlots[i].sensitivityKey && s.axisSensitivity[i] > 0.0f)
            SetIniFloat(ini, "Hardware", kAxisSlots[i].sensitivityKey, s.axisSensitivity[i]);
        if (kAxisSlots[i].saturationKey)
            SetIniFloat(ini, "Hardware", kAxisSlots[i].saturationKey, s.axisSaturation[i]);
        if (kAxisSlots[i].deadzoneKey)
            SetIniFloat(ini, "Hardware", kAxisSlots[i].deadzoneKey, s.axisDeadzone[i]);
    }

    // Throttle calibration
    SetIniFloat(ini, "Normalization", "fIdlePlateau", s.idlePlateau);
    SetIniLong(ini, "Normalization", "iDetentCenter", s.detentCenter);
    SetIniLong(ini, "Normalization", "iDetentDeadzone", s.detentDeadzone);
    ini.SetBoolValue("Normalization", "bUnipolarReverse", s.unipolarReverse);
    SetIniLong(ini, "Normalization", "iReverseZoneCenter", s.reverseZoneCenter);
    SetIniLong(ini, "Normalization", "iReverseZoneDeadzone", s.reverseZoneDeadzone);
    ini.SetBoolValue("Normalization", "bBoostZone", s.boostZone);
    SetIniLong(ini, "Normalization", "iBoostZoneCenter", s.boostZoneCenter);
    SetIniLong(ini, "Normalization", "iBoostZoneDeadzone", s.boostZoneDeadzone);

    // Control buttons
    for (int i = 0; i < kNumButtonSlots; i++) {
        const char* val = (s.buttonBindings[i] != "(unbound)") ? s.buttonBindings[i].c_str() : "-1";
        ini.SetValue("Buttons", kButtonSlots[i].iniKey, val);
    }
    const char* gateMode = s.pilotGateMode == 2 ? "Full"
        : (s.pilotGateMode == 1 ? "InjectionOnly" : "Off");
    ini.SetValue("Gate", "PilotGateMode", gateMode);
    ini.SetValue("Gate", "PilotSignal", s.automaticPilotSignal ? "Auto" : "Manual");
    SetIniInt(ini, "Gate", "iPilotLatchMilliseconds",
        std::clamp(s.pilotLatchMilliseconds, 500, 30000));
    for (int i = 0; i < kNumControlExtensionSlots; ++i) {
        const char* val = s.controlExtensionBindings[i] != "(unbound)"
            ? s.controlExtensionBindings[i].c_str() : "-1";
        ini.SetValue("ControlExtensions", kControlExtensionSlots[i].iniKey, val);
    }

    // Ship actions
    for (auto& sa : s.shipActionSlots) {
        const char* val = (sa.binding != "(unbound)") ? sa.binding.c_str() : "-1";
        ini.SetValue("ShipButtons", sa.iniKey.c_str(), val);
    }

    for (std::size_t index = 0; index < kMenuNavigationCatalog.size(); ++index) {
        const auto& binding = s.menuNavigationBindings[index];
        ini.SetValue("MenuControls", kMenuNavigationCatalog[index].iniKey.data(),
            binding != "(unbound)" && !binding.empty()
                ? binding.c_str() : "-1");
    }

    ini.SetBoolValue("MenuControls", "bUsePitchAxisForNavigation", s.usePitchAxisForMenu);
    ini.SetBoolValue("MenuControls", "bUseYawAxisForNavigation", s.useYawAxisForMenu);
    ini.SetBoolValue("MenuControls", "bUsePrimaryWeaponForSelect", s.usePrimaryWeaponForMenuSelect);
    ini.SetBoolValue("MenuControls", "bInvertVerticalNavigation", s.invertMenuVertical);
    ini.SetBoolValue("MenuControls", "bInvertHorizontalNavigation", s.invertMenuHorizontal);
    SetIniFloat(ini, "MenuControls", "fAxisEngageThreshold",
        std::clamp(s.menuAxisEngageThreshold, 0.35f, 0.95f));
    SetIniFloat(ini, "MenuControls", "fAxisReleaseThreshold",
        std::clamp(s.menuAxisReleaseThreshold, 0.05f,
            std::clamp(s.menuAxisEngageThreshold, 0.35f, 0.95f) - 0.05f));

    // Digital axes
    for (int i = 0; i < kNumDigitalAxisSlots; i++) {
        const char* val = (s.digitalAxisBindings[i] != "(unbound)") ? s.digitalAxisBindings[i].c_str() : "-1";
        ini.SetValue("DigitalAxes", kDigitalAxisSlots[i].iniKey, val);
    }
    SetIniFloat(ini, "DigitalAxes", "fDigitalRollValue", s.digitalRollValue);
    SetIniFloat(ini, "DigitalAxes", "fDigitalStrafeValue", s.digitalStrafeValue);

    // Aim
    ini.SetBoolValue("Aim", "bSourceObjectAim", s.sourceObjectAim);
    SetIniFloat(ini, "Aim", "fAimSensitivity", s.aimSensitivity);
    ini.SetBoolValue("Aim", "bMirrorFlightToAim", true);
    for (int i = 0; i < kNumAimAxisSlots; i++) {
        const char* val = (s.aimAxisBindings[i] != "(unbound)") ? s.aimAxisBindings[i].c_str() : "";
        ini.SetValue("Aim", kAimAxisSlots[i].iniKey, val);
        ini.SetBoolValue("Aim", kAimAxisSlots[i].invertIniKey, s.aimAxisInvert[i]);
        SetIniFloat(ini, "Aim", kAimAxisSlots[i].sensitivityKey, s.aimAxisSensitivity[i]);
    }
    SetIniFloat(ini, "Aim", "fAimSmoothing", s.aimSmoothing);

    // Digital aim
    for (int i = 0; i < kNumDigitalAimSlots; i++) {
        const char* val = (s.digitalAimBindings[i] != "(unbound)") ? s.digitalAimBindings[i].c_str() : "-1";
        ini.SetValue("Aim", kDigitalAimSlots[i].iniKey, val);
    }
    SetIniFloat(ini, "Aim", "fDigitalAimValue", s.digitalAimValue);
    {
        const char* val = (s.toggleAimModeBinding != "(unbound)") ? s.toggleAimModeBinding.c_str() : "-1";
        ini.SetValue("Aim", "iToggleAimModeButton", val);
    }

    // DualStick accumulator
    ini.SetBoolValue("DualStick", "bAccumulatorThrottle", s.accumulatorThrottle);
    SetIniFloat(ini, "DualStick", "fAccumulatorRate", s.accumulatorRate, "%.1f");
    SetIniFloat(ini, "DualStick", "fAccumulatorDecay", s.accumulatorDecay, "%.1f");
    SetIniFloat(ini, "DualStick", "fReverseGateVelocity", s.reverseGateVelocity, "%.1f");
    ini.SetBoolValue("DualStick", "bAccumulatorTurnAssist", s.accumulatorTurnAssist);
    SetIniInt(ini, "DualStick", "iTurnAssistMode", s.turnAssistMode);
    {
        const char* val = (s.turnAssistBinding != "(unbound)") ? s.turnAssistBinding.c_str() : "-1";
        ini.SetValue("DualStick", "iTurnAssistButton", val);
    }
    // Relocated from [Injection] in 4.0 (see LoadConfig alias read).
    ini.SetBoolValue("DualStick", "bHoldForBoost", s.holdForBoost);

    // Calibration
    ini.Delete("Calibration", nullptr);
    for (const auto& [key, range] : s.calibData) {
        int devIdx = (key >> 8) & 0xFF;
        int usage = key & 0xFF;
        char keyBuf[64], valBuf[64];
        std::snprintf(keyBuf, sizeof(keyBuf), "iCalib_%d_0x%02X", devIdx, usage);
        std::snprintf(valBuf, sizeof(valBuf), "%ld,%ld", range.first, range.second);
        ini.SetValue("Calibration", keyBuf, valBuf);
    }

    // Custom button expansion
    ini.Delete("ButtonExpansion", nullptr);
    for (const auto& row : s.customBindings) {
        if (row.buttonBinding == "(unbound)" || row.output == "none" || row.output.empty()) continue;
        std::string iniKey;
        auto atPos = row.buttonBinding.rfind('@');
        if (atPos != std::string::npos) {
            iniKey = row.buttonBinding.substr(0, atPos) + "@iButton" + row.buttonBinding.substr(atPos + 1);
        } else {
            iniKey = "iButton" + row.buttonBinding;
        }
        ini.SetValue("ButtonExpansion", iniKey.c_str(), row.output.c_str());
    }
}

static void DeleteMacroSections(CSimpleIniA& ini) {
    CSimpleIniA::TNamesDepend sections;
    ini.GetAllSections(sections);
    std::vector<std::string> remove;
    for (const auto& sec : sections)
        if (sec.pItem && _strnicmp(sec.pItem, "Macro:", 6) == 0) remove.emplace_back(sec.pItem);
    for (const auto& section : remove) ini.Delete(section.c_str(), nullptr);
}

void SerializeMacros(const WizardState& state, CSimpleIniA& ini) {
    DeleteMacroSections(ini);
    std::vector<std::string> usedKeys;
    for (const auto& m : state.macros) {
        const std::string name = SanitizeMacroName(m.name);
        if (name.empty()) continue;
        const std::string section = "Macro:" + MakeUniqueMacroKey(name, usedKeys);
        const bool bound = m.buttonBinding != "(unbound)";
        ini.SetValue(section.c_str(), "sName", name.c_str());
        ini.SetValue(section.c_str(), "iButton", bound ? m.buttonBinding.c_str() : "-1");
        ini.SetBoolValue(section.c_str(), "bTurbo", m.turbo);
        int written = 0;
        for (const auto& step : m.steps) {
            if (step.targets.empty()) continue;
            std::string value;
            for (size_t i = 0; i < step.targets.size(); ++i) {
                if (i) value += '+';
                value += step.targets[i];
            }
            value += step.hold ? " hold " : " tap ";
            value += std::to_string(std::max(0, step.amount));
            value += ' ';
            value += std::to_string(std::max(0, step.gapMs));
            ini.SetValue(section.c_str(), ("Step" + std::to_string(written++)).c_str(), value.c_str());
        }
    }
}

void ReplaceHotasOwnedState(CSimpleIniA& destination,
                            const CSimpleIniA& incoming) {
    CSimpleIniA managed;
    managed.SetUnicode(false);
    SerializeUserOwnedState(WizardState{}, managed);
    for (const auto& action : kShipActionCatalog) {
        managed.SetValue("ShipButtons", action.sourceIniKey.data(), "-1");
    }
    ConfigOwnershipPolicy::ReplaceManagedPayload(
        destination, incoming, managed);
}

std::string StateSignature(const WizardState& state) {
    CSimpleIniA ini;
    ini.SetUnicode(false);
    SerializeUserOwnedState(state, ini);
    SerializeMacros(state, ini);
    std::string signature;
    ini.Save(signature);
    // Include incomplete editor rows that intentionally do not yet serialize to
    // runnable config, keeping them visibly dirty until completed or removed.
    signature += "\n[EditorDrafts]\n";
    for (const auto& row : state.customBindings) {
        signature += "custom=" + std::to_string(row.buttonBinding.size()) + ":" + row.buttonBinding
            + ":" + std::to_string(row.output.size()) + ":" + row.output + "\n";
    }
    for (const auto& macro : state.macros) {
        signature += "macro=" + std::to_string(macro.name.size()) + ":" + macro.name
            + ":" + macro.buttonBinding + ":" + std::to_string(macro.turbo) + "\n";
        for (const auto& step : macro.steps) {
            signature += "step=" + std::to_string(step.hold) + ":" + std::to_string(step.amount)
                + ":" + std::to_string(step.gapMs);
            for (const auto& target : step.targets)
                signature += ":" + std::to_string(target.size()) + ":" + target;
            signature += "\n";
        }
    }
    return signature;
}

}  // namespace WizardConfig::Detail
