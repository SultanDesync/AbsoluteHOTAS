#include "PCH.h"

#include "WizardSession.h"

#include "WizardConfig.h"

namespace WizardSession {
namespace {

Page s_page = Page::Bind;
BindPage s_bindPage = BindPage::FlightAxes;
TunePage s_tunePage = TunePage::Aiming;
AdvancedPage s_advancedPage = AdvancedPage::Macros;
Status s_status;
std::string s_pendingProfile;
bool s_hasPendingProfile = false;
bool s_pendingClose = false;
std::string s_captureProfile;
Route s_captureRoute = Route::BindFlightAxes;
std::vector<WizardConfig::ProfileSummary> s_profiles;
std::string s_baseActivationTrigger = "(unbound)";
std::string s_baseActivationMode = "momentary";
std::vector<WizardConfig::ProfileSummary> s_savedProfiles;
std::string s_savedBaseActivationTrigger = "(unbound)";
std::string s_savedBaseActivationMode = "momentary";

bool ActivationDraftDirty() {
    if (s_baseActivationTrigger != s_savedBaseActivationTrigger
        || s_baseActivationMode != s_savedBaseActivationMode
        || s_profiles.size() != s_savedProfiles.size()) return true;
    for (const auto& profile : s_profiles) {
        const auto saved = std::find_if(s_savedProfiles.begin(), s_savedProfiles.end(),
            [&](const auto& candidate) { return candidate.name == profile.name; });
        if (saved == s_savedProfiles.end() || saved->trigger != profile.trigger
            || saved->mode != profile.mode) return true;
    }
    return false;
}

void RestoreActivationDraft() {
    s_profiles = s_savedProfiles;
    s_baseActivationTrigger = s_savedBaseActivationTrigger;
    s_baseActivationMode = s_savedBaseActivationMode;
}

void SwapBindingDevicePrefix(std::string& binding, int first, int second) {
    const std::string firstPrefix = "#" + std::to_string(first) + "@";
    const std::string secondPrefix = "#" + std::to_string(second) + "@";
    if (binding.rfind(firstPrefix, 0) == 0)
        binding = secondPrefix + binding.substr(firstPrefix.size());
    else if (binding.rfind(secondPrefix, 0) == 0)
        binding = firstPrefix + binding.substr(secondPrefix.size());
}

void CancelCalibrationGestures() {
    auto& draft = WizardConfig::GetState();
    draft.calibratingCenter = false;
    draft.calibratingReverseZone = false;
    draft.calibratingBoostZone = false;
    WizardCapture::GetCalibState().Reset();
}

void CancelCaptureForContextChange() {
    if (!WizardCapture::GetPendingBind().active) return;
    WizardCapture::CancelCapture();
    s_captureProfile.clear();
    SetStatus("Input capture cancelled because the editing context changed.", StatusKind::Warning);
}

} // namespace

void Initialize() {
    s_page = Page::Bind;
    s_bindPage = BindPage::FlightAxes;
    s_tunePage = TunePage::Aiming;
    s_advancedPage = AdvancedPage::Macros;
    s_status = {};
    s_pendingProfile.clear();
    s_hasPendingProfile = false;
    s_pendingClose = false;
    s_captureProfile.clear();

    std::string err;
    if (!WizardConfig::EnsureStarterProfiles(err)) {
        SetStatus(err.empty() ? "Starter profiles could not be initialized." : err,
                  StatusKind::Error);
    }
    RefreshProfiles();
}

Page GetPage() { return s_page; }
BindPage GetBindPage() { return s_bindPage; }
TunePage GetTunePage() { return s_tunePage; }
AdvancedPage GetAdvancedPage() { return s_advancedPage; }

Route GetRoute() {
    switch (s_page) {
        case Page::Bind:
            return s_bindPage == BindPage::FlightAxes
                ? Route::BindFlightAxes : Route::BindShipButtons;
        case Page::Tune:
            switch (s_tunePage) {
                case TunePage::Aiming: return Route::TuneAiming;
                case TunePage::GamepadThrottle: return Route::TuneGamepadThrottle;
            }
            break;
        case Page::Advanced:
            switch (s_advancedPage) {
                case AdvancedPage::Macros: return Route::AdvancedMacros;
                case AdvancedPage::PluginControls: return Route::AdvancedPluginControls;
                case AdvancedPage::Devices: return Route::AdvancedDevices;
            }
            break;
    }
    return Route::BindFlightAxes;
}

void SelectPage(Page page) {
    if (s_page == page) return;
    CancelCaptureForContextChange();
    CancelCalibrationGestures();
    s_page = page;
}

void SelectBindPage(BindPage page) {
    if (s_page == Page::Bind && s_bindPage == page) return;
    CancelCaptureForContextChange();
    CancelCalibrationGestures();
    s_page = Page::Bind;
    s_bindPage = page;
}

void SelectTunePage(TunePage page) {
    if (s_page == Page::Tune && s_tunePage == page) return;
    CancelCaptureForContextChange();
    CancelCalibrationGestures();
    s_page = Page::Tune;
    s_tunePage = page;
}

void SelectAdvancedPage(AdvancedPage page) {
    if (s_page == Page::Advanced && s_advancedPage == page) return;
    CancelCaptureForContextChange();
    CancelCalibrationGestures();
    s_page = Page::Advanced;
    s_advancedPage = page;
}

void Navigate(Route route) {
    switch (route) {
        case Route::BindFlightAxes: SelectBindPage(BindPage::FlightAxes); break;
        case Route::BindShipButtons: SelectBindPage(BindPage::ShipButtons); break;
        case Route::TuneAiming: SelectTunePage(TunePage::Aiming); break;
        case Route::TuneGamepadThrottle: SelectTunePage(TunePage::GamepadThrottle); break;
        case Route::AdvancedMacros: SelectAdvancedPage(AdvancedPage::Macros); break;
        case Route::AdvancedPluginControls: SelectAdvancedPage(AdvancedPage::PluginControls); break;
        case Route::AdvancedDevices: SelectAdvancedPage(AdvancedPage::Devices); break;
    }
}

const Status& GetStatus() { return s_status; }

void SetStatus(std::string message, StatusKind kind) {
    s_status.kind = kind;
    s_status.message = std::move(message);
}

void ClearStatus() { s_status = {}; }

std::string VisibleProfileName(const std::string& name) {
    return name.empty() ? "Main controls" : name;
}

bool SaveCurrentProfile() {
    std::string err;
    const std::string target = VisibleProfileName(WizardConfig::GetEditProfile());
    if (!WizardConfig::SaveActiveProfile(err)) {
        SetStatus(err.empty() ? "The profile could not be saved." : err, StatusKind::Error);
        return false;
    }

    if (s_baseActivationTrigger != s_savedBaseActivationTrigger
        || s_baseActivationMode != s_savedBaseActivationMode) {
        if (!WizardConfig::SetProfileActivation("", s_baseActivationTrigger,
                                                s_baseActivationMode, err)) {
            SetStatus(err.empty() ? "The main-controls activation could not be saved." : err,
                      StatusKind::Error);
            return false;
        }
    }
    for (const auto& profile : s_profiles) {
        const auto saved = std::find_if(s_savedProfiles.begin(), s_savedProfiles.end(),
            [&](const auto& candidate) { return candidate.name == profile.name; });
        if (saved != s_savedProfiles.end() && saved->trigger == profile.trigger
            && saved->mode == profile.mode) continue;
        if (!WizardConfig::SetProfileActivation(profile.name, profile.trigger,
                                                profile.mode, err)) {
            SetStatus(err.empty() ? "A profile activation could not be saved." : err,
                      StatusKind::Error);
            return false;
        }
    }
    RefreshProfiles();
    SetStatus("Saved and applied: " + target, StatusKind::Success);
    return true;
}

bool LoadEditorProfile(const std::string& name) {
    CancelTransientInteractions();
    std::string err;
    if (!WizardConfig::LoadProfileForEditing(name, err)) {
        SetStatus(err.empty() ? "The profile could not be opened." : err, StatusKind::Error);
        return false;
    }
    SetStatus("Editing: " + VisibleProfileName(name), StatusKind::Success);
    return true;
}

bool HasUnsavedChanges() {
    return WizardConfig::HasUnsavedChanges() || ActivationDraftDirty();
}

void RequestEditorProfile(const std::string& name) {
    if (name == WizardConfig::GetEditProfile()) return;
    CancelTransientInteractions();
    if (HasUnsavedChanges()) {
        s_pendingProfile = name;
        s_hasPendingProfile = true;
        return;
    }
    LoadEditorProfile(name);
}

bool HasPendingProfileSwitch() { return s_hasPendingProfile; }
const std::string& PendingProfile() { return s_pendingProfile; }

bool ResolveProfileSwitch(ProfileSwitchChoice choice) {
    if (!s_hasPendingProfile) return true;
    if (choice == ProfileSwitchChoice::Cancel) {
        s_pendingProfile.clear();
        s_hasPendingProfile = false;
        return true;
    }

    const std::string target = s_pendingProfile;
    if (choice == ProfileSwitchChoice::Save && !SaveCurrentProfile()) return false;
    if (choice == ProfileSwitchChoice::Discard) RestoreActivationDraft();
    if (!LoadEditorProfile(target)) return false;
    s_pendingProfile.clear();
    s_hasPendingProfile = false;
    return true;
}

bool RequestClose() {
    CancelTransientInteractions();
    if (!HasUnsavedChanges()) return true;
    s_pendingClose = true;
    SetStatus("Unsaved changes must be saved or discarded before closing.",
              StatusKind::Warning);
    return false;
}

bool HasPendingClose() { return s_pendingClose; }
void CancelPendingClose() { s_pendingClose = false; }

bool DiscardChanges() {
    RestoreActivationDraft();
    std::string err;
    const std::string current = WizardConfig::GetEditProfile();
    if (!WizardConfig::LoadProfileForEditing(current, err)) {
        SetStatus(err.empty() ? "The saved profile could not be restored." : err,
                  StatusKind::Error);
        return false;
    }
    SetStatus("Discarded unsaved changes.", StatusKind::Success);
    s_pendingClose = false;
    return true;
}

const std::vector<WizardConfig::ProfileSummary>& Profiles() { return s_profiles; }
const std::string& BaseActivationTrigger() { return s_baseActivationTrigger; }
const std::string& BaseActivationMode() { return s_baseActivationMode; }

bool RefreshProfiles() {
    s_profiles = WizardConfig::ListProfileSummaries();
    WizardConfig::GetBaseActivation(s_baseActivationTrigger, s_baseActivationMode);
    s_savedProfiles = s_profiles;
    s_savedBaseActivationTrigger = s_baseActivationTrigger;
    s_savedBaseActivationMode = s_baseActivationMode;
    return true;
}

bool SetActivationDraft(const std::string& profile, const std::string& trigger,
                        const std::string& mode) {
    if (profile.empty()) {
        s_baseActivationTrigger = trigger;
        s_baseActivationMode = mode;
        return true;
    }
    const auto target = std::find_if(s_profiles.begin(), s_profiles.end(),
        [&](const auto& candidate) { return candidate.name == profile; });
    if (target == s_profiles.end()) {
        SetStatus("The activation target is no longer available.", StatusKind::Error);
        return false;
    }
    target->trigger = trigger;
    target->mode = mode;
    return true;
}

void SwapActivationDeviceIndices(int first, int second) {
    SwapBindingDevicePrefix(s_baseActivationTrigger, first, second);
    for (auto& profile : s_profiles)
        SwapBindingDevicePrefix(profile.trigger, first, second);
}

void BeginAxisCapture(int slot, const char* label) {
    CancelCapture();
    s_captureProfile = WizardConfig::GetEditProfile();
    s_captureRoute = GetRoute();
    WizardCapture::StartAxisCapture(slot, label);
}

void BeginButtonCapture(int slotIndex, int categoryOffset, const char* label,
                        int settleWindowMs) {
    CancelCapture();
    s_captureProfile = WizardConfig::GetEditProfile();
    s_captureRoute = GetRoute();
    WizardCapture::StartButtonCapture(slotIndex, categoryOffset, label, settleWindowMs);
}

void UpdateCapture(WizardCapture::BindingCommitFn commit) {
    auto& pending = WizardCapture::GetPendingBind();
    if (pending.active && (s_captureProfile != WizardConfig::GetEditProfile()
        || s_captureRoute != GetRoute())) {
        CancelCaptureForContextChange();
        return;
    }
    const bool completed = WizardCapture::UpdateCapture(commit);
    if (completed || !pending.active) s_captureProfile.clear();
}

void CancelCapture() {
    WizardCapture::CancelCapture();
    s_captureProfile.clear();
}

bool IsCapturing() { return WizardCapture::GetPendingBind().active; }
const WizardCapture::PendingBind& Capture() { return WizardCapture::GetPendingBind(); }

void CancelTransientInteractions() {
    CancelCapture();
    CancelCalibrationGestures();
}

} // namespace WizardSession
