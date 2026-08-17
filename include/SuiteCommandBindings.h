#pragma once

#include "AbsoluteHOTASAPI.h"

// Runtime owner for daughter-module commands bound to HOTAS buttons.
namespace SuiteCommandBindings {
void Initialize();
void Reload();
void Poll();
void Shutdown();
// Process-lifetime table used by AbsoluteHOTAS's own module UI. External clients
// continue to use AbsoluteHOTAS_QueryApi; this avoids importing our own DLL.
const AbsoluteHOTASApi::ApiV1* GetApi() noexcept;
} // namespace SuiteCommandBindings
