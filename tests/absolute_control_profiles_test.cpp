#include "AbsoluteControlProfiles.h"

#include "TestCheck.h"
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
    REQUIRE(false);
    return session.Records().front();
}

} // namespace

int main()
{
    using namespace AbsoluteControlProfiles;

    MemoryRepository repository;
    Session session(repository);
    std::string error;
    REQUIRE(session.Open(error));
    REQUIRE(error.empty());
    REQUIRE(session.Records().size() == 3);
    REQUIRE(session.SelectedRecordId() == kMainRecordId);
    REQUIRE(!session.Dirty());

    const auto& overlay = FindByName(session, "Flight Aux");
    const auto overlayId = overlay.recordId;
    REQUIRE(overlay.inheritsMain);
    REQUIRE(overlay.overrideCount == 4);
    REQUIRE(overlay.keyboardShortcut == "key:0x11+0x32");
    REQUIRE(overlay.activationTrigger == "#1@7");
    REQUIRE(overlay.recordId.starts_with("profile-"));
    REQUIRE(FindByName(session, "Independent").recordId != overlay.recordId);

    // Stable record IDs survive a refresh and clean target switching edits the
    // selected WizardSession target directly.
    REQUIRE(session.Select(overlayId, error) == SelectResult::Selected);
    REQUIRE(repository.currentTarget == "Flight Aux");
    REQUIRE(session.SelectedRecordId() == overlayId);

    // Activation draft supports all legacy modes and detects conflicts across
    // Main, overlays, and independent profiles.
    REQUIRE(session.SetActivationMode("selector"));
    REQUIRE(!session.SetActivationMode("invalid"));
    REQUIRE(session.SetActivationBinding("1") == BindingResult::Conflict);
    REQUIRE(session.SetActivationBinding("#2@11") == BindingResult::Ok);
    REQUIRE(session.Dirty());

    const auto fullId = FindByName(session, "Independent").recordId;
    REQUIRE(session.Select(fullId, error) == SelectResult::NeedsResolution);
    REQUIRE(session.PendingRecordId() == fullId);
    REQUIRE(session.ResolveSwitch(SwitchChoice::Cancel, error));
    REQUIRE(session.SelectedRecordId() == overlayId);
    REQUIRE(!session.HasPendingSwitch());
    REQUIRE(session.Dirty());

    // Discard failure keeps both the draft and pending switch intact. A later
    // successful discard restores the saved activation before switching.
    REQUIRE(session.Select(fullId, error) == SelectResult::NeedsResolution);
    repository.failDiscard = true;
    repository.editDirty = true;
    REQUIRE(!session.ResolveSwitch(SwitchChoice::Discard, error));
    REQUIRE(session.SelectedRecordId() == overlayId);
    REQUIRE(session.HasPendingSwitch());
    REQUIRE(session.Selected()->activationTrigger == "#2@11");
    repository.failDiscard = false;
    REQUIRE(session.ResolveSwitch(SwitchChoice::Discard, error));
    REQUIRE(session.SelectedRecordId() == fullId);
    REQUIRE(FindByName(session, "Flight Aux").activationTrigger == "#1@7");
    REQUIRE(!session.Dirty());

    // Save-before-switch persists the Wizard edit snapshot and all activation
    // changes as one repository batch.
    REQUIRE(session.SetActivationBinding("#3@12") == BindingResult::Ok);
    repository.editDirty = true;
    REQUIRE(session.Select(kMainRecordId, error) == SelectResult::NeedsResolution);
    REQUIRE(session.ResolveSwitch(SwitchChoice::Save, error));
    REQUIRE(repository.saveEditCalls == 1);
    REQUIRE(repository.activationSaveCalls == 1);
    REQUIRE(repository.lastUpdates.size() == 1);
    REQUIRE(repository.lastUpdates.front().profile == "Independent");
    REQUIRE(repository.lastUpdates.front().trigger == "#3@12");
    REQUIRE(session.SelectedRecordId() == kMainRecordId);

    // Explicit reassignment clears the previous profile while ordinary writes
    // remain non-destructive.
    REQUIRE(session.SetActivationBinding("#3@12") == BindingResult::Conflict);
    REQUIRE(session.ReassignActivationBinding("#3@12") == BindingResult::Ok);
    REQUIRE(FindByName(session, "Independent").activationTrigger == "(unbound)");
    REQUIRE(session.ClearActivationBinding());
    REQUIRE(session.Selected()->activationTrigger == "(unbound)");
    REQUIRE(session.Cancel(error));

    // Create/export/import/reset expose only the legacy operations requested by
    // the page adapter. There is intentionally no rename or delete operation.
    session.SetOperationName("Landing Layer");
    REQUIRE(session.CreateOverlay(error));
    REQUIRE(repository.created == std::vector<std::string>{"Landing Layer"});
    REQUIRE(session.Selected()->configurationName == "Landing Layer");
    REQUIRE(session.Selected()->inheritsMain);

    REQUIRE(session.Select(kMainRecordId, error) == SelectResult::Selected);
    session.SetOperationName("Cruise Full");
    REQUIRE(session.ExportMainFull(error));
    REQUIRE(repository.exported == std::vector<std::string>{"Cruise Full"});
    REQUIRE(session.SelectedRecordId() == kMainRecordId);

    const auto exportedId = FindByName(session, "Cruise Full").recordId;
    REQUIRE(session.Select(exportedId, error) == SelectResult::Selected);
    REQUIRE(session.ImportSelectedFull(error));
    REQUIRE(repository.imported == std::vector<std::string>{"Cruise Full"});
    REQUIRE(session.SelectedRecordId() == kMainRecordId);
    REQUIRE(session.ResetMain(error));
    REQUIRE(repository.resets == 1);
    REQUIRE(session.SelectedRecordId() == kMainRecordId);

    // Invalid controller button references never enter the draft.
    REQUIRE(session.SetActivationBinding("#0@145") == BindingResult::Invalid);
    REQUIRE(session.SetActivationBinding("not-a-binding") == BindingResult::Invalid);
    return 0;
}
