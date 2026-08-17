#include "PCH.h"

#include "AbsoluteControlSubscriber.h"
#include "ThrottleHook.h"
#include "ThrottleController.h"
#include "RuntimePaths.h"
#include "SettingBeacon.h"
#include "UIHook.h"
#include "BindingWizard.h"
#include "HeadTracking.h"
#include "NativeShipControl.h"
#include "SFSEInterface.h"
#include <windows.h>
#include <format>
#include <filesystem>

static bool AbsoluteZeroPresent() {
    std::error_code error;
    return GetModuleHandleW(L"AbsoluteZero.dll") != nullptr ||
           std::filesystem::exists(
               RuntimePaths::PluginDirectory() / L"AbsoluteZero.dll", error);
}

static bool AbsoluteHeadTrackingPresent() {
    std::error_code error;
    return GetModuleHandleW(L"AbsoluteHeadTracking.dll") != nullptr ||
           std::filesystem::exists(
               RuntimePaths::PluginDirectory() / L"AbsoluteHeadTracking.dll", error);
}

static void MainLog(const std::string& msg) {
    RuntimePaths::Log("[Main]", msg);
}

static void OnSfseMessage(SFSE::MessagingInterface::Message* message) {
    if (!message || AbsoluteControlSubscriber::IsHosted() ||
        (message->type != SFSE::MessagingInterface::kPostDataLoad &&
         message->type != SFSE::MessagingInterface::kPostPostDataLoad)) {
        return;
    }

    const auto result = AbsoluteControlSubscriber::RegisterDiscoveredHost();
    using AbsoluteControlPanelApi::Result;
    if (result == Result::Ok || result == Result::Duplicate) {
        MainLog("Registered read-only Setup Overview and Plugin & Compatibility pages with Absolute Control.");
    } else if (result == Result::NotReady &&
               message->type == SFSE::MessagingInterface::kPostDataLoad) {
        MainLog("Absolute Control is not ready; registration will retry at post-post-data-load.");
    } else if (result == Result::NotFound) {
        if (message->type == SFSE::MessagingInterface::kPostPostDataLoad) {
            MainLog("Absolute Control is not installed; standalone HOTAS operation continues.");
        }
    } else {
        MainLog(std::format(
            "WARNING: Absolute Control registration was rejected (result {}); standalone HOTAS operation continues.",
            static_cast<std::uint32_t>(result)));
    }
}

static bool RegisterSfseListener(const SFSE::LoadInterface* sfse) noexcept {
    if (!sfse) return false;
    const auto* messaging = sfse->GetMessagingInterface();
    return messaging && messaging->Version() >= 1 &&
           messaging->RegisterListener(sfse->GetPluginHandle(), &OnSfseMessage);
}

static LONG NTAPI CrashLogHandler(EXCEPTION_POINTERS* a_exceptionInfo) {
    if (!a_exceptionInfo || !a_exceptionInfo->ExceptionRecord || !a_exceptionInfo->ContextRecord) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    const auto* const record = a_exceptionInfo->ExceptionRecord;
    // Skip non-crash exception codes:
    // 0x40010006 = DBG_PRINTEXCEPTION_C (OutputDebugString)
    // 0x406D1388 = MS_VC_EXCEPTION (SetThreadName)
    // 0xE06D7363 = C++ throw — handled by SEH __try/__except in our render path
    if (record->ExceptionCode == 0x40010006 ||
        record->ExceptionCode == 0x406D1388 ||
        record->ExceptionCode == 0xE06D7363) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    const auto* const context = a_exceptionInfo->ContextRecord;
    MEMORY_BASIC_INFORMATION mbi{};
    char modulePath[MAX_PATH]{};
    const auto queryOk = VirtualQuery(record->ExceptionAddress, &mbi, sizeof(mbi)) != 0;
    if (queryOk) {
        GetModuleFileNameA(static_cast<HMODULE>(mbi.AllocationBase), modulePath, static_cast<DWORD>(std::size(modulePath)));
    }

    RuntimePaths::Log("[Crash]",
        std::format("Exception code=0x{:08X} address=0x{:016X}",
            static_cast<std::uint32_t>(record->ExceptionCode),
            reinterpret_cast<std::uintptr_t>(record->ExceptionAddress)));
    if (queryOk) {
        RuntimePaths::Log("[Crash]",
            std::format("Module base=0x{:016X} rva=0x{:X} path={}",
                reinterpret_cast<std::uintptr_t>(mbi.AllocationBase),
                reinterpret_cast<std::uintptr_t>(record->ExceptionAddress) - reinterpret_cast<std::uintptr_t>(mbi.AllocationBase),
                modulePath[0] ? modulePath : "<unknown>"));
    }
    RuntimePaths::Log("[Crash]",
        std::format("RIP=0x{:016X} RSP=0x{:016X} RCX=0x{:016X} RDX=0x{:016X} R8=0x{:016X} R9=0x{:016X}",
            static_cast<std::uintptr_t>(context->Rip),
            static_cast<std::uintptr_t>(context->Rsp),
            static_cast<std::uintptr_t>(context->Rcx),
            static_cast<std::uintptr_t>(context->Rdx),
            static_cast<std::uintptr_t>(context->R8),
            static_cast<std::uintptr_t>(context->R9)));
    if (record->NumberParameters > 1) {
        RuntimePaths::Log("[Crash]",
            std::format("Exception info[0]=0x{:016X} info[1]=0x{:016X}",
                static_cast<std::uintptr_t>(record->ExceptionInformation[0]),
                static_cast<std::uintptr_t>(record->ExceptionInformation[1])));
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void InstallCrashLogger() {
    static void* handler = nullptr;
    if (!handler) {
        handler = AddVectoredExceptionHandler(1, CrashLogHandler);
        MainLog(handler ? "Crash logger installed." : "WARNING: Crash logger installation failed.");
    }
}

// SFSE plugin entry point
SFSE_PLUGIN_LOAD(const SFSE::LoadInterface* a_sfse)
{
    // Read bEnableLog from the INI once and cache it. With logging off the plugin
    // writes nothing, so there is no crash handler to install either.
    RuntimePaths::InitLogging();
    MainLog(std::format("=== {} {} startup ===", Plugin::Name, Plugin::VersionString));
    if (RuntimePaths::IsLoggingEnabled()) {
        InstallCrashLogger();
    }

    // Plant the discovery beacon and zero game deadzones unless Signal Hunter fallback is enabled
    bool fallbackMode = GetPrivateProfileIntA(
        "Injection", "bSignalHunterFallback", 0,
        RuntimePaths::IniPath().string().c_str()) != 0;
    if (!fallbackMode) {
        if (!SettingBeacon::PlantBeacon()) {
            RuntimePaths::Log("[Main]",
                "Beacon failed. Signal Hunter will rely on StarfieldCustom.ini fallback.");
        }
    } else {
        RuntimePaths::Log("[Main]",
            "Signal Hunter fallback enabled. Using StarfieldCustom.ini discovery.");
    }

    // Phase 1: AOB scan + trampoline hook to capture ThrottleInterface pointer
    const bool absoluteZeroPresent = AbsoluteZeroPresent();
    ThrottleHook::SetExternalMouseSteeringOwner(absoluteZeroPresent);
    AbsoluteControlSubscriber::SetExternalMouseSteeringOwner(absoluteZeroPresent);
    if (absoluteZeroPresent) {
        MainLog("AbsoluteZero detected: it owns mouse pitch/yaw; HOTAS source aim, "
                "alignment assist, and pitch/yaw writer gates are released.");
    }
    bool hookOk = ThrottleHook::Install();
    if (!hookOk) {
        MainLog("WARNING: Hook installation failed. Throttle injection disabled.");
        MainLog("The game may have updated; the .text signature scan found no match.");
    }

    const bool absoluteHeadTrackingPresent = AbsoluteHeadTrackingPresent();
    HeadTracking::SetExternalOwner(absoluteHeadTrackingPresent);
    NativeShipControl::SetExternalCameraOwner(absoluteHeadTrackingPresent);
    AbsoluteControlSubscriber::SetExternalCameraOwner(absoluteHeadTrackingPresent);
    if (absoluteHeadTrackingPresent) {
        MainLog("Absolute Head Tracking detected: it owns camera composition; HOTAS retains the selected-flight observer.");
    }

    const bool nativeControlsInitialized = NativeShipControl::Initialize();
    if (!nativeControlsInitialized) {
        MainLog("WARNING: Native 5.0 control seams did not pass their exact runtime gates.");
    }

    bool controllerStarted = false;
    if (hookOk) {
        MainLog("Hook layer ready. Initializing and starting standalone ThrottleController.");
        if (ThrottleController::Initialize()) {
            ThrottleController::Start();
            controllerStarted = true;
            MainLog("ThrottleController started successfully.");
        } else {
            MainLog("WARNING: ThrottleController initialization failed or disabled in config.");
        }
    }

    // Phase 2: optional D3D12 ImGui workbench. This is intentionally independent
    // of the controller: users with an incompatible renderer stack can disable
    // every graphics hook and continue using their manual configuration.
    const bool workbenchConfigured = RuntimePaths::IsWorkbenchEnabled();
    bool workbenchInstalled = false;
    if (!workbenchConfigured) {
        MainLog("Workbench disabled by [UI] bEnableWorkbench=false; manual configuration and flight controls remain active.");
    } else if (UIHook::Install()) {
        BindingWizard::Initialize();
        workbenchInstalled = true;
        MainLog("UIHook + BindingWizard armed. Press Ctrl+Alt+B in-game to initialize the renderer.");
    } else {
        MainLog("WARNING: UIHook installation failed. Workbench disabled; manual configuration and flight controls remain active.");
    }

    AbsoluteControlSubscriber::SetRuntimeStatus({
        .throttleHookInstalled = hookOk,
        .nativeControlsInitialized = nativeControlsInitialized,
        .controllerStarted = controllerStarted,
        .legacyWorkbenchConfigured = workbenchConfigured,
        .legacyWorkbenchInstalled = workbenchInstalled,
    });

    if (!RegisterSfseListener(a_sfse)) {
        MainLog("WARNING: SFSE lifecycle messaging is unavailable; Absolute Control discovery is disabled for this session, while standalone HOTAS operation continues.");
    } else {
        MainLog("Absolute Control discovery deferred to SFSE post-data-load.");
    }

    MainLog("Plugin load complete.");
    return true;
}
