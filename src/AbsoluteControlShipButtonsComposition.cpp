#include "AbsoluteControlShipButtonsComposition.h"

#include "HotasBindingCatalog.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace Composition = AbsoluteControlCompositionExperimental;

template <std::size_t Size>
void Copy(char (&target)[Size], std::string_view source) noexcept
{
    const auto count = (std::min)(source.size(), Size - 1);
    std::memcpy(target, source.data(), count);
    target[count] = '\0';
}

Composition::NodeDescriptorV1 Node(
    Composition::NodeKind kind, std::string_view id,
    std::string_view parent = {}, std::string_view reference = {},
    std::string_view label = {}, std::string_view description = {},
    Composition::SemanticRole role = Composition::SemanticRole::Default,
    std::uint32_t flags = Composition::kNodeNone) noexcept
{
    Composition::NodeDescriptorV1 node;
    node.kind = kind;
    node.role = role;
    node.flags = flags;
    Copy(node.nodeId, id);
    Copy(node.parentNodeId, parent);
    Copy(node.referenceId, reference);
    Copy(node.label, label);
    Copy(node.description, description);
    return node;
}

void AddControl(std::vector<Composition::NodeDescriptorV1>& nodes,
                std::string_view parent, std::string_view control,
                Composition::SemanticRole role)
{
    nodes.push_back(Node(Composition::NodeKind::ControlSlot,
        std::string{parent} + "-c" + std::to_string(nodes.size()), parent,
        control, {}, {}, role));
}

void AddRow(std::vector<Composition::NodeDescriptorV1>& nodes,
            std::string_view id, std::string_view parent,
            std::initializer_list<std::pair<std::string_view,
                Composition::SemanticRole>> controls)
{
    nodes.push_back(Node(Composition::NodeKind::Row, id, parent));
    for (const auto& [control, role] : controls) {
        AddControl(nodes, id, control, role);
    }
}

const HotasBindingCatalog::Target* ShipTarget(std::string_view actionId)
{
    for (const auto& target : HotasBindingCatalog::kTargets) {
        if (target.family == HotasBindingCatalog::TargetFamily::ShipAction &&
            target.actionId == actionId) return &target;
    }
    return nullptr;
}

void AddShipAction(std::vector<Composition::NodeDescriptorV1>& nodes,
                   std::string_view section, std::string_view actionId,
                   bool outputChoice)
{
    const auto* target = ShipTarget(actionId);
    if (!target) return;
    if (outputChoice) {
        const auto rowId = "ship-action-row-" + std::to_string(nodes.size());
        const auto routeId = std::string{target->controlId} + "-route";
        AddRow(nodes, rowId, section, {
            {target->controlId, Composition::SemanticRole::Binding},
            {routeId, Composition::SemanticRole::Secondary},
        });
    } else {
        AddControl(nodes, section, target->controlId,
                   Composition::SemanticRole::Binding);
    }
}

void AddSection(std::vector<Composition::NodeDescriptorV1>& nodes,
                std::string_view id, std::string_view label,
                std::string_view description)
{
    nodes.push_back(Node(Composition::NodeKind::Section, id,
        "ship-bindings-root", {}, label, description));
}

std::vector<Composition::NodeDescriptorV1> BuildNodes()
{
    std::vector<Composition::NodeDescriptorV1> nodes;
    nodes.reserve(96);
    nodes.push_back(Node(Composition::NodeKind::Root,
        "ship-bindings-root"));

    AddSection(nodes, "axis-binding-section", "FLIGHT AXES",
        "Bind the seven analog HOTAS lanes here. Response graphs and inversion remain on Flight Axes.");
    for (std::size_t index = 0; index < kNumAxisSlots; ++index) {
        AddControl(nodes, "axis-binding-section",
            HotasBindingCatalog::kAxisControlIds[index],
            Composition::SemanticRole::Binding);
    }

    AddSection(nodes, "primary-binding-section", "PRIMARY FLIGHT & COMBAT",
        "The controller binding and its dispatch method share one row.");
    for (const auto actionId : {
             "FireWeapon0", "FireWeapon1", "FireWeapon2", "FireBoosters",
             "SwitchFlightModes", "ShipAction1", "OpenScanner", "Repair",
             "ShipAlternateControlHold", "Cruise", "AutopilotOnOff" }) {
        AddShipAction(nodes, "primary-binding-section", actionId, true);
        if (actionId == std::string_view{"FireBoosters"}) {
            AddControl(nodes, "primary-binding-section",
                "boost-throttle-authority", Composition::SemanticRole::Secondary);
        }
    }

    AddSection(nodes, "hotas-function-section",
        "ABSOLUTEHOTAS THROTTLE FUNCTIONS",
        "Plugin-owned functions operate directly on HOTAS state and never emit a keypress.");
    for (const auto& target : HotasBindingCatalog::kTargets) {
        if (target.pageId == HotasBindingCatalog::kShipButtonsPageId &&
            (target.family == HotasBindingCatalog::TargetFamily::FlightAssist ||
             target.family == HotasBindingCatalog::TargetFamily::TurnAssist)) {
            AddControl(nodes, "hotas-function-section", target.controlId,
                       Composition::SemanticRole::Binding);
        }
    }

    AddSection(nodes, "context-binding-section", "CONTEXT & NAVIGATION",
        "These bindings automatically follow menu, targeting, and cockpit context.");
    for (const auto actionId : {
             "SelectTarget", "IncreaseSystemPower", "DecreaseSystemPower",
             "PreviousSystem", "NextSystem", "Cancel" }) {
        AddShipAction(nodes, "context-binding-section", actionId, false);
    }

    AddSection(nodes, "camera-binding-section", "CAMERA & COCKPIT",
        "Camera, docking, and cockpit bindings with selectable native or compatibility output where supported.");
    for (const auto actionId : {
             "TogglePov", "ZoomCameraIn", "ZoomCameraOut", "UndockTakeOff",
             "GetUp", "ExitShipFromCockpit" }) {
        AddShipAction(nodes, "camera-binding-section", actionId, true);
    }

    AddSection(nodes, "menu-reuse-section", "MENU CONTROL REUSE",
        "Reuse flight controls for menu navigation with neutral arming and hysteresis.");
    for (const auto control : {
             "menu-use-pitch", "menu-use-yaw", "menu-use-primary-weapon",
             "menu-invert-vertical", "menu-invert-horizontal",
             "menu-engage-threshold", "menu-release-threshold" }) {
        AddControl(nodes, "menu-reuse-section", control,
                   Composition::SemanticRole::Secondary);
    }

    AddSection(nodes, "shortcut-section", "CUSTOM KEYBOARD & MOUSE SHORTCUTS",
        "Raw compatibility outputs and UI shortcuts; chords and sequences belong on Macros.");
    for (const auto control : {
             "shortcut-records", "shortcut-trigger", "shortcut-output",
             "shortcut-add", "shortcut-delete", "shortcut-menu-preset",
             "shortcut-macro-link", "shortcut-status" }) {
        AddControl(nodes, "shortcut-section", control,
                   Composition::SemanticRole::Secondary);
    }
    return nodes;
}

const auto g_nodes = BuildNodes();
} // namespace

namespace AbsoluteControlShipButtonsComposition {

Composition::PageCompositionDescriptorV1 Descriptor() noexcept
{
    Composition::PageCompositionDescriptorV1 descriptor;
    Copy(descriptor.moduleId, "absolute.hotas");
    Copy(descriptor.pageId, "hotas-ship-buttons");
    descriptor.nodeCount = static_cast<std::uint32_t>(g_nodes.size());
    descriptor.nodes = g_nodes.data();
    return descriptor;
}

std::span<const Composition::NodeDescriptorV1> Nodes() noexcept
{
    return g_nodes;
}

} // namespace AbsoluteControlShipButtonsComposition
