#include "PCH.h"
#include "DeviceManager.h"
#include "RuntimePaths.h"
#include "StringUtils.h"

static LPDIRECTINPUT8 g_pDI = nullptr;
static std::vector<DeviceInfo> g_devices;
static std::vector<LPDIRECTINPUTDEVICE8> g_openDevices;
static int g_enumCounter = 0;

static void DevLog(const std::string& msg) {
    RuntimePaths::AppendLog("[DeviceManager]", msg);
}


// Callback to enumerate objects (axes, buttons) to count them
static BOOL CALLBACK EnumObjectsCallback(const DIDEVICEOBJECTINSTANCE* pdidoi, VOID* pContext) {
    auto* info = static_cast<DeviceInfo*>(pContext);
    if (pdidoi->dwType & DIDFT_AXIS) {
        info->axisCount++;
    } else if (pdidoi->dwType & DIDFT_BUTTON) {
        info->buttonCount++;
    }
    return DIENUM_CONTINUE;
}

static BOOL CALLBACK EnumJoysticksCallback(const DIDEVICEINSTANCE* pdidInstance, VOID* pContext) {
    (void)pContext;
    DeviceInfo info{};
    info.guidInstance = pdidInstance->guidInstance;
    info.guidProduct = pdidInstance->guidProduct;
    info.instanceName = pdidInstance->tszInstanceName;
    info.productName = pdidInstance->tszProductName;
    
    // Extract VID and PID from guidProduct.Data1
    info.vid = LOWORD(info.guidProduct.Data1);
    info.pid = HIWORD(info.guidProduct.Data1);
    
    char vidpidBuf[16];
    sprintf_s(vidpidBuf, "%04X:%04X", info.vid, info.pid);
    info.vidpidString = vidpidBuf;
    
    info.enumIndex = g_enumCounter++;
    info.isOpen = false;
    info.axisCount = 0;
    info.buttonCount = 0;

    // Temporarily open the device to query capabilities
    LPDIRECTINPUTDEVICE8 tempDevice;
    if (SUCCEEDED(g_pDI->CreateDevice(pdidInstance->guidInstance, &tempDevice, NULL))) {
        tempDevice->EnumObjects(EnumObjectsCallback, &info, DIDFT_ALL);
        tempDevice->Release();
    }

    g_devices.push_back(info);
    g_openDevices.push_back(nullptr);
    return DIENUM_CONTINUE;
}

bool DeviceManager::Initialize() {
    if (g_pDI) return true;
    
    if (FAILED(DirectInput8Create(GetModuleHandle(NULL), DIRECTINPUT_VERSION, IID_IDirectInput8, (VOID**)&g_pDI, NULL))) {
        DevLog("Failed to create DirectInput8!");
        return false;
    }
    
    Refresh();
    return true;
}

void DeviceManager::Shutdown() {
    for (size_t i = 0; i < g_openDevices.size(); i++) {
        CloseDevice(static_cast<int>(i));
    }
    
    if (g_pDI) {
        g_pDI->Release();
        g_pDI = nullptr;
    }
    
    g_devices.clear();
    g_openDevices.clear();
}

void DeviceManager::Refresh() {
    for (size_t i = 0; i < g_openDevices.size(); i++) {
        CloseDevice(static_cast<int>(i));
    }
    g_devices.clear();
    g_openDevices.clear();
    g_enumCounter = 0;
    
    if (g_pDI) {
        g_pDI->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumJoysticksCallback, NULL, DIEDFL_ATTACHEDONLY);
    }
}

int DeviceManager::GetDeviceCount() {
    return static_cast<int>(g_devices.size());
}

const DeviceInfo& DeviceManager::GetDevice(int index) {
    if (index < 0 || index >= static_cast<int>(g_devices.size())) {
        throw std::out_of_range("Device index out of range");
    }
    return g_devices[index];
}

const std::vector<DeviceInfo>& DeviceManager::GetAllDevices() {
    return g_devices;
}

LPDIRECTINPUTDEVICE8 DeviceManager::OpenDevice(int index) {
    if (index < 0 || index >= static_cast<int>(g_devices.size())) return nullptr;
    
    if (g_openDevices[index]) return g_openDevices[index]; // Already open
    
    LPDIRECTINPUTDEVICE8 device;
    if (FAILED(g_pDI->CreateDevice(g_devices[index].guidInstance, &device, NULL))) {
        DevLog(std::format("Failed to create DirectInput device at index {}", index));
        return nullptr;
    }
    
    if (FAILED(device->SetDataFormat(&c_dfDIJoystick2))) {
        DevLog(std::format("Failed to set DirectInput data format for device {}", index));
        device->Release();
        return nullptr;
    }
    
    device->Acquire();
    g_openDevices[index] = device;
    g_devices[index].isOpen = true;
    
    return device;
}

void DeviceManager::CloseDevice(int index) {
    if (index < 0 || index >= static_cast<int>(g_openDevices.size())) return;
    
    if (g_openDevices[index]) {
        g_openDevices[index]->Unacquire();
        g_openDevices[index]->Release();
        g_openDevices[index] = nullptr;
    }
    if (index < static_cast<int>(g_devices.size())) {
        g_devices[index].isOpen = false;
    }
}

int DeviceManager::ResolveDevice(const std::string& vidpid, const std::string& name, int index) {
    std::string targetVidPid = TrimLower(vidpid);
    std::string targetName = TrimLower(name);
    
    // Priority 1: VID:PID match
    if (!targetVidPid.empty()) {
        for (size_t i = 0; i < g_devices.size(); ++i) {
            if (TrimLower(g_devices[i].vidpidString) == targetVidPid) {
                return static_cast<int>(i);
            }
        }
        DevLog(std::format("Warning: Device with VID:PID '{}' not found.", targetVidPid));
    }
    
    // Priority 2: Name match (instance or product name)
    if (!targetName.empty()) {
        for (size_t i = 0; i < g_devices.size(); ++i) {
            std::string instName = TrimLower(g_devices[i].instanceName);
            std::string prodName = TrimLower(g_devices[i].productName);
            if (instName.find(targetName) != std::string::npos || prodName.find(targetName) != std::string::npos) {
                return static_cast<int>(i);
            }
        }
        DevLog(std::format("Warning: Device with name containing '{}' not found.", targetName));
    }
    
    // Priority 3: Fallback index
    if (index >= 0 && index < static_cast<int>(g_devices.size())) {
        return index;
    }
    
    DevLog("Warning: Could not resolve device. Returning -1.");
    return -1;
}

bool DeviceManager::PollDevice(int index, DIJOYSTATE2& outState) {
    if (index < 0 || index >= static_cast<int>(g_openDevices.size())) return false;
    LPDIRECTINPUTDEVICE8 dev = g_openDevices[index];
    if (!dev) return false;
    
    dev->Poll();
    HRESULT hr = dev->GetDeviceState(sizeof(DIJOYSTATE2), &outState);
    if (FAILED(hr)) {
        dev->Acquire();
        return false;
    }
    return true;
}

static std::vector<DIJOYSTATE2> g_cachedStates;
static std::vector<bool> g_cachedValid;

void DeviceManager::PollAll() {
    size_t count = g_openDevices.size();
    g_cachedStates.resize(count);
    g_cachedValid.resize(count, false);
    
    for (size_t i = 0; i < count; ++i) {
        if (!g_openDevices[i]) {
            g_cachedValid[i] = false;
            continue;
        }
        
        g_openDevices[i]->Poll();
        HRESULT hr = g_openDevices[i]->GetDeviceState(sizeof(DIJOYSTATE2), &g_cachedStates[i]);
        if (FAILED(hr)) {
            g_openDevices[i]->Acquire();
            g_cachedValid[i] = false;
        } else {
            g_cachedValid[i] = true;
        }
    }
}

const DIJOYSTATE2* DeviceManager::GetCachedState(int deviceIndex) {
    if (deviceIndex < 0 || deviceIndex >= static_cast<int>(g_cachedStates.size())) return nullptr;
    if (!g_cachedValid[deviceIndex]) return nullptr;
    return &g_cachedStates[deviceIndex];
}

int DeviceManager::ResolveByName(const std::string& name) {
    return ResolveDevice("", name, -1);
}

void DeviceManager::LogDeviceManifest() {
    DevLog("=== Attached HID Devices ===");
    for (size_t i = 0; i < g_devices.size(); i++) {
        const auto& d = g_devices[i];
        char buf[256];
        sprintf_s(buf, "  [%d] %s '%s' (%s) — %d axes, %d buttons",
            (int)i, d.vidpidString.c_str(), d.instanceName.c_str(), d.productName.c_str(),
            d.axisCount, d.buttonCount);
        DevLog(buf);
    }
    DevLog("================================");
}

void DeviceManager::OpenAllDevices() {
    int count = GetDeviceCount();
    int opened = 0;
    for (int i = 0; i < count; i++) {
        if (OpenDevice(i)) {
            opened++;
        }
    }
    DevLog(std::format("OpenAllDevices: {}/{} devices opened for polling.", opened, count));
}

long DeviceManager::GetAxisFromState(const DIJOYSTATE2* st, int usageId) {
    if (!st) return 0;
    switch (usageId) {
        case 0x30: return st->lX;
        case 0x31: return st->lY;
        case 0x32: return st->lZ;
        case 0x33: return st->lRx;
        case 0x34: return st->lRy;
        case 0x35: return st->lRz;
        case 0x36: return st->rglSlider[0];
        case 0x37: return st->rglSlider[1];
        default:   return 0;
    }
}

static bool IsPovDirectionActive(const DIJOYSTATE2* state, int povIndex, int direction) {
    DWORD pov = state->rgdwPOV[povIndex];
    if (LOWORD(pov) == 0xFFFF) return false;
    static constexpr DWORD kDirAngles[4] = { 0, 9000, 18000, 27000 };
    DWORD target = kDirAngles[direction];
    DWORD diff = (pov > target) ? (pov - target) : (target - pov);
    if (diff > 18000) diff = 36000 - diff;
    return diff <= 4500;
}

bool DeviceManager::IsButtonPressed(const BindingRef& ref) {
    if (ref.value < 1) return false;
    const DIJOYSTATE2* st = GetCachedState(ref.deviceIndex);
    if (!st) return false;
    if (ref.value <= 128) return (st->rgbButtons[ref.value - 1] & 0x80) != 0;
    if (ref.value <= 144) {
        int povIndex  = (ref.value - 129) / 4;
        int direction = (ref.value - 129) % 4;
        return IsPovDirectionActive(st, povIndex, direction);
    }
    return false;
}

LPDIRECTINPUT8 DeviceManager::GetDirectInputContext() {
    return g_pDI;
}

