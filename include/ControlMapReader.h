#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// ============================================================================
// ControlMapReader — read Starfield's ControlMap_Custom.txt override table
//
// Starfield stores keyboard/mouse binding *overrides* (deltas from the base
// ControlMap.txt) in a small binary file confusingly given a .txt extension.
// This module parses it so the plugin can discover when a user has rebound a
// spaceship control in Starfield's own settings menu and realign its SendInput
// output to the new key.
//
// Format (reverse-engineered; see docs/reference/control-map-ship-functions.md):
//
//   File    = one or more Sections, laid out back-to-back
//   Section = 0x03 | lenHi | lenLo | records...   ; len is BIG-endian, incl. the
//                                                    3 header bytes
//   Record  = context\0 action\0 <8-byte payload>
//   Payload = device(u32 LE) | token(u16 LE) | flag(u8) | pad(u8)
//             device 1 = keyboard/mouse group
//             token    keyboard: low byte is a Windows Virtual-Key code
//                        (e.g. K=0x004B, '/'=0x00BF, Left=0x0025); mouse: 0..3;
//                        0x02FF means "explicitly unbound"
//             flag     0x00 = primary slot, anything else = an alternate slot
//
// Sections group records by device: Section 0 = keyboard, Section 1 = mouse
// (e.g. a vanilla-mouse action's primary slot lives in Section 1). A single
// function's primary + alternate can therefore span sections, so we aggregate
// every device==1 (keyboard/mouse) record across all sections. Records with
// device != 1 (e.g. gamepad) are skipped — the plugin can't emit them.
//
// TokenToOutput uses MapVirtualKey (Windows) to convert the VK to a layout-
// correct scancode, so this module depends on <windows.h>.
// ============================================================================

namespace ControlMap {

inline constexpr uint16_t kUnboundToken = 0x02FF;
inline constexpr uint8_t  kPrimaryFlag  = 0x00;

enum class OutputKind { None, Keyboard, Mouse };

// A keyboard scancode / mouse button resolved from a Starfield token, ready for
// the SendInput layer. Mirrors ShipOutput's fields without coupling to it.
struct Output {
    OutputKind kind     = OutputKind::None;
    uint16_t   code     = 0;      // keyboard scancode, or mouse button 1-4
    bool       extended = false;  // KEYEVENTF_EXTENDEDKEY (arrow keys)
};

// One override record parsed from a section.
struct Record {
    std::string context;
    std::string action;
    uint32_t    device = 0;
    uint16_t    token  = 0;
    uint8_t     flag   = 0;

    bool IsPrimary() const { return flag == kPrimaryFlag; }
    bool IsUnbound() const { return token == kUnboundToken; }
};

// A parsed file: each inner vector is one section's records.
struct ControlMapFile {
    std::vector<std::vector<Record>> sections;
    bool valid = false;  // false if the bytes were malformed/truncated or absent

    // Section 0 = keyboard/mouse overrides. Returns an empty list if absent.
    const std::vector<Record>& Section0() const;

    // All device==1 (keyboard/mouse) records across every section, flattened.
    // A function's primary and alternate can live in different sections (kbd in
    // Section 0, mouse in Section 1), so resolution must look at all of them.
    std::vector<Record> DeviceRecords() const;
};

// Parse raw file bytes. On malformed input returns { valid = false } with
// whatever sections parsed cleanly before the error.
ControlMapFile Parse(const uint8_t* data, size_t size);

// Read + parse a file from disk. Returns { valid = false, sections empty } if
// the file is missing or unreadable — a normal "no overrides yet" state.
// Takes a filesystem::path so wide (non-ASCII) paths open correctly on Windows.
ControlMapFile ReadFile(const std::filesystem::path& path);

// Convert a Starfield key token to a keyboard/mouse Output.
// Returns { None } for the unbound sentinel or any unrecognised token.
Output TokenToOutput(uint16_t token);

// Resolve the output the plugin should emit for one spaceship function, given
// Section 0 records and the function's baked-in vanilla default:
//
//   * bound primary (flag 0x00) override        -> follow it (main rebind case)
//   * primary unbound, a bound alternate present -> follow the alternate
//   * primary unbound, no usable alternate       -> { None } (binding cleared)
//   * no primary override record                 -> vanillaFallback (intact)
//
// LIMITATION: alternate slots that exist only in the base ControlMap.txt (e.g.
// the vanilla "V" alt on Increase System Power) are invisible here, so a user
// who unbinds only the primary may surface as { None } even though a base alt
// still fires in-game. Documented edge case.
Output ResolveBinding(const std::vector<Record>& section0,
                      std::string_view context,
                      std::string_view action,
                      Output vanillaFallback);

} // namespace ControlMap
