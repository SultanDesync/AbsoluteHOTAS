#include "AbsoluteControlMacros.h"

#include "BindingRef.h"

#include <algorithm>
#include <format>
#include <limits>

namespace {

std::string CanonicalButton(std::string_view raw)
{
    if (raw.empty() || raw == "-1" || raw == "(unbound)") return "(unbound)";
    const std::string owned{raw};
    const auto parsed = ParseBindingRef(owned.c_str(), -1);
    if (parsed.value < 1 || parsed.value > 144) return {};
    return FormatBindingRef(parsed, false);
}

std::string TargetLabel(std::string_view token)
{
    const std::string owned{token};
    if (const auto* label = FindMacroTargetLabel(owned)) return label;
    return owned.empty() ? "(empty target)" : owned;
}

} // namespace

namespace AbsoluteControlMacros {

bool Session::Open(std::string& error)
{
    WizardState state;
    if (!repository_.Load(state, error)) return false;
    saved_ = state;
    draft_ = std::move(state);
    loaded_ = true;
    dirty_ = false;
    BuildIds();
    return true;
}

bool Session::Apply(std::string& error)
{
    if (!loaded_) return false;
    if (!dirty_) return true;
    if (!repository_.Save(draft_, error)) return false;
    saved_ = draft_;
    dirty_ = false;
    return true;
}

void Session::Cancel() noexcept
{
    if (!loaded_) return;
    draft_ = saved_;
    dirty_ = false;
    BuildIds();
}

std::string Session::NextId(std::string_view prefix)
{
    return std::format("{}-{}", prefix, ++nextId_);
}

void Session::BuildIds()
{
    macroIds_.clear();
    shortcutIds_.clear();
    for (const auto& macro : draft_.macros) {
        MacroIds ids;
        ids.id = NextId("macro");
        for (const auto& step : macro.steps) {
            ids.steps.push_back(NextId("step"));
            auto& targets = ids.targets.emplace_back();
            for ([[maybe_unused]] const auto& target : step.targets)
                targets.push_back(NextId("target"));
        }
        macroIds_.push_back(std::move(ids));
    }
    for ([[maybe_unused]] const auto& row : draft_.customBindings)
        shortcutIds_.push_back(NextId("shortcut"));
    SelectFirstMacro();
    selectedShortcutId_ = shortcutIds_.empty() ? std::string{} : shortcutIds_.front();
}

std::size_t Session::MacroIndex() const noexcept
{
    const auto found = std::ranges::find(macroIds_, selectedMacroId_, &MacroIds::id);
    return found == macroIds_.end() ? std::numeric_limits<std::size_t>::max()
                                    : static_cast<std::size_t>(found - macroIds_.begin());
}

std::size_t Session::StepIndex() const noexcept
{
    const auto mi = MacroIndex();
    if (mi >= macroIds_.size()) return std::numeric_limits<std::size_t>::max();
    const auto& ids = macroIds_[mi].steps;
    const auto found = std::ranges::find(ids, selectedStepId_);
    return found == ids.end() ? std::numeric_limits<std::size_t>::max()
                              : static_cast<std::size_t>(found - ids.begin());
}

std::size_t Session::TargetIndex() const noexcept
{
    const auto mi = MacroIndex();
    const auto si = StepIndex();
    if (mi >= macroIds_.size() || si >= macroIds_[mi].targets.size())
        return std::numeric_limits<std::size_t>::max();
    const auto& ids = macroIds_[mi].targets[si];
    const auto found = std::ranges::find(ids, selectedTargetId_);
    return found == ids.end() ? std::numeric_limits<std::size_t>::max()
                              : static_cast<std::size_t>(found - ids.begin());
}

std::size_t Session::ShortcutIndex() const noexcept
{
    const auto found = std::ranges::find(shortcutIds_, selectedShortcutId_);
    return found == shortcutIds_.end() ? std::numeric_limits<std::size_t>::max()
                                       : static_cast<std::size_t>(found - shortcutIds_.begin());
}

void Session::SelectFirstMacro() noexcept
{
    selectedMacroId_ = macroIds_.empty() ? std::string{} : macroIds_.front().id;
    SelectFirstStep();
}

void Session::SelectFirstStep() noexcept
{
    const auto mi = MacroIndex();
    selectedStepId_ = mi < macroIds_.size() && !macroIds_[mi].steps.empty()
        ? macroIds_[mi].steps.front() : std::string{};
    SelectFirstTarget();
}

void Session::SelectFirstTarget() noexcept
{
    const auto mi = MacroIndex();
    const auto si = StepIndex();
    selectedTargetId_ = mi < macroIds_.size() && si < macroIds_[mi].targets.size() &&
            !macroIds_[mi].targets[si].empty()
        ? macroIds_[mi].targets[si].front() : std::string{};
}

std::vector<Record> Session::MacroRecords() const
{
    std::vector<Record> records;
    const auto count = draft_.macros.size();
    records.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& macro = draft_.macros[i];
        std::size_t duplicateCount{};
        std::size_t ordinal{};
        for (std::size_t candidate = 0; candidate < draft_.macros.size(); ++candidate) {
            if (draft_.macros[candidate].name != macro.name) continue;
            ++duplicateCount;
            if (candidate < i) ++ordinal;
        }
        const auto label = macro.name.empty() ? std::string{"(unnamed macro)"}
            : duplicateCount > 1 ? std::format("{} ({})", macro.name, ordinal)
                                 : macro.name;
        records.push_back({
            macroIds_[i].id,
            label,
            std::format("{} step{} | {}", macro.steps.size(), macro.steps.size() == 1 ? "" : "s",
                        macro.buttonBinding == "(unbound)" ? "no trigger" : macro.buttonBinding),
            macro.turbo ? "Repeats while held" : "Runs once per press",
            static_cast<std::uint32_t>(macro.name.empty() || macro.buttonBinding == "(unbound)" ||
                macro.steps.empty()),
        });
    }
    return records;
}

bool Session::SelectMacro(std::string_view recordId) noexcept
{
    const auto found = std::ranges::find(macroIds_, recordId, &MacroIds::id);
    if (found == macroIds_.end()) return false;
    selectedMacroId_ = found->id;
    SelectFirstStep();
    return true;
}

const MacroRow* Session::SelectedMacro() const noexcept
{
    const auto index = MacroIndex();
    return index < draft_.macros.size() ? &draft_.macros[index] : nullptr;
}

std::string Session::UniqueMacroName() const
{
    for (std::size_t suffix = 1;; ++suffix) {
        const auto candidate = std::format("Macro{}", suffix);
        if (std::ranges::none_of(draft_.macros,
            [&](const MacroRow& row) { return row.name == candidate; })) return candidate;
    }
}

bool Session::AddMacro()
{
    if (draft_.macros.size() >= kMaximumRecords) return false;
    MacroRow macro;
    macro.name = UniqueMacroName();
    macro.steps.push_back({{"NextSystem"}, false, 1, 50});
    draft_.macros.push_back(std::move(macro));
    MacroIds ids;
    ids.id = NextId("macro");
    ids.steps.push_back(NextId("step"));
    ids.targets.push_back({NextId("target")});
    macroIds_.push_back(std::move(ids));
    selectedMacroId_ = macroIds_.back().id;
    SelectFirstStep();
    Changed();
    return true;
}

bool Session::DeleteMacro() noexcept
{
    const auto index = MacroIndex();
    if (index >= draft_.macros.size()) return false;
    draft_.macros.erase(draft_.macros.begin() + index);
    macroIds_.erase(macroIds_.begin() + index);
    SelectFirstMacro();
    Changed();
    return true;
}

bool Session::SetMacroName(std::string name)
{
    const auto index = MacroIndex();
    if (index >= draft_.macros.size()) return false;
    if (draft_.macros[index].name == name) return true;
    draft_.macros[index].name = std::move(name);
    Changed();
    return true;
}

bool Session::SetMacroTrigger(std::string_view binding)
{
    const auto index = MacroIndex();
    if (index >= draft_.macros.size()) return false;
    auto canonical = CanonicalButton(binding);
    if (canonical.empty()) return false;
    if (draft_.macros[index].buttonBinding == canonical) return true;
    draft_.macros[index].buttonBinding = std::move(canonical);
    Changed();
    return true;
}

bool Session::SetMacroTurbo(bool turbo) noexcept
{
    const auto index = MacroIndex();
    if (index >= draft_.macros.size()) return false;
    if (draft_.macros[index].turbo == turbo) return true;
    draft_.macros[index].turbo = turbo;
    Changed();
    return true;
}

std::vector<Record> Session::StepRecords() const
{
    std::vector<Record> records;
    const auto mi = MacroIndex();
    if (mi >= draft_.macros.size()) return records;
    const auto count = draft_.macros[mi].steps.size();
    records.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& step = draft_.macros[mi].steps[i];
        records.push_back({
            macroIds_[mi].steps[i], std::format("Step {}", i + 1),
            std::format("{} target{} | {} {}", step.targets.size(), step.targets.size() == 1 ? "" : "s",
                        step.hold ? "hold" : "tap", step.amount),
            std::format("Gap {} ms", step.gapMs), static_cast<std::uint32_t>(step.targets.empty()),
        });
    }
    return records;
}

bool Session::SelectStep(std::string_view recordId) noexcept
{
    const auto mi = MacroIndex();
    if (mi >= macroIds_.size()) return false;
    const auto found = std::ranges::find(macroIds_[mi].steps, recordId);
    if (found == macroIds_[mi].steps.end()) return false;
    selectedStepId_ = *found;
    SelectFirstTarget();
    return true;
}

const MacroStepRow* Session::SelectedStep() const noexcept
{
    const auto mi = MacroIndex();
    const auto si = StepIndex();
    return mi < draft_.macros.size() && si < draft_.macros[mi].steps.size()
        ? &draft_.macros[mi].steps[si] : nullptr;
}

bool Session::AddStep()
{
    const auto mi = MacroIndex();
    if (mi >= draft_.macros.size() || draft_.macros[mi].steps.size() >= kMaximumRecords) return false;
    draft_.macros[mi].steps.push_back({{"NextSystem"}, false, 1, 50});
    macroIds_[mi].steps.push_back(NextId("step"));
    macroIds_[mi].targets.push_back({NextId("target")});
    selectedStepId_ = macroIds_[mi].steps.back();
    SelectFirstTarget();
    Changed();
    return true;
}

bool Session::DeleteStep() noexcept
{
    const auto mi = MacroIndex();
    const auto si = StepIndex();
    if (mi >= draft_.macros.size() || si >= draft_.macros[mi].steps.size()) return false;
    draft_.macros[mi].steps.erase(draft_.macros[mi].steps.begin() + si);
    macroIds_[mi].steps.erase(macroIds_[mi].steps.begin() + si);
    macroIds_[mi].targets.erase(macroIds_[mi].targets.begin() + si);
    SelectFirstStep();
    Changed();
    return true;
}

bool Session::MoveStep(int delta) noexcept
{
    const auto mi = MacroIndex();
    const auto si = StepIndex();
    if (mi >= draft_.macros.size() || si >= draft_.macros[mi].steps.size()) return false;
    const auto destination = static_cast<std::ptrdiff_t>(si) + delta;
    if (destination < 0 || destination >= static_cast<std::ptrdiff_t>(draft_.macros[mi].steps.size())) return false;
    std::swap(draft_.macros[mi].steps[si], draft_.macros[mi].steps[destination]);
    std::swap(macroIds_[mi].steps[si], macroIds_[mi].steps[destination]);
    std::swap(macroIds_[mi].targets[si], macroIds_[mi].targets[destination]);
    Changed();
    return true;
}

bool Session::SetStepHold(bool hold) noexcept
{
    const auto mi = MacroIndex(); const auto si = StepIndex();
    if (mi >= draft_.macros.size() || si >= draft_.macros[mi].steps.size()) return false;
    if (draft_.macros[mi].steps[si].hold == hold) return true;
    draft_.macros[mi].steps[si].hold = hold; Changed(); return true;
}

bool Session::SetStepAmount(int amount) noexcept
{
    const auto mi = MacroIndex(); const auto si = StepIndex();
    if (mi >= draft_.macros.size() || si >= draft_.macros[mi].steps.size() || amount < 0) return false;
    if (draft_.macros[mi].steps[si].amount == amount) return true;
    draft_.macros[mi].steps[si].amount = amount; Changed(); return true;
}

bool Session::SetStepGap(int gapMilliseconds) noexcept
{
    const auto mi = MacroIndex(); const auto si = StepIndex();
    if (mi >= draft_.macros.size() || si >= draft_.macros[mi].steps.size() || gapMilliseconds < 0) return false;
    if (draft_.macros[mi].steps[si].gapMs == gapMilliseconds) return true;
    draft_.macros[mi].steps[si].gapMs = gapMilliseconds; Changed(); return true;
}

std::vector<Record> Session::TargetRecords() const
{
    std::vector<Record> records;
    const auto mi = MacroIndex(); const auto si = StepIndex();
    if (mi >= draft_.macros.size() || si >= draft_.macros[mi].steps.size()) return records;
    const auto& targets = draft_.macros[mi].steps[si].targets;
    const auto count = targets.size();
    for (std::size_t i = 0; i < count; ++i)
        records.push_back({macroIds_[mi].targets[si][i], TargetLabel(targets[i]), targets[i],
                           "Pressed simultaneously with the other targets in this step.", 0});
    return records;
}

bool Session::SelectTarget(std::string_view recordId) noexcept
{
    const auto mi = MacroIndex(); const auto si = StepIndex();
    if (mi >= macroIds_.size() || si >= macroIds_[mi].targets.size()) return false;
    const auto found = std::ranges::find(macroIds_[mi].targets[si], recordId);
    if (found == macroIds_[mi].targets[si].end()) return false;
    selectedTargetId_ = *found; return true;
}

bool Session::AddTarget(std::size_t catalogIndex)
{
    const auto mi = MacroIndex(); const auto si = StepIndex();
    if (mi >= draft_.macros.size() || si >= draft_.macros[mi].steps.size() ||
        catalogIndex >= TargetCatalogSize() || draft_.macros[mi].steps[si].targets.size() >= kMaximumRecords) return false;
    draft_.macros[mi].steps[si].targets.emplace_back(TargetCatalogValue(catalogIndex));
    macroIds_[mi].targets[si].push_back(NextId("target"));
    selectedTargetId_ = macroIds_[mi].targets[si].back(); Changed(); return true;
}

bool Session::DeleteTarget() noexcept
{
    const auto mi = MacroIndex(); const auto si = StepIndex(); const auto ti = TargetIndex();
    if (mi >= draft_.macros.size() || si >= draft_.macros[mi].steps.size() ||
        ti >= draft_.macros[mi].steps[si].targets.size()) return false;
    draft_.macros[mi].steps[si].targets.erase(draft_.macros[mi].steps[si].targets.begin() + ti);
    macroIds_[mi].targets[si].erase(macroIds_[mi].targets[si].begin() + ti);
    SelectFirstTarget(); Changed(); return true;
}

std::vector<Record> Session::ShortcutRecords() const
{
    std::vector<Record> records;
    const auto count = draft_.customBindings.size();
    for (std::size_t i = 0; i < count; ++i) {
        const auto& row = draft_.customBindings[i];
        const auto output = OutputCatalogIndex(row.output);
        records.push_back({shortcutIds_[i], std::format("Shortcut {}", i + 1),
            std::format("{} -> {}", row.buttonBinding,
                output >= 0 ? OutputCatalogLabel(static_cast<std::size_t>(output)) : std::string_view{row.output}),
            "Controller button to raw keyboard or mouse output.",
            static_cast<std::uint32_t>(row.buttonBinding == "(unbound)" || row.output.empty() || row.output == "none")});
    }
    return records;
}

bool Session::SelectShortcut(std::string_view recordId) noexcept
{
    const auto found = std::ranges::find(shortcutIds_, recordId);
    if (found == shortcutIds_.end()) return false;
    selectedShortcutId_ = *found; return true;
}

const CustomBindingRow* Session::SelectedShortcut() const noexcept
{
    const auto index = ShortcutIndex();
    return index < draft_.customBindings.size() ? &draft_.customBindings[index] : nullptr;
}

bool Session::AddShortcut()
{
    if (draft_.customBindings.size() >= kMaximumRecords) return false;
    draft_.customBindings.push_back({"(unbound)", "none"});
    shortcutIds_.push_back(NextId("shortcut"));
    selectedShortcutId_ = shortcutIds_.back(); Changed(); return true;
}

bool Session::DeleteShortcut() noexcept
{
    const auto index = ShortcutIndex();
    if (index >= draft_.customBindings.size()) return false;
    draft_.customBindings.erase(draft_.customBindings.begin() + index);
    shortcutIds_.erase(shortcutIds_.begin() + index);
    selectedShortcutId_ = shortcutIds_.empty() ? std::string{} : shortcutIds_.front();
    Changed(); return true;
}

bool Session::SetShortcutTrigger(std::string_view binding)
{
    const auto index = ShortcutIndex();
    if (index >= draft_.customBindings.size()) return false;
    auto canonical = CanonicalButton(binding);
    if (canonical.empty()) return false;
    if (draft_.customBindings[index].buttonBinding == canonical) return true;
    draft_.customBindings[index].buttonBinding = std::move(canonical); Changed(); return true;
}

bool Session::SetShortcutOutput(std::size_t catalogIndex)
{
    const auto index = ShortcutIndex();
    if (index >= draft_.customBindings.size() || catalogIndex >= OutputCatalogSize()) return false;
    const auto value = OutputCatalogValue(catalogIndex);
    if (draft_.customBindings[index].output == value) return true;
    draft_.customBindings[index].output = value; Changed(); return true;
}

bool Session::AddMenuNavigationPreset()
{
    constexpr std::string_view values[]{"key:0x11", "key:0x1E", "key:0x1F", "key:0x20",
                                        "key:0x0F", "key:0x12", "key:0x01"};
    if (draft_.customBindings.size() + std::size(values) > kMaximumRecords) return false;
    for (const auto value : values) {
        draft_.customBindings.push_back({"(unbound)", std::string(value)});
        shortcutIds_.push_back(NextId("shortcut"));
    }
    selectedShortcutId_ = shortcutIds_[shortcutIds_.size() - std::size(values)];
    Changed(); return true;
}

std::size_t Session::TargetCatalogSize() noexcept { return kNumShipActionTargets + kOutputCatalogSize; }
std::string_view Session::TargetCatalogLabel(std::size_t index) noexcept
{
    return index < static_cast<std::size_t>(kNumShipActionTargets)
        ? kShipActionTargets[index].label
        : index < TargetCatalogSize() ? kOutputCatalog[index - kNumShipActionTargets].label : std::string_view{};
}
std::string_view Session::TargetCatalogValue(std::size_t index) noexcept
{
    return index < static_cast<std::size_t>(kNumShipActionTargets)
        ? kShipActionTargets[index].value
        : index < TargetCatalogSize() ? kOutputCatalog[index - kNumShipActionTargets].value : std::string_view{};
}
std::size_t Session::OutputCatalogSize() noexcept { return kOutputCatalogSize; }
std::string_view Session::OutputCatalogLabel(std::size_t index) noexcept
{
    return index < OutputCatalogSize() ? kOutputCatalog[index].label : std::string_view{};
}
std::string_view Session::OutputCatalogValue(std::size_t index) noexcept
{
    return index < OutputCatalogSize() ? kOutputCatalog[index].value : std::string_view{};
}
int Session::OutputCatalogIndex(std::string_view value) noexcept
{
    for (std::size_t i = 0; i < OutputCatalogSize(); ++i)
        if (OutputCatalogValue(i) == value) return static_cast<int>(i);
    return -1;
}

} // namespace AbsoluteControlMacros
