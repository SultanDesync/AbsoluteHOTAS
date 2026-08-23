#include "AbsoluteControlFlightAxesComposition.h"

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

void AddRow(std::vector<Composition::NodeDescriptorV1>& nodes,
            std::string_view id, std::string_view parent,
            std::initializer_list<std::pair<std::string_view,
                Composition::SemanticRole>> controls)
{
    nodes.push_back(Node(Composition::NodeKind::Row, id, parent));
    for (const auto& [control, role] : controls) {
        nodes.push_back(Node(Composition::NodeKind::ControlSlot,
            std::string{id} + "-" + std::string{control}, id, control, {}, {},
            role));
    }
}

void AddControl(std::vector<Composition::NodeDescriptorV1>& nodes,
                std::string_view parent, std::string_view control,
                Composition::SemanticRole role)
{
    if (control.empty()) return;
    nodes.push_back(Node(Composition::NodeKind::ControlSlot,
        std::string{parent} + "-" + std::string{control}, parent, control,
        {}, {}, role));
}

void AddAxisCard(std::vector<Composition::NodeDescriptorV1>& nodes,
                 std::string_view section, std::string_view card,
                 std::string_view label, std::string_view description,
                 std::string_view channel, std::string_view binding,
                 std::string_view invert, std::string_view sensitivity,
                 std::string_view saturation, std::string_view deadzone)
{
    nodes.push_back(Node(Composition::NodeKind::Card, card, section, {}, label,
        description, Composition::SemanticRole::Primary,
        Composition::kNodeEmphasized));
    const auto sourceRow = std::string{card} + "-source";
    nodes.push_back(Node(Composition::NodeKind::Row, sourceRow, card));
    AddControl(nodes, sourceRow, binding, Composition::SemanticRole::Binding);
    AddControl(nodes, sourceRow, invert, Composition::SemanticRole::Primary);
    const auto tunable = !saturation.empty() || !deadzone.empty();
    nodes.push_back(Node(Composition::NodeKind::LiveSlot,
        std::string{card} + "-live", card, channel,
        tunable ? "Axis range" : "Throttle zones",
        tunable ?
            "White is current logical input; green is the shaped output. Drag either deadzone or full-authority edge, or use the sliders below." :
            "Read-only mirror of positional throttle zones. Select the graph to edit them on Throttle Setup."));
    AddControl(nodes, card, deadzone, Composition::SemanticRole::Tuning);
    AddControl(nodes, card, saturation, Composition::SemanticRole::Tuning);
    AddControl(nodes, card, sensitivity, Composition::SemanticRole::Tuning);
}

std::vector<Composition::NodeDescriptorV1> BuildNodes()
{
    std::vector<Composition::NodeDescriptorV1> nodes;
    nodes.reserve(96);
    nodes.push_back(Node(Composition::NodeKind::Root, "flight-axes-root"));
    nodes.push_back(Node(Composition::NodeKind::Card, "flight-summary",
        "flight-axes-root", {}, "Flight Axes",
        "Bind, tune, and verify each flight direction below.",
        Composition::SemanticRole::Summary, Composition::kNodeEmphasized));
    AddRow(nodes, "flight-summary-controls", "flight-summary", {
        {"flight-controls-enabled", Composition::SemanticRole::Primary},
        {"flight-open-bindings", Composition::SemanticRole::Secondary},
    });

    nodes.push_back(Node(Composition::NodeKind::AnchorSet, "axis-anchors",
        "flight-axes-root", {}, "Axes on this page"));
    constexpr std::array<std::pair<std::string_view, std::string_view>, 6>
        anchors{{
            {"Throttle", "axis-throttle-card"},
            {"Pitch", "axis-pitch-card"},
            {"Yaw", "axis-yaw-card"},
            {"Roll", "axis-roll-card"},
            {"Lateral", "axis-strafe-lateral-card"},
            {"Vertical", "axis-strafe-vertical-card"},
        }};
    for (std::size_t index = 0; index < anchors.size(); ++index) {
        nodes.push_back(Node(Composition::NodeKind::Anchor,
            "axis-anchor-" + std::to_string(index), "axis-anchors",
            anchors[index].second, anchors[index].first));
    }

    nodes.push_back(Node(Composition::NodeKind::Section, "thrust-section",
        "flight-axes-root", {}, "Thrust"));
    AddAxisCard(nodes, "thrust-section", "axis-throttle-card", "Throttle",
        "Bind and invert here; positional and rate behavior remains on Throttle Setup.",
        "axis-throttle", "bind-throttle-axis", "throttle-inverted", {}, {}, {});
    AddRow(nodes, "axis-throttle-mode", "axis-throttle-card", {
        {"flight-throttle-summary", Composition::SemanticRole::Status},
        {"flight-open-throttle", Composition::SemanticRole::Secondary},
    });

    nodes.push_back(Node(Composition::NodeKind::Section, "rotation-section",
        "flight-axes-root", {}, "Rotation"));
    AddAxisCard(nodes, "rotation-section", "axis-pitch-card", "Pitch",
        "Bind, invert, and tune the live center and full-authority edges.", "axis-pitch", "bind-pitch-axis",
        "pitch-inverted", "pitch-sensitivity", "pitch-saturation",
        "pitch-deadzone");
    AddAxisCard(nodes, "rotation-section", "axis-yaw-card", "Yaw",
        "Bind, invert, and tune the live center and full-authority edges.", "axis-yaw", "bind-yaw-axis",
        "yaw-inverted", "yaw-sensitivity", "yaw-saturation", "yaw-deadzone");
    AddAxisCard(nodes, "rotation-section", "axis-roll-card", "Roll",
        "Bind, invert, and tune the live center and full-authority edges.", "axis-roll",
        "bind-roll-axis", "roll-inverted", "roll-sensitivity",
        "roll-saturation", "roll-deadzone");

    nodes.push_back(Node(Composition::NodeKind::Section,
        "translation-section", "flight-axes-root", {},
        "6-DOF Translation"));
    AddAxisCard(nodes, "translation-section", "axis-strafe-lateral-card",
        "Strafe Lateral", "Left and right translation.",
        "axis-strafe-lateral", "bind-strafe-lateral-axis",
        "strafe-lateral-inverted", "strafe-lateral-sensitivity",
        "strafe-lateral-saturation", "strafe-lateral-deadzone");
    AddAxisCard(nodes, "translation-section", "axis-strafe-vertical-card",
        "Strafe Vertical", "Bind, invert, and tune here. Sensitivity is shared with lateral strafe.",
        "axis-strafe-vertical", "bind-strafe-vertical-axis",
        "strafe-vertical-inverted", {}, "strafe-vertical-saturation",
        "strafe-vertical-deadzone");

    nodes.push_back(Node(Composition::NodeKind::Section, "fallback-section",
        "flight-axes-root", {}, "Reverse & Digital Fallbacks"));
    nodes.push_back(Node(Composition::NodeKind::Card, "reverse-card",
        "fallback-section", {}, "Reverse Authority",
        "Dedicated analog reverse and held digital reverse alternatives."));
    AddRow(nodes, "reverse-source", "reverse-card", {
        {"bind-reverse-axis", Composition::SemanticRole::Binding},
        {"reverse-axis-inverted", Composition::SemanticRole::Secondary},
    });
    AddRow(nodes, "reverse-tuning", "reverse-card", {
        {"reverse-axis-sensitivity", Composition::SemanticRole::Tuning},
        {"reverse-axis-saturation", Composition::SemanticRole::Tuning},
        {"bind-digital-reverse", Composition::SemanticRole::Binding},
    });

    nodes.push_back(Node(Composition::NodeKind::Card,
        "digital-fallback-card", "fallback-section", {},
        "Digital Axis Fallbacks",
        "Buttons and POV directions provide fixed roll and strafe authority."));
    AddRow(nodes, "digital-roll", "digital-fallback-card", {
        {"bind-digital-roll-left", Composition::SemanticRole::Binding},
        {"bind-digital-roll-right", Composition::SemanticRole::Binding},
        {"digital-roll-strength", Composition::SemanticRole::Tuning},
    });
    AddRow(nodes, "digital-strafe-horizontal", "digital-fallback-card", {
        {"bind-digital-strafe-left", Composition::SemanticRole::Binding},
        {"bind-digital-strafe-right", Composition::SemanticRole::Binding},
        {"digital-strafe-strength", Composition::SemanticRole::Tuning},
    });
    AddRow(nodes, "digital-strafe-vertical", "digital-fallback-card", {
        {"bind-digital-strafe-up", Composition::SemanticRole::Binding},
        {"bind-digital-strafe-down", Composition::SemanticRole::Binding},
    });
    return nodes;
}

Composition::AssociationDescriptorV1 Association(
    std::string_view id, std::string_view control,
    std::string_view liveNode, std::string_view marker) noexcept
{
    Composition::AssociationDescriptorV1 association;
    association.kind = Composition::AssociationKind::ControlEditsLiveMarker;
    association.flags = Composition::kAssociationDirectManipulation;
    Copy(association.associationId, id);
    Copy(association.sourceId, control);
    Copy(association.targetNodeId, liveNode);
    Copy(association.semanticId, marker);
    return association;
}

std::vector<Composition::AssociationDescriptorV1> BuildAssociations()
{
    std::vector<Composition::AssociationDescriptorV1> associations;
    struct Axis {
        std::string_view prefix;
        std::string_view liveNode;
        std::string_view saturation;
        std::string_view deadzone;
    };
    constexpr std::array<Axis, 5> axes{{
        {"pitch", "axis-pitch-card-live", "pitch-saturation",
            "pitch-deadzone"},
        {"yaw", "axis-yaw-card-live", "yaw-saturation", "yaw-deadzone"},
        {"roll", "axis-roll-card-live", "roll-saturation", "roll-deadzone"},
        {"strafe-lateral", "axis-strafe-lateral-card-live",
            "strafe-lateral-saturation", "strafe-lateral-deadzone"},
        {"strafe-vertical", "axis-strafe-vertical-card-live",
            "strafe-vertical-saturation", "strafe-vertical-deadzone"},
    }};
    for (const auto& axis : axes) {
        associations.push_back(Association(
            std::string{axis.prefix} + "-saturation-edge", axis.saturation,
            axis.liveNode,
            std::string{axis.prefix} + "-saturation-positive"));
        associations.push_back(Association(
            std::string{axis.prefix} + "-deadzone-edge", axis.deadzone,
            axis.liveNode,
            std::string{axis.prefix} + "-deadzone-positive"));
    }
    return associations;
}

const auto g_nodes = BuildNodes();
const auto g_associations = BuildAssociations();
static_assert(Composition::kMaximumNodesPerPage >= 83);
} // namespace

namespace AbsoluteControlFlightAxesComposition {

Composition::PageCompositionDescriptorV1 Descriptor(
    void* context, Composition::ReadNodeStatesCallback readStates) noexcept
{
    Composition::PageCompositionDescriptorV1 descriptor;
    Copy(descriptor.moduleId, "absolute.hotas");
    Copy(descriptor.pageId, "hotas-flight-axes");
    descriptor.nodeCount = static_cast<std::uint32_t>(g_nodes.size());
    descriptor.nodes = g_nodes.data();
    descriptor.associationCount =
        static_cast<std::uint32_t>(g_associations.size());
    descriptor.associations = g_associations.data();
    descriptor.context = context;
    descriptor.readNodeStates = readStates;
    return descriptor;
}

std::span<const Composition::NodeDescriptorV1> Nodes() noexcept
{
    return g_nodes;
}

std::span<const Composition::AssociationDescriptorV1> Associations() noexcept
{
    return g_associations;
}

} // namespace AbsoluteControlFlightAxesComposition
