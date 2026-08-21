#pragma once

#include "AbsoluteControlPanelAPI.h"
#include "AbsoluteControlCompositionExperimentalAPI.h"

#include <cstddef>

namespace AbsoluteControlSubscriber {

// Startup facts are copied into atomics before registration. Provider callbacks
// read only these bounded values and never touch the INI, DirectInput, ImGui, or
// game objects from Absolute Control's UI thread.
struct RuntimeStatus {
    bool throttleHookInstalled{};
    bool nativeControlsInitialized{};
    bool controllerStarted{};
    bool legacyWorkbenchConfigured{};
    bool legacyWorkbenchInstalled{};
};

void SetRuntimeStatus(const RuntimeStatus& status) noexcept;
void SetExternalMouseSteeringOwner(bool active) noexcept;
void SetExternalCameraOwner(bool active) noexcept;

// Safe to call at more than one documented SFSE lifecycle boundary. Host
// absence and rejection never prevent AbsoluteHOTAS gameplay initialization.
[[nodiscard]] AbsoluteControlPanelApi::Result RegisterDiscoveredHost() noexcept;
[[nodiscard]] bool IsHosted() noexcept;
// Frontend/runtime arbitration remains provider-owned. When the suite menu is
// open, HOTAS parks gameplay output even if the user is viewing another module.
[[nodiscard]] bool IsHostOpen() noexcept;
[[nodiscard]] bool IsHostInputCaptureActive() noexcept;
// Requests this provider's already-registered page through the optional host
// ABI tail. False means the caller should retain its legacy configuration UI.
[[nodiscard]] bool RequestHostPage(const char* pageId) noexcept;

namespace Testing {

using ResolveLoadedHostCallback = const AbsoluteControlPanelApi::ApiV1*(__cdecl*)(
    const wchar_t* moduleName) noexcept;

// Test seams exercise the same descriptor validation and registration path as
// production without loading an Absolute Control DLL into the test process.
[[nodiscard]] AbsoluteControlPanelApi::Result RegisterWithResolver(
    ResolveLoadedHostCallback resolver) noexcept;
[[nodiscard]] AbsoluteControlPanelApi::Result ValidateDescriptors(
    const AbsoluteControlPanelApi::PageDescriptorV1* pages,
    std::size_t pageCount) noexcept;
[[nodiscard]] const AbsoluteControlPanelApi::ModuleDescriptorV1& Module() noexcept;
[[nodiscard]] const AbsoluteControlPanelApi::PageDescriptorV1* Pages(
    std::size_t& pageCount) noexcept;
[[nodiscard]] AbsoluteControlPanelApi::Result
RegisterFlightAxesComposition(
    const AbsoluteControlCompositionExperimental::ApiV1* api) noexcept;
[[nodiscard]] bool IsFlightAxesCompositionRegistered() noexcept;
[[nodiscard]] AbsoluteControlPanelApi::Result
RegisterShipButtonsComposition(
    const AbsoluteControlCompositionExperimental::ApiV1* api) noexcept;
[[nodiscard]] bool IsShipButtonsCompositionRegistered() noexcept;
void ForceReadException(bool enabled) noexcept;
void Reset() noexcept;

} // namespace Testing
} // namespace AbsoluteControlSubscriber
