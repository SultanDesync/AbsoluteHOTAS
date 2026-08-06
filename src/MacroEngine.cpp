#include "PCH.h"
#include "MacroEngine.h"
#include "DeviceManager.h"
#include "RuntimePaths.h"
#include "StringUtils.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>

// ============================================================================
// Internal
// ============================================================================

static void MacroLog(const std::string& msg) {
    RuntimePaths::Log("[Macro]", msg);
}

static std::vector<Macro> s_macros;

// ---- Emission runtime (parallel to s_macros) ----
enum class MacroPhase { Press, Hold, Gap };

struct MacroRuntime {
    int        stepIdx        = -1;   // -1 = inactive
    int        repeatsLeft    = 0;    // remaining taps in the current step
    MacroPhase phase          = MacroPhase::Press;
    uint64_t   phaseStartMs   = 0;
    bool       prevButtonDown = false;
};

static std::vector<MacroRuntime> s_runtime;

static constexpr uint64_t kTapHoldMs = 40;  // how long a tap holds before release

static uint64_t NowMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

static uint32_t MacroOwner(size_t index) {
    return OwnerMacroBase + static_cast<uint32_t>(index);
}

static void PressStep(const MacroStep& step, uint32_t owner) {
    for (const ShipControlTarget& target : step.targets)
        ShipOutputSystem::SetControlTargetHeld(target, owner, true);
}

static void ReleaseStep(const MacroStep& step, uint32_t owner) {
    for (const ShipControlTarget& target : step.targets)
        ShipOutputSystem::SetControlTargetHeld(target, owner, false);
}

static void BeginStep(MacroRuntime& rt, const Macro& m, uint64_t now) {
    const MacroStep& step = m.steps[rt.stepIdx];
    rt.repeatsLeft  = (step.action == MacroAction::Tap) ? std::max(1, step.amount) : 1;
    rt.phase        = MacroPhase::Press;
    rt.phaseStartMs = now;
}

// Split on a delimiter, trimming each piece; skips empty pieces.
static std::vector<std::string> Split(std::string_view text, char delim) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find(delim, start);
        std::string_view piece = text.substr(start, (end == std::string_view::npos ? text.size() : end) - start);
        // trim
        while (!piece.empty() && std::isspace(static_cast<unsigned char>(piece.front()))) piece.remove_prefix(1);
        while (!piece.empty() && std::isspace(static_cast<unsigned char>(piece.back())))  piece.remove_suffix(1);
        if (!piece.empty()) out.emplace_back(piece);
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return out;
}

// Whitespace tokenizer (any run of spaces/tabs is one separator).
static std::vector<std::string> Tokenize(std::string_view text) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        const size_t start = i;
        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        if (i > start) out.emplace_back(text.substr(start, i - start));
    }
    return out;
}

// Parse one "Step<N> = <targets> <tap|hold> <amount> [gapMs]" line.
//   targets = '+'-joined ship action ids and/or key:0xNN / mouse:N
static bool ParseStep(std::string_view line, MacroStep& out) {
    // Strip an inline ';' comment (SimpleIni keeps these in the value).
    if (const size_t c = line.find(';'); c != std::string_view::npos)
        line = line.substr(0, c);

    const std::vector<std::string> tok = Tokenize(line);
    if (tok.size() < 3) return false;  // need targets + action + amount

    for (const std::string& t : Split(tok[0], '+')) {
        const ShipControlTarget target = ShipOutputSystem::ResolveControlTarget(t);
        if (target.IsNative() || target.output.kind != ShipOutputKind::None)
            out.targets.push_back(target);
    }
    if (out.targets.empty()) return false;

    out.action = (TrimLower(tok[1]) == "hold") ? MacroAction::Hold : MacroAction::Tap;
    out.amount = std::atoi(tok[2].c_str());
    out.gapMs  = (tok.size() >= 4) ? std::atoi(tok[3].c_str()) : 50;

    if (out.amount < 0) out.amount = 0;
    if (out.gapMs  < 0) out.gapMs  = 0;
    return true;
}

// ============================================================================
// Public API
// ============================================================================

namespace MacroEngine {

std::vector<Macro>& GetMacrosMutable() {
    return s_macros;
}

void LoadMacros(CSimpleIniA& ini) {
    ReleaseAll();        // release any keys held by the previous macro set
    s_macros.clear();

    CSimpleIniA::TNamesDepend sections;
    ini.GetAllSections(sections);

    for (const auto& s : sections) {
        if (!s.pItem) continue;
        std::string_view section(s.pItem);
        if (section.rfind("Macro:", 0) != 0) continue;  // only [Macro:*] sections

        Macro m;
        // Friendly name (sName) for logs; fall back to the section key for a pasted
        // chunk that predates sName. Firing is by button, so name is cosmetic here.
        if (const char* disp = ini.GetValue(s.pItem, "sName", nullptr); disp && *disp)
            m.name = disp;
        else
            m.name = std::string(section.substr(6));
        m.button = ParseBindingRef(ini.GetValue(s.pItem, "iButton", ""), -1);
        m.turbo  = ini.GetBoolValue(s.pItem, "bTurbo", false);

        for (int i = 0;; ++i) {
            const std::string key = "Step" + std::to_string(i);
            const char* line = ini.GetValue(s.pItem, key.c_str(), nullptr);
            if (!line) break;  // steps are contiguous Step0..StepN

            MacroStep step;
            if (ParseStep(line, step))
                m.steps.push_back(std::move(step));
            else
                MacroLog("Warning: macro '" + m.name + "' " + key + " is malformed; skipped.");
        }

        if (m.steps.empty()) {
            MacroLog("Warning: macro '" + m.name + "' has no valid steps; ignored.");
            continue;
        }
        if (m.button.value < 1) {
            MacroLog("Warning: macro '" + m.name + "' has no valid iButton; ignored.");
            continue;
        }

        s_macros.push_back(std::move(m));
    }

    s_runtime.assign(s_macros.size(), MacroRuntime{});
}

void ReleaseAll() {
    for (size_t i = 0; i < s_macros.size() && i < s_runtime.size(); ++i) {
        ShipOutputSystem::ReleaseOwnerOutputs(MacroOwner(i));
        s_runtime[i] = MacroRuntime{};
    }
}

void SeedDownButtonsConsumed() {
    for (size_t i = 0; i < s_macros.size() && i < s_runtime.size(); ++i)
        s_runtime[i].prevButtonDown = DeviceManager::IsButtonPressed(s_macros[i].button);
}

std::vector<Macro> SnapshotMacros() {
    return s_macros;
}

void RestoreMacros(const std::vector<Macro>& macros) {
    // Caller has already released held macro keys (ReleaseAll). Swap in the new set
    // and reset runtime to all-inactive, sized to match — no stale phase or step
    // index may survive into a different macro list.
    s_macros = macros;
    s_runtime.assign(s_macros.size(), MacroRuntime{});
}

void Update() {
    const uint64_t now = NowMs();

    for (size_t i = 0; i < s_macros.size(); ++i) {
        const Macro&  m     = s_macros[i];
        MacroRuntime& rt    = s_runtime[i];
        const uint32_t owner = MacroOwner(i);

        const bool down = DeviceManager::IsButtonPressed(m.button);

        // Start on the press edge, only if not already running.
        if (down && !rt.prevButtonDown && rt.stepIdx < 0) {
            rt.stepIdx = 0;
            BeginStep(rt, m, now);
        }
        rt.prevButtonDown = down;

        if (rt.stepIdx < 0) continue;  // inactive

        // Turbo means "repeat while held", so releasing stops it at once.
        //
        // A plain sequence is fire-and-forget: one press plays it to the end even if
        // the button comes up first. Aborting on release would make a multi-second
        // macro demand a multi-second hold, and — worse — an early release would
        // leave the sequence half-applied (Grav -> Shields would drain the grav
        // drive and never fill the shields). The master toggle, the stop binding,
        // and opening the wizard all still cancel via ReleaseAll().
        if (!down && m.turbo) {
            ShipOutputSystem::ReleaseOwnerOutputs(owner);
            rt.stepIdx = -1;
            continue;
        }

        const MacroStep& step    = m.steps[rt.stepIdx];
        const uint64_t   elapsed = now - rt.phaseStartMs;

        switch (rt.phase) {
            case MacroPhase::Press:
                PressStep(step, owner);
                rt.phase = MacroPhase::Hold;
                rt.phaseStartMs = now;
                break;

            case MacroPhase::Hold: {
                const uint64_t holdMs = (step.action == MacroAction::Hold)
                    ? static_cast<uint64_t>(std::max(0, step.amount))
                    : kTapHoldMs;
                if (elapsed >= holdMs) {
                    ReleaseStep(step, owner);
                    rt.phase = MacroPhase::Gap;
                    rt.phaseStartMs = now;
                }
                break;
            }

            case MacroPhase::Gap:
                if (elapsed >= static_cast<uint64_t>(std::max(0, step.gapMs))) {
                    // Another tap of the same step?
                    if (step.action == MacroAction::Tap && --rt.repeatsLeft > 0) {
                        rt.phase = MacroPhase::Press;
                        rt.phaseStartMs = now;
                        break;
                    }
                    // Advance to the next step, finish, or loop (turbo, still held).
                    ++rt.stepIdx;
                    if (rt.stepIdx >= static_cast<int>(m.steps.size())) {
                        if (m.turbo && down) { rt.stepIdx = 0; BeginStep(rt, m, now); }
                        else                 { rt.stepIdx = -1; }
                    } else {
                        BeginStep(rt, m, now);
                    }
                }
                break;
        }
    }
}

} // namespace MacroEngine
