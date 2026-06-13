#include "ThrottleHook.h"
#include "ThrottleController.h"
#include "RuntimePaths.h"
#include "SettingBeacon.h"
#include "UIHook.h"
#include "BindingWizard.h"
#include <SFSE/Interfaces.h>
#include <windows.h>
#include <format>

// Logging helper (also used by Prober/Hook modules)
static void InitializeLog() {
    RuntimePaths::AppendLog("[AbsoluteHOTAS]", "Started Log Session");
}

static void MainLog(const std::string& msg) {
    RuntimePaths::AppendLog("[Main]", msg);
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

    RuntimePaths::AppendLogAlways("[Crash]",
        std::format("Exception code=0x{:08X} address=0x{:016X}",
            static_cast<std::uint32_t>(record->ExceptionCode),
            reinterpret_cast<std::uintptr_t>(record->ExceptionAddress)));
    if (queryOk) {
        RuntimePaths::AppendLogAlways("[Crash]",
            std::format("Module base=0x{:016X} rva=0x{:X} path={}",
                reinterpret_cast<std::uintptr_t>(mbi.AllocationBase),
                reinterpret_cast<std::uintptr_t>(record->ExceptionAddress) - reinterpret_cast<std::uintptr_t>(mbi.AllocationBase),
                modulePath[0] ? modulePath : "<unknown>"));
    }
    RuntimePaths::AppendLogAlways("[Crash]",
        std::format("RIP=0x{:016X} RSP=0x{:016X} RCX=0x{:016X} RDX=0x{:016X} R8=0x{:016X} R9=0x{:016X}",
            static_cast<std::uintptr_t>(context->Rip),
            static_cast<std::uintptr_t>(context->Rsp),
            static_cast<std::uintptr_t>(context->Rcx),
            static_cast<std::uintptr_t>(context->Rdx),
            static_cast<std::uintptr_t>(context->R8),
            static_cast<std::uintptr_t>(context->R9)));
    if (record->NumberParameters > 1) {
        RuntimePaths::AppendLogAlways("[Crash]",
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
SFSEPluginLoad(const SFSE::LoadInterface* /*a_sfse*/)
{
    // Read bLogThrottle from INI once and cache the result globally.
    RuntimePaths::EnableFileLogging();

    InitializeLog();
    InstallCrashLogger();

    // Startup banner always writes so the user can confirm the plugin loaded.
    RuntimePaths::AppendLogAlways("[Main]", "======================================================");
    RuntimePaths::AppendLogAlways("[Main]", "AbsoluteHOTAS v3.0 - Direct HID + In-Game UI");
    RuntimePaths::AppendLogAlways("[Main]", "Target: Starfield 1.16.242+ / SFSE 0.2.20+");
    RuntimePaths::AppendLogAlways("[Main]", "======================================================");

    // Plant the discovery beacon and zero game deadzones unless Signal Hunter fallback is enabled
    bool fallbackMode = GetPrivateProfileIntA(
        "Injection", "bSignalHunterFallback", 0,
        RuntimePaths::IniPath().string().c_str()) != 0;
    if (!fallbackMode) {
        if (!SettingBeacon::PlantBeacon()) {
            RuntimePaths::AppendLogAlways("[Main]",
                "Beacon failed. Signal Hunter will rely on StarfieldCustom.ini fallback.");
        }
    } else {
        RuntimePaths::AppendLogAlways("[Main]",
            "Signal Hunter fallback enabled. Using StarfieldCustom.ini discovery.");
    }

    // Phase 1: AOB scan + trampoline hook to capture ThrottleInterface pointer
    bool hookOk = ThrottleHook::Install();
    if (!hookOk) {
        MainLog("WARNING: Hook installation failed. Throttle injection disabled.");
        MainLog("The game may have updated. Check StarfieldThrottleLog.txt for details.");
    }

    if (hookOk) {
        MainLog("Hook layer ready. Initializing and starting standalone ThrottleController.");
        if (ThrottleController::Initialize()) {
            ThrottleController::Start();
            MainLog("ThrottleController started successfully.");
        } else {
            MainLog("WARNING: ThrottleController initialization failed or disabled in config.");
        }
    }

    // Phase 2: D3D12 ImGui overlay
    if (UIHook::Install()) {
        BindingWizard::Initialize();
        MainLog("UIHook + BindingWizard initialized. Press Ctrl+Alt+B in-game.");
    } else {
        MainLog("WARNING: UIHook installation failed. In-game UI disabled.");
    }

    MainLog("Plugin load complete.");
    return true;
}
