#include "PCH.h"

#include "WizardConfig.h"
#include "WizardCapture.h"
#include "ThrottleController.h"
#include "ShipOutput.h"
#include "RuntimePaths.h"
#include "ConfigMigration.h"

#include <SimpleIni.h>
#include <cstdio>
#include <cmath>
#include <cstring>

static void WizLog(const std::string& msg) {
    RuntimePaths::Log("[BindingWizard]", msg);
}

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

namespace WizardConfig {

static WizardState s_state;

WizardState& GetState() { return s_state; }

// --- Macro row parsing (mirrors MacroEngine's grammar, at the token level) ---
namespace {
    std::vector<std::string> SplitOn(std::string_view text, char delim) {
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

    std::vector<std::string> SplitWhitespace(std::string_view text) {
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
    bool ParseStepRow(std::string_view line, MacroStepRow& out) {
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

    // Macro names become INI section names, so anything that would break the
    // section header (or the ';' comment scanner) is stripped.
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

    void LoadMacroRows(WizardState& s) {
        s.macros.clear();

        CSimpleIniA ini;
        ini.SetUnicode(false);
        if (ini.LoadFile(RuntimePaths::MacrosIniPath().string().c_str()) != SI_OK)
            return;  // no macros file yet — that's the normal fresh-install case

        CSimpleIniA::TNamesDepend sections;
        ini.GetAllSections(sections);

        for (const auto& sec : sections) {
            if (!sec.pItem) continue;
            std::string_view name(sec.pItem);
            if (name.rfind("Macro:", 0) != 0) continue;

            MacroRow row;
            row.name = std::string(name.substr(6));

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

        std::sort(s.macros.begin(), s.macros.end(),
                  [](const MacroRow& a, const MacroRow& b) { return a.name < b.name; });
    }
}

void LoadCurrentBindings() {
    if (s_state.loaded) return;

    auto& cfg = ThrottleController::GetConfig();
    auto& s = s_state;

    // Axis bindings
    const BindingRef* axisRefs[] = {
        &cfg.throttleAxis, &cfg.pitchAxis, &cfg.yawAxis, &cfg.rollAxis,
        &cfg.strafeLatAxis, &cfg.strafeVertAxis, &cfg.reverseAxis
    };
    const bool invertVals[] = {
        cfg.bInvertThrottle, cfg.bInvertPitch, cfg.bInvertYaw, cfg.bInvertRoll,
        cfg.bInvertStrafeLat, cfg.bInvertStrafeVert, cfg.bInvertReverse
    };
    const float sensVals[] = {
        cfg.fThrottleSensitivity, cfg.fPitchSensitivity, cfg.fYawSensitivity,
        cfg.fRollSensitivity, cfg.fStrafeSensitivity, 0.0f, cfg.fReverseSensitivity
    };
    const float satVals[] = {
        cfg.fThrottleSaturation, cfg.fPitchSaturation, cfg.fYawSaturation,
        cfg.fRollSaturation, cfg.fStrafeSaturation, cfg.fStrafeVertSaturation, cfg.fReverseSaturation
    };
    const float dzVals[] = {
        cfg.fThrottleDeadzone, cfg.fPitchDeadzone, cfg.fYawDeadzone,
        cfg.fRollDeadzone, cfg.fStrafeDeadzone, cfg.fStrafeVertDeadzone, 0.0f
    };

    for (int i = 0; i < kNumAxisSlots; i++) {
        s.axisBindings[i]    = FormatBindingRef(*axisRefs[i], true);
        s.axisInvert[i]      = invertVals[i];
        s.axisSensitivity[i] = sensVals[i];
        s.axisSaturation[i]  = satVals[i];
        s.axisDeadzone[i]    = dzVals[i];
    }

    // DualStick accumulator
    s.accumulatorThrottle = cfg.bAccumulatorThrottle;
    s.accumulatorRate     = cfg.fAccumulatorRate;
    s.accumulatorDecay    = cfg.fAccumulatorDecay;
    s.reverseGateVelocity = cfg.fReverseGateVelocity;
    s.accumulatorTurnAssist = cfg.bAccumulatorTurnAssist;
    s.turnAssistMode      = cfg.iTurnAssistMode;
    s.turnAssistBinding   = FormatBindingRef(cfg.turnAssistButton, false);
    s.symmetricalThrottleDz = (std::abs(cfg.idlePlateau - (1.0f - cfg.fThrottleSaturation)) < 0.01f);
    s.holdForBoost = cfg.bHoldForBoost;

    // HOSAM
    s.hosamMode         = cfg.bHOSAMMode;
    s.alignmentAssist   = cfg.bAlignmentAssist;
    s.alignmentRadius   = cfg.fAlignmentRadius;
    s.alignmentIdleMs   = cfg.iAlignmentIdleMs;
    s.alignmentDecayRate = cfg.fAlignmentDecayRate;

    // Throttle calibration
    s.idlePlateau       = cfg.idlePlateau;
    s.detentCenter      = cfg.detentCenter;
    s.detentDeadzone    = cfg.detentDeadzone;
    s.unipolarReverse   = cfg.bUnipolarReverse;
    s.reverseZoneCenter = cfg.reverseZoneCenter;
    s.reverseZoneDeadzone = cfg.reverseZoneDeadzone;
    s.boostZone         = cfg.bBoostZone;
    s.boostZoneCenter   = cfg.boostZoneCenter;
    s.boostZoneDeadzone = cfg.boostZoneDeadzone;

    // Control buttons
    const BindingRef* btnRefs[] = { &cfg.activateButton, &cfg.stopButton, &cfg.toggleWizardButton };
    for (int i = 0; i < kNumButtonSlots; i++) {
        s.buttonBindings[i] = FormatBindingRef(*btnRefs[i], false);
    }

    // Ship actions
    auto shipActions = ThrottleController::GetShipActionBindings();
    s.shipActionSlots.clear();
    for (auto& sa : shipActions) {
        s.shipActionSlots.push_back({ sa.label, sa.iniKey, FormatBindingRef(sa.binding, false) });
    }

    // Digital axes
    const BindingRef* digRefs[] = {
        &cfg.digitalReverseButton, &cfg.digitalRollLeftButton, &cfg.digitalRollRightButton,
        &cfg.digitalStrafeLeftButton, &cfg.digitalStrafeRightButton,
        &cfg.digitalStrafeUpButton, &cfg.digitalStrafeDownButton
    };
    for (int i = 0; i < kNumDigitalAxisSlots; i++) {
        s.digitalAxisBindings[i] = FormatBindingRef(*digRefs[i], false);
    }
    s.digitalRollValue   = cfg.digitalRollValue;
    s.digitalStrafeValue = cfg.digitalStrafeValue;

    // Aim
    s.aimAxisBindings[0]    = FormatBindingRef(cfg.aimYawAxis, true);
    s.aimAxisBindings[1]    = FormatBindingRef(cfg.aimPitchAxis, true);
    s.aimAxisInvert[0]      = cfg.bInvertAimYaw;
    s.aimAxisInvert[1]      = cfg.bInvertAimPitch;
    s.aimAxisSensitivity[0] = cfg.fAimYawSensitivity;
    s.aimAxisSensitivity[1] = cfg.fAimPitchSensitivity;
    s.aimSensitivity        = cfg.fAimSensitivity;
    s.aimSmoothing          = cfg.fAimSmoothing;
    s.sourceObjectAim       = cfg.bSourceObjectAim;

    // Digital aim
    const BindingRef* dAimRefs[] = {
        &cfg.digitalAimLeftButton, &cfg.digitalAimRightButton,
        &cfg.digitalAimUpButton, &cfg.digitalAimDownButton, &cfg.digitalAimCenterButton
    };
    for (int i = 0; i < kNumDigitalAimSlots; i++) {
        s.digitalAimBindings[i] = FormatBindingRef(*dAimRefs[i], false);
    }
    s.digitalAimValue      = cfg.fDigitalAimValue;
    s.toggleAimModeBinding = FormatBindingRef(cfg.toggleAimModeButton, false);

    // Calibration data
    if (s.calibData.empty()) {
        s.calibData = ThrottleController::GetCalibrationData();
    }

    // Custom bindings from [ButtonExpansion]
    if (s.customBindings.empty()) {
        int count = ShipOutputSystem::GetShipButtonCount();
        for (int i = 0; i < count; i++) {
            const auto& b = ShipOutputSystem::GetShipButtonBindings()[i];
            if (strcmp(b.actionId, "ButtonExpansion") != 0) continue;
            if (b.buttonRef.value < 1) continue;

            std::string binding;
            if (!b.buttonRef.deviceName.empty()) {
                binding = b.buttonRef.deviceName + "@" + std::to_string(b.buttonRef.value);
            } else if (b.buttonRef.deviceIndex >= 0) {
                binding = "#" + std::to_string(b.buttonRef.deviceIndex) + "@" + std::to_string(b.buttonRef.value);
            } else {
                binding = std::to_string(b.buttonRef.value);
            }

            std::string output;
            switch (b.output.kind) {
                case ShipOutputKind::Keyboard:
                    { char buf[32]; std::snprintf(buf, sizeof(buf), "key:0x%02X", b.output.code); output = buf; }
                    break;
                case ShipOutputKind::Mouse:
                    { char buf[32]; std::snprintf(buf, sizeof(buf), "mouse:%d", b.output.code); output = buf; }
                    break;
                default: output = "none"; break;
            }
            s.customBindings.push_back({ binding, output });
        }
    }

    // Macros come straight from their INI, not from MacroEngine — see WizardDefs.h.
    LoadMacroRows(s);

    s.loaded = true;
}

void SaveBindingsToINI() {
    // Write ONLY the user file. The main ini is mod-owned and overwrite-safe; a
    // single user key written there reintroduces the update-clobber bug the split
    // exists to prevent. See docs/reference/config-layout.md.
    auto iniPath = RuntimePaths::UserIniPath();
    WizLog("Saving bindings to: " + iniPath.string());

    CSimpleIniA ini;
    ini.SetUnicode(false);
    ini.LoadFile(iniPath.string().c_str());

    auto& s = s_state;

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

    // Ship actions
    for (auto& sa : s.shipActionSlots) {
        const char* val = (sa.binding != "(unbound)") ? sa.binding.c_str() : "-1";
        ini.SetValue("ShipButtons", sa.iniKey.c_str(), val);
    }

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
    // Relocated from [Injection] in 3.1 (see LoadConfig alias read).
    ini.SetBoolValue("DualStick", "bHoldForBoost", s.holdForBoost);

    // HOSAM
    ini.SetBoolValue("Aim", "bHOSAMMode", s.hosamMode);
    ini.SetBoolValue("Aim", "bAlignmentAssist", s.alignmentAssist);
    SetIniFloat(ini, "Aim", "fAlignmentRadius", s.alignmentRadius, "%.1f");
    SetIniInt(ini, "Aim", "iAlignmentIdleMs", s.alignmentIdleMs);
    SetIniFloat(ini, "Aim", "fAlignmentDecayRate", s.alignmentDecayRate, "%.1f");

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

    // Stamp the schema version so a fresh-install user file (which never went through
    // MigrateIfNeeded) still carries one for future semantic migrations.
    ini.SetLongValue("Meta", "iConfigVersion", ConfigMigration::kConfigVersion);

    ini.SaveFile(iniPath.string().c_str());

    SaveMacrosToINI();  // separate file, same Save action

    WizLog("INI saved. Reloading config...");

    ThrottleController::ReloadConfig();
    WizLog("Config reload requested.");
}

void SaveMacrosToINI() {
    const auto path = RuntimePaths::MacrosIniPath();

    // Full rewrite from the editor rows. The file holds nothing but [Macro:*], so
    // there is no foreign state to preserve — deliberately do NOT LoadFile first,
    // or deleted macros would linger.
    CSimpleIniA ini;
    ini.SetUnicode(false);

    // A half-built macro (no trigger button yet, no steps yet) is still the user's
    // work, and MacroEngine already ignores such macros at load with a warning. So
    // persist them rather than dropping them — otherwise the wizard, which reloads
    // from this file after every Save, would make them vanish as the user typed.
    int saved = 0;
    std::vector<std::string> used;
    for (const auto& m : s_state.macros) {
        const std::string name = SanitizeMacroName(m.name);
        if (name.empty()) continue;  // no name = no INI section; wizard flags it

        // Two macros with one name would share an INI section and silently merge
        // their steps. Skip the later one; the wizard flags it in red.
        if (std::find(used.begin(), used.end(), name) != used.end()) {
            WizLog("Skipped duplicate macro name: " + name);
            continue;
        }
        used.push_back(name);

        const std::string section = "Macro:" + name;
        const bool bound = (m.buttonBinding != "(unbound)");
        ini.SetValue(section.c_str(), "iButton", bound ? m.buttonBinding.c_str() : "-1");
        ini.SetBoolValue(section.c_str(), "bTurbo", m.turbo);

        // Step keys must be contiguous Step0..StepN — the engine stops at the first
        // gap — so index by what we actually write, not by the row's position.
        int written = 0;
        for (const auto& step : m.steps) {
            if (step.targets.empty()) continue;

            std::string value;
            for (size_t t = 0; t < step.targets.size(); ++t) {
                if (t) value += '+';
                value += step.targets[t];
            }
            value += step.hold ? " hold " : " tap ";
            value += std::to_string(std::max(0, step.amount));
            value += ' ';
            value += std::to_string(std::max(0, step.gapMs));

            ini.SetValue(section.c_str(), ("Step" + std::to_string(written)).c_str(), value.c_str());
            ++written;
        }
        if (bound && written > 0) ++saved;  // count only the ones that will actually run
    }

    if (ini.SaveFile(path.string().c_str()) != SI_OK)
        WizLog("Could not write macros file: " + path.string());
    else
        WizLog("Saved macros (" + std::to_string(saved) + " runnable) -> " + path.string());
}

// --- Profiles ---

namespace {
    // Strip anything that isn't safe in a filename; collapse to a trimmed token.
    // Keeps letters, digits, space, dash, underscore, dot — enough for readable
    // names, nothing that can escape ProfilesDir().
    std::string SanitizeProfileName(const std::string& in) {
        std::string out;
        for (char c : in) {
            if (std::isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_' || c == '.')
                out.push_back(c);
        }
        size_t a = out.find_first_not_of(' ');
        size_t b = out.find_last_not_of(' ');
        if (a == std::string::npos) return "";
        return out.substr(a, b - a + 1);
    }

    // Timestamp. compact=true -> "20260709_143005" (filenames); false -> readable.
    std::string TimeStamp(bool compact) {
        std::time_t t = std::time(nullptr);
        std::tm tm{};
        localtime_s(&tm, &t);
        char buf[32];
        std::strftime(buf, sizeof(buf), compact ? "%Y%m%d_%H%M%S" : "%Y-%m-%d %H:%M:%S", &tm);
        return buf;
    }

    std::string ModVersionString() {
        return std::to_string(PLUGIN_VERSION_MAJOR) + "." +
               std::to_string(PLUGIN_VERSION_MINOR) + "." +
               std::to_string(PLUGIN_VERSION_PATCH);
    }
}

std::vector<std::string> ListProfiles() {
    std::vector<std::string> out;
    std::error_code ec;
    const auto dir = RuntimePaths::ProfilesDir();
    if (!std::filesystem::exists(dir, ec)) return out;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        const auto& p = entry.path();
        if (p.extension() != ".ini") continue;
        std::string stem = p.stem().string();
        if (stem.empty() || stem[0] == '_') continue;  // hide _autobackup_*
        out.push_back(stem);
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool ExportProfile(const std::string& name, std::string& err) {
    const std::string clean = SanitizeProfileName(name);
    if (clean.empty()) { err = "Enter a profile name."; return false; }

    std::error_code ec;
    std::filesystem::create_directories(RuntimePaths::ProfilesDir(), ec);

    const auto userPath  = RuntimePaths::UserIniPath();
    const auto macroPath = RuntimePaths::MacrosIniPath();
    if (!std::filesystem::exists(userPath, ec) && !std::filesystem::exists(macroPath, ec)) {
        err = "Nothing to export yet — Save & Apply your bindings first.";
        return false;
    }

    // User + macros are disjoint by ownership, so merging is a plain two-file load.
    CSimpleIniA prof;
    prof.SetUnicode(false);
    prof.LoadFile(userPath.string().c_str());
    prof.LoadFile(macroPath.string().c_str());

    const long ver = prof.GetLongValue("Meta", "iConfigVersion", ConfigMigration::kConfigVersion);
    prof.SetValue("Profile", "sName", clean.c_str());
    prof.SetValue("Profile", "sModVersion", ModVersionString().c_str());
    prof.SetLongValue("Profile", "iConfigVersion", ver);
    prof.SetValue("Profile", "sExported", TimeStamp(false).c_str());

    const auto out = RuntimePaths::ProfilesDir() / (clean + ".ini");
    if (prof.SaveFile(out.string().c_str()) != SI_OK) {
        err = "Could not write profile file.";
        return false;
    }
    WizLog("Exported profile -> " + out.string());
    return true;
}

bool ImportProfile(const std::string& name, std::string& err) {
    const auto path = RuntimePaths::ProfilesDir() / (SanitizeProfileName(name) + ".ini");
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) { err = "Profile not found."; return false; }

    // Auto-backup the current pair before overwriting it — importing is a full swap,
    // so the working setup must be recoverable.
    {
        CSimpleIniA cur;
        cur.SetUnicode(false);
        cur.LoadFile(RuntimePaths::UserIniPath().string().c_str());
        cur.LoadFile(RuntimePaths::MacrosIniPath().string().c_str());
        cur.SetValue("Profile", "sName", "autobackup");
        cur.SetValue("Profile", "sExported", TimeStamp(false).c_str());
        std::filesystem::create_directories(RuntimePaths::ProfilesDir(), ec);
        const auto bak = RuntimePaths::ProfilesDir() / ("_autobackup_" + TimeStamp(true) + ".ini");
        cur.SaveFile(bak.string().c_str());
        WizLog("Auto-backed up current config -> " + bak.string());
    }

    CSimpleIniA prof;
    prof.SetUnicode(false);
    if (prof.LoadFile(path.string().c_str()) != SI_OK) { err = "Could not read profile file."; return false; }

    const long ver = prof.GetLongValue("Profile", "iConfigVersion", ConfigMigration::kConfigVersion);

    // Split back by section ownership: [Macro:*] -> macros file, [Profile] dropped
    // (metadata), everything else -> user file.
    CSimpleIniA user, macros;
    user.SetUnicode(false);
    macros.SetUnicode(false);

    CSimpleIniA::TNamesDepend sections;
    prof.GetAllSections(sections);
    for (const auto& sec : sections) {
        const char* secName = sec.pItem;
        if (_stricmp(secName, "Profile") == 0) continue;
        CSimpleIniA& dst = (_strnicmp(secName, "Macro:", 6) == 0) ? macros : user;
        CSimpleIniA::TNamesDepend keys;
        prof.GetAllKeys(secName, keys);
        for (const auto& k : keys) {
            const char* v = prof.GetValue(secName, k.pItem, nullptr);
            if (v) dst.SetValue(secName, k.pItem, v);
        }
    }
    user.SetLongValue("Meta", "iConfigVersion", ver);

    // Full replace so no stale key or macro survives the swap. SaveFile overwrites
    // both files; an empty macros object clears a previously-populated macro file.
    if (user.SaveFile(RuntimePaths::UserIniPath().string().c_str()) != SI_OK) {
        err = "Could not write user config.";
        return false;
    }
    macros.SaveFile(RuntimePaths::MacrosIniPath().string().c_str());

    ThrottleController::ReloadConfig();  // wizard state refreshes via ConfigGeneration
    WizLog("Imported profile: " + path.string());
    return true;
}

std::string FormatBindingDisplay(const std::string& binding) {
    if (binding == "(unbound)") return binding;
    auto atPos = binding.rfind('@');
    std::string numPart = (atPos != std::string::npos) ? binding.substr(atPos + 1) : binding;
    char* endPtr = nullptr;
    long btnId = std::strtol(numPart.c_str(), &endPtr, 10);
    if (endPtr != numPart.c_str() && *endPtr == '\0' && btnId >= 129 && btnId <= 144) {
        int povIndex = (int)(btnId - 129) / 4;
        int direction = (int)(btnId - 129) % 4;
        char label[256];
        std::snprintf(label, sizeof(label), "%s (POV%d-%s)", binding.c_str(), povIndex + 1,
            WizardCapture::PovDirectionName(direction));
        return label;
    }
    return binding;
}

} // namespace WizardConfig
