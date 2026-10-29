#include "MacroEngine.h"
#include "DeviceManager.h"
#include "InputRecoveryPolicy.h"
#include "TestCheck.h"

#include <array>
#include <unordered_set>

namespace {
std::array<bool, 2> connected{true, true};
std::array<bool, 2> pressed{true, true};
std::array<InputRecoveryPolicy::Button, 2> buttons;
std::unordered_set<std::uint32_t> heldOwners;
DIJOYSTATE2 sample{};
}

namespace RuntimePaths { void Log(const char*, const std::string&) {} }
const DIJOYSTATE2* DeviceManager::GetCachedState(int index) {
    if (index < 0 || index >= 2) return nullptr;
    if (!connected[index]) { buttons[index].Lost(); return nullptr; }
    return &sample;
}
bool DeviceManager::IsButtonPressed(const BindingRef& ref) {
    return GetCachedState(ref.deviceIndex) && buttons[ref.deviceIndex].Observe(pressed[ref.deviceIndex]);
}
namespace ShipOutputSystem {
ShipControlTarget ResolveControlTarget(std::string_view) { return {}; }
void SetControlTargetHeld(const ShipControlTarget&, std::uint32_t owner, bool held) {
    if (held) heldOwners.insert(owner);
    else heldOwners.erase(owner);
}
void ReleaseOwnerOutputs(std::uint32_t owner) { heldOwners.erase(owner); }
}

int main()
{
    ShipControlTarget target;
    target.kind = ShipControlTargetKind::RawOutput;
    target.output = SpaceOutput;
    Macro first;
    first.name = "Long hold";
    first.button = {"", 1, 0};
    first.steps = {{{target}, MacroAction::Hold, 10000, 0}};
    Macro second = first;
    second.button.deviceIndex = 1;
    MacroEngine::RestoreMacros({first, second});
    MacroEngine::Update();
    REQUIRE(heldOwners.size() == 2);
    pressed[0] = false;
    MacroEngine::Update();
    REQUIRE(heldOwners.size() == 2); // Normal release preserves a plain sequence.
    connected[0] = false;
    MacroEngine::Update();
    REQUIRE(!heldOwners.contains(OwnerMacroBase));
    REQUIRE(heldOwners.contains(OwnerMacroBase + 1)); // Other hardware continues.
    connected[0] = true;
    pressed[0] = true;
    MacroEngine::Update();
    REQUIRE(!heldOwners.contains(OwnerMacroBase)); // Held reconnect cannot restart.
    pressed[0] = false;
    MacroEngine::Update();
    pressed[0] = true;
    MacroEngine::Update();
    REQUIRE(heldOwners.contains(OwnerMacroBase));
    MacroEngine::ReleaseAll();
    REQUIRE(heldOwners.empty());
}
