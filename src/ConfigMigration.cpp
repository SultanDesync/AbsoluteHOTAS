#include "PCH.h"

#include "ConfigMigration.h"
#include "RuntimePaths.h"

#include <SimpleIni.h>

namespace {
    void MigLog(const std::string& msg) { RuntimePaths::Log("[ConfigMigration]", msg); }

    // Sections copied wholesale from the monolith into the user file. These are the
    // user-owned sections from the ownership table in config-layout.md. Everything
    // else ([General], [Injection], [Gate]) stays mod-owned in the main ini.
    constexpr const char* kUserSections[] = {
        "Hardware", "Buttons", "ShipButtons", "ShipButtonOutputs", "ButtonExpansion",
        "Normalization", "DigitalAxes", "Aim", "DualStick", "Calibration", "Layers",
    };

    void CopySection(const CSimpleIniA& src, CSimpleIniA& dst, const char* section)
    {
        CSimpleIniA::TNamesDepend keys;
        src.GetAllKeys(section, keys);
        for (const auto& k : keys) {
            const char* v = src.GetValue(section, k.pItem, nullptr);
            if (v) dst.SetValue(section, k.pItem, v);
        }
    }
}

namespace ConfigMigration {

void SplitUserConfig(const CSimpleIniA& src, CSimpleIniA& dst)
{
    for (const char* section : kUserSections) {
        CopySection(src, dst, section);
    }

    // bHoldForBoost was mis-homed under [Injection] in 3.0.x. Relocate it to
    // [DualStick] so its new home is authoritative; the old location is only read as
    // a migration alias (see ThrottleController::LoadConfig).
    if (const char* hold = src.GetValue("Injection", "bHoldForBoost", nullptr)) {
        dst.SetValue("DualStick", "bHoldForBoost", hold);
    }

    dst.SetLongValue("Meta", "iConfigVersion", kConfigVersion);
}

void MigrateIfNeeded()
{
    const auto userPath = RuntimePaths::UserIniPath();

    // Idempotent gate: the user file's existence means we already split. This is the
    // one check that keeps migration a one-time event.
    std::error_code ec;
    if (std::filesystem::exists(userPath, ec)) return;

    const auto mainPath = RuntimePaths::IniPath();
    if (!std::filesystem::exists(mainPath, ec)) {
        // Fresh install with no monolith at all. Nothing to lift; the layered load
        // runs on hardcoded defaults and the wizard creates the user file on first
        // save. Leave the user file absent so we don't stamp an empty config.
        return;
    }

    // Back up the monolith before reading it. Never destroy the source: an upgrader's
    // only copy of their bindings lives here until the split completes.
    const auto backupPath = RuntimePaths::PluginDirectory() / L"AbsoluteHOTAS.ini.v30.bak";
    std::filesystem::copy_file(mainPath, backupPath,
        std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) MigLog("Backup of monolith failed (continuing): " + ec.message());

    CSimpleIniA src;
    src.SetUnicode();
    if (src.LoadFile(mainPath.string().c_str()) != SI_OK) {
        MigLog("Could not read monolith for migration; leaving user file absent.");
        return;
    }

    CSimpleIniA dst;
    dst.SetUnicode();
    SplitUserConfig(src, dst);

    if (dst.SaveFile(userPath.string().c_str()) != SI_OK) {
        // Read-only / locked target: run this session from the still-intact monolith
        // (loaded as the base layer). Migration retries next launch.
        MigLog("Could not write user file; running from monolith for this session.");
        return;
    }

    MigLog("Migrated monolith -> " + userPath.string() + " (backup: " + backupPath.string() + ").");
}

} // namespace ConfigMigration
