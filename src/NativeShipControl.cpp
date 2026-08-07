#include "PCH.h"

#include "NativeShipControl.h"
#include "RuntimePaths.h"

namespace {

using NativeShipControl::Action;
using ThrusterOutputFunction = void (*)(void*, const float*, float*);
using CameraRotationFunction = void (*)(void*, float*);
using ButtonBroadcaster = void (*)(void*, void*);

constexpr std::size_t kActionCount = static_cast<std::size_t>(Action::Count);
constexpr std::uintptr_t kFlightHandlerVtableRva = 0x4C41BA0;
constexpr std::uintptr_t kThrusterOutputSlotRva = 0x4C41BB0;
constexpr std::uintptr_t kThrusterOutputRva = 0x12BA5E0;
constexpr std::uintptr_t kHandlerUpdateRva = 0x12BABD0;
constexpr std::uintptr_t kActionHandlerRva = 0x12BA980;

constexpr std::array<std::string_view, kActionCount> kActionIds{
    "FireBoosters", "SwitchFlightModes", "TogglePov",
    "FireWeapon0", "FireWeapon1", "FireWeapon2", "ShipAction1",
    "SelectTarget", "IncreaseSystemPower", "DecreaseSystemPower",
    "PreviousSystem", "NextSystem", "OpenScanner", "Repair",
    "ShipAlternateControlHold", "Cruise", "Cancel", "UndockTakeOff",
    "GetUp", "ExitShipFromCockpit", "ZoomCameraIn", "ZoomCameraOut",
    "AutopilotOnOff",
};

struct SemanticAction {
    Action action;
    const char* eventName;
    std::uint32_t referenceId;
};

constexpr std::array kSemanticActions{
    SemanticAction{ Action::ShipAction1, "XButton", 82 },
    SemanticAction{ Action::IncreaseSystemPower, "Up", 38 },
    SemanticAction{ Action::DecreaseSystemPower, "Down", 40 },
    SemanticAction{ Action::PreviousSystem, "Left", 37 },
    SemanticAction{ Action::NextSystem, "Right", 39 },
    SemanticAction{ Action::OpenScanner, "SHMonocle", 70 },
    SemanticAction{ Action::ShipAlternateControlHold, "AltHold", 164 },
    SemanticAction{ Action::Cruise, "Cruise", 0 },
    SemanticAction{ Action::Cancel, "Cancel", 27 },
    SemanticAction{ Action::UndockTakeOff, "TakeOff", 0 },
    // The validated Get Up lifecycle is the SelectTarget semantic event.
    SemanticAction{ Action::GetUp, "SelectTarget", 69 },
    SemanticAction{ Action::ExitShipFromCockpit, "ExitShip", 0 },
    SemanticAction{ Action::AutopilotOnOff, "LockCourse", 32 },
};

std::atomic<bool> g_enabled{ false };
std::atomic<std::uintptr_t> g_cluster{ 0 };
std::atomic<std::uintptr_t> g_handler{ 0 };
std::atomic<std::uintptr_t> g_drainHandler{ 0 };
std::atomic<std::uint32_t> g_desiredMask{ 0 };
std::uint32_t g_controllerAppliedMask = 0;
std::uint32_t g_shipAppliedMask = 0;
std::array<std::vector<std::uint32_t>, kActionCount> g_actionOwners;
std::array<std::chrono::steady_clock::time_point, kActionCount> g_pressedAt{};

std::atomic<bool> g_splitActive{ false };
std::atomic<std::uint32_t> g_splitRollBits{ std::bit_cast<std::uint32_t>(0.0f) };
std::atomic<std::uint32_t> g_splitLateralBits{ std::bit_cast<std::uint32_t>(0.0f) };

std::array<std::atomic<std::uint32_t>, 4> g_headQuaternionBits{
    std::bit_cast<std::uint32_t>(1.0f), std::bit_cast<std::uint32_t>(0.0f),
    std::bit_cast<std::uint32_t>(0.0f), std::bit_cast<std::uint32_t>(0.0f)
};
std::atomic<bool> g_headPoseActive{ false };

ThrusterOutputFunction g_thrusterOutputOriginal = nullptr;
CameraRotationFunction g_cameraRotationOriginal = nullptr;
std::atomic<bool> g_thrusterHookInstalled{ false };
std::atomic<bool> g_cameraHookInstalled{ false };
std::atomic<std::int64_t> g_lastSelectedHandlerOutputMs{ 0 };

constexpr std::int64_t kHeadPosePilotFreshMilliseconds = 400;

std::int64_t SteadyNowMilliseconds()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::int64_t SelectedHandlerOutputAgeAt(std::int64_t nowMilliseconds)
{
    const auto observed = g_lastSelectedHandlerOutputMs.load(std::memory_order_acquire);
    if (observed <= 0 || nowMilliseconds < observed) return -1;
    return nowMilliseconds - observed;
}

bool SelectedHandlerOutputFreshAt(std::int64_t nowMilliseconds,
                                  std::int64_t maximumAgeMilliseconds)
{
    const auto age = SelectedHandlerOutputAgeAt(nowMilliseconds);
    return age >= 0 && age <= std::max<std::int64_t>(0, maximumAgeMilliseconds);
}

void NativeLog(std::string_view message)
{
    RuntimePaths::Log("[NativeShipControl]", std::string(message));
}

constexpr std::uint32_t ActionBit(Action action)
{
    const auto index = static_cast<std::uint32_t>(action);
    return index < static_cast<std::uint32_t>(Action::Count) ? (1u << index) : 0;
}

constexpr bool IsSemantic(Action action)
{
    for (const auto& entry : kSemanticActions)
        if (entry.action == action) return true;
    return false;
}

#pragma warning(push)
#pragma warning(disable: 4733)
template <class T>
bool SafeRead(std::uintptr_t address, T& value)
{
    if (!address) return false;
    __try {
        value = *reinterpret_cast<volatile T*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeCopy(void* destination, const void* source, std::size_t size)
{
    if (!destination || !source || size == 0) return false;
    __try {
        std::memcpy(destination, source, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <class T>
bool SafeWrite(std::uintptr_t address, const T& value)
{
    if (!address) return false;
    __try {
        *reinterpret_cast<volatile T*>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
#pragma warning(pop)

template <std::size_t Size>
bool BytesMatch(std::uintptr_t module, std::uintptr_t rva,
                const std::array<std::uint8_t, Size>& expected)
{
    std::array<std::uint8_t, Size> actual{};
    return module && SafeCopy(actual.data(), reinterpret_cast<const void*>(module + rva),
                              actual.size()) && actual == expected;
}

void HookedThrusterOutput(void* handler, const float* input, float* output);

bool ValidateSelectedHandler(std::uintptr_t cluster, std::uintptr_t& handler)
{
    handler = 0;
    std::uint32_t count = 0;
    std::uint32_t index = 0;
    std::uintptr_t array = 0;
    if (!cluster || !SafeRead(cluster + 0x80, count) || count == 0 || count >= 4096 ||
        !SafeRead(cluster + 0x94, index) || index >= count ||
        !SafeRead(cluster + 0x88, array) || !array ||
        !SafeRead(array + index * sizeof(void*), handler) || !handler) {
        return false;
    }

    const auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    std::uintptr_t vtable = 0;
    std::uintptr_t outputMethod = 0;
    std::uintptr_t updateMethod = 0;
    std::uintptr_t actionMethod = 0;
    const bool baseValid = module && SafeRead(handler, vtable) &&
        vtable == module + kFlightHandlerVtableRva &&
        SafeRead(vtable + 0x10, outputMethod);
    const bool outputValid = outputMethod == module + kThrusterOutputRva ||
        (g_thrusterHookInstalled.load(std::memory_order_acquire) &&
         outputMethod == reinterpret_cast<std::uintptr_t>(&HookedThrusterOutput) &&
         g_thrusterOutputOriginal ==
            reinterpret_cast<ThrusterOutputFunction>(module + kThrusterOutputRva));
    return baseValid && outputValid &&
        SafeRead(vtable + 0x18, updateMethod) && updateMethod == module + kHandlerUpdateRva &&
        SafeRead(vtable + 0x28, actionMethod) && actionMethod == module + kActionHandlerRva;
}

bool InstallPointerHook(std::uintptr_t* slot, std::uintptr_t expected,
                        std::uintptr_t detour, std::uintptr_t& original)
{
    std::uintptr_t current = 0;
    if (!SafeRead(reinterpret_cast<std::uintptr_t>(slot), current) || current != expected)
        return false;

    DWORD oldProtection = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtection))
        return false;
    original = current;
    InterlockedExchangePointer(reinterpret_cast<void* volatile*>(slot),
                               reinterpret_cast<void*>(detour));
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(*slot), oldProtection, &ignored);
    return true;
}

void RestorePointerHook(std::uintptr_t* slot, std::uintptr_t detour,
                        std::uintptr_t original)
{
    if (!slot || !original) return;
    std::uintptr_t current = 0;
    if (!SafeRead(reinterpret_cast<std::uintptr_t>(slot), current) || current != detour)
        return;
    DWORD oldProtection = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtection)) return;
    InterlockedExchangePointer(reinterpret_cast<void* volatile*>(slot),
                               reinterpret_cast<void*>(original));
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(*slot), oldProtection, &ignored);
}

bool DispatchSemanticButton(std::string_view actionName, std::uint32_t referenceId,
                            float value, float heldSeconds)
{
    if (actionName.empty() || actionName.size() >= 48) return false;
    const auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if (!module) return false;

    constexpr std::uintptr_t kBroadcasterRva = 0x2541B10;
    constexpr std::uintptr_t kInternStringRva = 0x28CBA80;
    constexpr std::uintptr_t kReleaseStringRva = 0x28CAEA0;
    constexpr std::array<std::uint8_t, 7> kBroadcasterBytes{
        0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10
    };
    constexpr std::array<std::uint8_t, 5> kInternBytes{
        0x44, 0x88, 0x44, 0x24, 0x18
    };
    constexpr std::array<std::uint8_t, 5> kReleaseBytes{
        0x41, 0x54, 0x48, 0x83, 0xEC
    };
    if (!BytesMatch(module, kBroadcasterRva, kBroadcasterBytes) ||
        !BytesMatch(module, kInternStringRva, kInternBytes) ||
        !BytesMatch(module, kReleaseStringRva, kReleaseBytes)) {
        return false;
    }

    constexpr std::uintptr_t kInputManagerSingletonRva = 0x5FD9B80;
    constexpr std::uintptr_t kButtonEventSourceVtableRva = 0x4D7E408;
    std::uintptr_t manager = 0;
    std::uintptr_t sourceVtable = 0;
    if (!SafeRead(module + kInputManagerSingletonRva, manager) || !manager ||
        !SafeRead(manager + 0x10, sourceVtable) ||
        sourceVtable != module + kButtonEventSourceVtableRva) {
        return false;
    }

    std::string stableName(actionName);
    std::uintptr_t ownedAction = 0;
    using InternString = void (*)(std::uintptr_t*, const char*, bool);
    using ReleaseString = void (*)(std::uintptr_t*);
    reinterpret_cast<InternString>(module + kInternStringRva)(
        &ownedAction, stableName.c_str(), true);
    if (!ownedAction) return false;

    const auto release = [&] {
        if (ownedAction)
            reinterpret_cast<ReleaseString>(module + kReleaseStringRva)(&ownedAction);
    };

    // BSStringPool entries may be indirections. Require the leaf text to match
    // the operation we are about to publish before it reaches any listener.
    std::uintptr_t leaf = ownedAction;
    bool textMatches = false;
    for (std::size_t depth = 0; leaf && depth < 4; ++depth) {
        std::uint8_t flags = 0;
        if (!SafeRead(leaf + 0x14, flags)) break;
        if ((flags & 0x02) == 0) {
            std::array<char, 48> text{};
            textMatches = SafeCopy(text.data(), reinterpret_cast<const void*>(leaf + 0x18),
                                   actionName.size() + 1) &&
                std::string_view(text.data(), actionName.size()) == actionName &&
                text[actionName.size()] == '\0';
            break;
        }
        if (!SafeRead(leaf + 0x08, leaf)) break;
    }
    if (!textMatches) {
        release();
        return false;
    }

    alignas(16) std::array<std::uint8_t, 96> event{};
    const std::uintptr_t primaryVtable = module + 0x4D59F50;
    const std::uintptr_t idVtable = module + 0x4D59F28;
    const std::uintptr_t userVtable = module + 0x4D59F00;
    const std::uint32_t keyboardDevice = 0;
    const std::uint32_t buttonEvent = 0;
    std::memcpy(event.data() + 0x00, &primaryVtable, sizeof(primaryVtable));
    std::memcpy(event.data() + 0x08, &keyboardDevice, sizeof(keyboardDevice));
    std::memcpy(event.data() + 0x10, &buttonEvent, sizeof(buttonEvent));
    std::memcpy(event.data() + 0x28, &ownedAction, sizeof(ownedAction));
    std::memcpy(event.data() + 0x30, &referenceId, sizeof(referenceId));
    std::memcpy(event.data() + 0x38, &idVtable, sizeof(idVtable));
    std::memcpy(event.data() + 0x40, &userVtable, sizeof(userVtable));
    std::memcpy(event.data() + 0x48, &value, sizeof(value));
    std::memcpy(event.data() + 0x4C, &heldSeconds, sizeof(heldSeconds));
    reinterpret_cast<ButtonBroadcaster>(module + kBroadcasterRva)(
        reinterpret_cast<void*>(manager + 0x10), event.data());
    release();
    return true;
}

bool DispatchFlightMode(void* handler, const float* input, float value,
                        float heldSeconds)
{
    if (!handler || !input) return false;
    const auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    constexpr std::uintptr_t kActionGetterRva = 0x12B94C0;
    constexpr std::array<std::uint8_t, 10> kActionMethodBytes{
        0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18
    };
    constexpr std::array<std::uint8_t, 6> kActionGetterBytes{
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20
    };
    std::uintptr_t vtable = 0;
    std::uintptr_t actionMethod = 0;
    if (!module || !SafeRead(reinterpret_cast<std::uintptr_t>(handler), vtable) ||
        vtable != module + kFlightHandlerVtableRva ||
        !SafeRead(vtable + 0x28, actionMethod) ||
        actionMethod != module + kActionHandlerRva ||
        !BytesMatch(module, kActionHandlerRva, kActionMethodBytes) ||
        !BytesMatch(module, kActionGetterRva, kActionGetterBytes)) {
        return false;
    }
    using ActionGetter = const std::uintptr_t* (*)();
    const auto action = reinterpret_cast<ActionGetter>(module + kActionGetterRva)();
    if (!action || !*action) return false;

    alignas(16) std::array<std::uint8_t, 96> event{};
    const std::uintptr_t primaryVtable = module + 0x4D59F50;
    const std::uintptr_t idVtable = module + 0x4D59F28;
    const std::uintptr_t userVtable = module + 0x4D59F00;
    const std::uint32_t keyboardDevice = 0;
    const std::uint32_t buttonEvent = 0;
    const std::uint32_t referenceId = 0x20;
    std::memcpy(event.data() + 0x00, &primaryVtable, sizeof(primaryVtable));
    std::memcpy(event.data() + 0x08, &keyboardDevice, sizeof(keyboardDevice));
    std::memcpy(event.data() + 0x10, &buttonEvent, sizeof(buttonEvent));
    std::memcpy(event.data() + 0x28, action, sizeof(*action));
    std::memcpy(event.data() + 0x30, &referenceId, sizeof(referenceId));
    std::memcpy(event.data() + 0x38, &idVtable, sizeof(idVtable));
    std::memcpy(event.data() + 0x40, &userVtable, sizeof(userVtable));
    std::memcpy(event.data() + 0x48, &value, sizeof(value));
    std::memcpy(event.data() + 0x4C, &heldSeconds, sizeof(heldSeconds));
    using DigitalActionFunction = void (*)(void*, const float*, void*);
    reinterpret_cast<DigitalActionFunction>(actionMethod)(handler, input, event.data());
    return true;
}

bool DispatchWeaponGroup(std::uint32_t groupIndex, bool stop)
{
    const auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if (!module || groupIndex >= 3) return false;
    constexpr std::uintptr_t kContextGetterRva = 0x17D9F70;
    constexpr std::uintptr_t kLeafStartRva = 0x214F910;
    constexpr std::uintptr_t kLeafStopRva = 0x214FAC0;
    constexpr std::array<std::uint8_t, 7> kContextGate{
        0x48, 0x8B, 0x0D, 0xB3, 0x92, 0x76, 0x04
    };
    constexpr std::array<std::uint8_t, 8> kStartGate{
        0xC5, 0xFA, 0x10, 0x05, 0x1A, 0x1E, 0xD2, 0x03
    };
    constexpr std::array<std::uint8_t, 6> kStopGate{
        0xF6, 0x41, 0x18, 0x04, 0x74, 0x4E
    };
    if (!BytesMatch(module, 0x17D9F76, kContextGate) ||
        !(stop ? BytesMatch(module, 0x214FAD5, kStopGate)
               : BytesMatch(module, 0x214F922, kStartGate))) {
        return false;
    }

    using ContextGetter = void* (*)();
    auto* context = reinterpret_cast<ContextGetter>(module + kContextGetterRva)();
    std::uintptr_t contextVtable = 0;
    if (!context || !SafeRead(reinterpret_cast<std::uintptr_t>(context), contextVtable) ||
        contextVtable != module + 0x4B95FB8) return false;

    std::int32_t storageState = -1;
    std::uintptr_t descriptor = module + 0x591A190;
    if (!SafeRead(module + 0x591A188, storageState)) return false;
    if (storageState >= 0 && (!SafeRead(module + 0x591A190, descriptor) || !descriptor))
        return false;
    descriptor += groupIndex * 24;

    std::uint32_t weaponCount = 0;
    std::uintptr_t weaponArray = 0;
    if (!SafeRead(descriptor, weaponCount) || weaponCount == 0 || weaponCount > 16 ||
        !SafeRead(descriptor + 0x08, weaponArray) || !weaponArray) return false;

    std::array<std::uintptr_t, 16> weapons{};
    for (std::uint32_t index = 0; index < weaponCount; ++index) {
        std::uintptr_t vtable = 0;
        if (!SafeRead(weaponArray + index * sizeof(void*), weapons[index]) ||
            !weapons[index] || !SafeRead(weapons[index], vtable) ||
            vtable != module + 0x4D466F8) return false;
    }
    using LeafStart = void (*)(void*, void*);
    using LeafStop = void (*)(void*, void*, bool);
    for (std::uint32_t index = 0; index < weaponCount; ++index) {
        if (stop)
            reinterpret_cast<LeafStop>(module + kLeafStopRva)(
                reinterpret_cast<void*>(weapons[index]), context, false);
        else
            reinterpret_cast<LeafStart>(module + kLeafStartRva)(
                reinterpret_cast<void*>(weapons[index]), context);
    }
    return true;
}

bool DispatchNoArgument(std::uintptr_t rva, std::span<const std::uint8_t> expected)
{
    const auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if (!module || expected.empty()) return false;
    std::array<std::uint8_t, 16> actual{};
    if (expected.size() > actual.size() ||
        !SafeCopy(actual.data(), reinterpret_cast<const void*>(module + rva), expected.size()) ||
        !std::equal(expected.begin(), expected.end(), actual.begin())) return false;
    reinterpret_cast<void (*)()>(module + rva)();
    return true;
}

bool DispatchCameraAction(std::uint16_t actionCode)
{
    const auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if (!module || actionCode < 1 || actionCode > 3) return false;
    constexpr std::array<std::uintptr_t, 3> kActionGetterRvas{
        0xF9B6E0, 0x4B25F0, 0x4B2680
    };
    constexpr std::array<std::uint8_t, 10> kActionGetterBytes{
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0xBA, 0xB8, 0x00, 0x00
    };
    const auto getterRva = kActionGetterRvas[actionCode - 1];
    if (actionCode != 1 && !BytesMatch(module, getterRva, kActionGetterBytes))
        return false;

    std::uintptr_t playerCamera = 0;
    std::uintptr_t state = 0;
    std::uintptr_t vtable = 0;
    std::uintptr_t handler = 0;
    if (!SafeRead(module + 0x61DD460, playerCamera) || !playerCamera ||
        !SafeRead(playerCamera + 0x10, state) || !state ||
        !SafeRead(state, vtable) || !SafeRead(vtable + 0x40, handler)) return false;
    const bool firstPerson = vtable == module + 0x4D04360 &&
                             handler == module + 0x1E85AB0;
    const bool flightCamera = vtable == module + 0x4D053C0 &&
                              handler == module + 0xFA4030;
    if (!firstPerson && !flightCamera) return false;

    if (actionCode == 1) {
        constexpr std::array<std::uint8_t, 6> kEnterBytes{
            0x48, 0x89, 0x5C, 0x24, 0x08, 0x48
        };
        constexpr std::array<std::uint8_t, 6> kPresetBytes{
            0x40, 0x55, 0x53, 0x56, 0x57, 0x41
        };
        constexpr std::array<std::uint8_t, 6> kExitBytes{
            0x40, 0x53, 0x48, 0x83, 0xEC, 0x30
        };
        if (firstPerson) {
            if (!BytesMatch(module, 0x1E89940, kEnterBytes)) return false;
            reinterpret_cast<void (*)(void*, std::uint32_t)>(module + 0x1E89940)(
                reinterpret_cast<void*>(playerCamera), 8);
            return true;
        }
        std::uint8_t preset = 0;
        if (!SafeRead(state + 0x178, preset) || preset > 1) return false;
        if (preset == 1) {
            if (!BytesMatch(module, 0xFA4470, kExitBytes)) return false;
            auto stateArgument = state;
            reinterpret_cast<void (*)(void*)>(module + 0xFA4470)(&stateArgument);
            return true;
        }
        if (!BytesMatch(module, 0xFA37D0, kPresetBytes)) return false;
        const std::uint8_t nextPreset = 1;
        if (!SafeWrite(state + 0x178, nextPreset)) return false;
        const bool applied = reinterpret_cast<bool (*)(void*)>(module + 0xFA37D0)(
            reinterpret_cast<void*>(state));
        if (!applied) {
            SafeWrite(state + 0x178, preset);
            return false;
        }
        const std::uint32_t persistedPreset = nextPreset;
        SafeWrite(module + 0x5916680, persistedPreset);
        return true;
    }

    using ActionGetter = const std::uintptr_t* (*)();
    const auto action = reinterpret_cast<ActionGetter>(module + getterRva)();
    if (!action || !*action) return false;
    alignas(16) std::array<std::uint8_t, 96> event{};
    const std::uintptr_t primaryVtable = module + 0x4D59F50;
    const std::uintptr_t idVtable = module + 0x4D59F28;
    const std::uintptr_t userVtable = module + 0x4D59F00;
    const float released = 0.0F;
    const float heldSeconds = 0.05F;
    std::memcpy(event.data() + 0x00, &primaryVtable, sizeof(primaryVtable));
    std::memcpy(event.data() + 0x28, action, sizeof(*action));
    std::memcpy(event.data() + 0x38, &idVtable, sizeof(idVtable));
    std::memcpy(event.data() + 0x40, &userVtable, sizeof(userVtable));
    std::memcpy(event.data() + 0x48, &released, sizeof(released));
    std::memcpy(event.data() + 0x4C, &heldSeconds, sizeof(heldSeconds));
    reinterpret_cast<ButtonBroadcaster>(handler)(reinterpret_cast<void*>(state), event.data());
    return true;
}

bool DispatchShipPress(Action action, void* handler, const float* input)
{
    switch (action) {
    case Action::FireBoosters:
        return SafeWrite(reinterpret_cast<std::uintptr_t>(handler) + 0x15,
                         std::uint8_t{ 1 });
    case Action::SwitchFlightModes:
        return DispatchFlightMode(handler, input, 1.0F, 0.0F);
    case Action::TogglePov:
        return DispatchCameraAction(1);
    case Action::FireWeapon0:
    case Action::FireWeapon1:
    case Action::FireWeapon2:
        return DispatchWeaponGroup(static_cast<std::uint32_t>(action) -
                                   static_cast<std::uint32_t>(Action::FireWeapon0), false);
    case Action::SelectTarget: {
        constexpr std::array<std::uint8_t, 4> expected{ 0x48, 0x83, 0xEC, 0x58 };
        return DispatchNoArgument(0x1564CC0, expected);
    }
    case Action::Repair: {
        constexpr std::array<std::uint8_t, 5> expected{ 0x48, 0x89, 0x4C, 0x24, 0x08 };
        return DispatchNoArgument(0x15609C0, expected);
    }
    case Action::ZoomCameraIn:
        return DispatchCameraAction(2);
    case Action::ZoomCameraOut:
        return DispatchCameraAction(3);
    default:
        return false;
    }
}

bool DispatchShipRelease(Action action, void* handler, const float* input,
                         float heldSeconds)
{
    switch (action) {
    case Action::SwitchFlightModes:
        return DispatchFlightMode(handler, input, 0.0F, heldSeconds);
    case Action::FireWeapon0:
    case Action::FireWeapon1:
    case Action::FireWeapon2:
        return DispatchWeaponGroup(static_cast<std::uint32_t>(action) -
                                   static_cast<std::uint32_t>(Action::FireWeapon0), true);
    default:
        return true;
    }
}

void ProcessShipActions(void* handler, const float* input, bool acceptsPresses)
{
    const auto desired = acceptsPresses && g_enabled.load(std::memory_order_acquire)
        ? g_desiredMask.load(std::memory_order_acquire)
        : 0;
    const auto now = std::chrono::steady_clock::now();
    for (std::uint32_t raw = 0; raw < static_cast<std::uint32_t>(Action::Count); ++raw) {
        const auto action = static_cast<Action>(raw);
        if (IsSemantic(action)) continue;
        const auto bit = ActionBit(action);
        const bool wantsHeld = (desired & bit) != 0;
        const bool wasApplied = (g_shipAppliedMask & bit) != 0;

        if (wantsHeld) {
            // Weapons and the native flight-mode modifier are level-driven on
            // the selected-handler update and intentionally repeat while held.
            const bool levelDriven = action == Action::SwitchFlightModes ||
                action == Action::FireWeapon0 || action == Action::FireWeapon1 ||
                action == Action::FireWeapon2;
            if ((!wasApplied || levelDriven) && DispatchShipPress(action, handler, input)) {
                if (!wasApplied) g_pressedAt[raw] = now;
                g_shipAppliedMask |= bit;
            }
        } else if (wasApplied) {
            const float held = std::chrono::duration<float>(now - g_pressedAt[raw]).count();
            if (DispatchShipRelease(action, handler, input, std::max(0.0F, held)))
                g_shipAppliedMask &= ~bit;
        }
    }
}

void HookedThrusterOutput(void* handler, const float* input, float* output)
{
    const auto original = g_thrusterOutputOriginal;
    if (!original) return;
    original(handler, input, output);
    if (!handler || !output) return;
    const auto address = reinterpret_cast<std::uintptr_t>(handler);
    const auto activeHandler = g_handler.load(std::memory_order_acquire);
    const auto drainHandler = g_drainHandler.load(std::memory_order_acquire);
    if (address != activeHandler && address != drainHandler) return;

    if (address == activeHandler)
        g_lastSelectedHandlerOutputMs.store(
            SteadyNowMilliseconds(), std::memory_order_release);

    ProcessShipActions(handler, input, address == activeHandler);
    if (address == drainHandler && g_shipAppliedMask == 0) {
        auto expected = drainHandler;
        g_drainHandler.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
    }
    if (address != activeHandler) return;
    if (g_enabled.load(std::memory_order_acquire) &&
        g_splitActive.load(std::memory_order_acquire)) {
        output[0] = std::bit_cast<float>(
            g_splitLateralBits.load(std::memory_order_relaxed));
        output[5] = std::bit_cast<float>(
            g_splitRollBits.load(std::memory_order_relaxed));
    }
}

void HookedCameraRotation(void* state, float* quaternion)
{
    const auto original = g_cameraRotationOriginal;
    if (!original) return;
    original(state, quaternion);
    if (!state || !quaternion || !g_enabled.load(std::memory_order_acquire) ||
        !g_headPoseActive.load(std::memory_order_acquire) ||
        !g_handler.load(std::memory_order_acquire) ||
        !SelectedHandlerOutputFreshAt(
            SteadyNowMilliseconds(), kHeadPosePilotFreshMilliseconds)) return;

    const auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    std::uintptr_t playerCamera = 0;
    std::uintptr_t currentState = 0;
    if (!module || !SafeRead(module + 0x61DD460, playerCamera) || !playerCamera ||
        !SafeRead(playerCamera + 0x10, currentState) ||
        currentState != reinterpret_cast<std::uintptr_t>(state)) return;

    std::array<float, 4> native{};
    std::array<float, 4> head{};
    if (!SafeCopy(native.data(), quaternion, sizeof(native))) return;
    for (std::size_t index = 0; index < head.size(); ++index)
        head[index] = std::bit_cast<float>(g_headQuaternionBits[index].load(
            std::memory_order_relaxed));

    // NiQuaternion is (w,x,y,z). Match the accepted waveform seam's
    // post-composition order: native camera rotation * head rotation.
    const std::array<float, 4> result{
        native[0] * head[0] - native[1] * head[1] - native[2] * head[2] - native[3] * head[3],
        native[0] * head[1] + native[1] * head[0] - native[2] * head[3] + native[3] * head[2],
        native[0] * head[2] + native[1] * head[3] + native[2] * head[0] - native[3] * head[1],
        native[0] * head[3] - native[1] * head[2] + native[2] * head[1] + native[3] * head[0],
    };
    SafeCopy(quaternion, result.data(), sizeof(result));
}

bool InstallHooks()
{
    const auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if (!module) return false;

    // Publish each original before replacing its slot. Another game thread may
    // enter the detour immediately after the interlocked exchange.
    std::uintptr_t thrusterOriginal = module + kThrusterOutputRva;
    g_thrusterOutputOriginal = reinterpret_cast<ThrusterOutputFunction>(thrusterOriginal);
    auto* thrusterSlot = reinterpret_cast<std::uintptr_t*>(module + kThrusterOutputSlotRva);
    const bool thrusterOk = InstallPointerHook(
        thrusterSlot, module + kThrusterOutputRva,
        reinterpret_cast<std::uintptr_t>(&HookedThrusterOutput), thrusterOriginal);
    if (thrusterOk) {
        g_thrusterOutputOriginal = reinterpret_cast<ThrusterOutputFunction>(thrusterOriginal);
        g_thrusterHookInstalled.store(true, std::memory_order_release);
        NativeLog("Validated selected-handler output hook installed.");
    } else {
        g_thrusterOutputOriginal = nullptr;
        NativeLog("Native ship controls unavailable: selected-handler vtable gate failed.");
    }

    constexpr std::size_t kRotationSlot = 13;
    auto* cameraSlot = reinterpret_cast<std::uintptr_t*>(
        module + 0x4D04360 + kRotationSlot * sizeof(std::uintptr_t));
    std::uintptr_t cameraOriginal = module + 0x1E84A20;
    g_cameraRotationOriginal = reinterpret_cast<CameraRotationFunction>(cameraOriginal);
    const bool cameraOk = InstallPointerHook(
        cameraSlot, module + 0x1E84A20,
        reinterpret_cast<std::uintptr_t>(&HookedCameraRotation), cameraOriginal);
    if (cameraOk) {
        g_cameraRotationOriginal = reinterpret_cast<CameraRotationFunction>(cameraOriginal);
        g_cameraHookInstalled.store(true, std::memory_order_release);
        NativeLog("Validated FirstPersonState head-rotation hook installed.");
    } else {
        g_cameraRotationOriginal = nullptr;
        NativeLog("Head tracking unavailable: FirstPersonState vtable gate failed.");
    }
    return thrusterOk || cameraOk;
}

} // namespace

namespace NativeShipControl {

bool Initialize()
{
    return InstallHooks();
}

void Shutdown()
{
    SetEnabled(false);
    g_lastSelectedHandlerOutputMs.store(0, std::memory_order_release);
    const auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if (!module) return;
    if (g_thrusterHookInstalled.exchange(false, std::memory_order_acq_rel)) {
        RestorePointerHook(reinterpret_cast<std::uintptr_t*>(module + kThrusterOutputSlotRva),
            reinterpret_cast<std::uintptr_t>(&HookedThrusterOutput),
            reinterpret_cast<std::uintptr_t>(g_thrusterOutputOriginal));
    }
    if (g_cameraHookInstalled.exchange(false, std::memory_order_acq_rel)) {
        RestorePointerHook(reinterpret_cast<std::uintptr_t*>(module + 0x4D04360 + 13 * 8),
            reinterpret_cast<std::uintptr_t>(&HookedCameraRotation),
            reinterpret_cast<std::uintptr_t>(g_cameraRotationOriginal));
    }
}

void SetEnabled(bool enabled)
{
    if (!enabled) {
        ReleaseAll();
        SetSplitFlightAxes(0.0F, 0.0F, false);
        ClearHeadPose();
    }
    g_enabled.store(enabled, std::memory_order_release);
    if (!enabled) PumpControllerThread();
}

bool Enabled()
{
    return g_enabled.load(std::memory_order_acquire);
}

void UpdateCluster(std::uintptr_t cluster)
{
    g_cluster.store(cluster, std::memory_order_release);
    std::uintptr_t handler = 0;
    const bool valid = cluster && g_thrusterHookInstalled.load(std::memory_order_acquire) &&
                       ValidateSelectedHandler(cluster, handler);
    const auto next = valid ? handler : 0;
    const auto previous = g_handler.exchange(next, std::memory_order_acq_rel);
    if (previous != next)
        g_lastSelectedHandlerOutputMs.store(0, std::memory_order_release);
    if (previous && previous != handler)
        g_drainHandler.store(previous, std::memory_order_release);
    if (valid && previous != handler)
        NativeLog("Selected flight handler validated; native ship operations are ready.");
    else if (!valid && previous)
        NativeLog("Selected flight handler lost validation; native ship operations suspended.");
}

bool ShipHandlerReady()
{
    return g_handler.load(std::memory_order_acquire) != 0;
}

std::int64_t SelectedHandlerOutputAgeMilliseconds()
{
    return SelectedHandlerOutputAgeAt(SteadyNowMilliseconds());
}

bool SelectedHandlerOutputFresh(std::int64_t maximumAgeMilliseconds)
{
    return SelectedHandlerOutputFreshAt(
        SteadyNowMilliseconds(), maximumAgeMilliseconds);
}

Action ActionFromId(std::string_view actionId)
{
    for (std::size_t index = 0; index < kActionIds.size(); ++index)
        if (kActionIds[index] == actionId) return static_cast<Action>(index);
    return Action::Invalid;
}

std::string_view ActionId(Action action)
{
    const auto index = static_cast<std::size_t>(action);
    return index < kActionIds.size() ? kActionIds[index] : std::string_view{};
}

void SetActionHeld(Action action, std::uint32_t ownerId, bool held)
{
    const auto index = static_cast<std::size_t>(action);
    if (index >= kActionCount || (!Enabled() && held)) return;
    auto& owners = g_actionOwners[index];
    const auto existing = std::find(owners.begin(), owners.end(), ownerId);
    if (held) {
        if (existing == owners.end()) owners.push_back(ownerId);
    } else if (existing != owners.end()) {
        owners.erase(existing);
    }
    const auto bit = ActionBit(action);
    if (owners.empty())
        g_desiredMask.fetch_and(~bit, std::memory_order_acq_rel);
    else
        g_desiredMask.fetch_or(bit, std::memory_order_acq_rel);
}

void ReleaseOwner(std::uint32_t ownerId)
{
    for (std::size_t index = 0; index < g_actionOwners.size(); ++index) {
        auto& owners = g_actionOwners[index];
        std::erase(owners, ownerId);
        if (owners.empty())
            g_desiredMask.fetch_and(~ActionBit(static_cast<Action>(index)),
                                    std::memory_order_acq_rel);
    }
}

void ReleaseAll()
{
    for (auto& owners : g_actionOwners) owners.clear();
    g_desiredMask.store(0, std::memory_order_release);
}

bool IsActionHeld(Action action)
{
    const auto bit = ActionBit(action);
    return bit && (g_desiredMask.load(std::memory_order_acquire) & bit) != 0;
}

void PumpControllerThread()
{
    // Context-keyed actions must not leak into on-foot or menu input. Losing the
    // selected ship handler still drains any already-published release edges.
    const auto desired = Enabled() && ShipHandlerReady()
        ? g_desiredMask.load(std::memory_order_acquire)
        : 0;
    const auto now = std::chrono::steady_clock::now();
    for (const auto& entry : kSemanticActions) {
        const auto raw = static_cast<std::size_t>(entry.action);
        const auto bit = ActionBit(entry.action);
        const bool wantsHeld = (desired & bit) != 0;
        const bool wasApplied = (g_controllerAppliedMask & bit) != 0;
        if (wantsHeld == wasApplied) continue;
        const float heldSeconds = wasApplied
            ? std::max(0.0F, std::chrono::duration<float>(now - g_pressedAt[raw]).count())
            : 0.0F;
        if (DispatchSemanticButton(entry.eventName, entry.referenceId,
                                   wantsHeld ? 1.0F : 0.0F, heldSeconds)) {
            if (wantsHeld) {
                g_controllerAppliedMask |= bit;
                g_pressedAt[raw] = now;
            } else {
                g_controllerAppliedMask &= ~bit;
            }
        }
    }
}

void SetSplitFlightAxes(float roll, float lateral, bool active)
{
    g_splitRollBits.store(std::bit_cast<std::uint32_t>(roll), std::memory_order_relaxed);
    g_splitLateralBits.store(std::bit_cast<std::uint32_t>(lateral),
                             std::memory_order_relaxed);
    g_splitActive.store(active, std::memory_order_release);
}

void SetHeadQuaternion(float w, float x, float y, float z, bool active)
{
    const float norm = std::sqrt(w * w + x * x + y * y + z * z);
    if (!active || !std::isfinite(norm) || norm < 1e-6F) {
        ClearHeadPose();
        return;
    }
    const std::array<float, 4> normalized{ w / norm, x / norm, y / norm, z / norm };
    for (std::size_t index = 0; index < normalized.size(); ++index)
        g_headQuaternionBits[index].store(std::bit_cast<std::uint32_t>(normalized[index]),
                                          std::memory_order_relaxed);
    g_headPoseActive.store(true, std::memory_order_release);
}

void ClearHeadPose()
{
    g_headPoseActive.store(false, std::memory_order_release);
    const std::array<float, 4> identity{ 1.0F, 0.0F, 0.0F, 0.0F };
    for (std::size_t index = 0; index < identity.size(); ++index)
        g_headQuaternionBits[index].store(std::bit_cast<std::uint32_t>(identity[index]),
                                          std::memory_order_relaxed);
}

} // namespace NativeShipControl
