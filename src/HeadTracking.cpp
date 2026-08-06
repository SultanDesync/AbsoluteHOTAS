#include "PCH.h"

#include "HeadTracking.h"
#include "DeviceManager.h"
#include "NativeShipControl.h"
#include "RuntimePaths.h"

namespace {

// FreeTrack 2.0 shared-memory ABI used by OpenTrack's freetrack output.
// Rotations are radians; OpenTrack defines positive yaw/roll to the left and
// positive pitch upward.
struct FreeTrackData {
    std::int32_t dataId;
    std::int32_t cameraWidth;
    std::int32_t cameraHeight;
    float yaw;
    float pitch;
    float roll;
    float x;
    float y;
    float z;
    float rawYaw;
    float rawPitch;
    float rawRoll;
    float rawX;
    float rawY;
    float rawZ;
    float pointX1;
    float pointY1;
    float pointX2;
    float pointY2;
    float pointX3;
    float pointY3;
    float pointX4;
    float pointY4;
};

struct FreeTrackHeap {
    FreeTrackData data;
    std::int32_t gameId;
    std::array<std::uint8_t, 8> table;
    std::int32_t gameId2;
};

static_assert(sizeof(FreeTrackData) == 92);
static_assert(sizeof(FreeTrackHeap) == 108);

struct Quaternion {
    float w = 1.0F;
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

HANDLE g_mapping = nullptr;
HANDLE g_mutex = nullptr;
const FreeTrackHeap* g_view = nullptr;
bool g_loggedWaiting = false;
bool g_haveCenter = false;
bool g_prevRecenter = false;
bool g_prevToggle = false;
bool g_runtimeEnabled = true;
std::int32_t g_lastDataId = std::numeric_limits<std::int32_t>::min();
std::chrono::steady_clock::time_point g_lastFrame{};
std::chrono::steady_clock::time_point g_nextOpenAttempt{};
HeadTracking::Source g_source = HeadTracking::Source::OpenTrack;
std::array<float, 3> g_center{};   // yaw, pitch, roll radians
std::array<float, 3> g_filtered{}; // yaw, pitch, roll degrees
std::array<std::atomic<std::uint32_t>, 3> g_liveTrackerBits{
    std::bit_cast<std::uint32_t>(0.0F), std::bit_cast<std::uint32_t>(0.0F),
    std::bit_cast<std::uint32_t>(0.0F)
};
std::atomic<bool> g_liveTrackerActive{ false };

void HeadLog(std::string_view message)
{
    RuntimePaths::Log("[HeadTracking]", std::string(message));
}

void ClearLiveTracker()
{
    g_liveTrackerActive.store(false, std::memory_order_release);
}

void PublishLiveTracker(const std::array<float, 3>& degrees)
{
    for (std::size_t index = 0; index < degrees.size(); ++index)
        g_liveTrackerBits[index].store(std::bit_cast<std::uint32_t>(degrees[index]),
                                       std::memory_order_relaxed);
    g_liveTrackerActive.store(true, std::memory_order_release);
}

void CloseTransport()
{
    if (g_view) UnmapViewOfFile(g_view);
    if (g_mapping) CloseHandle(g_mapping);
    if (g_mutex) CloseHandle(g_mutex);
    g_view = nullptr;
    g_mapping = nullptr;
    g_mutex = nullptr;
    g_haveCenter = false;
    g_lastDataId = std::numeric_limits<std::int32_t>::min();
    g_lastFrame = {};
    ClearLiveTracker();
}

bool EnsureTransport(HeadTracking::Source source)
{
    if (source != g_source) {
        CloseTransport();
        g_source = source;
    }
    if (g_view) return true;
    const auto now = std::chrono::steady_clock::now();
    if (now < g_nextOpenAttempt) return false;
    g_nextOpenAttempt = now + std::chrono::seconds(1);

    g_mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, L"FT_SharedMem");
    if (!g_mapping) {
        if (!g_loggedWaiting) {
            HeadLog(source == HeadTracking::Source::TobiiViaOpenTrack
                ? "Waiting for OpenTrack FreeTrack output (Tobii tracker mode)."
                : "Waiting for OpenTrack FreeTrack 2.0 output.");
            g_loggedWaiting = true;
        }
        return false;
    }
    g_view = static_cast<const FreeTrackHeap*>(MapViewOfFile(
        g_mapping, FILE_MAP_READ, 0, 0, sizeof(FreeTrackHeap)));
    if (!g_view) {
        CloseHandle(g_mapping);
        g_mapping = nullptr;
        return false;
    }
    // The historical protocol intentionally spells this name "Mutext".
    g_mutex = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, L"FT_Mutext");
    g_loggedWaiting = false;
    HeadLog(source == HeadTracking::Source::TobiiViaOpenTrack
        ? "Connected to Tobii pose through OpenTrack FreeTrack output."
        : "Connected to OpenTrack FreeTrack 2.0 output.");
    return true;
}

bool ReadFrame(FreeTrackData& data)
{
    if (!g_view) return false;
    bool locked = false;
    if (g_mutex) {
        const DWORD wait = WaitForSingleObject(g_mutex, 2);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) return false;
        locked = true;
    }
    std::memcpy(&data, &g_view->data, sizeof(data));
    if (locked) ReleaseMutex(g_mutex);
    return std::isfinite(data.yaw) && std::isfinite(data.pitch) &&
           std::isfinite(data.roll);
}

bool ReadCurrentTrackerFrame(const HeadTracking::Settings& settings,
                             FreeTrackData& frame)
{
    if (!settings.openTrackEnabled || !EnsureTransport(settings.source) ||
        !ReadFrame(frame)) return false;

    const auto now = std::chrono::steady_clock::now();
    if (frame.dataId != g_lastDataId) {
        g_lastDataId = frame.dataId;
        g_lastFrame = now;
        return true;
    }
    return g_lastFrame.time_since_epoch().count() != 0 &&
        now - g_lastFrame <= std::chrono::milliseconds(
            std::clamp(settings.staleMilliseconds, 50, 5000));
}

std::array<float, 3> CenteredTrackerDegrees(const FreeTrackData& frame)
{
    constexpr float kDegreesPerRadian = 57.295779513082320876F;
    return {
        (frame.yaw - g_center[0]) * kDegreesPerRadian,
        (frame.pitch - g_center[1]) * kDegreesPerRadian,
        (frame.roll - g_center[2]) * kDegreesPerRadian,
    };
}

float ShapeAngle(float degrees, float deadzone, float maximum)
{
    const float sign = degrees < 0.0F ? -1.0F : 1.0F;
    const float magnitude = std::abs(degrees);
    const float shaped = magnitude <= deadzone ? 0.0F : magnitude - deadzone;
    return sign * std::min(shaped, maximum);
}

float ReadJoystickAxis(const BindingRef& binding,
                       const HeadTracking::AxisCalibrationMap& calibration,
                       float deadzone)
{
    if (!binding.IsValid() || binding.value < 0x30 || binding.value > 0x37)
        return 0.0F;

    long minimum = 0;
    long maximum = 65535;
    const int calibrationKey = (binding.deviceIndex << 8) | binding.value;
    const auto range = calibration.find(calibrationKey);
    if (range != calibration.end() && range->second.first < range->second.second) {
        minimum = range->second.first;
        maximum = range->second.second;
    }

    const float low = static_cast<float>(minimum);
    const float high = static_cast<float>(maximum);
    const float center = (low + high) * 0.5F;
    const float halfRange = std::max(1.0F, (high - low) * 0.5F);
    const float raw = static_cast<float>(DeviceManager::GetRawAxis(binding));
    float normalized = std::clamp((raw - center) / halfRange, -1.0F, 1.0F);

    const float dz = std::clamp(deadzone, 0.0F, 0.95F);
    const float magnitude = std::abs(normalized);
    if (magnitude <= dz) return 0.0F;
    normalized = std::copysign((magnitude - dz) / (1.0F - dz), normalized);
    return normalized;
}

Quaternion Multiply(const Quaternion& lhs, const Quaternion& rhs)
{
    return {
        lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z,
        lhs.w * rhs.x + lhs.x * rhs.w - lhs.y * rhs.z + lhs.z * rhs.y,
        lhs.w * rhs.y + lhs.x * rhs.z + lhs.y * rhs.w - lhs.z * rhs.x,
        lhs.w * rhs.z - lhs.x * rhs.y + lhs.y * rhs.x + lhs.z * rhs.w,
    };
}

Quaternion AxisAngle(float x, float y, float z, float radians)
{
    const float half = radians * 0.5F;
    const float sine = std::sin(half);
    return { std::cos(half), x * sine, y * sine, z * sine };
}

Quaternion HeadQuaternion(float yawDegrees, float pitchDegrees, float rollDegrees)
{
    constexpr float kRadiansPerDegree = 0.01745329251994329577F;
    // Accepted Starfield camera axes: X=pitch, Y=roll, Z=yaw.
    const auto pitch = AxisAngle(1.0F, 0.0F, 0.0F, pitchDegrees * kRadiansPerDegree);
    const auto roll = AxisAngle(0.0F, 1.0F, 0.0F, rollDegrees * kRadiansPerDegree);
    const auto yaw = AxisAngle(0.0F, 0.0F, 1.0F, yawDegrees * kRadiansPerDegree);
    return Multiply(Multiply(yaw, roll), pitch);
}

} // namespace

namespace HeadTracking {

void Update(const Settings& settings, const AxisCalibrationMap& calibration,
            float dt, bool eligible)
{
    const bool recenterDown = settings.recenterButton.IsValid() &&
        DeviceManager::IsButtonPressed(settings.recenterButton);
    const bool toggleDown = settings.toggleButton.IsValid() &&
        DeviceManager::IsButtonPressed(settings.toggleButton);

    if (!settings.enabled) {
        g_runtimeEnabled = true;
        g_prevRecenter = recenterDown;
        g_prevToggle = toggleDown;
        NativeShipControl::ClearHeadPose();
        return;
    }

    if (toggleDown && !g_prevToggle) {
        g_runtimeEnabled = !g_runtimeEnabled;
        g_filtered = {};
        if (g_runtimeEnabled) g_haveCenter = false;
        HeadLog(g_runtimeEnabled ? "Camera look toggled on." : "Camera look toggled off.");
    }
    g_prevToggle = toggleDown;

    if (!eligible || !g_runtimeEnabled || !NativeShipControl::ShipHandlerReady()) {
        NativeShipControl::ClearHeadPose();
        g_prevRecenter = recenterDown;
        return;
    }

    FreeTrackData frame{};
    const bool haveTrackerFrame = ReadCurrentTrackerFrame(settings, frame);

    const bool recenterPressed = recenterDown && !g_prevRecenter;
    if (haveTrackerFrame && (!g_haveCenter || recenterPressed)) {
        g_center = { frame.yaw, frame.pitch, frame.roll };
        g_filtered = {};
        g_haveCenter = true;
        if (recenterPressed) HeadLog("Camera look recentered.");
    } else if (recenterPressed) {
        g_filtered = {};
        HeadLog("Joystick camera look recentered.");
    }
    g_prevRecenter = recenterDown;

    const float deadzone = std::clamp(settings.deadzoneDegrees, 0.0F, 20.0F);
    const std::array<float, 3> scales{ settings.yawScale, settings.pitchScale, settings.rollScale };
    const std::array<float, 3> maximums{
        std::clamp(settings.maxYawDegrees, 1.0F, 180.0F),
        std::clamp(settings.maxPitchDegrees, 1.0F, 180.0F),
        std::clamp(settings.maxRollDegrees, 1.0F, 180.0F),
    };
    const std::array<bool, 3> inverted{ settings.invertYaw, settings.invertPitch, settings.invertRoll };
    const std::array<bool, 3> axisEnabled{
        settings.yawEnabled, settings.pitchEnabled, settings.rollEnabled
    };
    const std::array<BindingRef, 3> joystickAxes{ settings.yawAxis, settings.pitchAxis, settings.rollAxis };
    std::array<float, 3> target{};

    if (haveTrackerFrame && g_haveCenter)
        PublishLiveTracker(CenteredTrackerDegrees(frame));
    else
        ClearLiveTracker();

    if (std::none_of(axisEnabled.begin(), axisEnabled.end(), [](bool enabled) { return enabled; })) {
        NativeShipControl::ClearHeadPose();
        g_filtered = {};
        return;
    }

    if (haveTrackerFrame && g_haveCenter) {
        const auto trackerAngles = CenteredTrackerDegrees(frame);
        for (std::size_t index = 0; index < target.size(); ++index) {
            if (!axisEnabled[index]) continue;
            target[index] = ShapeAngle(trackerAngles[index] * scales[index] *
                (inverted[index] ? -1.0F : 1.0F), deadzone, maximums[index]);
        }
    }

    bool haveJoystickAxis = false;
    for (std::size_t index = 0; index < joystickAxes.size(); ++index) {
        if (!axisEnabled[index] || !joystickAxes[index].IsValid()) continue;
        haveJoystickAxis = true;
        const float axis = ReadJoystickAxis(joystickAxes[index], calibration,
                                            settings.joystickDeadzone);
        target[index] = std::clamp(axis * maximums[index] * scales[index] *
            (inverted[index] ? -1.0F : 1.0F), -maximums[index], maximums[index]);
    }

    if (!haveTrackerFrame && !haveJoystickAxis) {
        NativeShipControl::ClearHeadPose();
        return;
    }

    const float smoothing = std::clamp(settings.smoothing, 0.0F, 0.99F);
    const float retention = std::pow(smoothing, std::max(0.0F, dt) * 60.0F);
    const float blend = 1.0F - retention;
    for (std::size_t index = 0; index < target.size(); ++index)
        g_filtered[index] += (target[index] - g_filtered[index]) * blend;

    const auto pose = HeadQuaternion(g_filtered[0], g_filtered[1], g_filtered[2]);
    NativeShipControl::SetHeadQuaternion(pose.w, pose.x, pose.y, pose.z, true);
}

void PollPreview(const Settings& settings)
{
    const bool recenterDown = settings.recenterButton.IsValid() &&
        DeviceManager::IsButtonPressed(settings.recenterButton);
    FreeTrackData frame{};
    const bool haveTrackerFrame = ReadCurrentTrackerFrame(settings, frame);
    const bool recenterPressed = recenterDown && !g_prevRecenter;

    if (haveTrackerFrame && (!g_haveCenter || recenterPressed)) {
        g_center = { frame.yaw, frame.pitch, frame.roll };
        g_haveCenter = true;
    }
    g_prevRecenter = recenterDown;

    if (haveTrackerFrame && g_haveCenter)
        PublishLiveTracker(CenteredTrackerDegrees(frame));
    else
        ClearLiveTracker();
}

LiveInput GetLiveInput()
{
    LiveInput input;
    input.trackerActive = g_liveTrackerActive.load(std::memory_order_acquire);
    if (!input.trackerActive) return input;
    for (std::size_t index = 0; index < input.trackerDegrees.size(); ++index)
        input.trackerDegrees[index] = std::bit_cast<float>(
            g_liveTrackerBits[index].load(std::memory_order_relaxed));
    return input;
}

void Suspend()
{
    NativeShipControl::ClearHeadPose();
    g_filtered = {};
}

void Shutdown()
{
    Suspend();
    CloseTransport();
    g_prevRecenter = false;
    g_prevToggle = false;
    g_runtimeEnabled = true;
    ClearLiveTracker();
}

} // namespace HeadTracking
