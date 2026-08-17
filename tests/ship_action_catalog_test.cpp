#include "ShipActionCatalog.h"

#include <cstdio>
#include <string_view>

static int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("  FAIL [%s:%d] %s\n", __FILE__, __LINE__, #cond);     \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

static void TestCatalogCompleteness()
{
    std::printf("CatalogCompleteness\n");
    CHECK(kShipActionCatalog.size() == 23);
    for (std::size_t index = 0; index < kShipActionCatalog.size(); ++index) {
        const auto& definition = kShipActionCatalog[index];
        CHECK(!definition.actionId.empty());
        CHECK(!definition.displayLabel.empty());
        CHECK(!definition.sourceIniKey.empty());
        CHECK(!definition.legacyOutputIniKey.empty());
        CHECK(!definition.controlMapContext.empty());
        CHECK(!definition.controlMapAction.empty());
        CHECK(AllowsShipControlMethod(definition, definition.recommendedMethod));
        CHECK(FindShipAction(definition.actionId) == &definition);
        for (std::size_t other = index + 1; other < kShipActionCatalog.size(); ++other)
            CHECK(definition.actionId != kShipActionCatalog[other].actionId);
    }
    CHECK(FindShipAction("UnknownAction") == nullptr);
}

static void TestReviewedRecommendations()
{
    std::printf("ReviewedRecommendations\n");
    CHECK(FindShipAction("GetUp")->recommendedMethod == ShipControlMethod::Direct);
    CHECK(FindShipAction("UndockTakeOff")->recommendedMethod ==
        ShipControlMethod::KeyboardCompatibility);
    CHECK(FindShipAction("ExitShipFromCockpit")->recommendedMethod ==
        ShipControlMethod::KeyboardCompatibility);
    CHECK(FindShipAction("SelectTarget")->recommendedMethod == ShipControlMethod::Context);
    CHECK(FindShipAction("PreviousSystem")->recommendedMethod == ShipControlMethod::Context);
}

static void TestOverridePolicy()
{
    std::printf("OverridePolicy\n");
    const auto& boosters = *FindShipAction("FireBoosters");
    const auto defaultRoute = ResolveShipControlMethod(boosters, {});
    CHECK(defaultRoute.method == ShipControlMethod::Direct);
    CHECK(!defaultRoute.overridePresent);

    const auto compatibility = ResolveShipControlMethod(boosters, "keyboard");
    CHECK(compatibility.method == ShipControlMethod::KeyboardCompatibility);
    CHECK(compatibility.overridePresent);
    CHECK(compatibility.overrideAccepted);

    const auto invalid = ResolveShipControlMethod(boosters, "context");
    CHECK(invalid.method == ShipControlMethod::Direct);
    CHECK(invalid.overridePresent);
    CHECK(!invalid.overrideAccepted);

    const auto& select = *FindShipAction("SelectTarget");
    const auto forbiddenFallback = ResolveShipControlMethod(select, "keyboard");
    CHECK(forbiddenFallback.method == ShipControlMethod::Context);
    CHECK(!forbiddenFallback.overrideAccepted);

    // Runtime unavailability is diagnostic state only. It never changes the
    // selected Direct method into a hidden fallback.
    CHECK(ResolveShipActionAvailability(false, true, true) ==
        ShipActionAvailability::UnavailableForBuild);
    CHECK(defaultRoute.method == ShipControlMethod::Direct);
}

static void TestResolutionPrecedenceAndAvailability()
{
    std::printf("ResolutionPrecedenceAndAvailability\n");
    CHECK(ResolveKeyboardResolutionSource(false, false) ==
        KeyboardResolutionSource::VanillaFallback);
    CHECK(ResolveKeyboardResolutionSource(true, false) ==
        KeyboardResolutionSource::ControlMapCustom);
    CHECK(ResolveKeyboardResolutionSource(false, true) ==
        KeyboardResolutionSource::LegacyManualOverride);
    CHECK(ResolveKeyboardResolutionSource(true, true) ==
        KeyboardResolutionSource::LegacyManualOverride);

    CHECK(ResolveShipActionAvailability(true, true, false) ==
        ShipActionAvailability::SupportedWaitingForContext);
    CHECK(ResolveShipActionAvailability(true, true, true) ==
        ShipActionAvailability::AvailableNow);
    CHECK(ResolveShipActionAvailability(true, false, true) ==
        ShipActionAvailability::UnavailableInContext);
}

int main()
{
    TestCatalogCompleteness();
    TestReviewedRecommendations();
    TestOverridePolicy();
    TestResolutionPrecedenceAndAvailability();

    if (g_failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
