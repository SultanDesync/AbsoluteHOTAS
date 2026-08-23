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
                   std::string_view section, std::string_view actionId)
{
    const auto* target = ShipTarget(actionId);
    if (!target) return;
    const auto rowId = "ship-action-row-" + std::to_string(nodes.size());
    const auto routeId = std::string{target->controlId} + "-route";
    AddRow(nodes, rowId, section, {
        {target->controlId, Composition::SemanticRole::Binding},
        {routeId, Composition::SemanticRole::Secondary},
    });
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
    nodes.reserve(100);
    nodes.push_back(Node(Composition::NodeKind::Root,
        "ship-bindings-root"));

    AddSection(nodes, "native-binding-section", "NATIVE SHIP CONTROLS",
        "Complete controller equivalents for Starfield's native ship-button list, in native menu order. Analog flight lanes remain on Flight Axes.");
    for (const auto actionId : kNativeShipButtonActions) {
        AddShipAction(nodes, "native-binding-section", actionId);
        if (actionId == std::string_view{"FireBoosters"}) {
            AddControl(nodes, "native-binding-section",
                "boost-throttle-authority", Composition::SemanticRole::Secondary);
        }
    }

    AddSection(nodes, "hotas-function-section",
        "ABSOLUTEHOTAS HOTKEYS",
        "Plugin-owned throttle and turn-assist functions operate directly on HOTAS state and never emit a keypress.");
    for (const auto& target : HotasBindingCatalog::kTargets) {
        if (target.pageId == HotasBindingCatalog::kShipButtonsPageId &&
            (target.family == HotasBindingCatalog::TargetFamily::FlightAssist ||
             target.family == HotasBindingCatalog::TargetFamily::TurnAssist)) {
            AddControl(nodes, "hotas-function-section", target.controlId,
                       Composition::SemanticRole::Binding);
        }
    }
    AddSection(nodes, "menu-navigation-section", "OPTIONAL MENU NAVIGATION",
        "Dedicated controller bindings for ordinary menus. They are independent of native ship controls, release-arm on menu entry, and remain unbound by default.");
    for (const auto& target : HotasBindingCatalog::kTargets) {
        if (target.family == HotasBindingCatalog::TargetFamily::MenuNavigation) {
            AddControl(nodes, "menu-navigation-section", target.controlId,
                       Composition::SemanticRole::Binding);
        }
    }
    for (const auto control : {
             "menu-use-pitch", "menu-use-yaw", "menu-use-primary-weapon",
             "menu-invert-vertical", "menu-invert-horizontal",
             "menu-engage-threshold", "menu-release-threshold" }) {
        AddControl(nodes, "menu-navigation-section", control,
                   Composition::SemanticRole::Secondary);
    }

    AddSection(nodes, "shortcut-section", "CUSTOM SENDINPUT BINDINGS",
        "Arbitrary keyboard and mouse outputs; chords and sequences belong on Macros.");
    for (const auto control : {
             "shortcut-records", "shortcut-trigger", "shortcut-output",
             "shortcut-add", "shortcut-delete",
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
