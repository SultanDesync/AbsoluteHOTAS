#pragma once

#include <cstdint>
#include <string>

namespace AbsoluteControlSettings {

// Renderer-neutral H1 configuration slice. This remains an internal HOTAS
// model: only POD values are copied into Absolute Control's C ABI records.
struct ScalarState {
    bool flightControlsEnabled{true};
    bool pitchInverted{};
    double pitchSensitivity{1.0};
    int pilotContextMode{1};
    bool automaticPilotDetection{true};
    int pilotLatchMilliseconds{5000};
};

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

// Atomically updates only this slice in the user-owned custom file, requests
// the normal runtime reload, then parses the layered files again for semantic
// read-back. On failure the caller retains its draft.
[[nodiscard]] bool Apply(const ScalarState& state, const Revision& expected,
                         ScalarState& readBack, Revision& revision,
                         std::string& error) noexcept;

[[nodiscard]] Revision CurrentRevision() noexcept;

// The embedded workbench and Absolute Control cannot own authoritative drafts
// simultaneously. Reads remain available for diagnostics; writes fail closed.
[[nodiscard]] bool CanEdit() noexcept;

} // namespace AbsoluteControlSettings
