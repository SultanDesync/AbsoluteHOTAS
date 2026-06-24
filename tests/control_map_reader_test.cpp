// Unit test for ControlMapReader. Built via the CMake/CTest target (MSVC), not
// a standalone compiler. Enable with -DABSOLUTEHOTAS_BUILD_TESTS=ON:
//   cmake --preset build-release-user -DABSOLUTEHOTAS_BUILD_TESTS=ON
//   cmake --build --preset release-user --target control_map_reader_test
//   ctest --test-dir build/release --output-on-failure
//
// Fixtures (real captured files, passed as argv[1]):
//   control_map_default.bin       9-byte vanilla baseline (3 empty sections)
//   control_map_configurator.bin  680-byte legacy configurator output (alt records)

#include "ControlMapReader.h"

#include <cstdio>
#include <string>

using namespace ControlMap;

static int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("  FAIL [%s:%d] %s\n", __FILE__, __LINE__, #cond);     \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

static Record MakeRecord(const char* ctx, const char* act, uint16_t token, uint8_t flag) {
    Record r;
    r.context = ctx;
    r.action  = act;
    r.device  = 1;
    r.token   = token;
    r.flag    = flag;
    return r;
}

static void TestParseDefaultBaseline(const std::string& dir) {
    std::printf("ParseDefaultBaseline\n");
    ControlMapFile f = ReadFile(dir + "/control_map_default.bin");
    CHECK(f.valid);
    CHECK(f.sections.size() == 3);
    for (const auto& s : f.sections) CHECK(s.empty());
    CHECK(f.Section0().empty());
}

static void TestParseConfiguratorFixture(const std::string& dir) {
    std::printf("ParseConfiguratorFixture\n");
    ControlMapFile f = ReadFile(dir + "/control_map_configurator.bin");
    CHECK(f.valid);
    CHECK(!f.sections.empty());
    const auto& s0 = f.Section0();
    CHECK(s0.size() == 23);

    // Every configurator record is a keyboard (device 1) alternate slot.
    for (const auto& r : s0) {
        CHECK(r.device == 1);
        CHECK(!r.IsPrimary());      // configurator wrote alternates (flag 0x02)
        CHECK(!r.IsUnbound());
    }

    // Spot-check a known record round-trips: ShipHUD/Boosters -> token 0x01DB -> '['.
    const Record* boosters = nullptr;
    for (const auto& r : s0)
        if (r.context == "ShipHUD" && r.action == "Boosters") boosters = &r;
    CHECK(boosters != nullptr);
    if (boosters) {
        CHECK(boosters->token == 0x01DB);
        const Output o = TokenToOutput(boosters->token);
        CHECK(o.kind == OutputKind::Keyboard);   // 0x01DB low byte = VK_OEM_4 ('['); the exact
        CHECK(!o.extended);                       // scancode is keyboard-layout dependent, so not asserted
    }
}

// Regression for the cross-section bug: a function's primary can live in a
// different section (mouse §1) than its alternate (keyboard §0). DeviceRecords
// must aggregate both so resolution sees the unbound primary and follows the alt.
static void TestCrossSectionResolution() {
    std::printf("CrossSectionResolution\n");
    ControlMapFile f;
    f.sections.push_back({ MakeRecord("ShipHUD", "WeaponGroup1", 0x004E, 0x02) });        // §0: alt = N
    f.sections.push_back({ MakeRecord("ShipHUD", "WeaponGroup1", kUnboundToken, 0x00) });  // §1: primary unbound
    const std::vector<Record> recs = f.DeviceRecords();
    CHECK(recs.size() == 2);

    const Output vanilla{ OutputKind::Mouse, 1, false };  // Fire Weapon 0 vanilla = Mouse1
    const Output o = ResolveBinding(recs, "ShipHUD", "WeaponGroup1", vanilla);
    CHECK(o.kind == OutputKind::Keyboard && o.code == 0x31);  // VK_N -> N scancode
}

static void TestTokenToOutput() {
    std::printf("TokenToOutput\n");
    // Keyboard, non-extended
    CHECK(TokenToOutput(0x00A0).kind == OutputKind::Keyboard);  // L Shift
    CHECK(TokenToOutput(0x00A0).code == 0x2A);
    CHECK(!TokenToOutput(0x00A0).extended);
    CHECK(TokenToOutput(0x0156).code == 0x2F);                  // V
    // Keyboard, extended (arrows)
    CHECK(TokenToOutput(0x0026).code == 0x48 && TokenToOutput(0x0026).extended);  // Up
    CHECK(TokenToOutput(0x0025).code == 0x4B && TokenToOutput(0x0025).extended);  // Left
    CHECK(TokenToOutput(0x0027).code == 0x4D && TokenToOutput(0x0027).extended);  // Right
    CHECK(TokenToOutput(0x0028).code == 0x50 && TokenToOutput(0x0028).extended);  // Down
    // Mouse
    CHECK(TokenToOutput(0x0000).kind == OutputKind::Mouse && TokenToOutput(0x0000).code == 1);
    CHECK(TokenToOutput(0x0003).kind == OutputKind::Mouse && TokenToOutput(0x0003).code == 4);
    // Real in-game tokens are VK codes (0x00 | VK). Regression for the bug where
    // the configurator's 0x01xx table missed them (game wrote 0x004B / 0x00BF).
    CHECK(TokenToOutput(0x004B).kind == OutputKind::Keyboard);                 // VK_K (Cruise->K)
    CHECK(TokenToOutput(0x004B).code == 0x25 && !TokenToOutput(0x004B).extended);
    CHECK(TokenToOutput(0x00BF).kind == OutputKind::Keyboard);                 // VK_OEM_2 ('/')
    // Unbound + unknown
    CHECK(TokenToOutput(kUnboundToken).kind == OutputKind::None);
    CHECK(TokenToOutput(0x0999).kind == OutputKind::None);
}

static void TestResolveBinding() {
    std::printf("ResolveBinding\n");
    const Output vanilla{ OutputKind::Keyboard, 0x2A, false };  // pretend default = L Shift

    // No override record for the function → vanilla primary intact.
    {
        std::vector<Record> s0;
        const Output o = ResolveBinding(s0, "ShipHUD", "Boosters", vanilla);
        CHECK(o.code == 0x2A && o.kind == OutputKind::Keyboard);
    }
    // Bound primary override (rebound to J) → follow it.
    {
        std::vector<Record> s0{ MakeRecord("ShipHUD", "Boosters", 0x014A, 0x00) };  // J
        const Output o = ResolveBinding(s0, "ShipHUD", "Boosters", vanilla);
        CHECK(o.kind == OutputKind::Keyboard && o.code == 0x24);                     // J scancode
    }
    // Primary unbound, bound alternate present → follow the alternate.
    {
        std::vector<Record> s0{
            MakeRecord("ShipHUD", "Boosters", kUnboundToken, 0x00),
            MakeRecord("ShipHUD", "Boosters", 0x01DB, 0x02),  // '['
        };
        const Output o = ResolveBinding(s0, "ShipHUD", "Boosters", vanilla);
        CHECK(o.kind == OutputKind::Keyboard && o.code == 0x1A);
    }
    // Primary unbound, no usable alternate → None (user cleared it).
    {
        std::vector<Record> s0{ MakeRecord("ShipHUD", "Boosters", kUnboundToken, 0x00) };
        const Output o = ResolveBinding(s0, "ShipHUD", "Boosters", vanilla);
        CHECK(o.kind == OutputKind::None);
    }
    // Alternate-only override (no primary record) → vanilla primary still wins.
    {
        std::vector<Record> s0{ MakeRecord("ShipHUD", "Boosters", 0x01DB, 0x02) };
        const Output o = ResolveBinding(s0, "ShipHUD", "Boosters", vanilla);
        CHECK(o.code == 0x2A);
    }
    // The Cancel collision: same action, different context must not cross over.
    {
        std::vector<Record> s0{ MakeRecord("Spaceship_Interaction", "Cancel", 0x014A, 0x00) };  // Get Up -> J
        const Output getUp  = ResolveBinding(s0, "Spaceship_Interaction", "Cancel", vanilla);
        const Output cancel = ResolveBinding(s0, "ShipHUD_Cancel",        "Cancel", vanilla);
        CHECK(getUp.code == 0x24);    // followed the Get Up override
        CHECK(cancel.code == 0x2A);   // Cancel untouched → vanilla
    }
}

static void TestMalformedInput() {
    std::printf("MalformedInput\n");
    // Empty input: valid, no sections, no overrides.
    {
        ControlMapFile f = Parse(nullptr, 0);
        CHECK(f.valid);
        CHECK(f.sections.empty());
        CHECK(f.Section0().empty());
    }
    // Bad section marker.
    {
        const uint8_t bytes[] = { 0x04, 0x00, 0x03 };
        CHECK(!Parse(bytes, sizeof(bytes)).valid);
    }
    // Length runs past the buffer.
    {
        const uint8_t bytes[] = { 0x03, 0x00, 0xFF };
        CHECK(!Parse(bytes, sizeof(bytes)).valid);
    }
    // Truncated payload (header + partial record).
    {
        const uint8_t bytes[] = { 0x03, 0x00, 0x09, 'A', 0x00, 'B', 0x00, 0x01, 0x00 };
        CHECK(!Parse(bytes, sizeof(bytes)).valid);
    }
}

int main(int argc, char** argv) {
    const std::string dir = (argc > 1) ? argv[1] : "tests/fixtures";
    std::printf("ControlMapReader tests (fixtures: %s)\n", dir.c_str());

    TestParseDefaultBaseline(dir);
    TestParseConfiguratorFixture(dir);
    TestTokenToOutput();
    TestResolveBinding();
    TestCrossSectionResolution();
    TestMalformedInput();

    if (g_failures == 0) {
        std::printf("\nALL TESTS PASSED\n");
        return 0;
    }
    std::printf("\n%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
