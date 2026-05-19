#include "PapyrusHook.h"
#include "Papyrus.h"
#include "RuntimePaths.h"

#include <SFSE/Interfaces.h>

#include <windows.h>

#include <array>
#include <cstring>
#include <format>
#include <limits>

namespace {
    using BindEverythingToScript_t = void (*)(RE::BSScript::IVirtualMachine**);
    using AllocateFromBranchPool_t = void* (*)(SFSE::PluginHandle, std::size_t);

    struct SFSETrampolineInterfaceRaw {
        std::uint32_t interfaceVersion;
        AllocateFromBranchPool_t AllocateFromBranchPool;
        AllocateFromBranchPool_t AllocateFromLocalPool;
    };

    // Hook the unique CALL to GameVM::BindEverythingToScript for Starfield 1.16.236.
    // offsets-1-16-236-0.txt: ID 171438 -> 0x1433B5760, target RVA 0x33B5760.
    // The validated 1.16.236 executable has one E8 call to that target at RVA 0x33C61B9.
    constexpr std::uintptr_t kBindEverythingCallOffset_1_16_236 = 0x33C61B9;
    constexpr std::uintptr_t kBindEverythingTargetOffset_1_16_236 = 0x33B5760;
    constexpr std::uint32_t kRuntime_1_16_236 = (1u << 24) | (16u << 16) | (236u << 4);
    constexpr std::uint32_t kRuntime_1_16_242 = (1u << 24) | (16u << 16) | (242u << 4);

    BindEverythingToScript_t g_originalBindEverything = nullptr;

    void RegisterPapyrus(RE::BSScript::IVirtualMachine** a_vm);

    const SFSE::detail::SFSEInterface* RawSFSE(const SFSE::LoadInterface* a_sfse) {
        return reinterpret_cast<const SFSE::detail::SFSEInterface*>(a_sfse);
    }

    std::uintptr_t ModuleBase() {
        return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    }

    bool WriteCall(std::uintptr_t a_callAddress, const void* a_destination) {
        const auto nextInstruction = a_callAddress + 5;
        const auto displacement = reinterpret_cast<std::intptr_t>(a_destination) - static_cast<std::intptr_t>(nextInstruction);
        if (displacement < (std::numeric_limits<std::int32_t>::min)() ||
            displacement > (std::numeric_limits<std::int32_t>::max)()) {
            RuntimePaths::AppendLog("[PapyrusHook]", "Skipped: replacement call target is out of rel32 range.");
            return false;
        }

        DWORD oldProtect = 0;
        if (!VirtualProtect(reinterpret_cast<void*>(a_callAddress), 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            RuntimePaths::AppendLog("[PapyrusHook]", std::format("Skipped: VirtualProtect failed with code {}", GetLastError()));
            return false;
        }

        *reinterpret_cast<std::uint8_t*>(a_callAddress) = 0xE8;
        *reinterpret_cast<std::int32_t*>(a_callAddress + 1) = static_cast<std::int32_t>(displacement);

        DWORD ignored = 0;
        VirtualProtect(reinterpret_cast<void*>(a_callAddress), 5, oldProtect, &ignored);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(a_callAddress), 5);
        return true;
    }

    std::uintptr_t DecodeCallTarget(std::uintptr_t a_callAddress) {
        const auto displacement = *reinterpret_cast<const std::int32_t*>(a_callAddress + 1);
        return a_callAddress + 5 + displacement;
    }

    void* AllocateBranchStub(const SFSE::LoadInterface* a_sfse) {
        const auto* const sfse = RawSFSE(a_sfse);
        auto* const rawInterface = sfse && sfse->QueryInterface ? static_cast<SFSETrampolineInterfaceRaw*>(
                                                                      sfse->QueryInterface(SFSE::LoadInterface::kTrampoline)) :
                                                                  nullptr;
        if (!rawInterface || !rawInterface->AllocateFromBranchPool) {
            return nullptr;
        }

        const auto pluginHandle = sfse->GetPluginHandle ? sfse->GetPluginHandle() : SFSE::kInvalidPluginHandle;
        return rawInterface->AllocateFromBranchPool(pluginHandle, 12);
    }

    void* CreateRegisterPapyrusStub(const SFSE::LoadInterface* a_sfse) {
        auto* const stub = static_cast<std::uint8_t*>(AllocateBranchStub(a_sfse));
        if (!stub) {
            RuntimePaths::AppendLog("[PapyrusHook]", "Skipped: SFSE branch-pool allocation failed.");
            return nullptr;
        }

        // mov rax, imm64; jmp rax
        const std::array<std::uint8_t, 12> templateBytes{
            0x48, 0xB8,
            0, 0, 0, 0, 0, 0, 0, 0,
            0xFF, 0xE0
        };
        std::memcpy(stub, templateBytes.data(), templateBytes.size());
        *reinterpret_cast<std::uintptr_t*>(stub + 2) = reinterpret_cast<std::uintptr_t>(RegisterPapyrus);
        FlushInstructionCache(GetCurrentProcess(), stub, templateBytes.size());
        return stub;
    }

    void RegisterPapyrus(RE::BSScript::IVirtualMachine** a_vm) {
        RuntimePaths::AppendLog("[PapyrusHook]", "RegisterPapyrus entered");

        if (g_originalBindEverything) {
            RuntimePaths::AppendLog("[PapyrusHook]", "Calling original BindEverythingToScript");
            g_originalBindEverything(a_vm);
            RuntimePaths::AppendLog("[PapyrusHook]", "Original BindEverythingToScript returned");
        } else {
            RuntimePaths::AppendLog("[PapyrusHook]", "Original BindEverythingToScript pointer is null");
        }

        RuntimePaths::AppendLog("[PapyrusHook]", a_vm && *a_vm ? "VM pointer valid; registering AbsoluteHOTAS natives" : "VM pointer missing");
        const bool registered = a_vm && *a_vm && Papyrus::RegisterFunctions(*a_vm);
        RuntimePaths::AppendLog("[PapyrusHook]", registered ? "Native registration succeeded" : "Native registration failed");
    }
}

namespace PapyrusHook {
    void Install(const SFSE::LoadInterface* a_sfse) {
        const auto* const sfse = RawSFSE(a_sfse);
        const auto runtime = sfse ? sfse->runtimeVersion : 0;
        if (runtime != kRuntime_1_16_236 && runtime != kRuntime_1_16_242) {
            RuntimePaths::AppendLog("[PapyrusHook]", std::format("Skipped for unsupported runtime 0x{:08X}", runtime));
            return;
        }

        static bool installed = false;
        if (installed) {
            return;
        }

        const auto moduleBase = ModuleBase();
        const auto hookAddress = moduleBase + kBindEverythingCallOffset_1_16_236;
        if (*reinterpret_cast<const std::uint8_t*>(hookAddress) != 0xE8) {
            RuntimePaths::AppendLog("[PapyrusHook]",
                std::format("Skipped: Starfield.exe+{:X} is not the expected 1.16.236 call instruction for runtime 0x{:08X}",
                    kBindEverythingCallOffset_1_16_236,
                    runtime));
            return;
        }

        const auto originalTarget = DecodeCallTarget(hookAddress);
        const auto expectedTarget = moduleBase + kBindEverythingTargetOffset_1_16_236;
        if (originalTarget != expectedTarget) {
            RuntimePaths::AppendLog("[PapyrusHook]",
                std::format(
                    "Skipped: Starfield.exe+{:X} targets +{:X}, expected +{:X}",
                    kBindEverythingCallOffset_1_16_236,
                    originalTarget - moduleBase,
                    kBindEverythingTargetOffset_1_16_236));
            return;
        }

        g_originalBindEverything = reinterpret_cast<BindEverythingToScript_t>(originalTarget);

        const auto* const replacementTarget = CreateRegisterPapyrusStub(a_sfse);
        if (!replacementTarget || !WriteCall(hookAddress, replacementTarget)) {
            g_originalBindEverything = nullptr;
            return;
        }

        installed = true;
        RuntimePaths::AppendLog("[PapyrusHook]",
            std::format("Installed at Starfield.exe+{:X}", kBindEverythingCallOffset_1_16_236));
    }
}
