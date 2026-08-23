#include "PCH.h"

#include "WizardUI.h"

#include "DeviceManager.h"
#include "WizardCapture.h"
#include "WizardConfig.h"
#include "WizardDefs.h"
#include "WizardSession.h"

#include <imgui.h>

namespace WizardUI {


void DrawDevicesTab(WizardState& s) {
    int devCount = DeviceManager::GetDeviceCount();
    if (devCount == 0) ImGui::TextWrapped("No DirectInput devices detected.");
    ImGui::TextDisabled("Device indices (#N) follow DirectInput enumeration order; name-based bindings survive index changes.");
    ImGui::Spacing();

    auto& devCalib = WizardCapture::GetCalibState();
    constexpr long kGhostThreshold = 5000;

    for (int d = 0; d < devCount; d++) {
        const auto& info = DeviceManager::GetDevice(d);
        std::string header = "#" + std::to_string(d) + ": " + info.productName;
        if (!info.vidpidString.empty()) header += " [" + info.vidpidString + "]";

        if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(12.0f);
            ImGui::Text("Instance: %s", info.instanceName.c_str());
            ImGui::Text("Axes: %d  Buttons: %d", info.axisCount, info.buttonCount);

            // Swap button for duplicate device names
            if (d + 1 < devCount && info.productName == DeviceManager::GetDevice(d + 1).productName) {
                char swapLabel[64];
                std::snprintf(swapLabel, sizeof(swapLabel), "Swap #%d <-> #%d", d, d + 1);
                if (ImGui::SmallButton(swapLabel)) {
                    char prefA[16], prefB[16], tempPref[16];
                    std::snprintf(prefA, sizeof(prefA), "#%d@", d);
                    std::snprintf(prefB, sizeof(prefB), "#%d@", d + 1);
                    std::snprintf(tempPref, sizeof(tempPref), "#__SWAP__@");
                    size_t lenA = strlen(prefA), lenB = strlen(prefB), tempLen = strlen(tempPref);

                    auto doSwap = [&](std::string& binding) {
                        if (binding.substr(0, lenA) == prefA) binding = std::string(tempPref) + binding.substr(lenA);
                        else if (binding.substr(0, lenB) == prefB) binding = std::string(prefA) + binding.substr(lenB);
                    };
                    auto finalize = [&](std::string& binding) {
                        if (binding.substr(0, tempLen) == tempPref) binding = std::string(prefB) + binding.substr(tempLen);
                    };

                    std::vector<std::string*> allBindings;
                    for (auto& b : s.axisBindings) allBindings.push_back(&b);
                    for (auto& b : s.buttonBindings) allBindings.push_back(&b);
                    for (auto& b : s.controlExtensionBindings) allBindings.push_back(&b);
                    for (auto& b : s.digitalAxisBindings) allBindings.push_back(&b);
                    for (auto& sa : s.shipActionSlots) allBindings.push_back(&sa.binding);
                    for (auto& cb : s.customBindings) allBindings.push_back(&cb.buttonBinding);
                    for (auto& b : s.aimAxisBindings) allBindings.push_back(&b);
                    for (auto& b : s.digitalAimBindings) allBindings.push_back(&b);
                    allBindings.push_back(&s.toggleAimModeBinding);
                    allBindings.push_back(&s.turnAssistBinding);
                    for (auto& macro : s.macros) allBindings.push_back(&macro.buttonBinding);

                    for (auto* bp : allBindings) doSwap(*bp);
                    for (auto* bp : allBindings) finalize(*bp);

                    std::unordered_map<int, std::pair<long, long>> swappedCalibration;
                    for (const auto& [key, range] : s.calibData) {
                        int deviceIndex = key >> 8;
                        if (deviceIndex == d) deviceIndex = d + 1;
                        else if (deviceIndex == d + 1) deviceIndex = d;
                        swappedCalibration[(deviceIndex << 8) | (key & 0xFF)] = range;
                    }
                    s.calibData.swap(swappedCalibration);
                    WizardSession::SwapActivationDeviceIndices(d, d + 1);
                    WizardSession::SetStatus("Device reassignment staged. Save & Apply to commit it.",
                                             WizardSession::StatusKind::Warning);
                    Log("Staged device index swap #" + std::to_string(d) + " <-> #" + std::to_string(d + 1));
                }
            }

            // Device calibration
            bool isCalibThisDevice = devCalib.active && devCalib.deviceIndex == d;
            if (isCalibThisDevice) {
                const auto* st = DeviceManager::GetCachedState(d);
                if (st) {
                    for (int a = 0; a < 8; a++) {
                        long val = DeviceManager::GetAxisFromState(st, 0x30 + a);
                        if (val < devCalib.observedMin[a]) devCalib.observedMin[a] = val;
                        if (val > devCalib.observedMax[a]) devCalib.observedMax[a] = val;
                    }
                }
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Move ALL axes to their full extremes, then click Done.");
                int activeCount = 0;
                for (int a = 0; a < 8; a++) {
                    if (devCalib.observedMax[a] - devCalib.observedMin[a] > kGhostThreshold) activeCount++;
                }
                ImGui::Text("Detected %d active axes", activeCount);

                if (ImGui::Button("Done##devCalib")) {
                    int saved = 0;
                    for (int a = 0; a < 8; a++) {
                        if (devCalib.observedMax[a] - devCalib.observedMin[a] > kGhostThreshold) {
                            s.calibData[(d << 8) | (0x30 + a)] = { devCalib.observedMin[a], devCalib.observedMax[a] };
                            saved++;
                        }
                    }
                    Log("Calibrated device #" + std::to_string(d) + ": " + std::to_string(saved) + " axes saved");
                    devCalib.Reset();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel##devCalib")) devCalib.Reset();
            } else {
                bool hasCalib = false;
                for (int a = 0; a < 8; a++) {
                    if (s.calibData.count((d << 8) | (0x30 + a))) { hasCalib = true; break; }
                }
                if (hasCalib) {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Calibration:");
                    for (int a = 0; a < 8; a++) {
                        int calibKey = (d << 8) | (0x30 + a);
                        auto calibIt = s.calibData.find(calibKey);
                        if (calibIt != s.calibData.end()) {
                            ImGui::Text("  0x%02X (%s): [%ld - %ld]", 0x30 + a, WizardCapture::AxisName(0x30 + a),
                                calibIt->second.first, calibIt->second.second);
                        }
                    }
                    ImGui::PushID(d * 1000 + 998);
                    if (ImGui::SmallButton("Clear All##calib")) {
                        for (int a = 0; a < 8; a++) s.calibData.erase((d << 8) | (0x30 + a));
                        Log("Cleared all calibration for device #" + std::to_string(d));
                    }
                    ImGui::PopID();
                }
                if (!devCalib.active) {
                    ImGui::Spacing();
                    ImGui::PushID(d * 1000 + 999);
                    if (ImGui::SmallButton("Calibrate Device")) {
                        devCalib.Reset();
                        devCalib.active = true;
                        devCalib.deviceIndex = d;
                        const auto* st = DeviceManager::GetCachedState(d);
                        if (st) {
                            for (int a = 0; a < 8; a++) {
                                long val = DeviceManager::GetAxisFromState(st, 0x30 + a);
                                devCalib.observedMin[a] = val;
                                devCalib.observedMax[a] = val;
                            }
                        }
                    }
                    ImGui::PopID();
                }
            }
            ImGui::Unindent(12.0f);
        }
    }
}


void DrawPluginControls(WizardState& s) {
    ImGui::TextWrapped("Master runtime controls for enabling or parking AbsoluteHOTAS output.");
    for (int i = 0; i < kNumButtonSlots; ++i) {
        ImGui::PushID(2000 + i);
        DrawBindingRow(kButtonSlots[i].label, s.buttonBindings[i], CaptureSlot::kButtonBase + i, false);
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Ctrl+Alt+B always remains available as the keyboard recovery shortcut.");

    ImGui::Spacing();
    ImGui::SeparatorText("AUTOMATIC PILOT CONTEXT");
    ImGui::TextWrapped(
        "AbsoluteHOTAS detects active piloting from Starfield's selected flight-handler "
        "cadence. The cached ship object is not used as a pilot flag.");

    static constexpr const char* kGateModes[] = {
        "Do not park automatically",
        "Park flight controls only (recommended)",
        "Park all plugin output",
    };
    s.pilotGateMode = std::clamp(s.pilotGateMode, 0, 2);
    ImGui::SetNextItemWidth(320.0f);
    ImGui::Combo("Outside the pilot seat", &s.pilotGateMode,
                 kGateModes, static_cast<int>(std::size(kGateModes)));
    ImGui::Checkbox("Use automatic pilot detection", &s.automaticPilotSignal);
    if (s.automaticPilotSignal) {
        ImGui::SetNextItemWidth(220.0f);
        ImGui::SliderInt("Flight-control latch", &s.pilotLatchMilliseconds,
                         500, 30000, "%d ms", ImGuiSliderFlags_AlwaysClamp);
        ImGui::TextDisabled(
            "The longer latch tolerates targeting-mode pauses. Menus/loading suspend "
            "output without being classified as on foot.");
        ImGui::TextDisabled(
            "Camera Look uses a separate conservative 400 ms freshness gate and may "
            "pause temporarily in targeting mode.");
    } else {
        ImGui::TextDisabled(
            "Manual is retained for diagnostics and follows [Gate] iManualToggleKey.");
    }
}

// --- Tab: Macros ---

// Target picker: named ship/context actions first, raw keys/mouse second. An
// unrecognized token (hand-edited INI) previews as itself
// rather than vanishing.
static void DrawTargetCombo(std::string& token) {
    const char* label   = FindMacroTargetLabel(token);
    const char* preview = label ? label : token.c_str();

    ImGui::PushItemWidth(170);
    if (ImGui::BeginCombo("##target", preview)) {
        ImGui::SeparatorText("Ship Actions & Context Inputs");
        for (int i = 0; i < kNumShipActionTargets; i++) {
            const bool sel = (token == kShipActionTargets[i].value);
            ImGui::PushID(i);
            if (ImGui::Selectable(kShipActionTargets[i].label, sel)) token = kShipActionTargets[i].value;
            if (sel) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::SeparatorText("Keys & Mouse");
        for (int i = 0; i < kOutputCatalogSize; i++) {
            const bool sel = (token == kOutputCatalog[i].value);
            ImGui::PushID(1000 + i);
            if (ImGui::Selectable(kOutputCatalog[i].label, sel)) token = kOutputCatalog[i].value;
            if (sel) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();
}

static std::string UniqueMacroName(const WizardState& s) {
    for (int n = 1;; ++n) {
        std::string candidate = "Macro" + std::to_string(n);
        bool taken = false;
        for (const auto& m : s.macros) {
            if (m.name == candidate) { taken = true; break; }
        }
        if (!taken) return candidate;
    }
}

static void DrawMacroSteps(MacroRow& m) {
    int removeStep = -1, moveUp = -1, moveDown = -1;

    for (int si = 0; si < (int)m.steps.size(); si++) {
        auto& st = m.steps[si];
        ImGui::PushID(si);

        ImGui::Text("%2d", si);
        ImGui::SameLine();

        // Targets. More than one = a chord: pressed together in the same step.
        int removeTarget = -1;
        for (int ti = 0; ti < (int)st.targets.size(); ti++) {
            if (ti) { ImGui::SameLine(0, 4); ImGui::Text("+"); ImGui::SameLine(0, 4); }
            ImGui::PushID(ti);
            DrawTargetCombo(st.targets[ti]);
            if (st.targets.size() > 1) {
                ImGui::SameLine(0, 2);
                if (ImGui::SmallButton("x")) removeTarget = ti;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove this target from the chord");
            }
            ImGui::PopID();
        }
        if (removeTarget >= 0) st.targets.erase(st.targets.begin() + removeTarget);

        ImGui::SameLine(0, 4);
        if (ImGui::SmallButton("+")) st.targets.push_back("key:0x1E");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add a target to press together with this one (chord)");

        // Tap vs hold, and the amount that means different things for each.
        ImGui::SameLine();
        int actionIdx = st.hold ? 1 : 0;
        const char* kActions[] = { "Tap", "Hold" };
        ImGui::PushItemWidth(70);
        if (ImGui::Combo("##action", &actionIdx, kActions, 2)) st.hold = (actionIdx == 1);
        ImGui::PopItemWidth();

        ImGui::SameLine();
        ImGui::TextDisabled(st.hold ? "for" : "x");
        ImGui::SameLine();
        ImGui::PushItemWidth(64);
        ImGui::InputInt("##amount", &st.amount, 0);
        ImGui::PopItemWidth();
        if (st.amount < 0) st.amount = 0;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(st.hold ? "How long to hold, in milliseconds" : "How many times to press");

        ImGui::SameLine();
        ImGui::TextDisabled(st.hold ? "ms, gap" : "times, gap");
        ImGui::SameLine();
        ImGui::PushItemWidth(64);
        ImGui::InputInt("##gap", &st.gapMs, 0);
        ImGui::PopItemWidth();
        if (st.gapMs < 0) st.gapMs = 0;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Milliseconds to wait before the next step");
        ImGui::SameLine();
        ImGui::TextDisabled("ms");

        ImGui::SameLine();
        if (ImGui::SmallButton("^")) moveUp = si;
        ImGui::SameLine();
        if (ImGui::SmallButton("v")) moveDown = si;
        ImGui::SameLine();
        if (ImGui::SmallButton("Del")) removeStep = si;

        ImGui::PopID();
    }

    if (moveUp > 0)                                     std::swap(m.steps[moveUp], m.steps[moveUp - 1]);
    if (moveDown >= 0 && moveDown + 1 < (int)m.steps.size()) std::swap(m.steps[moveDown], m.steps[moveDown + 1]);
    if (removeStep >= 0)                                m.steps.erase(m.steps.begin() + removeStep);
}

void DrawMacrosTab(WizardState& s) {
    ImGui::TextWrapped(
        "A macro plays an ordered sequence of actions from one button press. Steps can target "
        "named ship actions, dedicated menu inputs, or explicit raw keys/mouse buttons. "
        "Press '+' on a step to add targets pressed together as a chord.");
    ImGui::TextWrapped(
        "One press runs the whole sequence to the end - you do not need to hold the button. "
        "Turbo instead repeats the sequence for as long as the button IS held.");
    if (ImGui::Button("Add Macro")) {
        MacroRow m;
        m.name = UniqueMacroName(s);
        m.steps.push_back({ {"NextSystem"}, false, 1, 50 });
        s.macros.push_back(std::move(m));
    }
    ImGui::SameLine();
    if (ImGui::Button("Add \"Grav -> Shields\" Preset")) {
        MacroRow m;
        m.name = "Power Grav to Shields";
        m.steps.push_back({ {"NextSystem"},          false, 6,    5 });  // right edge = GRV (clamps)
        m.steps.push_back({ {"DecreaseSystemPower"}, true,  1200, 0 });  // drain grav -> surplus
        m.steps.push_back({ {"PreviousSystem"},      false, 1,    5 });  // GRV -> SHD (adjacent)
        m.steps.push_back({ {"IncreaseSystemPower"}, true,  1200, 0 });  // pour surplus into shields
        s.macros.push_back(std::move(m));
        Log("Added Grav -> Shields preset macro.");
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Dumps grav-drive power into shields in one press.\n"
                          "Anchors to the right edge of the power bar, so it works\n"
                          "regardless of your weapon loadout. Bind a button and Save.");

    ImGui::Spacing();

    if (s.macros.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                           "No macros. Click 'Add Macro', or try the Grav -> Shields preset.");
        return;
    }

    int removeMacro = -1;
    for (int mi = 0; mi < (int)s.macros.size(); mi++) {
        auto& m = s.macros[mi];
        ImGui::PushID(6000 + mi);

        // Duplicate friendly names are allowed now (they get distinct section keys on
        // save). Disambiguate them for display only, with an integer suffix in paste
        // order: "Power Grav to Shield (0)", "(1)", ...
        int ordinal = 0, sameName = 0;
        for (int j = 0; j < (int)s.macros.size(); j++) {
            if (s.macros[j].name == m.name) { sameName++; if (j < mi) ordinal++; }
        }
        char disp[96];
        if (sameName > 1) std::snprintf(disp, sizeof(disp), "%s (%d)", m.name.c_str(), ordinal);
        else              std::snprintf(disp, sizeof(disp), "%s", m.name.c_str());

        char header[160];
        std::snprintf(header, sizeof(header), "%s%s###macro%d",
                      m.name.empty() ? "(unnamed)" : disp,
                      m.buttonBinding == "(unbound)" ? "  [no button]" : "", mi);

        if (ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(12);

            char nameBuf[64];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", m.name.c_str());
            ImGui::PushItemWidth(200);
            if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) m.name = nameBuf;
            ImGui::PopItemWidth();
            if (m.name.empty()) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "name required - will not save");
            }

            if (mi < 100) {
                DrawBindingRow("Trigger Button", m.buttonBinding, CaptureSlot::kMacroBase + mi, false);
            }

            if (ImGui::BeginTable("MacroOptions", 2,
                    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
                ImGui::TableSetupColumn("Options", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Delete", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Checkbox("Turbo (repeat while held)", &m.turbo);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Off: one press plays the sequence once, to the end.\n"
                                      "On:  the sequence repeats while the button is held,\n"
                                      "     and stops as soon as you release it.");
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("Delete Macro")) removeMacro = mi;
                ImGui::EndTable();
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Steps");
            DrawMacroSteps(m);

            if (ImGui::Button("Add Step")) m.steps.push_back({ {"NextSystem"}, false, 1, 50 });
            if (m.steps.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Add a step - this macro will not run yet.");
            else if (m.buttonBinding == "(unbound)")
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Bind a trigger button - this macro will not run yet.");

            ImGui::Unindent(12);
            ImGui::Spacing();
        }
        ImGui::PopID();
    }
    if (removeMacro >= 0) s.macros.erase(s.macros.begin() + removeMacro);
}

}  // namespace WizardUI
