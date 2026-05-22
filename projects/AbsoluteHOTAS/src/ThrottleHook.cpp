#include "ThrottleHook.h"
#include "RuntimePaths.h"
#include <windows.h>
#include <fstream>
#include <iomanip>
#include <string>
#include <cstring>
#include <vector>

// ---- Static member definitions ----
std::atomic<uintptr_t> ThrottleHook::s_basePtr{ 0 };
std::atomic<int64_t>   ThrottleHook::s_lastHookTimestamp{ 0 };
uint8_t*               ThrottleHook::s_trampoline = nullptr;
uintptr_t              ThrottleHook::s_hookAddress = 0;
std::atomic<bool>      ThrottleHook::s_silenceEnabled{ false };

// ---- Global pointer storage (accessed by shellcode via absolute address) ----
static volatile uintptr_t g_capturedRDI = 0;
static volatile int64_t   g_capturedTimestamp = 0;
static volatile uintptr_t g_capturedSourceR13 = 0;
static volatile uintptr_t g_capturedWriterCluster = 0;

// ---- Candidate array: stores unique RDI values from all hook fires ----
static volatile uintptr_t g_candidates[ThrottleHook::MAX_CANDIDATES] = {};
static volatile int g_candidateCount = 0;
static volatile bool g_captureEnabled = true;
static volatile bool g_silenceEnabled = false; // SILENCE FLAG: Skip original writes if true
static volatile uint8_t g_rotOverrideEnabled = 0;
static volatile uint8_t g_rollOverrideEnabled = 0;
static volatile uint8_t g_vertStrafeOverrideEnabled = 0;
static volatile uint8_t g_yawOverrideEnabled = 0;
static volatile uint8_t g_pitchOverrideEnabled = 0;
static volatile uint32_t g_rollBits = 0;
static volatile uint32_t g_vertStrafeBits = 0;
static volatile uint32_t g_yawBits = 0;
static volatile uint32_t g_pitchBits = 0;

static constexpr int MAX_MANUAL_CAMERA_GATES = 96;
static volatile uint8_t g_manualGateActive[MAX_MANUAL_CAMERA_GATES] = {};
static volatile uint32_t g_manualGateBits[MAX_MANUAL_CAMERA_GATES] = {};
static uintptr_t g_manualGateAddress[MAX_MANUAL_CAMERA_GATES] = {};
static uintptr_t g_manualGateOffset[MAX_MANUAL_CAMERA_GATES] = {};
static int g_manualGateCount = 0;

// ---- Hook Registry for Persistent Silencing ----
struct HookRecord {
    uintptr_t targetAddr;
    uint8_t origBytes[5];
    uint8_t trampJmp[5];
    bool isStore; // Identify if this instruction writes to memory
};
static std::vector<HookRecord> g_hookRegistry;

// ---- Logging helper ----
static bool IsHookFileLoggingEnabled() {
    static bool checked = false;
    static bool enabled = false;
    if (!checked) {
        enabled = GetPrivateProfileIntA(
            "Injection",
            "bLogThrottle",
            0,
            RuntimePaths::IniPath().string().c_str()) != 0;
        checked = true;
    }
    return enabled;
}

static bool IsCriticalHookLog(const std::string& msg) {
    return msg.find("CRITICAL") != std::string::npos;
}

static void RotateLogIfNeeded() {
    static bool checked = false;
    if (checked) return;
    checked = true;

    const auto logPath = RuntimePaths::LogPath();
    const auto oldLogPath = RuntimePaths::PluginDirectory() / L"AbsoluteHOTAS.log.old";
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (GetFileAttributesExW(logPath.c_str(), GetFileExInfoStandard, &data)) {
        ULARGE_INTEGER size{};
        size.HighPart = data.nFileSizeHigh;
        size.LowPart = data.nFileSizeLow;
        if (size.QuadPart > 1024ull * 1024ull) {
            DeleteFileW(oldLogPath.c_str());
            MoveFileExW(logPath.c_str(), oldLogPath.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
    }
}

static void HookLog(const std::string& msg) {
    if (!IsHookFileLoggingEnabled() && !IsCriticalHookLog(msg)) return;
    RotateLogIfNeeded();
    RuntimePaths::AppendLog("[ThrottleHook]", msg);
}

// ---- Find .text section ----
static bool GetTextSection(uintptr_t& textStart, size_t& textSize) {
    HMODULE hModule = GetModuleHandle(NULL);
    if (!hModule) return false;

    uintptr_t moduleBase = (uintptr_t)hModule;
    IMAGE_DOS_HEADER* dosHeader = (IMAGE_DOS_HEADER*)moduleBase;
    IMAGE_NT_HEADERS* ntHeaders = (IMAGE_NT_HEADERS*)(moduleBase + dosHeader->e_lfanew);
    IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(ntHeaders);

    for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++) {
        std::string name((char*)section[i].Name, 8);
        if (name.find(".text") != std::string::npos) {
            textStart = moduleBase + section[i].VirtualAddress;
            textSize = section[i].Misc.VirtualSize;
            return true;
        }
    }
    return false;
}

static uint32_t FloatToBits(float value) {
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static uint8_t* AllocateNear(uintptr_t targetAddr) {
    uintptr_t allocBase = targetAddr > 0x70000000 ? targetAddr - 0x70000000 : 0x10000;
    for (uintptr_t addr = allocBase; addr < targetAddr + 0x70000000; addr += 0x10000) {
        uint8_t* mem = (uint8_t*)VirtualAlloc((void*)addr, 4096,
            MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (mem) return mem;
    }
    return nullptr;
}

static bool PatchJump5(uintptr_t targetAddr, uint8_t* trampMem) {
    DWORD oldProtect;
    if (!VirtualProtect((void*)targetAddr, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }

    uint8_t* origCode = (uint8_t*)targetAddr;
    origCode[0] = 0xE9;
    int32_t jmpToTramp = (int32_t)((uintptr_t)trampMem - (targetAddr + 5));
    memcpy(&origCode[1], &jmpToTramp, 4);
    VirtualProtect((void*)targetAddr, 5, oldProtect, &oldProtect);
    return true;
}

static bool InstallRollGate(uintptr_t targetAddr) {
    uint8_t* trampMem = AllocateNear(targetAddr);
    if (!trampMem) {
        HookLog("CRITICAL: Could not allocate roll gate trampoline.");
        return false;
    }

    int idx = 0;
    trampMem[idx++] = 0x48; trampMem[idx++] = 0x8D; trampMem[idx++] = 0x50; trampMem[idx++] = 0x58; // lea rdx,[rax+58]
    trampMem[idx++] = 0x51; // push rcx
    trampMem[idx++] = 0x9C; // pushfq

    trampMem[idx++] = 0x48; trampMem[idx++] = 0xB9;
    uintptr_t sourceAddr = (uintptr_t)&g_capturedSourceR13;
    memcpy(&trampMem[idx], &sourceAddr, 8); idx += 8;
    trampMem[idx++] = 0x4C; trampMem[idx++] = 0x89; trampMem[idx++] = 0x29; // mov [rcx],r13

    trampMem[idx++] = 0x48; trampMem[idx++] = 0xB9;
    uintptr_t clusterAddr = (uintptr_t)&g_capturedWriterCluster;
    memcpy(&trampMem[idx], &clusterAddr, 8); idx += 8;
    trampMem[idx++] = 0x48; trampMem[idx++] = 0x89; trampMem[idx++] = 0x01; // mov [rcx],rax

    trampMem[idx++] = 0x48; trampMem[idx++] = 0xB9;
    uintptr_t activeAddr = (uintptr_t)&g_rollOverrideEnabled;
    memcpy(&trampMem[idx], &activeAddr, 8); idx += 8;
    trampMem[idx++] = 0x80; trampMem[idx++] = 0x39; trampMem[idx++] = 0x00; // cmp byte ptr [rcx],0
    trampMem[idx++] = 0x74;
    int inactiveJump = idx++;

    trampMem[idx++] = 0x48; trampMem[idx++] = 0xB9;
    uintptr_t valueAddr = (uintptr_t)&g_rollBits;
    memcpy(&trampMem[idx], &valueAddr, 8); idx += 8;
    trampMem[idx++] = 0x8B; trampMem[idx++] = 0x09; // mov ecx,[rcx]
    trampMem[idx++] = 0x89; trampMem[idx++] = 0x0A; // mov [rdx],ecx
    trampMem[idx++] = 0xEB;
    int doneJump = idx++;

    int inactiveLabel = idx;
    trampMem[idx++] = 0x9D; // popfq
    trampMem[idx++] = 0x59; // pop rcx
    trampMem[idx++] = 0xC5; trampMem[idx++] = 0xFA; trampMem[idx++] = 0x11; trampMem[idx++] = 0x02; // vmovss [rdx],xmm0
    trampMem[idx++] = 0xE9;
    int32_t inactiveBack = (int32_t)((targetAddr + 8) - ((uintptr_t)&trampMem[idx] + 4));
    memcpy(&trampMem[idx], &inactiveBack, 4); idx += 4;

    int doneLabel = idx;
    trampMem[idx++] = 0x9D; // popfq
    trampMem[idx++] = 0x59; // pop rcx
    trampMem[idx++] = 0xE9;
    int32_t activeBack = (int32_t)((targetAddr + 8) - ((uintptr_t)&trampMem[idx] + 4));
    memcpy(&trampMem[idx], &activeBack, 4); idx += 4;

    trampMem[inactiveJump] = (uint8_t)(inactiveLabel - inactiveJump - 1);
    trampMem[doneJump] = (uint8_t)(doneLabel - doneJump - 1);

    if (!PatchJump5(targetAddr, trampMem)) {
        HookLog("CRITICAL: Could not patch roll gate.");
        return false;
    }

    HookLog("Roll gate installed over lea rdx,[rax+58] + vmovss [rdx],xmm0.");
    return true;
}

static bool InstallRaxDispGate(uintptr_t targetAddr, uint8_t disp, volatile uint32_t* valueBits, volatile uint8_t* activeFlag, const char* label) {
    uint8_t* trampMem = AllocateNear(targetAddr);
    if (!trampMem) {
        HookLog("CRITICAL: Could not allocate " + std::string(label) + " gate trampoline.");
        return false;
    }

    int idx = 0;
    trampMem[idx++] = 0x51; // push rcx
    trampMem[idx++] = 0x9C; // pushfq

    trampMem[idx++] = 0x48; trampMem[idx++] = 0xB9;
    uintptr_t activeAddr = (uintptr_t)activeFlag;
    memcpy(&trampMem[idx], &activeAddr, 8); idx += 8;
    trampMem[idx++] = 0x80; trampMem[idx++] = 0x39; trampMem[idx++] = 0x00; // cmp byte ptr [rcx],0
    trampMem[idx++] = 0x74;
    int inactiveJump = idx++;

    trampMem[idx++] = 0x48; trampMem[idx++] = 0xB9;
    uintptr_t valueAddr = (uintptr_t)valueBits;
    memcpy(&trampMem[idx], &valueAddr, 8); idx += 8;
    trampMem[idx++] = 0x8B; trampMem[idx++] = 0x09; // mov ecx,[rcx]
    trampMem[idx++] = 0x89; trampMem[idx++] = 0x48; trampMem[idx++] = disp; // mov [rax+disp],ecx
    trampMem[idx++] = 0xEB;
    int doneJump = idx++;

    int inactiveLabel = idx;
    trampMem[idx++] = 0x9D; // popfq
    trampMem[idx++] = 0x59; // pop rcx
    memcpy(&trampMem[idx], (void*)targetAddr, 5); idx += 5;
    trampMem[idx++] = 0xE9;
    int32_t inactiveBack = (int32_t)((targetAddr + 5) - ((uintptr_t)&trampMem[idx] + 4));
    memcpy(&trampMem[idx], &inactiveBack, 4); idx += 4;

    int doneLabel = idx;
    trampMem[idx++] = 0x9D; // popfq
    trampMem[idx++] = 0x59; // pop rcx
    trampMem[idx++] = 0xE9;
    int32_t activeBack = (int32_t)((targetAddr + 5) - ((uintptr_t)&trampMem[idx] + 4));
    memcpy(&trampMem[idx], &activeBack, 4); idx += 4;

    trampMem[inactiveJump] = (uint8_t)(inactiveLabel - inactiveJump - 1);
    trampMem[doneJump] = (uint8_t)(doneLabel - doneJump - 1);

    if (!PatchJump5(targetAddr, trampMem)) {
        HookLog("CRITICAL: Could not patch " + std::string(label) + " gate.");
        return false;
    }

    HookLog(std::string(label) + " gate installed.");
    return true;
}

static bool InstallRdxDispGate(uintptr_t targetAddr, uint8_t disp, volatile uint32_t* valueBits, volatile uint8_t* activeFlag, const char* label) {
    uint8_t* trampMem = AllocateNear(targetAddr);
    if (!trampMem) {
        HookLog("CRITICAL: Could not allocate " + std::string(label) + " gate trampoline.");
        return false;
    }

    int idx = 0;
    trampMem[idx++] = 0x51; // push rcx
    trampMem[idx++] = 0x9C; // pushfq

    trampMem[idx++] = 0x48; trampMem[idx++] = 0xB9;
    uintptr_t activeAddr = (uintptr_t)activeFlag;
    memcpy(&trampMem[idx], &activeAddr, 8); idx += 8;
    trampMem[idx++] = 0x80; trampMem[idx++] = 0x39; trampMem[idx++] = 0x00; // cmp byte ptr [rcx],0
    trampMem[idx++] = 0x74;
    int inactiveJump = idx++;

    trampMem[idx++] = 0x48; trampMem[idx++] = 0xB9;
    uintptr_t valueAddr = (uintptr_t)valueBits;
    memcpy(&trampMem[idx], &valueAddr, 8); idx += 8;
    trampMem[idx++] = 0x8B; trampMem[idx++] = 0x09; // mov ecx,[rcx]
    trampMem[idx++] = 0x89; trampMem[idx++] = 0x4A; trampMem[idx++] = disp; // mov [rdx+disp],ecx
    trampMem[idx++] = 0xEB;
    int doneJump = idx++;

    int inactiveLabel = idx;
    trampMem[idx++] = 0x9D; // popfq
    trampMem[idx++] = 0x59; // pop rcx
    memcpy(&trampMem[idx], (void*)targetAddr, 5); idx += 5;
    trampMem[idx++] = 0xE9;
    int32_t inactiveBack = (int32_t)((targetAddr + 5) - ((uintptr_t)&trampMem[idx] + 4));
    memcpy(&trampMem[idx], &inactiveBack, 4); idx += 4;

    int doneLabel = idx;
    trampMem[idx++] = 0x9D; // popfq
    trampMem[idx++] = 0x59; // pop rcx
    trampMem[idx++] = 0xE9;
    int32_t activeBack = (int32_t)((targetAddr + 5) - ((uintptr_t)&trampMem[idx] + 4));
    memcpy(&trampMem[idx], &activeBack, 4); idx += 4;

    trampMem[inactiveJump] = (uint8_t)(inactiveLabel - inactiveJump - 1);
    trampMem[doneJump] = (uint8_t)(doneLabel - doneJump - 1);

    if (!PatchJump5(targetAddr, trampMem)) {
        HookLog("CRITICAL: Could not patch " + std::string(label) + " gate.");
        return false;
    }

    HookLog(std::string(label) + " gate installed.");
    return true;
}

static bool InstallManualCameraGate(uintptr_t targetAddr, uint8_t disp, int gateIndex, uintptr_t moduleBase) {
    if (gateIndex < 0 || gateIndex >= MAX_MANUAL_CAMERA_GATES) return false;

    uint8_t* trampMem = AllocateNear(targetAddr);
    if (!trampMem) {
        HookLog("CRITICAL: Could not allocate manual camera gate trampoline.");
        return false;
    }

    int idx = 0;
    trampMem[idx++] = 0x51; // push rcx
    trampMem[idx++] = 0x9C; // pushfq

    trampMem[idx++] = 0x48; trampMem[idx++] = 0xB9;
    uintptr_t activeAddr = (uintptr_t)&g_manualGateActive[gateIndex];
    memcpy(&trampMem[idx], &activeAddr, 8); idx += 8;
    trampMem[idx++] = 0x80; trampMem[idx++] = 0x39; trampMem[idx++] = 0x00; // cmp byte ptr [rcx],0
    trampMem[idx++] = 0x74;
    int inactiveJump = idx++;

    trampMem[idx++] = 0x48; trampMem[idx++] = 0xB9;
    uintptr_t valueAddr = (uintptr_t)&g_manualGateBits[gateIndex];
    memcpy(&trampMem[idx], &valueAddr, 8); idx += 8;
    trampMem[idx++] = 0x8B; trampMem[idx++] = 0x09; // mov ecx,[rcx]
    trampMem[idx++] = 0x89; trampMem[idx++] = 0x48; trampMem[idx++] = disp; // mov [rax+disp],ecx
    trampMem[idx++] = 0xEB;
    int doneJump = idx++;

    int inactiveLabel = idx;
    trampMem[idx++] = 0x9D; // popfq
    trampMem[idx++] = 0x59; // pop rcx
    memcpy(&trampMem[idx], (void*)targetAddr, 5); idx += 5;
    trampMem[idx++] = 0xE9;
    int32_t inactiveBack = (int32_t)((targetAddr + 5) - ((uintptr_t)&trampMem[idx] + 4));
    memcpy(&trampMem[idx], &inactiveBack, 4); idx += 4;

    int doneLabel = idx;
    trampMem[idx++] = 0x9D; // popfq
    trampMem[idx++] = 0x59; // pop rcx
    trampMem[idx++] = 0xE9;
    int32_t activeBack = (int32_t)((targetAddr + 5) - ((uintptr_t)&trampMem[idx] + 4));
    memcpy(&trampMem[idx], &activeBack, 4); idx += 4;

    trampMem[inactiveJump] = (uint8_t)(inactiveLabel - inactiveJump - 1);
    trampMem[doneJump] = (uint8_t)(doneLabel - doneJump - 1);

    uint8_t orig[5];
    memcpy(orig, (void*)targetAddr, 5);
    if (!PatchJump5(targetAddr, trampMem)) {
        HookLog("CRITICAL: Could not patch manual camera gate.");
        return false;
    }

    HookRecord rec;
    rec.targetAddr = targetAddr;
    rec.isStore = true;
    memcpy(rec.origBytes, orig, 5);
    memcpy(rec.trampJmp, (void*)targetAddr, 5);
    g_hookRegistry.push_back(rec);

    g_manualGateAddress[gateIndex] = targetAddr;
    g_manualGateOffset[gateIndex] = disp;

    char buf[192];
    sprintf_s(buf, "Manual camera gate #%d installed at Starfield.exe+%X disp=0x%X",
        gateIndex, (unsigned int)(targetAddr - moduleBase), disp);
    HookLog(buf);
    return true;
}

static void InstallManualCameraGates(uintptr_t textStart, size_t textSize, uintptr_t moduleBase) {
    g_manualGateCount = 0;
    memset((void*)g_manualGateActive, 0, sizeof(g_manualGateActive));
    memset((void*)g_manualGateBits, 0, sizeof(g_manualGateBits));
    memset(g_manualGateAddress, 0, sizeof(g_manualGateAddress));
    memset(g_manualGateOffset, 0, sizeof(g_manualGateOffset));

    struct Pattern {
        uint8_t bytes[5];
        uint8_t disp;
        const char* label;
    };

    const Pattern patterns[] = {
        {{0xC5, 0xFA, 0x11, 0x48, 0x64}, 0x64, "pitch-xmm1"},
        {{0xC5, 0xFA, 0x11, 0x58, 0x60}, 0x60, "yaw-xmm3"},
    };

    for (const auto& pat : patterns) {
        for (size_t i = 0; i + 5 < textSize && g_manualGateCount < MAX_MANUAL_CAMERA_GATES; ++i) {
            uintptr_t addr = textStart + i;
            uint8_t* ptr = (uint8_t*)addr;
            if (memcmp(ptr, pat.bytes, 5) != 0) continue;

            // Exact ship rotational gates are installed separately. Do not double-patch them.
            bool alreadyPatched = false;
            for (const auto& hook : g_hookRegistry) {
                if (hook.targetAddr == addr) {
                    alreadyPatched = true;
                    break;
                }
            }
            if (alreadyPatched) continue;

            int gateIndex = g_manualGateCount;
            if (InstallManualCameraGate(addr, pat.disp, gateIndex, moduleBase)) {
                ++g_manualGateCount;
            }
        }
    }

    char buf[128];
    sprintf_s(buf, "Manual camera broad gate install complete: %d gates", g_manualGateCount);
    HookLog(buf);
}

static bool InstallRotationalGates(uintptr_t textStart, size_t textSize, uintptr_t moduleBase) {
    const uint8_t writerBlock[] = {
        0x49, 0x8B, 0x85, 0xE8, 0x01, 0x00, 0x00,
        0x48, 0x8D, 0x50, 0x58,
        0xC5, 0xFA, 0x11, 0x02,
        0xC5, 0xFA, 0x11, 0x52, 0x04,
        0xC5, 0xFA, 0x11, 0x58, 0x60,
        0xC5, 0xFA, 0x11, 0x48, 0x64
    };

    uintptr_t blockAddr = 0;
    for (size_t i = 0; i < textSize - sizeof(writerBlock); i++) {
        uint8_t* ptr = (uint8_t*)(textStart + i);
        if (memcmp(ptr, writerBlock, sizeof(writerBlock)) == 0) {
            blockAddr = textStart + i;
            break;
        }
    }

    if (!blockAddr) {
        HookLog("WARNING: Exact rotational writer block not found; rotational gates disabled.");
        return false;
    }

    char buf[128];
    sprintf_s(buf, "Rotational writer block found at Starfield.exe+%X",
        (unsigned int)(blockAddr - moduleBase));
    HookLog(buf);

    bool ok = true;
    ok = InstallRollGate(blockAddr + 7) && ok;
    ok = InstallRdxDispGate(blockAddr + 15, 0x04, &g_vertStrafeBits, &g_vertStrafeOverrideEnabled, "Vertical strafe") && ok;
    ok = InstallRaxDispGate(blockAddr + 20, 0x60, &g_yawBits, &g_yawOverrideEnabled, "Yaw") && ok;
    ok = InstallRaxDispGate(blockAddr + 25, 0x64, &g_pitchBits, &g_pitchOverrideEnabled, "Pitch") && ok;

    if (ok) {
        HookLog("Validated roll/yaw/pitch gate hooks installed.");
    }
    return ok;
}

// ---- Install a 5-byte trampoline that captures RDI and stores in candidates ----
static bool InstallTrampoline5(uintptr_t targetAddr, const uint8_t* savedBytes, const char* label, bool isStore = false) {
    // Allocate trampoline within ±2GB
    uint8_t* trampMem = nullptr;
    uintptr_t allocBase = targetAddr > 0x70000000 ? targetAddr - 0x70000000 : 0x10000;
    for (uintptr_t addr = allocBase; addr < targetAddr + 0x70000000; addr += 0x10000) {
        trampMem = (uint8_t*)VirtualAlloc((void*)addr, 4096,
            MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (trampMem) break;
    }
    if (!trampMem) {
        HookLog("CRITICAL: Could not allocate trampoline for " + std::string(label));
        return false;
    }

    int idx = 0;

    // --- RED ZONE PROTECTION ---
    // sub rsp, 128
    trampMem[idx++] = 0x48; trampMem[idx++] = 0x81; trampMem[idx++] = 0xEC; 
    trampMem[idx++] = 0x80; trampMem[idx++] = 0x00; trampMem[idx++] = 0x00; trampMem[idx++] = 0x00;

    // push rax, push rcx, pushfq
    trampMem[idx++] = 0x50;
    trampMem[idx++] = 0x51;
    trampMem[idx++] = 0x9C;

    // --- Passive Check: if (!g_captureEnabled) skip EVERYTHING ---
    // mov rax, &g_captureEnabled
    trampMem[idx++] = 0x48; trampMem[idx++] = 0xB8;
    uintptr_t enabledAddr = (uintptr_t)&g_captureEnabled;
    memcpy(&trampMem[idx], &enabledAddr, 8); idx += 8;
    // cmp byte ptr [rax], 0
    trampMem[idx++] = 0x80; trampMem[idx++] = 0x38; trampMem[idx++] = 0x00;
    // je bypass_all (short jump to popfq)
    trampMem[idx++] = 0x74; 
    int bypassOffset = idx;
    trampMem[idx++] = 0x00; // placeholder

    // --- Sanity Check: if (rdi < 0x10000) skip all capture ---
    // cmp rdi, 0x10000
    trampMem[idx++] = 0x48; trampMem[idx++] = 0x81; trampMem[idx++] = 0xFF;
    trampMem[idx++] = 0x00; trampMem[idx++] = 0x00; trampMem[idx++] = 0x01; trampMem[idx++] = 0x00;
    // jl skip_capture (short jump to label)
    trampMem[idx++] = 0x7C;
    int totalSkipOffset = idx;
    trampMem[idx++] = 0x00; // placeholder for jump distance

    // mov rax, &g_capturedRDI
    trampMem[idx++] = 0x48; trampMem[idx++] = 0xB8;
    uintptr_t rdiAddr = (uintptr_t)&g_capturedRDI;
    memcpy(&trampMem[idx], &rdiAddr, 8); idx += 8;
    // mov [rax], rdi
    trampMem[idx++] = 0x48; trampMem[idx++] = 0x89; trampMem[idx++] = 0x38;

    // mov rax, &g_capturedTimestamp; mov rcx, [rax]; inc rcx; mov [rax], rcx
    trampMem[idx++] = 0x48; trampMem[idx++] = 0xB8;
    uintptr_t tsAddr = (uintptr_t)&g_capturedTimestamp;
    memcpy(&trampMem[idx], &tsAddr, 8); idx += 8;
    trampMem[idx++] = 0x48; trampMem[idx++] = 0x8B; trampMem[idx++] = 0x08;
    trampMem[idx++] = 0x48; trampMem[idx++] = 0xFF; trampMem[idx++] = 0xC1;
    trampMem[idx++] = 0x48; trampMem[idx++] = 0x89; trampMem[idx++] = 0x08;

    // Also store RDI in candidates array:
    // mov rax, &g_candidateCount
    trampMem[idx++] = 0x48; trampMem[idx++] = 0xB8;
    uintptr_t countAddr = (uintptr_t)&g_candidateCount;
    memcpy(&trampMem[idx], &countAddr, 8); idx += 8;
    // mov ecx, [rax]  (load current count)
    trampMem[idx++] = 0x8B; trampMem[idx++] = 0x08;
    // cmp ecx, 128
    trampMem[idx++] = 0x81; trampMem[idx++] = 0xF9; 
    trampMem[idx++] = 0x80; trampMem[idx++] = 0x00; trampMem[idx++] = 0x00; trampMem[idx++] = 0x00;
    // jge skip (skip if full) — 2 bytes for short jump
    trampMem[idx++] = 0x7D;
    int skipOffset = idx; // placeholder for jump distance
    trampMem[idx++] = 0x00; // will fill in

    // movsxd rcx, ecx (sign-extend for index)
    trampMem[idx++] = 0x48; trampMem[idx++] = 0x63; trampMem[idx++] = 0xC9;
    // push rax (save count ptr)
    trampMem[idx++] = 0x50;
    // mov rax, &g_candidates[0]
    trampMem[idx++] = 0x48; trampMem[idx++] = 0xB8;
    uintptr_t candAddr = (uintptr_t)&g_candidates[0];
    memcpy(&trampMem[idx], &candAddr, 8); idx += 8;
    // mov [rax + rcx*8], rdi
    trampMem[idx++] = 0x48; trampMem[idx++] = 0x89; trampMem[idx++] = 0x3C; trampMem[idx++] = 0xC8;
    // pop rax (restore count ptr)
    trampMem[idx++] = 0x58;
    // inc dword [rax]
    trampMem[idx++] = 0xFF; trampMem[idx++] = 0x00;

    // Labels/Jump offsets:
    trampMem[bypassOffset] = (uint8_t)(idx - bypassOffset - 1);
    trampMem[skipOffset]   = (uint8_t)(idx - skipOffset - 1);
    trampMem[totalSkipOffset] = (uint8_t)(idx - totalSkipOffset - 1);

    // popfq, pop rcx, pop rax
    trampMem[idx++] = 0x9D;
    trampMem[idx++] = 0x59;
    trampMem[idx++] = 0x58;

    // --- RED ZONE RESTORATION ---
    // add rsp, 128
    trampMem[idx++] = 0x48; trampMem[idx++] = 0x81; trampMem[idx++] = 0xC4; 
    trampMem[idx++] = 0x80; trampMem[idx++] = 0x00; trampMem[idx++] = 0x00; trampMem[idx++] = 0x00;

    // --- ABSOLUTE AUTHORITY: SILENCING Logic ---
    if (isStore) {
        // if (g_silenceEnabled) skip original bytes
        // mov rax, &g_silenceEnabled
        trampMem[idx++] = 0x48; trampMem[idx++] = 0xB8;
        uintptr_t silAddr = (uintptr_t)&g_silenceEnabled;
        memcpy(&trampMem[idx], &silAddr, 8); idx += 8;
        // cmp byte ptr [rax], 0
        trampMem[idx++] = 0x80; trampMem[idx++] = 0x38; trampMem[idx++] = 0x00;
        // jne skip_original (short jump past original bytes)
        trampMem[idx++] = 0x75;
        trampMem[idx++] = 0x05; // 5 bytes for the original instruction
    }

    // Replay original 5 bytes (May be skipped by silence logic)
    memcpy(&trampMem[idx], savedBytes, 5); idx += 5;

    // jmp back
    trampMem[idx++] = 0xE9;
    int32_t jmpBack = (int32_t)((targetAddr + 5) - ((uintptr_t)&trampMem[idx] + 4));
    memcpy(&trampMem[idx], &jmpBack, 4); idx += 4;

    // Patch original code with jmp to trampoline
    DWORD oldProtect;
    if (!VirtualProtect((void*)targetAddr, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        HookLog("CRITICAL: VirtualProtect failed for " + std::string(label));
        return false;
    }

    uint8_t* origCode = (uint8_t*)targetAddr;
    origCode[0] = 0xE9;
    int32_t jmpToTramp = (int32_t)((uintptr_t)trampMem - (targetAddr + 5));
    memcpy(&origCode[1], &jmpToTramp, 4);

    VirtualProtect((void*)targetAddr, 5, oldProtect, &oldProtect);

    // Record for persistent management
    HookRecord rec;
    rec.targetAddr = targetAddr;
    rec.isStore = isStore;
    memcpy(rec.origBytes, savedBytes, 5);
    memcpy(rec.trampJmp, origCode, 5);
    g_hookRegistry.push_back(rec);

    char buf[256];
    uintptr_t moduleBase = (uintptr_t)GetModuleHandle(NULL);
    sprintf_s(buf, "Hooked %s at Starfield.exe+%X",
        label, (unsigned int)(targetAddr - moduleBase));
    HookLog(buf);

    return true;
}

// ---- Public API ----
uintptr_t ThrottleHook::GetBasePtr() {
    return (uintptr_t)g_capturedRDI;
}

uintptr_t ThrottleHook::GetSourceBasePtr() {
    return (uintptr_t)g_capturedSourceR13;
}

uintptr_t ThrottleHook::GetWriterClusterBasePtr() {
    return (uintptr_t)g_capturedWriterCluster;
}

bool ThrottleHook::IsActive() {
    static int64_t s_lastCheckedCounter = 0;
    static std::chrono::steady_clock::time_point s_lastAlive = std::chrono::steady_clock::now();
    int64_t currentCounter = g_capturedTimestamp;
    auto now = std::chrono::steady_clock::now();
    if (currentCounter != s_lastCheckedCounter) {
        s_lastCheckedCounter = currentCounter;
        s_lastAlive = now;
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_lastAlive).count();
    return (g_capturedRDI != 0) && (elapsed < 2000);
}

bool ThrottleHook::IsInstalled() {
    return !g_hookRegistry.empty();
}

void ThrottleHook::Uninstall() {
    if (g_hookRegistry.empty()) return;

    for (auto it = g_hookRegistry.rbegin(); it != g_hookRegistry.rend(); ++it) {
        DWORD oldProtect = 0;
        if (!VirtualProtect((void*)it->targetAddr, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            continue;
        }
        memcpy((void*)it->targetAddr, it->origBytes, 5);
        FlushInstructionCache(GetCurrentProcess(), (void*)it->targetAddr, 5);
        VirtualProtect((void*)it->targetAddr, 5, oldProtect, &oldProtect);
    }

    g_hookRegistry.clear();
    HookLog("ThrottleHook uninstalled; original hook bytes restored.");
}

uintptr_t ThrottleHook::GetCandidate(int index) {
    if (index < 0 || index >= 128) return 0;
    return (uintptr_t)g_candidates[index];
}

int ThrottleHook::GetCandidateCount() {
    return (int)g_candidateCount;
}

void ThrottleHook::ClearCandidates() {
    g_candidateCount = 0;
    memset((void*)g_candidates, 0, sizeof(g_candidates));
}

void ThrottleHook::SetCaptureEnabled(bool enabled) {
    if (g_captureEnabled == enabled) return;
    g_captureEnabled = enabled;
    HookLog(enabled ? "DISCOVERY MODE ENABLED" : "DISCOVERY MODE DISABLED");
}

bool ThrottleHook::IsCaptureEnabled() {
    return g_captureEnabled;
}

void ThrottleHook::SetSilenceEnabled(bool enabled) {
    if (g_silenceEnabled == enabled) return;
    g_silenceEnabled = enabled;
    HookLog(enabled ? "ABSOLUTE SILENCE ACTIVE (Game inputs blocked)" : "SILENCE DEACTIVATED");
}

bool ThrottleHook::IsSilenceEnabled() {
    return g_silenceEnabled;
}

void ThrottleHook::SetRotationalOverride(float lateral, float yaw, float pitch, bool enabled, bool lateralEnabled, float vertical, bool verticalEnabled) {
    g_rollBits = FloatToBits(lateral);
    g_vertStrafeBits = FloatToBits(vertical);
    g_yawBits = FloatToBits(yaw);
    g_pitchBits = FloatToBits(pitch);
    g_rotOverrideEnabled = enabled ? 1 : 0;
    g_rollOverrideEnabled = (enabled && lateralEnabled) ? 1 : 0;
    g_vertStrafeOverrideEnabled = (enabled && verticalEnabled) ? 1 : 0;
    g_yawOverrideEnabled = enabled ? 1 : 0;
    g_pitchOverrideEnabled = enabled ? 1 : 0;
}

void ThrottleHook::SetManualLaneOverride(uintptr_t offset, float value, bool enabled) {
    uint32_t bits = FloatToBits(value);

    g_rollOverrideEnabled = 0;
    g_vertStrafeOverrideEnabled = 0;
    g_yawOverrideEnabled = 0;
    g_pitchOverrideEnabled = 0;

    if (!enabled) {
        g_rotOverrideEnabled = 0;
        return;
    }

    if (offset == 0x58) {
        g_rollBits = bits;
        g_rollOverrideEnabled = 1;
    } else if (offset == 0x5C) {
        g_vertStrafeBits = bits;
        g_vertStrafeOverrideEnabled = 1;
    } else if (offset == 0x60) {
        g_yawBits = bits;
        g_yawOverrideEnabled = 1;
    } else if (offset == 0x64) {
        g_pitchBits = bits;
        g_pitchOverrideEnabled = 1;
    } else {
        g_rotOverrideEnabled = 0;
        return;
    }

    g_rotOverrideEnabled = 1;
}

int ThrottleHook::GetManualGateCount() {
    return g_manualGateCount;
}

uintptr_t ThrottleHook::GetManualGateAddress(int index) {
    if (index < 0 || index >= g_manualGateCount) return 0;
    return g_manualGateAddress[index];
}

uintptr_t ThrottleHook::GetManualGateOffset(int index) {
    if (index < 0 || index >= g_manualGateCount) return 0;
    return g_manualGateOffset[index];
}

void ThrottleHook::SetManualGateOverride(int index, float value, bool enabled) {
    for (int i = 0; i < g_manualGateCount && i < MAX_MANUAL_CAMERA_GATES; ++i) {
        g_manualGateActive[i] = 0;
    }

    if (!enabled) return;
    if (index < 0 || index >= g_manualGateCount || index >= MAX_MANUAL_CAMERA_GATES) return;

    g_manualGateBits[index] = FloatToBits(value);
    g_manualGateActive[index] = 1;
}

static bool HasCompanion6C(uintptr_t addr, uintptr_t textStart, size_t textSize) {
    uint8_t* p = (uint8_t*)addr;
    for (int i = -64; i < 64; i++) {
        uintptr_t target = (uintptr_t)(p + i);
        if (target < textStart || target >= (textStart + textSize - 2)) continue;
        if (p[i] == 0x47 && p[i+1] == 0x6C) return true;
        if (p[i] == 0x4F && p[i+1] == 0x6C) return true;
    }
    return false;
}

bool ThrottleHook::Install() {
    HookLog("=== AbsoluteHOTAS hook installation ===");

    uintptr_t moduleBase = (uintptr_t)GetModuleHandle(NULL);
    int hookCount = 0;

    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!GetTextSection(textStart, textSize)) {
        HookLog("CRITICAL: Could not find .text section!");
        return false;
    }

    InstallRotationalGates(textStart, textSize, moduleBase);
    InstallManualCameraGates(textStart, textSize, moduleBase);

    // ======================================================
    // ======================================================
    // Safe Precision Strategy: Only 5-byte instructions
    // Specifically VMOVSS/MOVSS with +6C companion checks.
    // Includes a sanity check to ignore NULL/small pointers.
    // ======================================================
    
    struct Pattern {
        std::vector<uint8_t> bytes;
        std::string mask;
        std::string name;
    };

    std::vector<Pattern> patterns = {
        // --- Pitch (64) ---
        {{0xC5, 0x00, 0x11, 0x00, 0x64}, "x?x?x", "VMOVSS-STORE-64"},
        {{0xF3, 0x0F, 0x11, 0x00, 0x64}, "xxxx?x", "MOVSS-STORE-64"},
        {{0xC5, 0x00, 0x58, 0x00, 0x64}, "x?x?x", "VADDSS-STORE-64"}, // Catching arithmetic writes
        
        // --- Yaw (60) ---
        {{0xC5, 0x00, 0x11, 0x00, 0x60}, "x?x?x", "VMOVSS-STORE-60"},
        {{0xF3, 0x0F, 0x11, 0x00, 0x60}, "xxxx?x", "MOVSS-STORE-60"},
        {{0xC5, 0x00, 0x58, 0x00, 0x60}, "x?x?x", "VADDSS-STORE-60"},

        // --- Roll / Lat Strafe (58) ---
        {{0xC5, 0x00, 0x11, 0x00, 0x58}, "x?x?x", "VMOVSS-STORE-58"},
        {{0xF3, 0x0F, 0x11, 0x00, 0x58}, "xxxx?x", "MOVSS-STORE-58"},
        
        // --- Vert Strafe (5C) ---
        {{0xC5, 0x00, 0x11, 0x00, 0x5C}, "x?x?x", "VMOVSS-STORE-5C"},
        {{0xF3, 0x0F, 0x11, 0x00, 0x5C}, "xxxx?x", "MOVSS-STORE-5C"},

        // --- Throttle (68) ---
        {{0xC5, 0x00, 0x11, 0x00, 0x68}, "x?x?x", "VMOVSS-STORE-68"},
        {{0xF3, 0x0F, 0x11, 0x00, 0x68}, "xxxx?x", "MOVSS-STORE-68"},

        // --- Aligned Block Stores (Sometimes used for shared structs) ---
        {{0xC5, 0x00, 0x29, 0x00, 0x50}, "x?x?x", "VMOVAPS-STORE-VEC50"},
        {{0xC5, 0x00, 0x29, 0x00, 0x60}, "x?x?x", "VMOVAPS-STORE-VEC60"},

        // --- Telemetry Loads (Discovery) ---
        {{0xC5, 0x00, 0x10, 0x00, 0x58}, "x?x?x", "VMOVSS-LOAD-58"},
        {{0xC5, 0x00, 0x10, 0x00, 0x64}, "x?x?x", "VMOVSS-LOAD-64"},
        {{0xC5, 0x00, 0x10, 0x00, 0x68}, "x?x?x", "VMOVSS-LOAD-68"}
    };

    const uint8_t pat6C_VEX[] = { 0xC5, 0xFA, 0x11, 0x47, 0x6C };
    const uint8_t pat6C_STD[] = { 0xF3, 0x0F, 0x11, 0x47, 0x6C };

    for (const auto& pat : patterns) {
        for (size_t i = 0; i < textSize - 10; i++) {
            uint8_t* ptr = (uint8_t*)(textStart + i);
            
            bool match = true;
            for (size_t j = 0; j < pat.bytes.size(); j++) {
                if (pat.mask[j] == 'x' && ptr[j] != pat.bytes[j]) {
                    match = false;
                    break;
                }
            }

            if (match) {
                uintptr_t offset = (textStart + i) - moduleBase;
                if (offset < 0x12B0000 || offset > 0x12C0000) continue;

                // --- RELAXED 6DOF SCANNER (Beta 1.6) ---
                // Throttle (Offset 68) still requires the +6C companion for safety.
                // Pitch/Yaw/Roll/Strafe (58, 5C, 60, 64) are hooked on-sight within our target range.
                bool isThrottle = (pat.name.find("-68") != std::string::npos);
                bool hasCompanion = HasCompanion6C(textStart + i, textStart, textSize);

                if (hasCompanion || !isThrottle) {
                    char label[64];
                    sprintf_s(label, "%s-%X", pat.name.c_str(), (unsigned int)offset);
                    
                    uint8_t orig[5];
                    memcpy(orig, ptr, 5);
                    
                    bool isStore = (pat.name.find("STORE") != std::string::npos);
                    if (InstallTrampoline5(textStart + i, orig, label, isStore)) {
                        hookCount++;
                    }
                }
            }
        }
    }

    char summary[128];
    sprintf_s(summary, "=== Hook installation complete: %d hooks total ===", hookCount);
    HookLog(summary);

    return hookCount > 0;
}
