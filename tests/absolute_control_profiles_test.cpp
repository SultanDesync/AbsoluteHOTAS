#include "AbsoluteControlProfiles.h"

#include <cassert>
#include <string>
#include <utility>
#include <vector>

namespace {

class MemoryRepository final : public AbsoluteControlProfiles::Repository {
public:
    std::vector<WizardConfig::ProfileSummary> summaries{
        {.name = "Flight Aux", .kind = "overlay",
         .filename = "Profile_01_Flight_Aux.ini", .trigger = "#1@7",
         .keyboardShortcut = "key:0x11+0x32", .mode = "toggle",
         .sequence = 1, .slot = 2, .overrideCount = 4},
        {.name = "Independent", .kind = "full",
         .filename = "Profile_02_Independent.ini", .trigger = "8",
         .mode = "selector", .sequence = 2, .slot = 3,
         .overrideCount = 87},
    };
    std::string mainTrigger = "1";
    std::string mainMode = "momentary";
    std::string currentTarget;
    bool editDirty{};
    bool failDiscard{};
    int saveEditCalls{};
    int discardCalls{};
    int activationSaveCalls{};
    std::vector<WizardConfig::ProfileActivationUpdate> lastUpdates;
    std::vector<std::string> created;
    std::vector<std::string> exported;
    std::vector<std::string> imported;
    int resets{};

    std::vector<WizardConfig::ProfileSummary> List() override { return summaries; }
    void GetMainActivation(std::string& trigger, std::string& mode) override
    {
        trigger = mainTrigger;
        mode = mainMode;
    }
    std::string CurrentEditTarget() override { return currentTarget; }
    bool LoadEditTarget(const std::string& name, std::string&) override
    {
        currentTarget = name;
        editDirty = false;
        return true;
    }
    bool HasEditTargetChanges() override { return editDirty; }
    bool SaveEditTarget(std::string&) override
    {
        ++saveEditCalls;
        editDirty = false;
        return true;
    }
    bool DiscardEditTarget(std::string& error) override
    {
        ++discardCalls;
        if (failDiscard) {
            error = "discard failed";
            return false;
        }
        editDirty = false;
        return true;
    }
    bool SaveActivations(
        const std::vector<WizardConfig::ProfileActivationUpdate>& updates,
        std::string&) override
    {
        ++activationSaveCalls;
        lastUpdates = updates;
        return true;
    }
    bool CreateOverlay(const std::string& name, std::string&) override
    {
        created.push_back(name);
        summaries.push_back({.name = name, .kind = "overlay",
            .filename = "created.ini", .overrideCount = 0});
        return true;
    }
    bool ExportFull(const std::string& name, std::string&) override
    {
        exported.push_back(name);
        summaries.push_back({.name = name, .kind = "full",
            .filename = "exported.ini", .overrideCount = 87});
        return true;
    }
    bool ImportFull(const std::string& name, std::string&) override
    {
        imported.push_back(name);
        return true;
    }
    bool ResetMain(std::string&) override
    {
        ++resets;
        return true;
    }
};

const AbsoluteControlProfiles::Record& FindByName(
    const AbsoluteControlProfiles::Session& session, std::string_view name)
{
    for (const auto& record : session.Records()) {
        if (record.configurationName == name) return record;
    }
    assert(false);
    return session.Records().front();
}

} // namespace

int main()
{
    using namespace AbsoluteControlProfiles;

    MemoryRepository repository;
    Session session(repository);
    std::string error;
    assert(session.Open(error));
    assert(error.empty());
    assert(session.Records().size() == 3);
    assert(session.SelectedRecordId() == kMainRecordId);
    assert(!session.Dirty());

    const auto& overlay = FindByName(session, "Flight Aux");
    const auto overlayId = overlay.recordId;
    assert(overlay.inheritsMain);
    assert(overlay.overrideCount == 4);
    assert(overlay.keyboardShortcut == "key:0x11+0x32");
    assert(overlay.activationTrigger == "#1@7");
    assert(overlay.recordId.starts_with("profile-"));
    assert(FindByName(session, "Independent").recordId != overlay.recordId);

    // Stable record IDs survive a refresh and clean target switching edits the
    // selected WizardSession target directly.
    assert(session.Select(overlayId, error) == SelectResult::Selected);
    assert(repository.currentTarget == "Flight Aux");
    assert(session.SelectedRecordId() == overlayId);

    // Activation draft supports all legacy modes and detects conflicts across
    // Main, overlays, and independent profiles.
    assert(session.SetActivationMode("selector"));
    assert(!session.SetActivationMode("invalid"));
    assert(session.SetActivationBinding("1") == BindingResult::Conflict);
    assert(session.SetActivationBinding("#2@11") == BindingResult::Ok);
    assert(session.Dirty());

    const auto fullId = FindByName(session, "Independent").recordId;
    assert(session.Select(fullId, error) == SelectResult::NeedsResolution);
    assert(session.PendingRecordId() == fullId);
    assert(session.ResolveSwitch(SwitchChoice::Cancel, error));
    assert(session.SelectedRecordId() == overlayId);
    assert(!session.HasPendingSwitch());
    assert(session.Dirty());

    // Discard failure keeps both the draft and pending switch intact. A later
    // successful discard restores the saved activation before switching.
    assert(session.Select(fullId, error) == SelectResult::NeedsResolution);
    repository.failDiscard = true;
    repository.editDirty = true;
    assert(!session.ResolveSwitch(SwitchChoice::Discard, error));
    assert(session.SelectedRecordId() == overlayId);
    assert(session.HasPendingSwitch());
    assert(session.Selected()->activationTrigger == "#2@11");
    repository.failDiscard = false;
    assert(session.ResolveSwitch(SwitchChoice::Discard, error));
    assert(session.SelectedRecordId() == fullId);
    assert(FindByName(session, "Flight Aux").activationTrigger == "#1@7");
    assert(!session.Dirty());

    // Save-before-switch persists the Wizard edit snapshot and all activation
    // changes as one repository batch.
    assert(session.SetActivationBinding("#3@12") == BindingResult::Ok);
    repository.editDirty = true;
    assert(session.Select(kMainRecordId, error) == SelectResult::NeedsResolution);
    assert(session.ResolveSwitch(SwitchChoice::Save, error));
    assert(repository.saveEditCalls == 1);
    assert(repository.activationSaveCalls == 1);
    assert(repository.lastUpdates.size() == 1);
    assert(repository.lastUpdates.front().profile == "Independent");
    assert(repository.lastUpdates.front().trigger == "#3@12");
    assert(session.SelectedRecordId() == kMainRecordId);

    // Explicit reassignment clears the previous profile while ordinary writes
    // remain non-destructive.
    assert(session.SetActivationBinding("#3@12") == BindingResult::Conflict);
    assert(session.ReassignActivationBinding("#3@12") == BindingResult::Ok);
    assert(FindByName(session, "Independent").activationTrigger == "(unbound)");
    assert(session.ClearActivationBinding());
    assert(session.Selected()->activationTrigger == "(unbound)");
    assert(session.Cancel(error));

    // Create/export/import/reset expose only the legacy operations requested by
    // the page adapter. There is intentionally no rename or delete operation.
    session.SetOperationName("Landing Layer");
    assert(session.CreateOverlay(error));
    assert(repository.created == std::vector<std::string>{"Landing Layer"});
    assert(session.Selected()->configurationName == "Landing Layer");
    assert(session.Selected()->inheritsMain);

    assert(session.Select(kMainRecordId, error) == SelectResult::Selected);
    session.SetOperationName("Cruise Full");
    assert(session.ExportMainFull(error));
    assert(repository.exported == std::vector<std::string>{"Cruise Full"});
    assert(session.SelectedRecordId() == kMainRecordId);

    const auto exportedId = FindByName(session, "Cruise Full").recordId;
    assert(session.Select(exportedId, error) == SelectResult::Selected);
    assert(session.ImportSelectedFull(error));
    assert(repository.imported == std::vector<std::string>{"Cruise Full"});
    assert(session.SelectedRecordId() == kMainRecordId);
    assert(session.ResetMain(error));
    assert(repository.resets == 1);
    assert(session.SelectedRecordId() == kMainRecordId);

    // Invalid controller button references never enter the draft.
    assert(session.SetActivationBinding("#0@145") == BindingResult::Invalid);
    assert(session.SetActivationBinding("not-a-binding") == BindingResult::Invalid);
    return 0;
}
