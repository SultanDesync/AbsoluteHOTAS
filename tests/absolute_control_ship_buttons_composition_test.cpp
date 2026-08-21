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
    assert(nodes.size() == 93);
    assert(nodes.size() <= Composition::kMaximumNodesPerPage);

    std::unordered_map<std::string_view,
        const Composition::NodeDescriptorV1*> byId;
    std::unordered_map<std::string_view,
        const Composition::NodeDescriptorV1*> byControl;
    std::size_t sections{};
    std::size_t rows{};
    for (const auto& node : nodes) {
        assert(byId.emplace(node.nodeId, &node).second);
        if (node.parentNodeId[0] != '\0') {
            assert(byId.contains(node.parentNodeId));
        }
        if (node.kind == Composition::NodeKind::Section) ++sections;
        if (node.kind == Composition::NodeKind::Row) ++rows;
        if (node.kind == Composition::NodeKind::ControlSlot) {
            assert(byControl.emplace(node.referenceId, &node).second);
        }
    }
    assert(sections == 7);
    assert(rows == 17);
    assert(byControl.size() == 68);

    for (const auto& target : HotasBindingCatalog::kTargets) {
        if (target.family == HotasBindingCatalog::TargetFamily::CoreAxis ||
            target.family == HotasBindingCatalog::TargetFamily::ReverseAxis) {
            assert(target.pageId == HotasBindingCatalog::kShipButtonsPageId);
            assert(byControl.contains(target.controlId));
        }
        if (target.family != HotasBindingCatalog::TargetFamily::ShipAction) {
            continue;
        }
        const auto* action = FindShipAction(target.actionId);
        assert(action && byControl.contains(target.controlId));
        const auto routeId = std::string{target.controlId} + "-route";
        if (action->allowedMethods == kDirectOrKeyboard) {
            assert(byControl.contains(routeId));
            const auto* bindingNode = byControl.at(target.controlId);
            const auto* routeNode = byControl.at(routeId);
            assert(std::string_view{bindingNode->parentNodeId} ==
                   routeNode->parentNodeId);
            const auto parent = byId.at(bindingNode->parentNodeId);
            assert(parent->kind == Composition::NodeKind::Row);
        } else {
            assert(!byControl.contains(routeId));
        }
    }

    const auto descriptor =
        AbsoluteControlShipButtonsComposition::Descriptor();
    assert(descriptor.nodeCount == nodes.size());
    assert(descriptor.associationCount == 0);
    assert(std::string_view{descriptor.moduleId} == "absolute.hotas");
    assert(std::string_view{descriptor.pageId} == "hotas-ship-buttons");
    return 0;
}
