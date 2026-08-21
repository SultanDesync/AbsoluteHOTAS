#pragma once

#include "AbsoluteControlCompositionExperimentalAPI.h"

#include <span>

namespace AbsoluteControlFlightAxesComposition {

namespace Composition = AbsoluteControlCompositionExperimental;

[[nodiscard]] Composition::PageCompositionDescriptorV1 Descriptor(
    void* context, Composition::ReadNodeStatesCallback readStates) noexcept;
[[nodiscard]] std::span<const Composition::NodeDescriptorV1> Nodes() noexcept;
[[nodiscard]] std::span<const Composition::AssociationDescriptorV1>
Associations() noexcept;

} // namespace AbsoluteControlFlightAxesComposition
