#include "AbsoluteControlFlightAxesComposition.h"
#include "AbsoluteControlScalarCatalog.h"
#include "HotasBindingCatalog.h"

#include <cassert>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

int main()
{
    namespace Composition = AbsoluteControlCompositionExperimental;
    const auto nodes = AbsoluteControlFlightAxesComposition::Nodes();
    const auto associations =
        AbsoluteControlFlightAxesComposition::Associations();
    assert(nodes.size() == 83);
    assert(nodes.size() <= Composition::kMaximumNodesPerPage);
    assert(associations.size() == 10);

    std::unordered_map<std::string_view, const Composition::NodeDescriptorV1*>
        byId;
    std::unordered_set<std::string_view> placedControls;
    std::size_t anchors{};
    std::size_t cards{};
    std::size_t liveSlots{};
    for (const auto& node : nodes) {
        assert(byId.emplace(node.nodeId, &node).second);
        if (node.parentNodeId[0] != '\0') assert(byId.contains(node.parentNodeId));
        if (node.kind == Composition::NodeKind::Anchor) ++anchors;
        if (node.kind == Composition::NodeKind::Card) ++cards;
        if (node.kind == Composition::NodeKind::LiveSlot) {
            ++liveSlots;
            assert(std::string_view{node.referenceId}.starts_with("axis-"));
        }
        if (node.kind == Composition::NodeKind::ControlSlot) {
            assert(placedControls.insert(node.referenceId).second);
        }
        const std::string_view id{node.nodeId};
        assert(id.find("head") == std::string_view::npos);
        assert(id.find("hosam") == std::string_view::npos);
        assert(id.find("mouse") == std::string_view::npos);
        assert(id.find("power") == std::string_view::npos);
    }
    assert(anchors == 6);
    assert(cards == 9);
    assert(liveSlots == 6);
    assert(placedControls.size() == 43);

    for (std::size_t index = 0;
         index <= static_cast<std::size_t>(
             AbsoluteControlSettings::ScalarField::DigitalStrafeStrength);
         ++index) {
        const auto field =
            static_cast<AbsoluteControlSettings::ScalarField>(index);
        if (field == AbsoluteControlSettings::ScalarField::ThrottleSensitivity ||
            field == AbsoluteControlSettings::ScalarField::ThrottleSaturation ||
            field == AbsoluteControlSettings::ScalarField::ThrottleDeadzone) {
            continue;
        }
        assert(placedControls.contains(
            AbsoluteControlSettings::Definition(field).controlId));
    }
    for (const auto& target : HotasBindingCatalog::kTargets) {
        if (target.pageId == HotasBindingCatalog::kFlightAxesPageId) {
            assert(placedControls.contains(target.controlId));
        }
    }
    for (const auto controlId : HotasBindingCatalog::kAxisControlIds) {
        const auto* target = HotasBindingCatalog::Find(controlId);
        assert(target && target->pageId == HotasBindingCatalog::kFlightAxesPageId);
        assert(placedControls.contains(controlId));
    }
    assert(placedControls.contains("flight-throttle-summary"));
    assert(placedControls.contains("flight-open-throttle"));
    assert(placedControls.contains("flight-open-bindings"));

    std::unordered_set<std::string_view> associationIds;
    for (const auto& association : associations) {
        assert(associationIds.insert(association.associationId).second);
        assert(association.kind ==
            Composition::AssociationKind::ControlEditsLiveMarker);
        assert((association.flags &
            Composition::kAssociationDirectManipulation) != 0);
        assert(placedControls.contains(association.sourceId));
        const auto target = byId.find(association.targetNodeId);
        assert(target != byId.end());
        assert(target->second->kind == Composition::NodeKind::LiveSlot);
        assert(std::string_view{association.semanticId}.ends_with(
            "-positive"));
    }

    const auto descriptor =
        AbsoluteControlFlightAxesComposition::Descriptor(nullptr, nullptr);
    assert(descriptor.nodeCount == nodes.size());
    assert(descriptor.associationCount == associations.size());
    assert(std::string_view{descriptor.moduleId} == "absolute.hotas");
    assert(std::string_view{descriptor.pageId} == "hotas-flight-axes");
    return 0;
}
