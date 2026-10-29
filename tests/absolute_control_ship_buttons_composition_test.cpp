#include "AbsoluteControlShipButtonsComposition.h"

#include "HotasBindingCatalog.h"

#include "TestCheck.h"
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

int main()
{
    namespace Composition = AbsoluteControlCompositionExperimental;
    const auto nodes = AbsoluteControlShipButtonsComposition::Nodes();
    REQUIRE(nodes.size() == 100);
    REQUIRE(nodes.size() <= Composition::kMaximumNodesPerPage);

    std::unordered_map<std::string_view,
        const Composition::NodeDescriptorV1*> byId;
    std::unordered_map<std::string_view,
        const Composition::NodeDescriptorV1*> byControl;
    std::size_t sections{};
    std::size_t rows{};
    for (const auto& node : nodes) {
        const auto [idIt, idInserted] = byId.emplace(node.nodeId, &node);
        (void)idIt;
        REQUIRE(idInserted);
        if (node.parentNodeId[0] != '\0') {
            REQUIRE(byId.contains(node.parentNodeId));
        }
        if (node.kind == Composition::NodeKind::Section) ++sections;
        if (node.kind == Composition::NodeKind::Row) ++rows;
        if (node.kind == Composition::NodeKind::ControlSlot) {
            const auto [controlIt, controlInserted] =
                byControl.emplace(node.referenceId, &node);
            (void)controlIt;
            REQUIRE(controlInserted);
        }
    }
    REQUIRE(sections == 4);
    REQUIRE(rows == 23);
    REQUIRE(byControl.size() == 72);
    REQUIRE(std::string_view{nodes[1].nodeId} == "native-binding-section");
    REQUIRE(std::string_view{nodes[1].label} == "NATIVE SHIP CONTROLS");

    for (const auto& target : HotasBindingCatalog::kTargets) {
        if (target.family == HotasBindingCatalog::TargetFamily::CoreAxis ||
            target.family == HotasBindingCatalog::TargetFamily::ReverseAxis) {
            REQUIRE(target.pageId == HotasBindingCatalog::kFlightAxesPageId);
            REQUIRE(!byControl.contains(target.controlId));
        }
        if (target.family != HotasBindingCatalog::TargetFamily::ShipAction) {
            if (target.family == HotasBindingCatalog::TargetFamily::MenuNavigation) {
                const auto* node = byControl.at(target.controlId);
                REQUIRE(std::string_view{node->parentNodeId} ==
                       "menu-navigation-section");
            }
            continue;
        }
        const auto* action = FindShipAction(target.actionId);
        REQUIRE(action && byControl.contains(target.controlId));
        const auto routeId = std::string{target.controlId} + "-route";
        REQUIRE(byControl.contains(routeId));
        const auto* bindingNode = byControl.at(target.controlId);
        const auto* routeNode = byControl.at(routeId);
        REQUIRE(std::string_view{bindingNode->parentNodeId} ==
               routeNode->parentNodeId);
        const auto parent = byId.at(bindingNode->parentNodeId);
        REQUIRE(parent->kind == Composition::NodeKind::Row);
    }

    for (const auto actionId : kNativeShipButtonActions) {
        const auto* target = HotasBindingCatalog::kTargets.data();
        while (target != HotasBindingCatalog::kTargets.data() +
                             HotasBindingCatalog::kTargets.size() &&
               target->actionId != actionId) ++target;
        REQUIRE(target != HotasBindingCatalog::kTargets.data() +
                         HotasBindingCatalog::kTargets.size());
        const auto* node = byControl.at(target->controlId);
        const auto* parent = byId.at(node->parentNodeId);
        const auto section = parent->kind == Composition::NodeKind::Row
            ? std::string_view{parent->parentNodeId}
            : std::string_view{node->parentNodeId};
        REQUIRE(section == "native-binding-section");
    }

    REQUIRE(!byControl.contains("shortcut-menu-preset"));

    const auto descriptor =
        AbsoluteControlShipButtonsComposition::Descriptor();
    REQUIRE(descriptor.nodeCount == nodes.size());
    REQUIRE(descriptor.associationCount == 0);
    REQUIRE(std::string_view{descriptor.moduleId} == "absolute.hotas");
    REQUIRE(std::string_view{descriptor.pageId} == "hotas-ship-buttons");
    return 0;
}
