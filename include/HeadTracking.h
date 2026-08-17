#pragma once

#include "BindingRef.h"

#include <array>
#include <unordered_map>

namespace HeadTracking {

enum class Source {
    OpenTrack,
    TobiiViaOpenTrack,
};

struct Settings {
    bool enabled = false;
    bool openTrackEnabled = true;
    Source source = Source::OpenTrack;
    BindingRef recenterButton;
    BindingRef toggleButton;
    BindingRef yawAxis;
    BindingRef pitchAxis;
    BindingRef rollAxis;
    float yawScale = 1.0f;
    float pitchScale = 1.0f;
    float rollScale = 1.0f;
    float maxYawDegrees = 85.0f;
    float maxPitchDegrees = 60.0f;
    float maxRollDegrees = 45.0f;
    float deadzoneDegrees = 0.0f;
    float joystickDeadzone = 0.08f;
    float smoothing = 0.15f;
    int staleMilliseconds = 500;
    bool yawEnabled = true;
    bool pitchEnabled = true;
    bool rollEnabled = true;
    bool invertYaw = false;
    bool invertPitch = false;
    bool invertRoll = false;
};

using AxisCalibrationMap = std::unordered_map<int, std::pair<long, long>>;

// Monitor-only tracker sample used by the workbench. Values are the centered
// OpenTrack angles before AbsoluteHOTAS sensitivity, inversion, deadzone, and
// maximum-angle shaping are applied.
struct LiveInput {
    std::array<float, 3> trackerDegrees{}; // yaw, pitch, roll
    bool trackerActive = false;
};

// Poll the OpenTrack FreeTrack 2.0 shared-memory transport and publish a
// camera-only quaternion. OpenTrack itself can use its native Tobii tracker,
// which is the dependency-free Tobii path shipped by AbsoluteHOTAS.
void Update(const Settings& settings, const AxisCalibrationMap& calibration,
            float dt, bool eligible);
// Keep the workbench readout live while camera injection is parked. This polls
// only the OpenTrack transport and never publishes a camera quaternion.
void PollPreview(const Settings& settings);
LiveInput GetLiveInput();
// Parks the legacy embedded tracker when Absolute Head Tracking owns camera
// composition. This is process-lifetime arbitration; transport cleanup remains
// on the controller thread during normal shutdown.
void SetExternalOwner(bool active);
bool ExternalOwnerActive();
void Suspend();
void Shutdown();

} // namespace HeadTracking
