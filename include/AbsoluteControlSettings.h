#pragma once

#include "AbsoluteControlScalarCatalog.h"
#include "AbsoluteControlDevices.h"
#include "HotasBindingCatalog.h"
#include "ShipActionCatalog.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace AbsoluteControlSettings {

using ShipRouteState =
    std::array<ShipControlMethod, kShipActionCatalog.size()>;

[[nodiscard]] constexpr ShipRouteState DefaultShipRoutes() noexcept
{
    ShipRouteState routes{};
    for (std::size_t index = 0; index < routes.size(); ++index) {
        routes[index] = kShipActionCatalog[index].recommendedMethod;
    }
    return routes;
}

struct Revision {
    std::uint32_t runtimeGeneration{};
    std::uint64_t sourceFingerprint{};

    friend bool operator==(const Revision&, const Revision&) = default;
};

// Loads Main controls (shipped defaults + user custom file), never the active
// runtime profile overlay. The revision protects a dirty Control draft from a
// concurrent legacy/manual save. The runtime generation independently tells a
// clean session when to refresh after a completed controller reload.
[[nodiscard]] bool Load(ScalarState& state, Revision& revision,
                        std::string& error) noexcept;

// Loads the same Main-controls transaction plus the fixed HOTAS binding slice.
// Profiles, macros, and dynamic custom rows are deliberately outside this state.
[[nodiscard]] bool LoadWithBindings(
    ScalarState& state, HotasBindingCatalog::BindingState& bindings,
    Revision& revision, std::string& error) noexcept;

// Extends the same Main-controls transaction with the installation-wide ship
// dispatch choices. These choices are deliberately base-only and never enter
// sparse profile overlays.
[[nodiscard]] bool LoadWithBindingsAndRoutes(
    ScalarState& state, HotasBindingCatalog::BindingState& bindings,
    ShipRouteState& routes, Revision& revision, std::string& error) noexcept;

// Loads the effective configuration currently selected by the shared Wizard
// profile repository. Main uses the ordinary layered files; an overlay/full
// profile uses the materialized Wizard edit state. Ship dispatch routes remain
// installation-wide and are therefore read from Main in either case.
[[nodiscard]] bool LoadEditTargetWithBindingsAndRoutes(
    ScalarState& state, HotasBindingCatalog::BindingState& bindings,
    ShipRouteState& routes, std::string& editTarget,
    Revision& revision, std::string& error) noexcept;

// Atomically updates only this slice in the user-owned custom file, requests
// the normal runtime reload, then parses the layered files again for semantic
// read-back. On failure the caller retains its draft.
[[nodiscard]] bool Apply(const ScalarState& state, const Revision& expected,
                         ScalarState& readBack, Revision& revision,
                         std::string& error) noexcept;

// Commits scalar and binding edits through one custom-INI replacement so a page
// containing both kinds remains one provider-owned Apply/Cancel transaction.
[[nodiscard]] bool ApplyWithBindings(
    const ScalarState& state,
    const HotasBindingCatalog::BindingState& bindings,
    const Revision& expected, ScalarState& readBack,
    HotasBindingCatalog::BindingState& bindingReadBack,
    Revision& revision, std::string& error) noexcept;

[[nodiscard]] bool ApplyWithBindingsAndRoutes(
    const ScalarState& state,
    const HotasBindingCatalog::BindingState& bindings,
    const ShipRouteState& routes, const Revision& expected,
    ScalarState& readBack,
    HotasBindingCatalog::BindingState& bindingReadBack,
    ShipRouteState& routeReadBack, Revision& revision,
    std::string& error) noexcept;

[[nodiscard]] bool ApplyEditTargetWithBindingsAndRoutes(
    const ScalarState& state,
    const HotasBindingCatalog::BindingState& bindings,
    const ShipRouteState& routes, std::string_view expectedEditTarget,
    const Revision& expected, ScalarState& readBack,
    HotasBindingCatalog::BindingState& bindingReadBack,
    ShipRouteState& routeReadBack, Revision& revision,
    std::string& error) noexcept;

// Device actions use a short, independently revision-guarded transaction. It
// updates only the fixed HOTAS binding slice and the authoritative calibration
// map; scalar drafts, profiles, macros, Head Tracking, HOSAM, and Power are not
// part of this operation.
[[nodiscard]] bool LoadDeviceState(
    HotasBindingCatalog::BindingState& bindings,
    AbsoluteControlDevices::CalibrationMap& calibration,
    Revision& revision, std::string& error) noexcept;

[[nodiscard]] bool ApplyDeviceState(
    const HotasBindingCatalog::BindingState& bindings,
    const AbsoluteControlDevices::CalibrationMap& calibration,
    const Revision& expected,
    HotasBindingCatalog::BindingState& bindingReadBack,
    AbsoluteControlDevices::CalibrationMap& calibrationReadBack,
    Revision& revision, std::string& error) noexcept;

[[nodiscard]] Revision CurrentRevision() noexcept;

// The embedded workbench and Absolute Control cannot own authoritative drafts
// simultaneously. Reads remain available for diagnostics; writes fail closed.
[[nodiscard]] bool CanEdit() noexcept;

} // namespace AbsoluteControlSettings
