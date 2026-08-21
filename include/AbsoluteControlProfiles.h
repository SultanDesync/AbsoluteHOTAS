#pragma once

#include "WizardConfig.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace AbsoluteControlProfiles {

inline constexpr std::string_view kMainRecordId = "main-controls";

struct Record {
    std::string recordId;
    std::string configurationName; // Empty means Main controls.
    std::string label;
    std::string kind;
    std::string filename;
    std::string keyboardShortcut;
    std::string activationTrigger;
    std::string activationMode;
    int sequence{};
    int slot{};
    int overrideCount{};
    bool inheritsMain{};
};

enum class SelectResult { Selected, NeedsResolution, NotFound, Failed };
enum class SwitchChoice { Save, Discard, Cancel };
enum class BindingResult { Ok, Conflict, Invalid, NotFound };

// Injectable repository seam. Production delegates to WizardConfig/WizardSession;
// tests use an in-memory repository and never touch profile files.
class Repository {
public:
    virtual ~Repository() = default;
    virtual std::vector<WizardConfig::ProfileSummary> List() = 0;
    virtual void GetMainActivation(std::string& trigger, std::string& mode) = 0;
    virtual std::string CurrentEditTarget() = 0;
    virtual bool LoadEditTarget(const std::string& name, std::string& error) = 0;
    virtual bool HasEditTargetChanges() = 0;
    virtual bool SaveEditTarget(std::string& error) = 0;
    virtual bool DiscardEditTarget(std::string& error) = 0;
    virtual bool SaveActivations(
        const std::vector<WizardConfig::ProfileActivationUpdate>& updates,
        std::string& error) = 0;
    virtual bool CreateOverlay(const std::string& name, std::string& error) = 0;
    virtual bool ExportFull(const std::string& name, std::string& error) = 0;
    virtual bool ImportFull(const std::string& name, std::string& error) = 0;
    virtual bool ResetMain(std::string& error) = 0;
};

class Session {
public:
    explicit Session(Repository& repository) : repository_(repository) {}

    bool Open(std::string& error);
    const std::vector<Record>& Records() const noexcept { return records_; }
    const Record* Selected() const noexcept;
    std::string_view SelectedRecordId() const noexcept { return selectedRecordId_; }
    std::uint64_t Generation() const noexcept { return generation_; }
    bool Dirty() const;

    SelectResult Select(std::string_view recordId, std::string& error);
    bool HasPendingSwitch() const noexcept { return !pendingRecordId_.empty(); }
    std::string_view PendingRecordId() const noexcept { return pendingRecordId_; }
    bool ResolveSwitch(SwitchChoice choice, std::string& error);

    bool SetActivationMode(std::string_view mode);
    BindingResult SetActivationBinding(std::string_view binding);
    BindingResult ReassignActivationBinding(std::string_view binding);
    bool ClearActivationBinding();

    const std::string& OperationName() const noexcept { return operationName_; }
    void SetOperationName(std::string name) { operationName_ = std::move(name); }

    bool Apply(std::string& error);
    bool Cancel(std::string& error);

    bool CreateOverlay(std::string& error);
    bool ExportMainFull(std::string& error);
    bool ImportSelectedFull(std::string& error);
    bool ResetMain(std::string& error);

private:
    bool Refresh(std::string_view preferredConfiguration, std::string& error);
    bool SelectNow(std::string_view recordId, std::string& error);
    Record* MutableSelected() noexcept;
    void RestoreDraft() noexcept;

    Repository& repository_;
    std::vector<Record> records_;
    std::vector<Record> savedRecords_;
    std::string selectedRecordId_{std::string(kMainRecordId)};
    std::string pendingRecordId_;
    std::string operationName_;
    std::string savedOperationName_;
    std::uint64_t generation_{};
    bool loaded_{};
};

Repository& WizardRepository() noexcept;

} // namespace AbsoluteControlProfiles
