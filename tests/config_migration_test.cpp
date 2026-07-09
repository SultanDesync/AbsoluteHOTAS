// Unit test for ConfigMigration::SplitUserConfig — the pure monolith->split
// transform at the heart of the 3.0.x -> 3.1 config migration. Built via the
// `config_migration_test` xmake target (opt-in; not built by default):
//   xmake build config_migration_test
//   xmake run   config_migration_test
//
// The target compiles this file plus src/ConfigMigration.cpp and src/RuntimePaths.cpp
// (SplitUserConfig is filesystem-free, but shares a translation unit with
// MigrateIfNeeded, which references RuntimePaths). No PCH: the sources include
// PCH.h as an ordinary header, so the test stays independent of the plugin target.

#include "ConfigMigration.h"

#include <SimpleIni.h>
#include <cstdio>
#include <cstring>

static int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("  FAIL [%s:%d] %s\n", __FILE__, __LINE__, #cond);     \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

static bool HasKey(const CSimpleIniA& ini, const char* section, const char* key) {
    return ini.GetValue(section, key, nullptr) != nullptr;
}

// A representative 3.0.x monolith: user-owned bindings/calibration mixed in with
// mod-owned [General]/[Injection], and bHoldForBoost sitting in its old [Injection]
// home.
static void BuildMonolith(CSimpleIniA& src) {
    src.SetValue("General", "bEnabled", "true");                       // mod-owned
    src.SetValue("General", "bSyncShipOutputsFromControlMap", "true"); // mod-owned

    src.SetValue("Hardware", "iThrottleAxis", "VKBSim Throttle@50");   // user-owned
    src.SetBoolValue("Hardware", "bInvertPitch", true);
    src.SetValue("Buttons", "iActivateButtonId", "VKBSim Gunfighter@7");
    src.SetValue("ShipButtons", "iNextSystemButton", "VKBSim Gunfighter@11");
    src.SetValue("ButtonExpansion", "VKBSim Gunfighter@iButton7", "key:0x1E");
    src.SetValue("Calibration", "iCalib_0_0x32", "-32768,32767");
    src.SetValue("Aim", "fAimSensitivity", "1.5");
    src.SetValue("DualStick", "iTurnAssistMode", "1");

    src.SetValue("Injection", "iPollRateHz", "120");                   // mod-owned
    src.SetValue("Injection", "bEnableLog", "false");                  // mod-owned
    src.SetBoolValue("Injection", "bHoldForBoost", false);            // mis-homed user key
}

static void TestUserSectionsCopied() {
    std::printf("UserSectionsCopied\n");
    CSimpleIniA src; src.SetUnicode();
    CSimpleIniA dst; dst.SetUnicode();
    BuildMonolith(src);
    ConfigMigration::SplitUserConfig(src, dst);

    CHECK(std::strcmp(dst.GetValue("Hardware", "iThrottleAxis", ""), "VKBSim Throttle@50") == 0);
    CHECK(dst.GetBoolValue("Hardware", "bInvertPitch", false) == true);
    CHECK(std::strcmp(dst.GetValue("Buttons", "iActivateButtonId", ""), "VKBSim Gunfighter@7") == 0);
    CHECK(std::strcmp(dst.GetValue("ShipButtons", "iNextSystemButton", ""), "VKBSim Gunfighter@11") == 0);
    CHECK(std::strcmp(dst.GetValue("ButtonExpansion", "VKBSim Gunfighter@iButton7", ""), "key:0x1E") == 0);
    CHECK(std::strcmp(dst.GetValue("Calibration", "iCalib_0_0x32", ""), "-32768,32767") == 0);
    CHECK(std::strcmp(dst.GetValue("Aim", "fAimSensitivity", ""), "1.5") == 0);
    CHECK(std::strcmp(dst.GetValue("DualStick", "iTurnAssistMode", ""), "1") == 0);
}

static void TestModSectionsExcluded() {
    std::printf("ModSectionsExcluded\n");
    CSimpleIniA src; src.SetUnicode();
    CSimpleIniA dst; dst.SetUnicode();
    BuildMonolith(src);
    ConfigMigration::SplitUserConfig(src, dst);

    // Mod-owned sections must NOT leak into the user file, or the split doesn't
    // actually make the main ini overwrite-safe.
    CHECK(!HasKey(dst, "General", "bEnabled"));
    CHECK(!HasKey(dst, "General", "bSyncShipOutputsFromControlMap"));
    CHECK(!HasKey(dst, "Injection", "iPollRateHz"));
    CHECK(!HasKey(dst, "Injection", "bEnableLog"));
}

static void TestHoldForBoostRelocated() {
    std::printf("HoldForBoostRelocated\n");
    CSimpleIniA src; src.SetUnicode();
    CSimpleIniA dst; dst.SetUnicode();
    BuildMonolith(src);
    ConfigMigration::SplitUserConfig(src, dst);

    // Value lifted from old [Injection] home into [DualStick]; old home not carried.
    CHECK(dst.GetBoolValue("DualStick", "bHoldForBoost", true) == false);
    CHECK(!HasKey(dst, "Injection", "bHoldForBoost"));
}

static void TestVersionStamped() {
    std::printf("VersionStamped\n");
    CSimpleIniA src; src.SetUnicode();
    CSimpleIniA dst; dst.SetUnicode();
    BuildMonolith(src);
    ConfigMigration::SplitUserConfig(src, dst);

    CHECK(dst.GetLongValue("Meta", "iConfigVersion", -1) == ConfigMigration::kConfigVersion);
}

static void TestHoldForBoostAbsentInSource() {
    std::printf("HoldForBoostAbsentInSource\n");
    CSimpleIniA src; src.SetUnicode();
    CSimpleIniA dst; dst.SetUnicode();
    // Monolith predating bHoldForBoost: nothing to relocate, no phantom key written.
    src.SetValue("Hardware", "iThrottleAxis", "0x32");
    ConfigMigration::SplitUserConfig(src, dst);

    CHECK(!HasKey(dst, "DualStick", "bHoldForBoost"));
    CHECK(dst.GetLongValue("Meta", "iConfigVersion", -1) == ConfigMigration::kConfigVersion);
}

int main() {
    std::printf("ConfigMigration tests\n");
    TestUserSectionsCopied();
    TestModSectionsExcluded();
    TestHoldForBoostRelocated();
    TestVersionStamped();
    TestHoldForBoostAbsentInSource();

    if (g_failures == 0) {
        std::printf("ALL PASS\n");
        return 0;
    }
    std::printf("%d FAILURE(S)\n", g_failures);
    return 1;
}
