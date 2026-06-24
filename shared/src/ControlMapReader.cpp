#include "ControlMapReader.h"

#include <windows.h>
#include <fstream>
#include <iterator>

namespace ControlMap {

namespace {

inline uint32_t ReadU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

inline uint16_t ReadU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}

// Index of the first 0x00 byte in [start, end), or end if none.
size_t FindZero(const uint8_t* data, size_t start, size_t end) {
    for (size_t i = start; i < end; ++i)
        if (data[i] == 0) return i;
    return end;
}

// VKs that need KEYEVENTF_EXTENDEDKEY. Determined from the VK directly because
// the scancode alone is ambiguous (arrow Down and Numpad-2 share scancode 0x50),
// and MapVirtualKey's MAPVK_VK_TO_VSC_EX 0xE0 prefix is unreliable across setups.
bool IsExtendedVk(UINT vk) {
    switch (vk) {
        case VK_PRIOR: case VK_NEXT: case VK_END:    case VK_HOME:
        case VK_LEFT:  case VK_UP:   case VK_RIGHT:  case VK_DOWN:
        case VK_INSERT: case VK_DELETE:
        case VK_NUMLOCK: case VK_DIVIDE:
        case VK_RCONTROL: case VK_RMENU:
            return true;
        default:
            return false;
    }
}

} // namespace

const std::vector<Record>& ControlMapFile::Section0() const {
    static const std::vector<Record> kEmpty;
    return sections.empty() ? kEmpty : sections.front();
}

std::vector<Record> ControlMapFile::DeviceRecords() const {
    std::vector<Record> out;
    for (const auto& section : sections)
        for (const auto& r : section)
            if (r.device == 1) out.push_back(r);  // keyboard/mouse only
    return out;
}

ControlMapFile Parse(const uint8_t* data, size_t size) {
    ControlMapFile out;
    if (size == 0) { out.valid = true; return out; }  // empty buffer = no overrides
    if (!data) return out;                            // null with non-zero size = error

    size_t off = 0;
    while (off < size) {
        if (off + 3 > size) return out;          // truncated header → invalid
        if (data[off] != 0x03) return out;        // bad section marker → invalid

        const size_t len = (static_cast<size_t>(data[off + 1]) << 8) | data[off + 2];
        if (len < 3 || off + len > size) return out;  // bad length → invalid

        const size_t end = off + len;
        size_t ro = off + 3;
        std::vector<Record> records;

        while (ro < end) {
            const size_t ctxZero = FindZero(data, ro, end);
            if (ctxZero == end) break;            // benign trailing padding

            const size_t actStart = ctxZero + 1;
            const size_t actZero = FindZero(data, actStart, end);
            if (actZero == end) return out;        // context but no action → invalid

            const size_t payload = actZero + 1;
            if (payload + 8 > end) return out;     // payload past section → invalid

            Record rec;
            rec.context.assign(reinterpret_cast<const char*>(data + ro), ctxZero - ro);
            rec.action.assign(reinterpret_cast<const char*>(data + actStart), actZero - actStart);
            rec.device = ReadU32LE(data + payload);
            rec.token  = ReadU16LE(data + payload + 4);
            rec.flag   = data[payload + 6];
            records.push_back(std::move(rec));

            ro = payload + 8;
        }

        out.sections.push_back(std::move(records));
        off = end;
    }

    out.valid = true;
    return out;
}

ControlMapFile ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    return Parse(bytes.data(), bytes.size());
}

Output TokenToOutput(uint16_t token) {
    if (token == kUnboundToken) return {};
    if (token <= 0x0003)        return { OutputKind::Mouse, static_cast<uint16_t>(token + 1), false };

    // Keyboard: the token's low byte is a Windows Virtual-Key code. Confirmed
    // against Starfield's own in-game rebind writes — K = 0x004B (VK_K),
    // '/' = 0x00BF (VK_OEM_2), Left = 0x0025 (VK_LEFT), L Shift = 0x00A0
    // (VK_LSHIFT). The retired configurator's older 0x01xx tokens carry the
    // same VK in the low byte, so masking handles both encodings.
    //
    // MapVirtualKey converts the VK to a scancode for the USER'S actual keyboard
    // layout — what the SendInput layer needs, and more correct than a fixed US
    // table. The extended flag comes from IsExtendedVk (the VK), not the scancode.
    const UINT vk = token & 0x00FFu;
    const UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    if (sc == 0) return {};  // unmapped / unassigned VK
    return { OutputKind::Keyboard, static_cast<uint16_t>(sc & 0x00FFu), IsExtendedVk(vk) };
}

Output ResolveBinding(const std::vector<Record>& section0,
                      std::string_view context,
                      std::string_view action,
                      Output vanillaFallback) {
    const Record* primary  = nullptr;
    const Record* boundAlt = nullptr;
    for (const auto& r : section0) {
        if (r.context != context || r.action != action) continue;
        if (r.IsPrimary()) {
            if (!primary) primary = &r;
        } else if (!r.IsUnbound() && !boundAlt) {
            boundAlt = &r;
        }
    }

    // No primary override → vanilla primary slot is untouched and still fires.
    if (!primary) return vanillaFallback;

    if (!primary->IsUnbound()) {
        const Output o = TokenToOutput(primary->token);
        // Unknown token: we can't reproduce the new key; best-effort on vanilla.
        return (o.kind != OutputKind::None) ? o : vanillaFallback;
    }

    // Primary explicitly unbound — fall back to a usable alternate slot if any.
    if (boundAlt) {
        const Output o = TokenToOutput(boundAlt->token);
        if (o.kind != OutputKind::None) return o;
    }
    return {};  // function fully unbound by the user
}

} // namespace ControlMap
