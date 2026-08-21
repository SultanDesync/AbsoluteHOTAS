#pragma once

#include <SimpleIni.h>

#include <string_view>

namespace ConfigOwnershipPolicy {

// These settings moved to separately installed modules. HOTAS may retain old
// values for compatibility/read-only status, but must never author them.
[[nodiscard]] bool IsStandaloneOwned(std::string_view section,
                                     std::string_view key) noexcept;

// Used only when creating a new HOTAS export. Existing files are never scrubbed:
// their external keys are preserved in place by managed-payload replacement.
void RemoveStandaloneOwned(CSimpleIniA& ini);

// Replace exactly the key set described by managedTemplate plus HOTAS's dynamic
// collections. Destination is expected to be the already-loaded existing file,
// so every unknown or standalone-owned key survives unchanged. Incoming foreign
// keys are ignored and therefore cannot overwrite another module's state.
void ReplaceManagedPayload(CSimpleIniA& destination,
                           const CSimpleIniA& incoming,
                           const CSimpleIniA& managedTemplate);

} // namespace ConfigOwnershipPolicy
