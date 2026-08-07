#include "MenuControlReuse.h"

#include <cstdio>

static int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("  FAIL [%s:%d] %s\n", __FILE__, __LINE__, #cond);     \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

static void CheckAxisNeutralArmingAndHysteresis()
{
    MenuControlReuse::AxisState axis;

    // Entering a menu with the stick deflected must not navigate.
    CHECK(MenuControlReuse::UpdateAxis(axis, true, true, true, -0.9f, 0.55f, 0.35f) == 0);
    CHECK(!axis.neutralArmed);
    CHECK(MenuControlReuse::UpdateAxis(axis, true, true, true, 0.1f, 0.55f, 0.35f) == 0);
    CHECK(axis.neutralArmed);

    CHECK(MenuControlReuse::UpdateAxis(axis, true, true, true, -0.6f, 0.55f, 0.35f) == -1);
    CHECK(MenuControlReuse::UpdateAxis(axis, true, true, true, -0.4f, 0.55f, 0.35f) == -1);
    CHECK(MenuControlReuse::UpdateAxis(axis, true, true, true, -0.2f, 0.55f, 0.35f) == 0);
    CHECK(MenuControlReuse::UpdateAxis(axis, true, true, true, 0.7f, 0.55f, 0.35f) == 1);

    CHECK(MenuControlReuse::UpdateAxis(axis, false, true, true, 0.7f, 0.55f, 0.35f) == 0);
    CHECK(!axis.contextActive);
}

static void CheckButtonReleaseArming()
{
    MenuControlReuse::ButtonState button;

    // A trigger held while the menu opens is consumed until released.
    CHECK(!MenuControlReuse::UpdateButton(button, true, true, true, true));
    CHECK(!button.releasedArmed);
    CHECK(!MenuControlReuse::UpdateButton(button, true, true, true, false));
    CHECK(button.releasedArmed);
    CHECK(MenuControlReuse::UpdateButton(button, true, true, true, true));
    CHECK(!MenuControlReuse::UpdateButton(button, false, true, true, true));
    CHECK(!button.contextActive);
}

int main()
{
    CheckAxisNeutralArmingAndHysteresis();
    CheckButtonReleaseArming();

    if (g_failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
