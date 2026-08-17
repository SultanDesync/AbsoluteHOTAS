#pragma once

#include "WizardCapture.h"
#include "WizardConfig.h"

#include <string>
#include <vector>

namespace WizardSession {

enum class Page { Bind, Tune, Advanced, Power };
enum class BindPage { FlightAxes, ShipButtons };
enum class TunePage { Aiming, CameraLook, GamepadThrottle };
enum class AdvancedPage { Macros, PluginControls, Devices };

enum class Route {
    BindFlightAxes,
    BindShipButtons,
    TuneAiming,
    TuneCameraLook,
    TuneGamepadThrottle,
    AdvancedMacros,
    AdvancedPluginControls,
    AdvancedDevices,
};

enum class StatusKind { None, Success, Warning, Error };

struct Status {
    StatusKind kind = StatusKind::None;
    std::string message;
};

enum class ProfileSwitchChoice { Save, Discard, Cancel };

void Initialize();

Page GetPage();
BindPage GetBindPage();
TunePage GetTunePage();
AdvancedPage GetAdvancedPage();
Route GetRoute();

void SelectPage(Page page);
void SelectBindPage(BindPage page);
void SelectTunePage(TunePage page);
void SelectAdvancedPage(AdvancedPage page);
void Navigate(Route route);

const Status& GetStatus();
void SetStatus(std::string message, StatusKind kind = StatusKind::Success);
void ClearStatus();

std::string VisibleProfileName(const std::string& name);
bool SaveCurrentProfile();
bool LoadEditorProfile(const std::string& name);
bool HasUnsavedChanges();
void RequestEditorProfile(const std::string& name);
bool HasPendingProfileSwitch();
const std::string& PendingProfile();
bool ResolveProfileSwitch(ProfileSwitchChoice choice);
bool RequestClose();
void RequireCloseResolution(std::string message);
bool HasPendingClose();
void CancelPendingClose();
bool DiscardChanges();

// Cached repository metadata. Refresh only after explicit profile commands so
// ordinary rendering remains free of profile-directory and INI reads.
const std::vector<WizardConfig::ProfileSummary>& Profiles();
const std::string& BaseActivationTrigger();
const std::string& BaseActivationMode();
bool RefreshProfiles();
bool SetActivationDraft(const std::string& profile, const std::string& trigger,
                        const std::string& mode);
void SwapActivationDeviceIndices(int first, int second);

void BeginAxisCapture(int slot, const char* label);
void BeginButtonCapture(int slotIndex, int categoryOffset, const char* label,
                        int settleWindowMs = WizardCapture::kButtonCaptureMs);
void UpdateCapture(WizardCapture::BindingCommitFn commit);
void CancelCapture();
bool IsCapturing();
const WizardCapture::PendingBind& Capture();

// Cancel interactions whose target is meaningful only inside the current view.
// Draft values are preserved.
void CancelTransientInteractions();

} // namespace WizardSession
