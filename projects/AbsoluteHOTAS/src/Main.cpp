#include "ThrottleHook.h"
#include "ThrottleController.h"
#include "RuntimePaths.h"
#include <SFSE/Interfaces.h>
#include <windows.h>
#include <format>

static bool IsFileLoggingEnabled() {
    return RuntimePaths::IsFileLoggingEnabled();
}

// Logging helper (also used by Prober/Hook modules)
static void InitializeLog() {
    if (!IsFileLoggingEnabled()) return;
    RuntimePaths::AppendLog("[AbsoluteHOTAS]", "Started Log Session");
}

static void MainLog(const std::string& msg) {
    if (!IsFileLoggingEnabled()) return;
    RuntimePaths::AppendLog("[Main]", msg);
}

static LONG NTAPI CrashLogHandler(EXCEPTION_POINTERS* a_exceptionInfo) {
    if (!a_exceptionInfo || !a_exceptionInfo->ExceptionRecord || !a_exceptionInfo->ContextRecord) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    const auto* const record = a_exceptionInfo->ExceptionRecord;
    if (record->ExceptionCode == 0x40010006 || record->ExceptionCode == 0x406D1388) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    const auto* const context = a_exceptionInfo->ContextRecord;
    MEMORY_BASIC_INFORMATION mbi{};
    char modulePath[MAX_PATH]{};
    const auto queryOk = VirtualQuery(record->ExceptionAddress, &mbi, sizeof(mbi)) != 0;
    if (queryOk) {
        GetModuleFileNameA(static_cast<HMODULE>(mbi.AllocationBase), modulePath, static_cast<DWORD>(std::size(modulePath)));
    }

    RuntimePaths::AppendLog("[Crash]",
        std::format("Exception code=0x{:08X} address=0x{:016X}",
            static_cast<std::uint32_t>(record->ExceptionCode),
            reinterpret_cast<std::uintptr_t>(record->ExceptionAddress)));
    if (queryOk) {
        RuntimePaths::AppendLog("[Crash]",
            std::format("Module base=0x{:016X} rva=0x{:X} path={}",
                reinterpret_cast<std::uintptr_t>(mbi.AllocationBase),
                reinterpret_cast<std::uintptr_t>(record->ExceptionAddress) - reinterpret_cast<std::uintptr_t>(mbi.AllocationBase),
                modulePath[0] ? modulePath : "<unknown>"));
    }
    RuntimePaths::AppendLog("[Crash]",
        std::format("RIP=0x{:016X} RSP=0x{:016X} RCX=0x{:016X} RDX=0x{:016X} R8=0x{:016X} R9=0x{:016X}",
            static_cast<std::uintptr_t>(context->Rip),
            static_cast<std::uintptr_t>(context->Rsp),
            static_cast<std::uintptr_t>(context->Rcx),
            static_cast<std::uintptr_t>(context->Rdx),
            static_cast<std::uintptr_t>(context->R8),
            static_cast<std::uintptr_t>(context->R9)));
    if (record->NumberParameters > 1) {
        RuntimePaths::AppendLog("[Crash]",
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
    InitializeLog();
    InstallCrashLogger();
    MainLog("======================================================");
    MainLog("AbsoluteHOTAS v1.6.2 - Pure Flight Control");
    MainLog("Target: Starfield 1.16.242 / SFSE 0.2.20");
    MainLog("======================================================");

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

    MainLog("Plugin load complete.");
    return true;
}
