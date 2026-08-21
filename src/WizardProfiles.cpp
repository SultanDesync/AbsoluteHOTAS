#include "PCH.h"

#include "WizardConfigInternal.h"

#include "ProfileOverlay.h"
#include "ConfigOwnershipPolicy.h"
#include "RuntimePaths.h"
#include "ThrottleController.h"

#include <cstdio>
#include <cstring>
#include <ctime>

namespace WizardConfig::Detail {


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


bool SaveIniAtomically(CSimpleIniA& ini, const std::filesystem::path& path) {
    const std::filesystem::path temp = path.wstring() + L".tmp";
    if (ini.SaveFile(temp.string().c_str()) != SI_OK) return false;
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());
        return false;
    }
    return true;
}

bool SaveBindingsToINI(std::string& err) {
    // Write ONLY the custom file. The main ini is mod-owned and overwrite-safe; a
    // single user key written there reintroduces the update-clobber bug the split
    // exists to prevent. See docs/reference/config-layout.md.
    const auto iniPath = RuntimePaths::CustomIniPath();
    Log("Saving bindings to: " + iniPath.string());

    CSimpleIniA ini;
    ini.SetUnicode(false);
    const bool existed = ini.LoadFile(iniPath.string().c_str()) == SI_OK;
    if (existed) {
        const long version = ini.GetLongValue("Meta", "iConfigVersion", -1);
        if (version < 1 || version > kConfigVersion) {
            err = "The custom configuration is invalid or from a newer version.";
            Log("Refused to overwrite invalid or unsupported custom config: " + iniPath.string());
            return false;
        }
    }

    SerializeUserOwnedState(State(), ini);

    // Stamp the 4.0 baseline schema for future ordered migrations.
    SerializeMacros(State(), ini);
    ini.SetLongValue("Meta", "iConfigVersion", kConfigVersion);

    if (!SaveIniAtomically(ini, iniPath)) {
        err = "Could not write the custom configuration.";
        Log("Could not write custom config atomically: " + iniPath.string());
        return false;
    }

    Log("INI saved. Reloading config...");

    ThrottleController::ReloadConfig();
    Log("Config reload requested.");
    MarkStateSaved();
    return true;
}

// Write a switch profile as a SPARSE overlay: only the keys whose effective value
// differs from base. This is the hard requirement the whole profile UX rests on — a
// full dump would freeze the profile into a copy that stops tracking base. See
// docs/reference/profile-switching.md.
//
// PRECONDITION: the editable state must hold the profile's EFFECTIVE config
// (base + this profile's overrides). ComputeDiff visits every managed key, so if it were
// merely base (overrides not loaded), a managed key the user did not touch would
// read as "reverted" and its override would be deleted. Editing an existing profile
// therefore requires loading its overrides into the draft first (effective-load, lands
// with override rendering). Creating a fresh empty overlay is safe today: the draft is
// base + this session's edits, and the file starts empty.
bool SaveProfileOverlay(const std::string& name, std::string& err) {
    CSimpleIniA effIni, baseIni;
    effIni.SetUnicode(false);
    baseIni.SetUnicode(false);
    SerializeUserOwnedState(State(),     effIni);   // base + this session's edits
    SerializeUserOwnedState(BaseState(), baseIni);  // pristine base
    SerializeMacros(State(), effIni);
    SerializeMacros(BaseState(), baseIni);

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
        err = "The maximum of 16 profiles has been reached.";
        Log("Could not allocate a profile record (maximum 16). ");
        return false;
    }
    CSimpleIniA prof;
    prof.SetUnicode(false);
    prof.LoadFile(path.string().c_str());

    const char* existingKind = prof.GetValue("Profile", "sKind", "overlay");
    if (_stricmp(existingKind, "full") == 0) {
        const long sequence = prof.GetLongValue("Profile", "iSequence", record.sequence);
        SerializeUserOwnedState(State(), prof);
        SerializeMacros(State(), prof);
        prof.Delete("Profiles", nullptr);
        prof.Delete("ShipControlMethods", nullptr);
        prof.SetValue("Profile", "sName", name.c_str());
        prof.SetValue("Profile", "sKind", "full");
        prof.SetLongValue("Profile", "iSequence", sequence);
        prof.SetLongValue("Profile", "iConfigVersion", kConfigVersion);
        if (!SaveIniAtomically(prof, path)) {
            err = "Could not write the independent profile.";
            Log("Could not write full profile: " + path.string());
            return false;
        }
        Log("Saved independent profile '" + name + "' -> " + path.string());
        ThrottleController::ReloadConfig();
        MarkStateSaved();
        return true;
    }

    const int overrides = ProfileOverlay::ComputeDiff(effIni, baseIni, prof);

    prof.SetValue("Profile", "sName", name.c_str());
    prof.SetValue("Profile", "sKind", "overlay");
    if (!prof.GetValue("Profile", "iSequence", nullptr))
        prof.SetLongValue("Profile", "iSequence", record.sequence);
    prof.SetLongValue("Profile", "iConfigVersion", kConfigVersion);

    if (!SaveIniAtomically(prof, path)) {
        err = "Could not write the profile overlay.";
        Log("Could not write profile overlay: " + path.string());
        return false;
    }
    Log("Saved overlay '" + name + "' (" + std::to_string(overrides) + " override(s)) -> " + path.string());
    ThrottleController::ReloadConfig();
    MarkStateSaved();
    return true;
}

}  // namespace WizardConfig::Detail

namespace WizardConfig {

using Detail::AllocateProfileRecord;
using Detail::FindProfilePath;
using Detail::ProfileRecord;
using Detail::ReadProfileRecords;
using Detail::SaveIniAtomically;


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
        return std::string(Plugin::VersionString);
    }
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
        CSimpleIniA::TNamesDepend sections;
        profile.GetAllSections(sections);
        for (const auto& section : sections) {
            if (_stricmp(section.pItem, "Profile") == 0) continue;
            CSimpleIniA::TNamesDepend keys;
            profile.GetAllKeys(section.pItem, keys);
            summary.overrideCount += static_cast<int>(keys.size());
        }
        const char* keyboardShortcut = profile.GetValue("Profile", "sKeyboardShortcut", "-1");
        summary.keyboardShortcut = (!keyboardShortcut || !*keyboardShortcut
            || std::strcmp(keyboardShortcut, "-1") == 0)
            ? "(unbound)" : keyboardShortcut;

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

    auto EnsureKeyboardShortcut = [&](const char* name, const char* shortcut) {
        const auto path = FindProfilePath(name);
        CSimpleIniA profile;
        profile.SetUnicode(false);
        if (path.empty() || profile.LoadFile(path.string().c_str()) != SI_OK) {
            err = std::string("Could not open the ") + name + " starter profile.";
            return false;
        }
        if (profile.GetValue("Profile", "sKeyboardShortcut", nullptr)) return true;
        profile.SetValue("Profile", "sKeyboardShortcut", shortcut);
        if (!SaveIniAtomically(profile, path)) {
            err = std::string("Could not initialize the ") + name + " keyboard shortcut.";
            return false;
        }
        return true;
    };
    if (!EnsureKeyboardShortcut("FPS", "key:0x11+0x31")) return false;
    if (!EnsureKeyboardShortcut("Flight Aux", "key:0x11+0x32")) return false;

    for (const auto& profile : ListProfileSummaries()) {
        const char* oldDefault = profile.name == "FPS" ? "key:0x11+0x31"
            : profile.name == "Flight Aux" ? "key:0x11+0x32" : nullptr;
        if (!oldDefault) continue;
        // The keyboard shortcut lives in the profile file and remains active
        // independently of the optional controller/custom activation below. Move
        // legacy starter installs out of SlotNButton without disturbing a binding
        // the user has already replaced.
        if (profile.slot == 0 || _stricmp(profile.trigger.c_str(), oldDefault) == 0) {
            const std::string mode = profile.slot == 0 ? "toggle" : profile.mode;
            if (!SetProfileActivation(profile.name, "(unbound)", mode, err)) return false;
        }
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

bool SetProfileActivations(const std::vector<ProfileActivationUpdate>& updates,
                           std::string& err) {
    if (updates.empty()) return true;
    CSimpleIniA custom;
    custom.SetUnicode(false);
    const auto customPath = RuntimePaths::CustomIniPath();
    const bool existed = custom.LoadFile(customPath.string().c_str()) == SI_OK;
    if (existed && custom.GetLongValue("Meta", "iConfigVersion", -1) != kConfigVersion) {
        err = "Custom config version is invalid or unsupported.";
        return false;
    }

    for (const auto& update : updates) {
        // An empty name is the base config — a first-class swap position identified
        // by the sentinel "(base)" instead of a profile file.
        std::string fileId;
        if (update.profile.empty()) {
            fileId = "(base)";
        } else {
            const auto path = FindProfilePath(update.profile);
            if (path.empty()) { err = "Profile not found."; return false; }
            fileId = path.filename().string();
        }

        int assigned = 0;
        for (int slot = 1; slot <= 16; ++slot) {
            const std::string prefix = "Slot" + std::to_string(slot);
            const char* file = custom.GetValue(
                "Profiles", (prefix + "File").c_str(), nullptr);
            if (file && _stricmp(file, fileId.c_str()) == 0) {
                assigned = slot;
                break;
            }
        }
        if (!assigned) {
            for (int slot = 1; slot <= 16; ++slot) {
                const std::string key = "Slot" + std::to_string(slot) + "File";
                if (!custom.GetValue("Profiles", key.c_str(), nullptr)) {
                    assigned = slot;
                    break;
                }
            }
        }
        if (!assigned) { err = "No activation slots are available."; return false; }

        const std::string prefix = "Slot" + std::to_string(assigned);
        custom.SetValue("Profiles", (prefix + "File").c_str(), fileId.c_str());
        custom.SetValue("Profiles", (prefix + "Button").c_str(),
            update.trigger == "(unbound)" ? "-1" : update.trigger.c_str());
        const char* normalizedMode = update.mode == "toggle" ? "toggle"
            : update.mode == "selector" ? "selector" : "momentary";
        custom.SetValue("Profiles", (prefix + "Mode").c_str(), normalizedMode);
    }
    custom.SetLongValue("Meta", "iConfigVersion", kConfigVersion);
    if (!SaveIniAtomically(custom, customPath)) { err = "Could not save profile activation."; return false; }
    ThrottleController::ReloadConfig();
    return true;
}

bool SetProfileActivation(const std::string& name, const std::string& trigger,
                          const std::string& mode, std::string& err) {
    return SetProfileActivations(
        { ProfileActivationUpdate{name, trigger, mode} }, err);
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

    CSimpleIniA empty;
    empty.SetUnicode(false);
    Detail::ReplaceHotasOwnedState(current, empty);
    current.SetLongValue("Meta", "iConfigVersion", kConfigVersion);
    if (!SaveIniAtomically(current, customPath)) {
        err = "Could not reset the custom configuration.";
        return false;
    }

    Detail::EditProfile().clear();
    Detail::State() = WizardState{};
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
    prof.Delete("ShipControlMethods", nullptr);
    ConfigOwnershipPolicy::RemoveStandaloneOwned(prof);

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
    Detail::Log("Exported profile -> " + out.string());
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
        Detail::Log("Auto-backed up current config -> " + bak.string());
    }

    const long ver = prof.GetLongValue("Profile", "iConfigVersion", kConfigVersion);

    // Replace only HOTAS's managed payload. Existing unknown keys and all
    // standalone-module state stay byte-for-value; external fields carried by
    // an older full profile are deliberately ignored.
    CSimpleIniA custom;
    custom.SetUnicode(false);
    custom.LoadFile(RuntimePaths::CustomIniPath().string().c_str());
    Detail::ReplaceHotasOwnedState(custom, prof);
    custom.SetLongValue("Meta", "iConfigVersion", ver);

    // The managed slice is a full replacement, so stale HOTAS keys and macros do
    // not survive. Foreign and standalone-owned keys remain in the existing file.
    if (!SaveIniAtomically(custom, RuntimePaths::CustomIniPath())) {
        err = "Could not write custom config.";
        return false;
    }

    ThrottleController::ReloadConfig();  // wizard state refreshes via ConfigGeneration
    Detail::Log("Imported profile: " + path.string());
    return true;
}

}  // namespace WizardConfig
