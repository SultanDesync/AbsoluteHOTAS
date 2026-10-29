#include "AbsoluteControlFlightAxesComposition.h"
#include "AbsoluteControlScalarCatalog.h"
#include "HotasBindingCatalog.h"

#include "TestCheck.h"
#include <string_view>
#include <unordered_map>
#include <unordered_set>

int main()
{
    namespace Composition = AbsoluteControlCompositionExperimental;
    const auto nodes = AbsoluteControlFlightAxesComposition::Nodes();
    const auto associations =
        AbsoluteControlFlightAxesComposition::Associations();
    REQUIRE(nodes.size() == 83);
    REQUIRE(nodes.size() <= Composition::kMaximumNodesPerPage);
    REQUIRE(associations.size() == 10);

    std::unordered_map<std::string_view, const Composition::NodeDescriptorV1*>
        byId;
    std::unordered_set<std::string_view> placedControls;
    std::size_t anchors{};
    std::size_t cards{};
    std::size_t liveSlots{};
    for (const auto& node : nodes) {
        REQUIRE(byId.emplace(node.nodeId, &node).second);
        if (node.parentNodeId[0] != '\0') REQUIRE(byId.contains(node.parentNodeId));
        if (node.kind == Composition::NodeKind::Anchor) ++anchors;
        if (node.kind == Composition::NodeKind::Card) ++cards;
        if (node.kind == Composition::NodeKind::LiveSlot) {
            ++liveSlots;
            REQUIRE(std::string_view{node.referenceId}.starts_with("axis-"));
        }
        if (node.kind == Composition::NodeKind::ControlSlot) {
            REQUIRE(placedControls.insert(node.referenceId).second);
        }
        const std::string_view id{node.nodeId};
        REQUIRE(id.find("head") == std::string_view::npos);
        REQUIRE(id.find("hosam") == std::string_view::npos);
        REQUIRE(id.find("mouse") == std::string_view::npos);
        REQUIRE(id.find("power") == std::string_view::npos);
    }
    REQUIRE(anchors == 6);
    REQUIRE(cards == 9);
    REQUIRE(liveSlots == 6);
    REQUIRE(placedControls.size() == 43);

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
        REQUIRE(placedControls.contains(
            AbsoluteControlSettings::Definition(field).controlId));
    }
    for (const auto& target : HotasBindingCatalog::kTargets) {
        if (target.pageId == HotasBindingCatalog::kFlightAxesPageId) {
            REQUIRE(placedControls.contains(target.controlId));
        }
    }
    for (const auto controlId : HotasBindingCatalog::kAxisControlIds) {
        const auto* target = HotasBindingCatalog::Find(controlId);
        REQUIRE(target && target->pageId == HotasBindingCatalog::kFlightAxesPageId);
        REQUIRE(placedControls.contains(controlId));
    }
    REQUIRE(placedControls.contains("flight-throttle-summary"));
    REQUIRE(placedControls.contains("flight-open-throttle"));
    REQUIRE(placedControls.contains("flight-open-bindings"));

    std::unordered_set<std::string_view> associationIds;
    for (const auto& association : associations) {
        REQUIRE(associationIds.insert(association.associationId).second);
        REQUIRE(association.kind ==
            Composition::AssociationKind::ControlEditsLiveMarker);
        REQUIRE((association.flags &
            Composition::kAssociationDirectManipulation) != 0);
        REQUIRE(placedControls.contains(association.sourceId));
        const auto target = byId.find(association.targetNodeId);
        REQUIRE(target != byId.end());
        REQUIRE(target->second->kind == Composition::NodeKind::LiveSlot);
        REQUIRE(std::string_view{association.semanticId}.ends_with(
            "-positive"));
    }

    const auto descriptor =
        AbsoluteControlFlightAxesComposition::Descriptor(nullptr, nullptr);
    REQUIRE(descriptor.nodeCount == nodes.size());
    REQUIRE(descriptor.associationCount == associations.size());
    REQUIRE(std::string_view{descriptor.moduleId} == "absolute.hotas");
    REQUIRE(std::string_view{descriptor.pageId} == "hotas-flight-axes");
    return 0;
}
