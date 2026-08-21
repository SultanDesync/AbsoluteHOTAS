#pragma once

#include "AbsoluteControlCompositionExperimentalAPI.h"

#include <span>

namespace AbsoluteControlShipButtonsComposition {

namespace Composition = AbsoluteControlCompositionExperimental;

[[nodiscard]] Composition::PageCompositionDescriptorV1 Descriptor() noexcept;
[[nodiscard]] std::span<const Composition::NodeDescriptorV1> Nodes() noexcept;

} // namespace AbsoluteControlShipButtonsComposition
