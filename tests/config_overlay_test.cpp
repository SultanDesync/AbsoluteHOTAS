// Unit test for ProfileOverlay::ComputeDiff — the sparse-diff that turns a profile's
// effective config + base into a minimal overlay. Built via the opt-in
// `config_overlay_test` xmake target:
//   xmake test config_overlay_test/*
//
// ComputeDiff needs only SimpleIni, so the target compiles this file plus
// src/ProfileOverlay.cpp with no PCH, independent of the plugin target.

#include "ProfileOverlay.h"

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

static bool Has(const CSimpleIniA& ini, const char* s, const char* k) {
    return ini.GetValue(s, k, nullptr) != nullptr;
}
static const char* Val(const CSimpleIniA& ini, const char* s, const char* k) {
    const char* v = ini.GetValue(s, k, nullptr);
    return v ? v : "";
}

// A base config with a couple of representative sections.
static void MakeBase(CSimpleIniA& base) {
    base.SetValue("Hardware", "iThrottleAxis", "Throttle@50");
    base.SetValue("Hardware", "fPitchSensitivity", "1.00");
    base.SetValue("Injection", "bEnableInjection", "true");
}

static void TestOnlyDifferencesWritten() {
    std::printf("OnlyDifferencesWritten\n");
    CSimpleIniA base; base.SetUnicode(false); MakeBase(base);
    CSimpleIniA eff;  eff.SetUnicode(false);  MakeBase(eff);
    // One override: parked profile disables injection, everything else identical.
    eff.SetValue("Injection", "bEnableInjection", "false");

    CSimpleIniA out; out.SetUnicode(false);
    const int n = ProfileOverlay::ComputeDiff(eff, base, out);

    CHECK(n == 1);
    CHECK(std::strcmp(Val(out, "Injection", "bEnableInjection"), "false") == 0);
    CHECK(!Has(out, "Hardware", "iThrottleAxis"));     // identical -> not written
    CHECK(!Has(out, "Hardware", "fPitchSensitivity")); // identical -> not written
}

static void TestIdenticalProducesEmptyOverlay() {
    std::printf("IdenticalProducesEmptyOverlay\n");
    CSimpleIniA base; base.SetUnicode(false); MakeBase(base);
    CSimpleIniA eff;  eff.SetUnicode(false);  MakeBase(eff);

    CSimpleIniA out; out.SetUnicode(false);
    const int n = ProfileOverlay::ComputeDiff(eff, base, out);

    CHECK(n == 0);  // an aux profile identical to base overrides nothing
    CHECK(!Has(out, "Injection", "bEnableInjection"));
}

static void TestRevertRemovesStaleOverride() {
    std::printf("RevertRemovesStaleOverride\n");
    CSimpleIniA base; base.SetUnicode(false); MakeBase(base);
    CSimpleIniA eff;  eff.SetUnicode(false);  MakeBase(eff);  // user reverted to base value

    // The overlay file already carries a prior override.
    CSimpleIniA out; out.SetUnicode(false);
    out.SetValue("Hardware", "fPitchSensitivity", "0.40");

    const int n = ProfileOverlay::ComputeDiff(eff, base, out);

    CHECK(n == 0);
    // Reverting the value to base must drop it from the overlay, not leave it stale.
    CHECK(!Has(out, "Hardware", "fPitchSensitivity"));
}

static void TestForeignSectionsSurvive() {
    std::printf("ForeignSectionsSurvive\n");
    // ComputeDiff only visits keys present in `eff` (the serialized user-owned config).
    // Sections it never serializes — e.g. a [Profile] header or [Macro:*] a profile
    // carries — must pass through the diff untouched, not be scrubbed.
    CSimpleIniA base; base.SetUnicode(false); MakeBase(base);
    CSimpleIniA eff;  eff.SetUnicode(false);  MakeBase(eff);
    eff.SetValue("Injection", "bEnableInjection", "false");

    CSimpleIniA out; out.SetUnicode(false);
    out.SetValue("Macro:OnFoot", "iButton", "Stick@9");  // profile-carried macro

    ProfileOverlay::ComputeDiff(eff, base, out);

    CHECK(std::strcmp(Val(out, "Injection", "bEnableInjection"), "false") == 0);
    CHECK(std::strcmp(Val(out, "Macro:OnFoot", "iButton"), "Stick@9") == 0);  // untouched section survives
}

int main() {
    std::printf("ProfileOverlay tests\n");
    TestOnlyDifferencesWritten();
    TestIdenticalProducesEmptyOverlay();
    TestRevertRemovesStaleOverride();
    TestForeignSectionsSurvive();

    if (g_failures == 0) { std::printf("ALL PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", g_failures);
    return 1;
}
