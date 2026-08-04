#include "BindingRef.h"

#include <cstdio>
#include <string>

static int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("  FAIL [%s:%d] %s\n", __FILE__, __LINE__, #cond);     \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

static void CheckMissingAndCleared() {
    std::printf("MissingAndCleared\n");
    CHECK(ParseBindingRef(nullptr, 0x32).value == 0x32);
    CHECK(ParseBindingRef("", 0x32).value == -1);
    CHECK(ParseBindingRef("   ", 0x32).value == -1);
    CHECK(ParseBindingRef("-1", 0x32).value == -1);
}

static void CheckValidForms() {
    std::printf("ValidForms\n");
    {
        const BindingRef ref = ParseBindingRef("  0x32  ", -1);
        CHECK(ref.value == 0x32);
        CHECK(!ref.HasDevice());
        CHECK(!ref.HasIndex());
    }
    {
        const BindingRef ref = ParseBindingRef(" VKB Gladiator EVO R @ 17 ", -1);
        CHECK(ref.value == 17);
        CHECK(ref.deviceName == "VKB Gladiator EVO R");
        CHECK(!ref.HasIndex());
    }
    {
        const BindingRef ref = ParseBindingRef("#2@0x34", -1);
        CHECK(ref.value == 0x34);
        CHECK(ref.deviceName.empty());
        CHECK(ref.deviceIndex == 2);
    }
}

static void CheckMalformedForms() {
    std::printf("MalformedForms\n");
    const char* malformed[] = {
        "42garbage",
        "Device@17garbage",
        "Device@",
        "@17",
        "#2garbage@17",
        "#-1@17",
        "#2@17garbage",
        "#2",
        "999999999999999999999999",
    };
    for (const char* text : malformed) {
        const BindingRef ref = ParseBindingRef(text, 0x32);
        CHECK(ref.value == -1);
        CHECK(!ref.HasDevice());
        CHECK(!ref.HasIndex());
    }
}

static void CheckFormatting() {
    std::printf("Formatting\n");
    CHECK(FormatBindingRef({ "", 0x32, -1 }, true) == "0x32");
    CHECK(FormatBindingRef({ "", 5, 1 }, false) == "#1@5");
    CHECK(FormatBindingRef({ "Throttle", 0x36, -1 }, true) == "Throttle@0x36");
    CHECK(FormatBindingRef({ "", -1, -1 }, false) == "(unbound)");
}

int main() {
    CheckMissingAndCleared();
    CheckValidForms();
    CheckMalformedForms();
    CheckFormatting();

    if (g_failures == 0) {
        std::printf("\nALL TESTS PASSED\n");
        return 0;
    }
    std::printf("\n%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
