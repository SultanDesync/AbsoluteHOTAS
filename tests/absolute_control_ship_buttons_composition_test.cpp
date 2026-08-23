#include "AbsoluteControlShipButtonsComposition.h"

#include "HotasBindingCatalog.h"

#include <cassert>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

int main()
{
    namespace Composition = AbsoluteControlCompositionExperimental;
    const auto nodes = AbsoluteControlShipButtonsComposition::Nodes();
    assert(nodes.size() == 100);
    assert(nodes.size() <= Composition::kMaximumNodesPerPage);

    std::unordered_map<std::string_view,
        const Composition::NodeDescriptorV1*> byId;
    std::unordered_map<std::string_view,
        const Composition::NodeDescriptorV1*> byControl;
    std::size_t sections{};
    std::size_t rows{};
    for (const auto& node : nodes) {
        const auto [idIt, idInserted] = byId.emplace(node.nodeId, &node);
        (void)idIt;
        assert(idInserted);
        if (node.parentNodeId[0] != '\0') {
            assert(byId.contains(node.parentNodeId));
        }
        if (node.kind == Composition::NodeKind::Section) ++sections;
        if (node.kind == Composition::NodeKind::Row) ++rows;
        if (node.kind == Composition::NodeKind::ControlSlot) {
            const auto [controlIt, controlInserted] =
                byControl.emplace(node.referenceId, &node);
            (void)controlIt;
            assert(controlInserted);
        }
    }
    assert(sections == 4);
    assert(rows == 23);
    assert(byControl.size() == 72);
    assert(std::string_view{nodes[1].nodeId} == "native-binding-section");
    assert(std::string_view{nodes[1].label} == "NATIVE SHIP CONTROLS");

    for (const auto& target : HotasBindingCatalog::kTargets) {
        if (target.family == HotasBindingCatalog::TargetFamily::CoreAxis ||
            target.family == HotasBindingCatalog::TargetFamily::ReverseAxis) {
            assert(target.pageId == HotasBindingCatalog::kFlightAxesPageId);
            assert(!byControl.contains(target.controlId));
        }
        if (target.family != HotasBindingCatalog::TargetFamily::ShipAction) {
            if (target.family == HotasBindingCatalog::TargetFamily::MenuNavigation) {
                const auto* node = byControl.at(target.controlId);
                assert(std::string_view{node->parentNodeId} ==
                       "menu-navigation-section");
            }
            continue;
        }
        const auto* action = FindShipAction(target.actionId);
        assert(action && byControl.contains(target.controlId));
        const auto routeId = std::string{target.controlId} + "-route";
        assert(byControl.contains(routeId));
        const auto* bindingNode = byControl.at(target.controlId);
        const auto* routeNode = byControl.at(routeId);
        assert(std::string_view{bindingNode->parentNodeId} ==
               routeNode->parentNodeId);
        const auto parent = byId.at(bindingNode->parentNodeId);
        assert(parent->kind == Composition::NodeKind::Row);
    }

    for (const auto actionId : kNativeShipButtonActions) {
        const auto* target = HotasBindingCatalog::kTargets.data();
        while (target != HotasBindingCatalog::kTargets.data() +
                             HotasBindingCatalog::kTargets.size() &&
               target->actionId != actionId) ++target;
        assert(target != HotasBindingCatalog::kTargets.data() +
                         HotasBindingCatalog::kTargets.size());
        const auto* node = byControl.at(target->controlId);
        const auto* parent = byId.at(node->parentNodeId);
        const auto section = parent->kind == Composition::NodeKind::Row
            ? std::string_view{parent->parentNodeId}
            : std::string_view{node->parentNodeId};
        assert(section == "native-binding-section");
    }

    assert(!byControl.contains("shortcut-menu-preset"));

    const auto descriptor =
        AbsoluteControlShipButtonsComposition::Descriptor();
    assert(descriptor.nodeCount == nodes.size());
    assert(descriptor.associationCount == 0);
    assert(std::string_view{descriptor.moduleId} == "absolute.hotas");
    assert(std::string_view{descriptor.pageId} == "hotas-ship-buttons");
    return 0;
}
