#pragma once

#include "HotasBindingCatalog.h"

#include <string>
#include <string_view>

// Narrow provider-facing adapter over the legacy WizardCapture device policy.
// Keeping this renderer-neutral lets Absolute Control own presentation while the
// HOTAS provider continues to own DirectInput polling and binding syntax.
namespace HotasBindingCapture {

enum class PollState {
    Capturing,
    Captured,
    TimedOut,
};

void Begin(const HotasBindingCatalog::Target& target);
// Fixed provider-owned button slots that are intentionally outside the static
// binding catalog (profile activation, macros, and other record editors) share
// the same DirectInput capture lifecycle through this narrow entry point.
void BeginButton(int captureSlot, std::string_view label,
                 int settleWindowMilliseconds);
[[nodiscard]] PollState Poll(std::string& binding);
void Cancel() noexcept;

} // namespace HotasBindingCapture
