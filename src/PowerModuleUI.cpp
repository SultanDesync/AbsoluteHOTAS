#include "PCH.h"

#include "AbsolutePowerAPI.h"
#include "PowerModuleUI.h"
#include "RuntimePaths.h"
#include "SuiteCommandBindings.h"

#include <imgui.h>

namespace {
using namespace AbsolutePowerApi;

constexpr std::string_view kModuleId{"absolute.power"};
constexpr ImVec4 kAccent{0.20F, 0.72F, 0.95F, 1.0F};
constexpr ImVec4 kGood{0.25F, 0.86F, 0.52F, 1.0F};
constexpr ImVec4 kWarn{1.0F, 0.64F, 0.16F, 1.0F};
constexpr ImVec4 kBad{1.0F, 0.30F, 0.30F, 1.0F};
constexpr ImVec4 kGreen{0.16F, 0.68F, 0.34F, 1.0F};
constexpr ImVec4 kYellow{0.90F, 0.66F, 0.12F, 1.0F};
constexpr ImVec4 kRed{0.78F, 0.22F, 0.24F, 1.0F};
constexpr ImVec4 kHollow{0.10F, 0.13F, 0.17F, 1.0F};
constexpr std::array<const char*, kSystemCount> kSystemLabels{
    "Weapon 1", "Weapon 2", "Weapon 3", "Engines", "Shields", "Grav Drive"};
constexpr std::array<const char*, kSystemCount> kHudLabels{
    "W0", "W1", "W2", "ENG", "SHD", "GRV"};
constexpr std::array<const char*, kSystemCount> kSystemKeys{
    "Weapon0", "Weapon1", "Weapon2", "Engine", "Shield", "GravDrive"};

using QueryApi = const ApiV1*(__cdecl*)(std::uint32_t) noexcept;
const ApiV1* g_api{};
bool g_loaded{};
bool g_dirty{};
std::vector<PresetV1> g_presets;
std::vector<RuleV1> g_rules;
std::vector<PresetV1> g_openingPresets;
std::vector<RuleV1> g_openingRules;
std::vector<std::string> g_openingPresetIds;
std::vector<std::string> g_openingRuleIds;
std::string g_startupPreset;
std::string g_openingStartupPreset;
bool g_automationEnabled{};
bool g_openingAutomationEnabled{};
std::size_t g_selectedPreset{};
std::uint32_t g_newPresetIndex{1};
std::uint32_t g_newRuleIndex{1};
char g_renameBuffer[kLabelCapacity]{};
std::string g_status{"Absolute Power not detected."};
bool g_capturePopupRequested{};
std::chrono::steady_clock::time_point g_nextRefresh{};

enum class Tier : std::uint8_t { Green, Yellow, Red, Hollow };
Tier g_addTier{Tier::Green};

template <std::size_t Size>
void Copy(char (&destination)[Size], std::string_view value) {
    std::ranges::fill(destination, '\0');
    std::memcpy(destination, value.data(), std::min(value.size(), Size - 1));
}

std::string Bounded(const char* value, std::size_t capacity) {
    return value ? std::string(value, strnlen_s(value, capacity)) : std::string{};
}

const char* ResultName(Result result) {
    switch (result) {
        case Result::Ok: return "ok";
        case Result::NotReady: return "not ready";
        case Result::WorkbenchMissing: return "Workbench handshake missing";
        case Result::UnsupportedRuntime: return "unsupported runtime";
        case Result::NativeSeamUnavailable: return "native seam unavailable";
        case Result::PilotNotReady: return "pilot not ready";
        case Result::InvalidArgument: return "invalid argument";
        case Result::NotFound: return "not found";
        case Result::Rejected: return "rejected";
        case Result::Conflict: return "binding conflict";
        case Result::WriteFailure: return "configuration write failed";
    }
    return "unknown";
}

const char* HotasResultName(AbsoluteHOTASApi::Result result) {
    switch (result) {
        case AbsoluteHOTASApi::Result::Ok: return "ok";
        case AbsoluteHOTASApi::Result::NotReady: return "not ready";
        case AbsoluteHOTASApi::Result::InvalidArgument: return "invalid argument";
        case AbsoluteHOTASApi::Result::NotFound: return "command not found";
        case AbsoluteHOTASApi::Result::Busy: return "capture busy";
        case AbsoluteHOTASApi::Result::WriteFailure: return "write failed";
    }
    return "unknown";
}

template <class Record>
bool SameRecords(const std::vector<Record>& left, const std::vector<Record>& right) {
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(), [](const auto& a, const auto& b) {
               return std::memcmp(&a, &b, sizeof(Record)) == 0;
           });
}

void UpdateDirty(std::string detail) {
    g_dirty = !SameRecords(g_presets, g_openingPresets) ||
              !SameRecords(g_rules, g_openingRules) ||
              g_startupPreset != g_openingStartupPreset ||
              g_automationEnabled != g_openingAutomationEnabled;
    g_status = std::move(detail);
}

bool ContainsId(const auto& records, std::string_view id) {
    return std::ranges::any_of(records, [id](const auto& record) {
        return Bounded(record.id, kIdCapacity) == id;
    });
}

std::string TriggerName(std::uint8_t trigger) {
    constexpr std::array<const char*, 4> values{
        "WeaponFired", "IncomingDamage", "ThrottleAbove", "Manual"};
    return trigger < values.size() ? values[trigger] : "Manual";
}

std::string SystemName(std::uint8_t system, bool allowAny) {
    return system < kSystemKeys.size() ? kSystemKeys[system] : allowAny ? "Any" : "Invalid";
}

bool ApiValid(const ApiV1* api) {
    constexpr std::size_t minimum =
        offsetof(ApiV1, previewPreset) + sizeof(api->previewPreset);
    return api && api->structSize >= minimum && api->abiVersion == kAbiVersion &&
           api->moduleId && std::string_view(api->moduleId) == kModuleId && api->displayName &&
           api->version && api->getStatus && api->getSnapshot && api->getPresetCount &&
           api->getPreset && api->getRuleCount && api->getRule && api->getCommandCount &&
           api->getCommand && api->invokeCommand && api->setAutomationEnabled &&
           api->reloadConfiguration && api->previewPreset;
}

bool ReadBackend(std::vector<PresetV1>& presets, std::vector<RuleV1>& rules,
                 StatusV1& status, std::string& detail) {
    if (!g_api || g_api->getStatus(&status) != Result::Ok) {
        detail = "Absolute Power status is unavailable.";
        return false;
    }
    const auto presetCount = g_api->getPresetCount();
    const auto ruleCount = g_api->getRuleCount();
    if (presetCount > 256 || ruleCount > 256) {
        detail = "Absolute Power record count exceeds the editor safety limit.";
        return false;
    }
    presets.clear(); rules.clear();
    for (std::uint32_t index = 0; index < presetCount; ++index) {
        PresetV1 preset{};
        if (g_api->getPreset(index, &preset) != Result::Ok) {
            detail = std::format("Preset {} could not be read.", index);
            return false;
        }
        presets.push_back(preset);
    }
    for (std::uint32_t index = 0; index < ruleCount; ++index) {
        RuleV1 rule{};
        if (g_api->getRule(index, &rule) != Result::Ok) {
            detail = std::format("Rule {} could not be read.", index);
            return false;
        }
        rules.push_back(rule);
    }
    return true;
}

bool LoadDraft(bool resetOpening) {
    const bool firstLoad = !g_loaded;
    StatusV1 status{};
    std::vector<PresetV1> presets;
    std::vector<RuleV1> rules;
    std::string detail;
    if (!ReadBackend(presets, rules, status, detail)) {
        g_status = std::move(detail);
        return false;
    }
    g_presets = std::move(presets);
    g_rules = std::move(rules);
    g_automationEnabled = status.automationEnabled != 0;
    if (resetOpening) {
        std::array<char, kIdCapacity> shipped{};
        std::array<char, kIdCapacity> configured{};
        const auto shippedPath = (RuntimePaths::PluginDirectory() / L"AbsolutePower.ini").string();
        const auto customPath = (RuntimePaths::PluginDirectory() / L"AbsolutePower_Custom.ini").string();
        GetPrivateProfileStringA("General", "sStartupPreset", status.activePreset,
                                 shipped.data(), static_cast<DWORD>(shipped.size()),
                                 shippedPath.c_str());
        GetPrivateProfileStringA("General", "sStartupPreset", shipped.data(),
                                 configured.data(), static_cast<DWORD>(configured.size()),
                                 customPath.c_str());
        g_startupPreset = configured.data();
        if (g_startupPreset.empty() && !g_presets.empty())
            g_startupPreset = Bounded(g_presets.front().id, kIdCapacity);
    }
    if (resetOpening) {
        g_openingPresets = g_presets;
        g_openingRules = g_rules;
        g_openingStartupPreset = g_startupPreset;
        g_openingAutomationEnabled = g_automationEnabled;
        g_openingPresetIds.clear(); g_openingRuleIds.clear();
        for (const auto& preset : g_presets)
            g_openingPresetIds.push_back(Bounded(preset.id, kIdCapacity));
        for (const auto& rule : g_rules)
            g_openingRuleIds.push_back(Bounded(rule.id, kIdCapacity));
        g_dirty = false;
    }
    if (g_selectedPreset >= g_presets.size())
        g_selectedPreset = g_presets.empty() ? 0 : g_presets.size() - 1;
    g_loaded = true;
    g_nextRefresh = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    if (firstLoad)
        g_status = std::format("Absolute Power {} registered with AbsoluteHOTAS.", g_api->version);
    return true;
}

bool EnsureAvailable() {
    if (g_api) return true;
    const HMODULE module = GetModuleHandleW(L"AbsolutePower.dll");
    if (!module) return false;
    const FARPROC address = GetProcAddress(module, "AbsolutePower_QueryApi");
    if (!address) {
        g_status = "AbsolutePower.dll lacks its suite API export.";
        return false;
    }
    const auto candidate = reinterpret_cast<QueryApi>(address)(kAbiVersion);
    if (!ApiValid(candidate)) {
        g_status = "Absolute Power API is incompatible or incomplete.";
        return false;
    }
    g_api = candidate;
    return LoadDraft(true);
}

bool WriteCustomConfiguration() {
    const auto destination = RuntimePaths::PluginDirectory() / L"AbsolutePower_Custom.ini";
    auto temporary = destination;
    temporary += L".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) return false;
        stream << "; Absolute Power custom overlay managed by the active Absolute suite UI.\n\n"
               << "[General]\nsStartupPreset=" << g_startupPreset
               << "\nbAutomationEnabled=" << (g_automationEnabled ? "true" : "false")
               << "\n\n";
        for (const auto& id : g_openingPresetIds) {
            if (!ContainsId(g_presets, id))
                stream << "[Preset." << id << "]\nDeleted=true\n\n";
        }
        for (const auto& preset : g_presets) {
            const std::string id = Bounded(preset.id, kIdCapacity);
            stream << "[Preset." << id << "]\nName=" << Bounded(preset.label, kLabelCapacity)
                   << "\nOrder=";
            for (std::size_t index = 0; index < kSystemCount; ++index) {
                if (index) stream << ',';
                stream << SystemName(preset.tieBreakOrder[index], false);
            }
            stream << '\n';
            for (std::size_t system = 0; system < kSystemCount; ++system) {
                const auto& plan = preset.systems[system];
                stream << kSystemKeys[system] << '=' << plan.green << ',' << plan.yellow
                       << ',' << plan.red << '\n';
            }
            stream << '\n';
        }
        for (const auto& id : g_openingRuleIds) {
            if (!ContainsId(g_rules, id))
                stream << "[Rule." << id << "]\nDeleted=true\n\n";
        }
        for (const auto& rule : g_rules) {
            stream << "[Rule." << Bounded(rule.id, kIdCapacity) << "]\nName="
                   << Bounded(rule.label, kLabelCapacity) << "\nEnabled="
                   << (rule.enabled ? "true" : "false") << "\nTrigger="
                   << TriggerName(rule.trigger) << "\nSource="
                   << SystemName(rule.sourceSystem, true) << "\nTarget="
                   << SystemName(rule.targetSystem, false) << "\nTargetPips=";
            if (rule.targetPips == std::numeric_limits<std::uint16_t>::max()) stream << "Max";
            else stream << rule.targetPips;
            stream << "\nThresholdPercent=" << static_cast<unsigned>(rule.thresholdPercent)
                   << "\nHysteresisPercent=" << static_cast<unsigned>(rule.hysteresisPercent)
                   << "\nHoldMilliseconds=" << rule.holdMilliseconds
                   << "\nPriority=" << rule.priority << "\n\n";
        }
        stream.flush();
        if (!stream) {
            std::error_code error; std::filesystem::remove(temporary, error); return false;
        }
    }
    if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code error; std::filesystem::remove(temporary, error); return false;
    }
    return true;
}

std::uint32_t Total(const TierPlanV1& plan) {
    return static_cast<std::uint32_t>(plan.green) + plan.yellow + plan.red;
}

Tier TierAt(const TierPlanV1& plan, std::uint32_t index) {
    if (index < plan.green) return Tier::Green;
    if (index < static_cast<std::uint32_t>(plan.green) + plan.yellow) return Tier::Yellow;
    if (index < Total(plan)) return Tier::Red;
    return Tier::Hollow;
}

ImVec4 TierColor(Tier tier) {
    switch (tier) {
        case Tier::Green: return kGreen;
        case Tier::Yellow: return kYellow;
        case Tier::Red: return kRed;
        case Tier::Hollow: return kHollow;
    }
    return kHollow;
}

void AddTier(TierPlanV1& plan, Tier tier, std::uint32_t count = 1) {
    auto Add = [count](std::uint16_t& value) {
        value = static_cast<std::uint16_t>(std::min<std::uint32_t>(value + count, 99));
    };
    if (tier == Tier::Green) Add(plan.green);
    else if (tier == Tier::Yellow) Add(plan.yellow);
    else if (tier == Tier::Red) Add(plan.red);
}

void TrimTo(TierPlanV1& plan, std::uint32_t total) {
    const auto green = std::min<std::uint32_t>(plan.green, total);
    total -= green;
    const auto yellow = std::min<std::uint32_t>(plan.yellow, total);
    total -= yellow;
    const auto red = std::min<std::uint32_t>(plan.red, total);
    plan.green = static_cast<std::uint16_t>(green);
    plan.yellow = static_cast<std::uint16_t>(yellow);
    plan.red = static_cast<std::uint16_t>(red);
}

void PushTierStyle(Tier tier, bool selected = false) {
    const auto color = TierColor(tier);
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
        ImVec4(std::min(color.x + .12F, 1.F), std::min(color.y + .12F, 1.F),
               std::min(color.z + .12F, 1.F), 1.F));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
        ImVec4(color.x * .82F, color.y * .82F, color.z * .82F, 1.F));
    ImGui::PushStyleColor(ImGuiCol_Border, selected ? ImVec4(1,1,1,1) : ImVec4(0,0,0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, selected ? 2.F : 0.F);
}

void PopTierStyle() { ImGui::PopStyleVar(); ImGui::PopStyleColor(4); }

void DrawTierSelector() {
    ImGui::TextUnformatted("ADD PIPS AS");
    constexpr std::array<Tier, 3> tiers{Tier::Green, Tier::Yellow, Tier::Red};
    constexpr std::array<const char*, 3> labels{
        "1 GREEN - FIRST", "2 YELLOW - SECOND", "3 RED - RESERVE"};
    for (std::size_t index = 0; index < tiers.size(); ++index) {
        ImGui::SameLine(); ImGui::PushID(static_cast<int>(index));
        PushTierStyle(tiers[index], g_addTier == tiers[index]);
        if (ImGui::Button(labels[index])) g_addTier = tiers[index];
        PopTierStyle(); ImGui::PopID();
    }
}

void DrawPowerColumn(std::size_t index, PresetV1& preset, const SnapshotV1* snapshot,
                     const PreviewV1* preview, int tallest, bool& changed) {
    auto& plan = preset.systems[index];
    const auto* live = snapshot ? &snapshot->systems[index] : nullptr;
    const int requested = static_cast<int>(Total(plan));
    const int capacity = live && live->present ? std::min<int>(live->maximum, 32) : 0;
    const int visible = std::max(requested, capacity);
    ImGui::TextColored(kAccent, "%s", kHudLabels[index]);
    ImGui::TextWrapped("%s", kSystemLabels[index]);
    if (live && live->present) ImGui::Text("NOW %u / %u", live->current, live->maximum);
    else ImGui::TextDisabled(snapshot ? "NOT INSTALLED" : "SHIP OFFLINE");
    if (preview) ImGui::Text("TARGET %u", preview->target[index]);
    else ImGui::TextDisabled("TARGET --");
    constexpr float height = 18.F;
    if (tallest > visible) ImGui::Dummy(ImVec2(1, (tallest - visible) * (height + 7.F)));
    for (int pip = visible - 1; pip >= 0; --pip) {
        ImGui::PushID(pip);
        const Tier tier = TierAt(plan, static_cast<std::uint32_t>(pip));
        const bool powered = live && live->present && pip < live->current;
        PushTierStyle(tier, powered);
        const char* symbol = tier == Tier::Green ? "1" : tier == Tier::Yellow ? "2" :
                             tier == Tier::Red ? "3" : "-";
        if (ImGui::Button(symbol, ImVec2(std::max(26.F, ImGui::GetContentRegionAvail().x), height))) {
            if (tier == Tier::Hollow) AddTier(plan, g_addTier, pip + 1 - requested);
            else TrimTo(plan, static_cast<std::uint32_t>(pip));
            changed = true;
        }
        PopTierStyle(); ImGui::PopID();
    }
    constexpr std::array<Tier, 3> tiers{Tier::Green, Tier::Yellow, Tier::Red};
    for (std::size_t tier = 0; tier < tiers.size(); ++tier) {
        if (tier) ImGui::SameLine(0, 3); ImGui::PushID(static_cast<int>(100 + tier));
        PushTierStyle(tiers[tier]);
        if (ImGui::SmallButton(std::format("{}+", tier + 1).c_str())) {
            AddTier(plan, tiers[tier]); changed = true;
        }
        PopTierStyle(); ImGui::PopID();
    }
    ImGui::SameLine(0, 3);
    if (ImGui::SmallButton("-") && requested) { TrimTo(plan, requested - 1); changed = true; }
    ImGui::Text("1:%u 2:%u 3:%u", plan.green, plan.yellow, plan.red);
}

void BeginHotasCapture(const std::string& commandId) {
    auto* api = SuiteCommandBindings::GetApi();
    const auto result = api->beginButtonCapture(kModuleId.data(), commandId.c_str());
    if (result == AbsoluteHOTASApi::Result::Ok) g_capturePopupRequested = true;
    else g_status = std::format("HOTAS capture could not start: {}.", HotasResultName(result));
}

void DrawBinding(const std::string& presetId) {
    auto* api = SuiteCommandBindings::GetApi();
    const std::string command = "preset:" + presetId;
    AbsoluteHOTASApi::CommandBindingV1 binding{};
    const auto result = api->getCommandBinding(kModuleId.data(), command.c_str(), &binding);
    const bool bound = result == AbsoluteHOTASApi::Result::Ok;
    ImGui::SeparatorText("PRESET HOTAS BINDING");
    ImGui::TextDisabled("AbsoluteHOTAS owns this joystick binding and invokes Power on its press edge.");
    const std::string label = bound ? Bounded(binding.binding, AbsoluteHOTASApi::kBindingCapacity)
                                    : "Bind HOTAS button...";
    if (ImGui::Button(label.c_str(), ImVec2(260, 0))) BeginHotasCapture(command);
    if (bound) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear binding")) {
            const auto clear = api->clearCommandBinding(kModuleId.data(), command.c_str());
            g_status = std::format("Clear HOTAS binding: {}.", HotasResultName(clear));
        }
    }
}

void DrawCaptureModal() {
    if (g_capturePopupRequested) {
        ImGui::OpenPopup("Bind Power preset HOTAS button");
        g_capturePopupRequested = false;
    }
    if (!ImGui::BeginPopupModal("Bind Power preset HOTAS button", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) return;
    auto* api = SuiteCommandBindings::GetApi();
    AbsoluteHOTASApi::CaptureV1 capture{};
    const auto result = api->pollButtonCapture(&capture);
    ImGui::TextColored(kAccent, "Press one button or POV direction.");
    if (result == AbsoluteHOTASApi::Result::Ok)
        ImGui::TextWrapped("%s", Bounded(capture.detail, AbsoluteHOTASApi::kDetailCapacity).c_str());
    if (result == AbsoluteHOTASApi::Result::Ok &&
        capture.state == AbsoluteHOTASApi::CaptureState::Captured) {
        g_status = std::format("HOTAS binding saved: {}.",
            Bounded(capture.binding, AbsoluteHOTASApi::kBindingCapacity));
        ImGui::CloseCurrentPopup();
    } else if (ImGui::Button("Cancel", ImVec2(100, 0))) {
        (void)api->cancelButtonCapture();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void DrawPresets(const SnapshotV1& snapshot, Result snapshotResult) {
    if (!ImGui::BeginTable("PowerPresetEditor", 2, ImGuiTableFlags_Resizable)) return;
    ImGui::TableSetupColumn("Plans", ImGuiTableColumnFlags_WidthFixed, 250.F);
    ImGui::TableSetupColumn("Plan", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableNextColumn();
    ImGui::TextWrapped("Each saved plan is an independent command. Bind every plan you want to switch to in flight.");
    ImGui::Spacing();
    for (std::size_t index = 0; index < g_presets.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        const auto label = Bounded(g_presets[index].label, kLabelCapacity);
        const auto id = Bounded(g_presets[index].id, kIdCapacity);
        const std::string command = "preset:" + id;
        AbsoluteHOTASApi::CommandBindingV1 binding{};
        const bool bound = SuiteCommandBindings::GetApi()->getCommandBinding(
            kModuleId.data(), command.c_str(), &binding) == AbsoluteHOTASApi::Result::Ok;
        if (ImGui::Selectable(label.c_str(), index == g_selectedPreset, 0, ImVec2(145, 0)))
            g_selectedPreset = index;
        ImGui::SameLine(0, 8);
        ImGui::TextDisabled("%s", bound
            ? Bounded(binding.binding, AbsoluteHOTASApi::kBindingCapacity).c_str()
            : "Unbound");
        ImGui::PopID();
    }
    if (ImGui::Button("New Plan")) {
        PresetV1 preset{};
        std::string id;
        do id = std::format("Custom{}", g_newPresetIndex++); while (ContainsId(g_presets, id));
        Copy(preset.id, id); Copy(preset.label, id);
        for (std::size_t index = 0; index < kSystemCount; ++index)
            preset.tieBreakOrder[index] = static_cast<std::uint8_t>(index);
        g_presets.push_back(preset); g_selectedPreset = g_presets.size() - 1;
        UpdateDirty("New Power preset added.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate Plan") && g_selectedPreset < g_presets.size()) {
        auto copy = g_presets[g_selectedPreset];
        std::string id;
        do id = std::format("Custom{}", g_newPresetIndex++); while (ContainsId(g_presets, id));
        Copy(copy.id, id); Copy(copy.label, Bounded(copy.label, kLabelCapacity) + " Copy");
        g_presets.push_back(copy); g_selectedPreset = g_presets.size() - 1;
        UpdateDirty("Power preset duplicated.");
    }
    if (g_selectedPreset < g_presets.size()) {
        if (ImGui::Button("Rename")) {
            Copy(g_renameBuffer, Bounded(g_presets[g_selectedPreset].label, kLabelCapacity));
            ImGui::OpenPopup("Rename Power preset");
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            g_presets.erase(g_presets.begin() + static_cast<std::ptrdiff_t>(g_selectedPreset));
            if (g_selectedPreset >= g_presets.size() && g_selectedPreset) --g_selectedPreset;
            UpdateDirty("Power preset marked for deletion.");
        }
    }
    if (ImGui::BeginPopupModal("Rename Power preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Display name", g_renameBuffer, sizeof(g_renameBuffer));
        if (ImGui::Button("Apply") && g_selectedPreset < g_presets.size()) {
            Copy(g_presets[g_selectedPreset].label, g_renameBuffer);
            UpdateDirty("Power preset renamed."); ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::TableNextColumn();
    if (g_selectedPreset < g_presets.size()) {
        auto& preset = g_presets[g_selectedPreset];
        const std::string id = Bounded(preset.id, kIdCapacity);
        bool startup = g_startupPreset == id;
        if (ImGui::Checkbox("Startup preset", &startup)) {
            if (startup) g_startupPreset = id;
            UpdateDirty("Startup Power preset changed.");
        }
        DrawBinding(id);
        const SnapshotV1* live = snapshotResult == Result::Ok ? &snapshot : nullptr;
        PreviewV1 preview{};
        const Result previewResult = live && live->pilotReady
            ? g_api->previewPreset(&preset, &preview) : Result::PilotNotReady;
        const PreviewV1* target = previewResult == Result::Ok ? &preview : nullptr;
        ImGui::SeparatorText("SHIP POWER PLAN"); DrawTierSelector();
        int tallest{};
        for (std::size_t i = 0; i < kSystemCount; ++i)
            tallest = std::max(tallest, std::max<int>(Total(preset.systems[i]),
                live && live->systems[i].present ? live->systems[i].maximum : 0));
        bool changed{};
        if (ImGui::BeginTable("StarfieldPowerGrid", static_cast<int>(kSystemCount),
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame |
            ImGuiTableFlags_NoSavedSettings)) {
            for (std::size_t i = 0; i < kSystemCount; ++i) {
                ImGui::TableNextColumn(); ImGui::PushID(static_cast<int>(i));
                DrawPowerColumn(i, preset, live, target, tallest, changed); ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::SeparatorText("WITHIN-TIER ORDER");
        for (std::size_t pos = 0; pos < kSystemCount; ++pos) {
            ImGui::PushID(static_cast<int>(500 + pos));
            const auto system = preset.tieBreakOrder[pos];
            ImGui::Text("%zu. %s", pos + 1, system < kSystemCount ? kSystemLabels[system] : "Invalid");
            ImGui::SameLine(170);
            if (ImGui::SmallButton("Up") && pos > 0) {
                std::swap(preset.tieBreakOrder[pos], preset.tieBreakOrder[pos - 1]); changed = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Down") && pos + 1 < kSystemCount) {
                std::swap(preset.tieBreakOrder[pos], preset.tieBreakOrder[pos + 1]); changed = true;
            }
            ImGui::PopID();
        }
        if (changed) UpdateDirty("Power allocation plan changed.");
        if (target) ImGui::Text("Unassigned reactor pips: %u", preview.unassigned);
        else ImGui::TextDisabled("Live allocation preview unavailable until piloting.");
        if (ImGui::Button("Save Draft")) (void)PowerModuleUI::Save();
        ImGui::SameLine();
        ImGui::BeginDisabled(!(live && live->pilotReady));
        if (ImGui::Button("Save & Activate") && PowerModuleUI::Save()) {
            const auto result = g_api->invokeCommand(("preset:" + id).c_str());
            g_status = std::format("Power preset activation: {}.", ResultName(result));
        }
        ImGui::SameLine();
        if (ImGui::Button("Activate Without Saving")) {
            const auto result = g_api->invokeCommand(("preset:" + id).c_str());
            g_status = std::format("Power preset activation: {}.", ResultName(result));
        }
        ImGui::EndDisabled();
    }
    ImGui::EndTable();
}

void DrawAutomation() {
    ImGui::TextColored(kWarn, "CHANGES GAME BALANCE");
    ImGui::TextWrapped("Automatic power reassignment is optional and disabled by default.");
    bool enabled = g_automationEnabled;
    if (ImGui::Checkbox("Enable all Power automation", &enabled)) {
        const auto result = g_api->setAutomationEnabled(enabled ? 1 : 0);
        if (result == Result::Ok) {
            g_automationEnabled = enabled; UpdateDirty("Power automation gate changed.");
        } else g_status = std::format("Automation gate failed: {}.", ResultName(result));
    }
    for (std::size_t index = 0; index < g_rules.size(); ++index) {
        auto& rule = g_rules[index]; ImGui::PushID(static_cast<int>(index));
        if (ImGui::TreeNode(Bounded(rule.label, kLabelCapacity).c_str())) {
            bool changed{}; bool ruleEnabled = rule.enabled != 0;
            if (ImGui::Checkbox("Rule enabled", &ruleEnabled)) {
                rule.enabled = ruleEnabled ? 1 : 0; changed = true;
            }
            int trigger = rule.trigger;
            constexpr std::array<const char*, 4> triggers{
                "Weapon Fired", "Incoming Damage", "Throttle Above", "Manual"};
            if (ImGui::Combo("Trigger", &trigger, triggers.data(),
                             static_cast<int>(triggers.size()))) {
                rule.trigger = static_cast<std::uint8_t>(trigger); changed = true;
            }
            int source = rule.sourceSystem < kSystemCount ? rule.sourceSystem : 6;
            constexpr std::array<const char*, 7> sources{
                "Weapon 1", "Weapon 2", "Weapon 3", "Engines", "Shields", "Grav Drive", "Any"};
            if (ImGui::Combo("Source", &source, sources.data(),
                             static_cast<int>(sources.size()))) {
                rule.sourceSystem = source == 6 ? 0xFF : static_cast<std::uint8_t>(source); changed = true;
            }
            int target = rule.targetSystem < kSystemCount ? rule.targetSystem : 0;
            if (ImGui::Combo("Target", &target, kSystemLabels.data(),
                             static_cast<int>(kSystemLabels.size()))) {
                rule.targetSystem = static_cast<std::uint8_t>(target); changed = true;
            }
            bool maximum = rule.targetPips == std::numeric_limits<std::uint16_t>::max();
            if (ImGui::Checkbox("Demand maximum", &maximum)) {
                rule.targetPips = maximum ? std::numeric_limits<std::uint16_t>::max() : 1; changed = true;
            }
            if (!maximum) {
                int pips = rule.targetPips;
                if (ImGui::SliderInt("Target pips", &pips, 0, 32)) {
                    rule.targetPips = static_cast<std::uint16_t>(pips); changed = true;
                }
            }
            int threshold = rule.thresholdPercent, hysteresis = rule.hysteresisPercent;
            int hold = static_cast<int>(std::min<std::uint32_t>(rule.holdMilliseconds, 60000));
            int priority = rule.priority;
            if (ImGui::SliderInt("Threshold", &threshold, 0, 100, "%d%%")) changed = true;
            if (ImGui::SliderInt("Hysteresis", &hysteresis, 0, 50, "%d%%")) changed = true;
            if (ImGui::SliderInt("Hold", &hold, 0, 60000, "%d ms")) changed = true;
            if (ImGui::InputInt("Priority", &priority)) changed = true;
            rule.thresholdPercent = static_cast<std::uint8_t>(std::clamp(threshold, 0, 100));
            rule.hysteresisPercent = static_cast<std::uint8_t>(std::clamp(hysteresis, 0, 100));
            rule.holdMilliseconds = static_cast<std::uint32_t>(std::clamp(hold, 0, 60000));
            rule.priority = static_cast<std::uint16_t>(std::clamp(priority, 0, 65535));
            if (ImGui::Button("Delete rule")) {
                g_rules.erase(g_rules.begin() + static_cast<std::ptrdiff_t>(index));
                UpdateDirty("Power automation rule deleted."); ImGui::TreePop(); ImGui::PopID(); break;
            }
            if (changed) UpdateDirty("Power automation rule changed.");
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (ImGui::Button("New manual rule")) {
        RuleV1 rule{};
        const std::string id = std::format("CustomRule{}", g_newRuleIndex++);
        Copy(rule.id, id); Copy(rule.label, id); rule.trigger = 3; rule.sourceSystem = 0xFF;
        rule.targetSystem = 4; rule.targetPips = 1;
        g_rules.push_back(rule); UpdateDirty("New Power automation rule added.");
    }
    ImGui::SameLine(); if (ImGui::Button("Save automation")) (void)PowerModuleUI::Save();
}

void DrawDiagnostics(const StatusV1& status, const SnapshotV1& snapshot, Result snapshotResult) {
    ImGui::Text("Module: %s", g_api->moduleId);
    ImGui::Text("Version: %s | ABI %u", g_api->version, g_api->abiVersion);
    ImGui::Text("Runtime state: %u | automation: %s", static_cast<unsigned>(status.state),
                status.automationEnabled ? "enabled" : "disabled");
    ImGui::Text("Presets: %u | rules: %u | commands: %u", g_api->getPresetCount(),
                g_api->getRuleCount(), g_api->getCommandCount());
    if (snapshotResult == Result::Ok)
        ImGui::Text("Pilot ready: %s | reactor: %u | available: %u",
                    snapshot.pilotReady ? "yes" : "no", snapshot.totalPower, snapshot.available);
    else ImGui::TextColored(kWarn, "Snapshot: %s", ResultName(snapshotResult));
    ImGui::TextWrapped("Power config: %s",
        (RuntimePaths::PluginDirectory() / L"AbsolutePower_Custom.ini").string().c_str());
    ImGui::TextWrapped("HOTAS command bindings: %s",
        RuntimePaths::CustomIniPath().string().c_str());
}
} // namespace

namespace PowerModuleUI {
void Initialize() { (void)EnsureAvailable(); }
bool Available() { return EnsureAvailable(); }

void Draw() {
    if (!EnsureAvailable() || !g_loaded) {
        ImGui::TextColored(kBad, "Absolute Power backend unavailable");
        ImGui::TextWrapped("%s", g_status.c_str());
        return;
    }
    if (!g_dirty && std::chrono::steady_clock::now() >= g_nextRefresh)
        (void)LoadDraft(true);
    StatusV1 status{}; SnapshotV1 snapshot{};
    const auto statusResult = g_api->getStatus(&status);
    const auto snapshotResult = g_api->getSnapshot(&snapshot);
    ImGui::TextColored(kAccent, "ABSOLUTE POWER"); ImGui::SameLine();
    ImGui::TextDisabled("registered module | backend %s", g_api->version);
    if (statusResult == Result::Ok)
        ImGui::Text("Active preset: %s", status.activePreset[0] ? status.activePreset : "none");
    if (ImGui::BeginTabBar("PowerModuleSections")) {
        if (ImGui::BeginTabItem("Power Presets")) {
            DrawPresets(snapshot, snapshotResult); ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Automation")) {
            DrawAutomation(); ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Diagnostics")) {
            DrawDiagnostics(status, snapshot, snapshotResult); ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::Separator();
    ImGui::TextColored(g_dirty ? kWarn : kGood, "%s", g_status.c_str());
    DrawCaptureModal();
}

bool Dirty() { return g_dirty; }

bool Save() {
    if (!g_api || !g_loaded) return false;
    const auto submittedPresets = g_presets;
    const auto submittedRules = g_rules;
    std::vector<std::string> deleted;
    for (const auto& id : g_openingPresetIds)
        if (!ContainsId(submittedPresets, id)) deleted.push_back(id);
    const bool wrote = WriteCustomConfiguration();
    const auto reload = wrote ? g_api->reloadConfiguration() : Result::Rejected;
    std::vector<PresetV1> readPresets; std::vector<RuleV1> readRules;
    StatusV1 status{}; std::string detail;
    const bool read = reload == Result::Ok && ReadBackend(readPresets, readRules, status, detail);
    const bool verified = read && SameRecords(submittedPresets, readPresets) &&
                          SameRecords(submittedRules, readRules) &&
                          (status.automationEnabled != 0) == g_automationEnabled;
    if (!wrote || reload != Result::Ok || !verified) {
        g_dirty = true;
        g_status = !wrote ? "AbsolutePower_Custom.ini write failed."
            : reload != Result::Ok ? std::format("Power reload failed: {}.", ResultName(reload))
            : read ? "Power API read-back differs from the submitted draft." : detail;
        return false;
    }
    for (const auto& id : deleted) {
        const std::string command = "preset:" + id;
        (void)SuiteCommandBindings::GetApi()->clearCommandBinding(kModuleId.data(), command.c_str());
    }
    g_presets = std::move(readPresets); g_rules = std::move(readRules);
    g_openingPresets = g_presets; g_openingRules = g_rules;
    g_openingStartupPreset = g_startupPreset;
    g_openingAutomationEnabled = g_automationEnabled;
    g_openingPresetIds.clear(); g_openingRuleIds.clear();
    for (const auto& preset : g_presets)
        g_openingPresetIds.push_back(Bounded(preset.id, kIdCapacity));
    for (const auto& rule : g_rules)
        g_openingRuleIds.push_back(Bounded(rule.id, kIdCapacity));
    g_dirty = false;
    g_status = "Power overlay written, reloaded, and API read-back verified.";
    return true;
}

void Discard() {
    g_dirty = false;
    if (!LoadDraft(true)) {
        g_presets = g_openingPresets; g_rules = g_openingRules;
        g_startupPreset = g_openingStartupPreset;
        g_automationEnabled = g_openingAutomationEnabled;
        g_status = "Power draft discarded; the saved opening snapshot was restored.";
    } else {
        g_status = "Power draft discarded; live backend values reloaded.";
    }
    CancelTransientInteractions();
}

void CancelTransientInteractions() {
    auto* api = SuiteCommandBindings::GetApi();
    AbsoluteHOTASApi::CaptureV1 capture{};
    if (api->pollButtonCapture(&capture) == AbsoluteHOTASApi::Result::Ok &&
        capture.state == AbsoluteHOTASApi::CaptureState::Capturing)
        (void)api->cancelButtonCapture();
    g_capturePopupRequested = false;
}

std::string_view StatusText() { return g_status; }
} // namespace PowerModuleUI
