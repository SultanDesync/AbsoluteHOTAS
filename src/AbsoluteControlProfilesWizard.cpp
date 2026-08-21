#include "PCH.h"

#include "AbsoluteControlProfiles.h"
#include "WizardSession.h"

namespace {

class WizardProfileRepository final : public AbsoluteControlProfiles::Repository {
public:
    std::vector<WizardConfig::ProfileSummary> List() override
    {
        return WizardConfig::ListProfileSummaries();
    }

    void GetMainActivation(std::string& trigger, std::string& mode) override
    {
        WizardConfig::GetBaseActivation(trigger, mode);
    }

    std::string CurrentEditTarget() override
    {
        return WizardConfig::GetEditProfile();
    }

    bool LoadEditTarget(const std::string& name, std::string& error) override
    {
        if (WizardSession::LoadEditorProfile(name)) return true;
        error = WizardSession::GetStatus().message;
        return false;
    }

    bool HasEditTargetChanges() override
    {
        return WizardConfig::HasUnsavedChanges();
    }

    bool SaveEditTarget(std::string& error) override
    {
        return WizardConfig::SaveActiveProfile(error);
    }

    bool DiscardEditTarget(std::string& error) override
    {
        return WizardConfig::LoadProfileForEditing(
            WizardConfig::GetEditProfile(), error);
    }

    bool SaveActivations(
        const std::vector<WizardConfig::ProfileActivationUpdate>& updates,
        std::string& error) override
    {
        return WizardConfig::SetProfileActivations(updates, error);
    }

    bool CreateOverlay(const std::string& name, std::string& error) override
    {
        return WizardConfig::CreateOverlayProfile(name, error);
    }

    bool ExportFull(const std::string& name, std::string& error) override
    {
        return WizardConfig::ExportProfile(name, error);
    }

    bool ImportFull(const std::string& name, std::string& error) override
    {
        return WizardConfig::ImportProfile(name, error);
    }

    bool ResetMain(std::string& error) override
    {
        return WizardConfig::ResetBaseToDefaults(error);
    }
};

WizardProfileRepository g_wizardRepository;

} // namespace

namespace AbsoluteControlProfiles {

Repository& WizardRepository() noexcept
{
    return g_wizardRepository;
}

} // namespace AbsoluteControlProfiles
