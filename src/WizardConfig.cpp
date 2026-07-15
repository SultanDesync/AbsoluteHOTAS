#include "PCH.h"

#include "WizardConfig.h"
#include "WizardCapture.h"
#include "ThrottleController.h"
#include "ShipOutput.h"
#include "RuntimePaths.h"
#include "ProfileOverlay.h"

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

inline constexpr int kConfigVersion = 1;

static WizardState s_state;

// Pristine base config, captured whenever s_state is (re)loaded. Valid because the
// wizard being open forces the engine to base (slot 0), so GetConfig() == base while
// we edit. Sparse profile saves diff s_state against this.
static WizardState s_baseState;

// Save target: empty = base (_Custom.ini); otherwise a managed profile overlay.
static std::string s_editProfile;

WizardState& GetState() { return s_state; }

const std::string& GetEditProfile() { return s_editProfile; }
void SetEditProfile(const std::string& name) { s_editProfile = name; }

namespace {
    struct ProfileRecord {
        std::filesystem::path path;
        std::string name;
        int sequence = 0;
    };

    std::vector<ProfileRecord> ReadProfileRecords() {
        std::vector<ProfileRecord> records;
        std::error_code ec;
        const auto dir = RuntimePaths::ProfilesDir();
        if (!std::filesystem::exists(dir, ec)) return records;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".ini") continue;
            if (entry.path().stem().string().starts_with("_autobackup_")) continue;
            CSimpleIniA ini;
            ini.SetUnicode(false);
            if (ini.LoadFile(entry.path().string().c_str()) != SI_OK) continue;
            const char* name = ini.GetValue("Profile", "sName", nullptr);
            if (!name || !*name) continue;
            records.push_back({entry.path(), name, (int)ini.GetLongValue("Profile", "iSequence", 0)});
        }
        std::sort(records.begin(), records.end(), [](const auto& a, const auto& b) {
            if (a.sequence != b.sequence) return a.sequence < b.sequence;
            return a.name < b.name;
        });
        return records;
    }

    std::filesystem::path FindProfilePath(const std::string& name) {
        for (const auto& record : ReadProfileRecords())
            if (_stricmp(record.name.c_str(), name.c_str()) == 0) return record.path;
        return {};
    }

    ProfileRecord AllocateProfileRecord(const std::string& name) {
        const auto records = ReadProfileRecords();
        for (const auto& record : records)
            if (_stricmp(record.name.c_str(), name.c_str()) == 0) return record;
        for (int sequence = 1; sequence <= 16; ++sequence) {
            char filename[32];
            std::snprintf(filename, sizeof(filename), "profile_%02d.ini", sequence);
            const auto path = RuntimePaths::ProfilesDir() / filename;
            const bool used = std::any_of(records.begin(), records.end(), [&](const auto& record) {
                return record.sequence == sequence || record.path == path;
            });
            if (!used) return {path, name, sequence};
        }
        return {};
    }
}

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
    std::string SlugifyMacroName(const std::string& in) {
        std::string out;
        for (char c : in) {
            if (std::isalnum((unsigned char)c)) out.push_back((char)std::tolower((unsigned char)c));
            else if (c == ' ' || c == '-' || c == '_') { if (!out.empty() && out.back() != '_') out.push_back('_'); }
        }
        while (!out.empty() && out.back() == '_') out.pop_back();
        return out.empty() ? "macro" : out;
    }

    std::string MakeUniqueMacroKey(const std::string& sName, std::vector<std::string>& used) {
        const std::string base = SlugifyMacroName(sName);
        std::string key = base;
        for (int n = 2; std::find(used.begin(), used.end(), key) != used.end(); ++n)
            key = base + "_" + std::to_string(n);
        used.push_back(key);
        return key;
    }

    void LoadMacroRows(WizardState& s, const std::filesystem::path* profilePath = nullptr) {
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

        std::sort(s.macros.begin(), s.macros.end(),
                  [](const MacroRow& a, const MacroRow& b) { return a.name < b.name; });
    }
}

void LoadCurrentBindings() {
    if (s_state.loaded) return;

    auto& cfg = ThrottleController::GetConfig();
    auto& s = s_state;
    s.axisInjectionEnabled = cfg.bEnableInjection;

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
    const BindingRef* extensionRefs[] = {
        &cfg.cruiseHoldButton, &cfg.fullStopButton, &cfg.cruiseHalfButton, &cfg.cruiseMaxButton
    };
    for (int i = 0; i < kNumControlExtensionSlots; ++i)
        s.controlExtensionBindings[i] = FormatBindingRef(*extensionRefs[i], false);

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

    // Snapshot base for sparse-overlay diffs (see s_baseState). The wizard forces the
    // engine to base while open, so what we just loaded IS base.
    s_baseState = s;

    s.loaded = true;
}

static std::string IniBinding(const CSimpleIniA& ini, const char* section,
                              const char* key, const std::string& fallback,
                              bool axis) {
    const char* value = ini.GetValue(section, key, nullptr);
    if (!value) return fallback;
    if (!*value || std::strcmp(value, "-1") == 0) return "(unbound)";
    return FormatBindingRef(ParseBindingRef(value, axis ? 0 : -1), axis);
}

static void ApplyProfileScalars(const CSimpleIniA& ini, WizardState& s) {
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
    for (int i = 0; i < kNumControlExtensionSlots; ++i)
        s.controlExtensionBindings[i] = IniBinding(ini, "ControlExtensions",
            kControlExtensionSlots[i].iniKey, s.controlExtensionBindings[i], false);
    for (auto& action : s.shipActionSlots)
        action.binding = IniBinding(ini, "ShipButtons", action.iniKey.c_str(), action.binding, false);
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

static void LoadEffectiveCollections(const std::filesystem::path& profilePath,
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

bool LoadProfileForEditing(const std::string& name, std::string& err) {
    LoadCurrentBindings();
    if (name.empty()) {
        s_state = s_baseState;
        s_editProfile.clear();
        return true;
    }

    const auto path = FindProfilePath(name);
    if (path.empty()) { err = "Profile not found."; return false; }
    CSimpleIniA profile;
    profile.SetUnicode(false);
    if (profile.LoadFile(path.string().c_str()) != SI_OK) {
        err = "Could not read profile file.";
        return false;
    }
    const char* kind = profile.GetValue("Profile", "sKind", nullptr);
    const long version = profile.GetLongValue("Profile", "iConfigVersion", -1);
    if (!kind || (_stricmp(kind, "full") != 0 && _stricmp(kind, "overlay") != 0)
        || version < 1 || version > kConfigVersion) {
        err = "Profile format is invalid or newer than this version of AbsoluteHOTAS.";
        return false;
    }

    WizardState effective = s_baseState;
    ApplyProfileScalars(profile, effective);
    LoadEffectiveCollections(path, effective);
    LoadMacroRows(effective, &path);
    effective.loaded = true;
    s_state = std::move(effective);
    s_editProfile = name;
    return true;
}

// Serialize every user-owned key from a WizardState into an ini object. Shared by
// the full user-file save and the sparse profile-overlay diff, so both write the
// exact same key set for the same state — which is what makes the diff clean.
static void SerializeUserOwnedState(const WizardState& s, CSimpleIniA& ini) {
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
}

static void DeleteMacroSections(CSimpleIniA& ini) {
    CSimpleIniA::TNamesDepend sections;
    ini.GetAllSections(sections);
    std::vector<std::string> remove;
    for (const auto& sec : sections)
        if (sec.pItem && _strnicmp(sec.pItem, "Macro:", 6) == 0) remove.emplace_back(sec.pItem);
    for (const auto& section : remove) ini.Delete(section.c_str(), nullptr);
}

static int SerializeMacros(const WizardState& state, CSimpleIniA& ini) {
    DeleteMacroSections(ini);
    int runnable = 0;
    std::vector<std::string> used;
    for (const auto& m : state.macros) {
        const std::string name = SanitizeMacroName(m.name);
        if (name.empty() || std::find(used.begin(), used.end(), name) != used.end()) continue;
        used.push_back(name);
        const std::string section = "Macro:" + name;
        const bool bound = m.buttonBinding != "(unbound)";
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
        if (bound && written > 0) ++runnable;
    }
    return runnable;
}

static bool SaveIniAtomically(CSimpleIniA& ini, const std::filesystem::path& path) {
    const std::filesystem::path temp = path.wstring() + L".tmp";
    if (ini.SaveFile(temp.string().c_str()) != SI_OK) return false;
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());
        return false;
    }
    return true;
}

void SaveBindingsToINI() {
    // Write ONLY the custom file. The main ini is mod-owned and overwrite-safe; a
    // single user key written there reintroduces the update-clobber bug the split
    // exists to prevent. See docs/reference/config-layout.md.
    const auto iniPath = RuntimePaths::CustomIniPath();
    WizLog("Saving bindings to: " + iniPath.string());

    CSimpleIniA ini;
    ini.SetUnicode(false);
    const bool existed = ini.LoadFile(iniPath.string().c_str()) == SI_OK;
    if (existed) {
        const long version = ini.GetLongValue("Meta", "iConfigVersion", -1);
        if (version < 1 || version > kConfigVersion) {
            WizLog("Refused to overwrite invalid or unsupported custom config: " + iniPath.string());
            return;
        }
    }

    SerializeUserOwnedState(s_state, ini);

    // Stamp the 3.1 baseline schema for future ordered migrations.
    SerializeMacros(s_state, ini);
    ini.SetLongValue("Meta", "iConfigVersion", kConfigVersion);

    if (!SaveIniAtomically(ini, iniPath)) {
        WizLog("Could not write custom config atomically: " + iniPath.string());
        return;
    }

    WizLog("INI saved. Reloading config...");

    ThrottleController::ReloadConfig();
    WizLog("Config reload requested.");
}

// Write a switch profile as a SPARSE overlay: only the keys whose effective value
// differs from base. This is the hard requirement the whole profile UX rests on — a
// full dump would freeze the profile into a copy that stops tracking base. See
// docs/reference/profile-switching.md.
//
// PRECONDITION: s_state must hold the profile's EFFECTIVE config (base + this
// profile's overrides). ComputeDiff visits every managed key, so if s_state were
// merely base (overrides not loaded), a managed key the user did not touch would
// read as "reverted" and its override would be deleted. Editing an existing profile
// therefore requires loading its overrides into s_state first (effective-load, lands
// with override rendering). Creating a fresh empty overlay is safe today: s_state is
// base + this session's edits, and the file starts empty.
void SaveProfileOverlay(const std::string& name) {
    CSimpleIniA effIni, baseIni;
    effIni.SetUnicode(false);
    baseIni.SetUnicode(false);
    SerializeUserOwnedState(s_state,     effIni);   // base + this session's edits
    SerializeUserOwnedState(s_baseState, baseIni);  // pristine base
    SerializeMacros(s_state, effIni);
    SerializeMacros(s_baseState, baseIni);

    // Merge onto any existing overlay so overrides from earlier sessions that the user
    // did not touch this time survive; we only add/update/remove what changed.
    std::error_code ec;
    std::filesystem::create_directories(RuntimePaths::ProfilesDir(), ec);
    auto path = FindProfilePath(name);
    ProfileRecord record;
    if (path.empty()) {
        record = AllocateProfileRecord(name);
        path = record.path;
    }
    if (path.empty()) {
        WizLog("Could not allocate a profile record (maximum 16). ");
        return;
    }
    CSimpleIniA prof;
    prof.SetUnicode(false);
    prof.LoadFile(path.string().c_str());

    const char* existingKind = prof.GetValue("Profile", "sKind", "overlay");
    if (_stricmp(existingKind, "full") == 0) {
        const long sequence = prof.GetLongValue("Profile", "iSequence", record.sequence);
        SerializeUserOwnedState(s_state, prof);
        SerializeMacros(s_state, prof);
        prof.Delete("Profiles", nullptr);
        prof.SetValue("Profile", "sName", name.c_str());
        prof.SetValue("Profile", "sKind", "full");
        prof.SetLongValue("Profile", "iSequence", sequence);
        prof.SetLongValue("Profile", "iConfigVersion", kConfigVersion);
        if (!SaveIniAtomically(prof, path)) {
            WizLog("Could not write full profile: " + path.string());
            return;
        }
        WizLog("Saved independent profile '" + name + "' -> " + path.string());
        ThrottleController::ReloadConfig();
        return;
    }

    const int overrides = ProfileOverlay::ComputeDiff(effIni, baseIni, prof);

    prof.SetValue("Profile", "sName", name.c_str());
    prof.SetValue("Profile", "sKind", "overlay");
    if (!prof.GetValue("Profile", "iSequence", nullptr))
        prof.SetLongValue("Profile", "iSequence", record.sequence);
    prof.SetLongValue("Profile", "iConfigVersion", kConfigVersion);

    if (!SaveIniAtomically(prof, path)) {
        WizLog("Could not write profile overlay: " + path.string());
        return;
    }
    WizLog("Saved overlay '" + name + "' (" + std::to_string(overrides) + " override(s)) -> " + path.string());
    ThrottleController::ReloadConfig();
}

// Route Save to whichever profile is the current edit target.
void SaveActiveProfile() {
    if (s_editProfile.empty()) SaveBindingsToINI();     // base -> _Custom.ini
    else                       SaveProfileOverlay(s_editProfile);
}

void SaveMacrosToINI() {
    const auto path = RuntimePaths::CustomIniPath();

    // Full rewrite from the editor rows. The file holds nothing but [Macro:*], so
    // there is no foreign state to preserve — deliberately do NOT LoadFile first,
    // or deleted macros would linger.
    CSimpleIniA ini;
    ini.SetUnicode(false);
    const bool existed = ini.LoadFile(path.string().c_str()) == SI_OK;
    if (existed) {
        const long version = ini.GetLongValue("Meta", "iConfigVersion", -1);
        if (version < 1 || version > kConfigVersion) {
            WizLog("Refused to overwrite invalid or unsupported custom config: " + path.string());
            return;
        }
    }
    DeleteMacroSections(ini);

    // A half-built macro (no trigger button yet, no steps yet) is still the user's
    // work, and MacroEngine already ignores such macros at load with a warning. So
    // persist them rather than dropping them — otherwise the wizard, which reloads
    // from this file after every Save, would make them vanish as the user typed.
    int saved = 0;
    std::vector<std::string> usedKeys;
    for (const auto& m : s_state.macros) {
        const std::string sName = SanitizeMacroName(m.name);
        if (sName.empty()) continue;  // no display name = nothing to show; wizard flags it

        // The section key is generated (slug + collision index), NOT the display name,
        // so two macros sharing a friendly name get distinct sections instead of
        // merging. The friendly name rides along as sName.
        const std::string section = "Macro:" + MakeUniqueMacroKey(sName, usedKeys);
        ini.SetValue(section.c_str(), "sName", sName.c_str());
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

    ini.SetLongValue("Meta", "iConfigVersion", kConfigVersion);
    if (!SaveIniAtomically(ini, path))
        WizLog("Could not write macros into custom config: " + path.string());
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
    for (const auto& record : ReadProfileRecords()) out.push_back(record.name);
    return out;
}

std::vector<ProfileSummary> ListProfileSummaries() {
    std::vector<ProfileSummary> out;
    CSimpleIniA custom;
    custom.SetUnicode(false);
    custom.LoadFile(RuntimePaths::CustomIniPath().string().c_str());

    for (const auto& record : ReadProfileRecords()) {
        ProfileSummary summary;
        summary.name = record.name;
        summary.filename = record.path.filename().string();
        summary.sequence = record.sequence;
        CSimpleIniA profile;
        profile.SetUnicode(false);
        profile.LoadFile(record.path.string().c_str());
        summary.kind = profile.GetValue("Profile", "sKind", "overlay");

        for (int slot = 1; slot <= 16; ++slot) {
            const std::string prefix = "Slot" + std::to_string(slot);
            const char* file = custom.GetValue("Profiles", (prefix + "File").c_str(), nullptr);
            if (!file || _stricmp(file, summary.filename.c_str()) != 0) continue;
            summary.slot = slot;
            const char* trigger = custom.GetValue("Profiles", (prefix + "Button").c_str(), "-1");
            summary.trigger = (!trigger || !*trigger || std::strcmp(trigger, "-1") == 0)
                ? "(unbound)" : trigger;
            summary.mode = custom.GetValue("Profiles", (prefix + "Mode").c_str(), "momentary");
            break;
        }
        out.push_back(std::move(summary));
    }
    return out;
}

bool CreateOverlayProfile(const std::string& name, std::string& err) {
    const std::string clean = SanitizeProfileName(name);
    if (clean.empty()) { err = "Enter a profile name."; return false; }
    if (!FindProfilePath(clean).empty()) { err = "A profile with that name already exists."; return false; }
    std::error_code ec;
    std::filesystem::create_directories(RuntimePaths::ProfilesDir(), ec);
    const ProfileRecord record = AllocateProfileRecord(clean);
    if (record.path.empty()) { err = "The maximum of 16 profiles has been reached."; return false; }

    CSimpleIniA profile;
    profile.SetUnicode(false);
    profile.SetValue("Profile", "sName", clean.c_str());
    profile.SetValue("Profile", "sKind", "overlay");
    profile.SetLongValue("Profile", "iSequence", record.sequence);
    profile.SetLongValue("Profile", "iConfigVersion", kConfigVersion);
    if (!SaveIniAtomically(profile, record.path)) { err = "Could not create profile."; return false; }
    return true;
}

bool EnsureStarterProfiles(std::string& err) {
    if (FindProfilePath("FPS").empty()) {
        if (!CreateOverlayProfile("FPS", err)) return false;
        const auto path = FindProfilePath("FPS");
        CSimpleIniA fps;
        fps.SetUnicode(false);
        fps.LoadFile(path.string().c_str());
        fps.SetBoolValue("Injection", "bEnableInjection", false);
        if (!SaveIniAtomically(fps, path)) { err = "Could not initialize FPS profile."; return false; }
    }
    if (FindProfilePath("Flight Aux").empty() && !CreateOverlayProfile("Flight Aux", err)) return false;
    for (const auto& profile : ListProfileSummaries()) {
        if (profile.slot != 0) continue;
        // Ctrl+1 / Ctrl+2, not F-keys: F5/F9 are quicksave/quickload and the F-row is
        // otherwise game-claimed. A Ctrl+digit chord is collision-safe out of the box.
        if (profile.name == "FPS" && !SetProfileActivation("FPS", "key:0x11+0x31", "toggle", err)) return false;
        if (profile.name == "Flight Aux" && !SetProfileActivation("Flight Aux", "key:0x11+0x32", "toggle", err)) return false;
    }
    return true;
}

void GetBaseActivation(std::string& trigger, std::string& mode) {
    trigger = "(unbound)";
    mode = "momentary";
    CSimpleIniA custom;
    custom.SetUnicode(false);
    if (custom.LoadFile(RuntimePaths::CustomIniPath().string().c_str()) != SI_OK) return;
    for (int slot = 1; slot <= 16; ++slot) {
        const std::string prefix = "Slot" + std::to_string(slot);
        const char* file = custom.GetValue("Profiles", (prefix + "File").c_str(), nullptr);
        if (!file || _stricmp(file, "(base)") != 0) continue;
        const char* t = custom.GetValue("Profiles", (prefix + "Button").c_str(), "-1");
        trigger = (!t || !*t || std::strcmp(t, "-1") == 0) ? "(unbound)" : t;
        mode = custom.GetValue("Profiles", (prefix + "Mode").c_str(), "momentary");
        return;
    }
}

bool SetProfileActivation(const std::string& name, const std::string& trigger,
                          const std::string& mode, std::string& err) {
    // An empty name is the base config — a first-class swap position identified by the
    // sentinel "(base)" instead of a profile file (see ParseProfileSlots).
    std::string fileId;
    if (name.empty()) {
        fileId = "(base)";
    } else {
        const auto path = FindProfilePath(name);
        if (path.empty()) { err = "Profile not found."; return false; }
        fileId = path.filename().string();
    }

    CSimpleIniA custom;
    custom.SetUnicode(false);
    const auto customPath = RuntimePaths::CustomIniPath();
    const bool existed = custom.LoadFile(customPath.string().c_str()) == SI_OK;
    if (existed && custom.GetLongValue("Meta", "iConfigVersion", -1) != kConfigVersion) {
        err = "Custom config version is invalid or unsupported.";
        return false;
    }

    int assigned = 0;
    for (int slot = 1; slot <= 16; ++slot) {
        const std::string prefix = "Slot" + std::to_string(slot);
        const char* file = custom.GetValue("Profiles", (prefix + "File").c_str(), nullptr);
        if (file && _stricmp(file, fileId.c_str()) == 0) { assigned = slot; break; }
    }
    if (!assigned) {
        for (int slot = 1; slot <= 16; ++slot) {
            const std::string key = "Slot" + std::to_string(slot) + "File";
            if (!custom.GetValue("Profiles", key.c_str(), nullptr)) { assigned = slot; break; }
        }
    }
    if (!assigned) { err = "No activation slots are available."; return false; }

    const std::string prefix = "Slot" + std::to_string(assigned);
    custom.SetValue("Profiles", (prefix + "File").c_str(), fileId.c_str());
    custom.SetValue("Profiles", (prefix + "Button").c_str(),
                    trigger == "(unbound)" ? "-1" : trigger.c_str());
    const char* normalizedMode = mode == "toggle" ? "toggle" : mode == "selector" ? "selector" : "momentary";
    custom.SetValue("Profiles", (prefix + "Mode").c_str(), normalizedMode);
    custom.SetLongValue("Meta", "iConfigVersion", kConfigVersion);
    if (!SaveIniAtomically(custom, customPath)) { err = "Could not save profile activation."; return false; }
    ThrottleController::ReloadConfig();
    return true;
}

bool ResetBaseToDefaults(std::string& err) {
    const auto customPath = RuntimePaths::CustomIniPath();
    CSimpleIniA current;
    current.SetUnicode(false);
    if (current.LoadFile(customPath.string().c_str()) == SI_OK) {
        std::error_code ec;
        std::filesystem::create_directories(RuntimePaths::ProfilesDir(), ec);
        CSimpleIniA backup;
        backup.SetUnicode(false);
        backup.LoadFile(customPath.string().c_str());
        backup.SetValue("Profile", "sName", "Base reset backup");
        backup.SetValue("Profile", "sKind", "full");
        backup.SetLongValue("Profile", "iConfigVersion", kConfigVersion);
        backup.SetValue("Profile", "sExported", TimeStamp(false).c_str());
        const auto backupPath = RuntimePaths::ProfilesDir()
            / ("_autobackup_reset_" + TimeStamp(true) + ".ini");
        if (!SaveIniAtomically(backup, backupPath)) {
            err = "Could not back up the current base configuration.";
            return false;
        }
    }

    CSimpleIniA clean;
    clean.SetUnicode(false);
    CSimpleIniA::TNamesDepend routeKeys;
    current.GetAllKeys("Profiles", routeKeys);
    for (const auto& key : routeKeys) {
        const char* value = current.GetValue("Profiles", key.pItem, nullptr);
        if (value) clean.SetValue("Profiles", key.pItem, value);
    }
    clean.SetLongValue("Meta", "iConfigVersion", kConfigVersion);
    if (!SaveIniAtomically(clean, customPath)) {
        err = "Could not reset the custom configuration.";
        return false;
    }

    s_editProfile.clear();
    s_state = WizardState{};
    ThrottleController::ReloadConfig();
    return true;
}

bool ExportProfile(const std::string& name, std::string& err) {
    const std::string clean = SanitizeProfileName(name);
    if (clean.empty()) { err = "Enter a profile name."; return false; }
    if (!FindProfilePath(clean).empty()) { err = "A profile with that name already exists."; return false; }

    std::error_code ec;
    std::filesystem::create_directories(RuntimePaths::ProfilesDir(), ec);

    const auto customPath = RuntimePaths::CustomIniPath();
    if (!std::filesystem::exists(customPath, ec)) {
        err = "Nothing to export yet — Save & Apply your bindings first.";
        return false;
    }

    // User + macros are disjoint by ownership, so merging is a plain two-file load.
    CSimpleIniA prof;
    prof.SetUnicode(false);
    prof.LoadFile(RuntimePaths::IniPath().string().c_str());
    prof.LoadFile(customPath.string().c_str());
    prof.Delete("Profiles", nullptr);
    prof.Delete("Meta", nullptr);

    prof.SetValue("Profile", "sName", clean.c_str());
    prof.SetValue("Profile", "sKind", "full");
    prof.SetValue("Profile", "sModVersion", ModVersionString().c_str());
    prof.SetLongValue("Profile", "iConfigVersion", kConfigVersion);
    prof.SetValue("Profile", "sExported", TimeStamp(false).c_str());

    auto record = AllocateProfileRecord(clean);
    if (record.path.empty()) { err = "No profile slots are available."; return false; }
    prof.SetLongValue("Profile", "iSequence", record.sequence);
    const auto out = record.path;
    if (!SaveIniAtomically(prof, out)) {
        err = "Could not write profile file.";
        return false;
    }
    WizLog("Exported profile -> " + out.string());
    return true;
}

bool ImportProfile(const std::string& name, std::string& err) {
    const auto path = FindProfilePath(name);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) { err = "Profile not found."; return false; }

    CSimpleIniA prof;
    prof.SetUnicode(false);
    if (prof.LoadFile(path.string().c_str()) != SI_OK) { err = "Could not read profile file."; return false; }
    const char* kind = prof.GetValue("Profile", "sKind", nullptr);
    if (!kind || _stricmp(kind, "full") != 0) {
        err = "Only independent (full) profiles can be imported as the base configuration.";
        return false;
    }
    const long profileVersion = prof.GetLongValue("Profile", "iConfigVersion", -1);
    if (profileVersion < 1 || profileVersion > kConfigVersion) {
        err = "Profile format is invalid or newer than this version of AbsoluteHOTAS.";
        return false;
    }

    CSimpleIniA current;
    current.SetUnicode(false);
    current.LoadFile(RuntimePaths::CustomIniPath().string().c_str());

    // Auto-backup the current pair before overwriting it — importing is a full swap,
    // so the working setup must be recoverable.
    {
        CSimpleIniA cur;
        cur.SetUnicode(false);
        cur.LoadFile(RuntimePaths::CustomIniPath().string().c_str());
        cur.SetValue("Profile", "sName", "autobackup");
        cur.SetValue("Profile", "sKind", "full");
        cur.SetValue("Profile", "sExported", TimeStamp(false).c_str());
        std::filesystem::create_directories(RuntimePaths::ProfilesDir(), ec);
        const auto bak = RuntimePaths::ProfilesDir() / ("_autobackup_" + TimeStamp(true) + ".ini");
        if (!SaveIniAtomically(cur, bak)) { err = "Could not create import backup."; return false; }
        WizLog("Auto-backed up current config -> " + bak.string());
    }

    const long ver = prof.GetLongValue("Profile", "iConfigVersion", kConfigVersion);

    // Drop profile metadata and rebuild one custom payload.
    CSimpleIniA custom;
    custom.SetUnicode(false);

    CSimpleIniA::TNamesDepend sections;
    prof.GetAllSections(sections);
    for (const auto& sec : sections) {
        const char* secName = sec.pItem;
        if (_stricmp(secName, "Profile") == 0) continue;
        if (_stricmp(secName, "Profiles") == 0) continue;
        CSimpleIniA::TNamesDepend keys;
        prof.GetAllKeys(secName, keys);
        for (const auto& k : keys) {
            const char* v = prof.GetValue(secName, k.pItem, nullptr);
            if (v) custom.SetValue(secName, k.pItem, v);
        }
    }
    custom.SetLongValue("Meta", "iConfigVersion", ver);
    CSimpleIniA::TNamesDepend routeKeys;
    current.GetAllKeys("Profiles", routeKeys);
    for (const auto& key : routeKeys) {
        const char* value = current.GetValue("Profiles", key.pItem, nullptr);
        if (value) custom.SetValue("Profiles", key.pItem, value);
    }

    // Full replace so no stale key or macro survives the swap. SaveFile overwrites
    // both files; an empty macros object clears a previously-populated macro file.
    if (!SaveIniAtomically(custom, RuntimePaths::CustomIniPath())) {
        err = "Could not write custom config.";
        return false;
    }

    ThrottleController::ReloadConfig();  // wizard state refreshes via ConfigGeneration
    WizLog("Imported profile: " + path.string());
    return true;
}

std::string FormatBindingDisplay(const std::string& binding) {
    if (binding == "(unbound)") return binding;
    if (binding.rfind("key:", 0) == 0) {
        const int vk = (int)std::strtol(binding.c_str() + 4, nullptr, 0);
        char keyName[64]{};
        const UINT scan = MapVirtualKeyA((UINT)vk, MAPVK_VK_TO_VSC);
        if (scan && GetKeyNameTextA((LONG)(scan << 16), keyName, (int)std::size(keyName)) > 0)
            return keyName;
    }
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
