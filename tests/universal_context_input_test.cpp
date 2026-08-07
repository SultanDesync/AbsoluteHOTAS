#include "UniversalContextInput.h"

#include <cstdio>

static int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("  FAIL [%s:%d] %s\n", __FILE__, __LINE__, #cond);     \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

static void CheckMapping(std::string_view actionId, std::uint16_t scanCode,
                         bool extended, bool targetingSelector = false)
{
    const auto* mapping = UniversalContextInput::Find(actionId);
    CHECK(mapping != nullptr);
    if (!mapping) return;
    CHECK(mapping->scanCode == scanCode);
    CHECK(mapping->extended == extended);
    CHECK(mapping->targetingSelector == targetingSelector);
}

int main()
{
    CheckMapping("SelectTarget", 0x12, false);
    CheckMapping("IncreaseSystemPower", 0x48, true);
    CheckMapping("DecreaseSystemPower", 0x50, true);
    CheckMapping("PreviousSystem", 0x4B, true, true);
    CheckMapping("NextSystem", 0x4D, true, true);
    CheckMapping("Cancel", 0x01, false);
    CHECK(UniversalContextInput::Find("FireBoosters") == nullptr);
    CHECK(UniversalContextInput::Find("selecttarget") == nullptr);

    const auto normalLeft = UniversalContextInput::ResolveRoute("PreviousSystem", false);
    CHECK(normalLeft.vanillaKey);
    CHECK(!normalLeft.targetingSelector);
    const auto targetingLeft = UniversalContextInput::ResolveRoute("PreviousSystem", true);
    CHECK(!targetingLeft.vanillaKey);
    CHECK(targetingLeft.targetingSelector);
    const auto targetingUp = UniversalContextInput::ResolveRoute("IncreaseSystemPower", true);
    CHECK(targetingUp.vanillaKey);
    CHECK(!targetingUp.targetingSelector);

    if (g_failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
