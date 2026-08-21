#pragma once

#include "WizardConfig.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace AbsoluteControlMacros {

inline constexpr std::size_t kMaximumRecords = 64;

struct Record {
    std::string recordId;
    std::string label;
    std::string summary;
    std::string detail;
    std::uint32_t flags{};
};

class Repository {
public:
    virtual ~Repository() = default;
    virtual bool Load(WizardState& state, std::string& error) = 0;
    virtual bool Save(const WizardState& state, std::string& error) = 0;
};

class Session {
public:
    explicit Session(Repository& repository) : repository_(repository) {}

    bool Open(std::string& error);
    bool Apply(std::string& error);
    void Cancel() noexcept;
    bool Dirty() const noexcept { return dirty_; }

    std::vector<Record> MacroRecords() const;
    std::string_view SelectedMacroId() const noexcept { return selectedMacroId_; }
    bool SelectMacro(std::string_view recordId) noexcept;
    const MacroRow* SelectedMacro() const noexcept;
    bool AddMacro();
    bool DeleteMacro() noexcept;
    bool SetMacroName(std::string name);
    bool SetMacroTrigger(std::string_view binding);
    bool SetMacroTurbo(bool turbo) noexcept;

    std::vector<Record> StepRecords() const;
    std::string_view SelectedStepId() const noexcept { return selectedStepId_; }
    bool SelectStep(std::string_view recordId) noexcept;
    const MacroStepRow* SelectedStep() const noexcept;
    bool AddStep();
    bool DeleteStep() noexcept;
    bool MoveStep(int delta) noexcept;
    bool SetStepHold(bool hold) noexcept;
    bool SetStepAmount(int amount) noexcept;
    bool SetStepGap(int gapMilliseconds) noexcept;

    std::vector<Record> TargetRecords() const;
    std::string_view SelectedTargetId() const noexcept { return selectedTargetId_; }
    bool SelectTarget(std::string_view recordId) noexcept;
    bool AddTarget(std::size_t catalogIndex);
    bool DeleteTarget() noexcept;

    std::vector<Record> ShortcutRecords() const;
    std::string_view SelectedShortcutId() const noexcept { return selectedShortcutId_; }
    bool SelectShortcut(std::string_view recordId) noexcept;
    const CustomBindingRow* SelectedShortcut() const noexcept;
    bool AddShortcut();
    bool DeleteShortcut() noexcept;
    bool SetShortcutTrigger(std::string_view binding);
    bool SetShortcutOutput(std::size_t catalogIndex);
    bool AddMenuNavigationPreset();

    static std::size_t TargetCatalogSize() noexcept;
    static std::string_view TargetCatalogLabel(std::size_t index) noexcept;
    static std::string_view TargetCatalogValue(std::size_t index) noexcept;
    static std::size_t OutputCatalogSize() noexcept;
    static std::string_view OutputCatalogLabel(std::size_t index) noexcept;
    static std::string_view OutputCatalogValue(std::size_t index) noexcept;
    static int OutputCatalogIndex(std::string_view value) noexcept;

private:
    struct MacroIds {
        std::string id;
        std::vector<std::string> steps;
        std::vector<std::vector<std::string>> targets;
    };

    void BuildIds();
    void SelectFirstMacro() noexcept;
    void SelectFirstStep() noexcept;
    void SelectFirstTarget() noexcept;
    std::size_t MacroIndex() const noexcept;
    std::size_t StepIndex() const noexcept;
    std::size_t TargetIndex() const noexcept;
    std::size_t ShortcutIndex() const noexcept;
    std::string NextId(std::string_view prefix);
    std::string UniqueMacroName() const;
    void Changed() noexcept { dirty_ = true; }

    Repository& repository_;
    WizardState saved_{};
    WizardState draft_{};
    std::vector<MacroIds> macroIds_;
    std::vector<std::string> shortcutIds_;
    std::string selectedMacroId_;
    std::string selectedStepId_;
    std::string selectedTargetId_;
    std::string selectedShortcutId_;
    std::uint64_t nextId_{};
    bool loaded_{};
    bool dirty_{};
};

Repository& WizardRepository() noexcept;

} // namespace AbsoluteControlMacros
