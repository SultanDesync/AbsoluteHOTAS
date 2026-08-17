#include "PCH.h"

#include "ThrottleHook.h"
#include "RuntimePaths.h"
#include <windows.h>
#include <chrono>

// ---- Static member definitions ----
std::atomic<uintptr_t> ThrottleHook::s_basePtr{ 0 };
std::atomic<int64_t>   ThrottleHook::s_lastHookTimestamp{ 0 };
uint8_t*               ThrottleHook::s_trampoline = nullptr;
uintptr_t              ThrottleHook::s_hookAddress = 0;

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
static volatile bool g_silence6CEnabled = false; // Per-offset: independently silence +0x6C stores
static volatile uint8_t g_rotOverrideEnabled = 0;
static volatile uint8_t g_rollOverrideEnabled = 0;
static volatile uint8_t g_vertStrafeOverrideEnabled = 0;
static volatile uint8_t g_yawOverrideEnabled = 0;
static volatile uint8_t g_pitchOverrideEnabled = 0;
static volatile uint32_t g_rollBits = 0;
static volatile uint32_t g_vertStrafeBits = 0;
static volatile uint32_t g_yawBits = 0;
static volatile uint32_t g_pitchBits = 0;
static std::atomic<bool> g_externalMouseSteeringOwner{false};
static std::atomic<bool> g_rotationalWriterHookInstalled{false};



// ---- Source-object aim injection ----
static volatile uint8_t  g_sourceAimEnabled  = 0;
static volatile uint32_t g_sourceAimYawBits  = 0;
static volatile uint32_t g_sourceAimPitchBits = 0;

// Guard flag: when true, reverse override owns the vertical strafe gate (+0x5C).
// SetRotationalOverride must not touch g_vertStrafeOverrideEnabled or
// g_vertStrafeBits while this flag is set.
static volatile uint8_t g_reverseOwnsVertStrafe = 0;


// ---- Hook Registry for Persistent Silencing ----
struct HookRecord {
    uintptr_t targetAddr;
    uint8_t origBytes[5];
    uint8_t trampJmp[5];
    bool isStore; // Identify if this instruction writes to memory
};
static std::vector<HookRecord> g_hookRegistry;

// ---- Logging helper ----
static void HookLog(const std::string& msg) {
    RuntimePaths::Log("[ThrottleHook]", msg);
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
static bool InstallTrampoline5(uintptr_t targetAddr, const uint8_t* savedBytes, const char* label, bool isStore = false, volatile bool* silenceFlag = nullptr) {
    // Allocate trampoline within ±2GB
    uint8_t* trampMem = AllocateNear(targetAddr);
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
        // Use per-hook silence flag if provided, otherwise fall back to global
        volatile bool* silAddr = silenceFlag ? silenceFlag : &g_silenceEnabled;
        // if (*silAddr) skip original bytes
        // mov rax, silAddr
        trampMem[idx++] = 0x48; trampMem[idx++] = 0xB8;
        uintptr_t silAddrVal = (uintptr_t)silAddr;
        memcpy(&trampMem[idx], &silAddrVal, 8); idx += 8;
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
uintptr_t ThrottleHook::GetSourceBasePtr() {
    return (uintptr_t)g_capturedSourceR13;
}

uintptr_t ThrottleHook::GetWriterClusterPtr() {
    return (uintptr_t)g_capturedWriterCluster;
}

void ThrottleHook::SetExternalMouseSteeringOwner(bool active) {
    g_externalMouseSteeringOwner.store(active, std::memory_order_release);
    if (active) {
        g_yawOverrideEnabled = 0;
        g_pitchOverrideEnabled = 0;
        g_sourceAimEnabled = 0;
    }
}

bool ThrottleHook::ExternalMouseSteeringOwnerActive() {
    return g_externalMouseSteeringOwner.load(std::memory_order_acquire);
}

bool ThrottleHook::RotationalWriterHookInstalled() {
    return g_rotationalWriterHookInstalled.load(std::memory_order_acquire);
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
    if (index < 0 || index >= MAX_CANDIDATES) return 0;
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
}

void ThrottleHook::SetSilenceEnabled(bool enabled) {
    if (g_silenceEnabled == enabled) return;
    g_silenceEnabled = enabled;
}

void ThrottleHook::SetSilence6CEnabled(bool enabled) {
    if (g_silence6CEnabled == enabled) return;
    g_silence6CEnabled = enabled;
}

void ThrottleHook::SetRotationalOverride(float lateral, float yaw, float pitch, bool enabled, bool lateralEnabled, float vertical, bool verticalEnabled, bool yawEnabled, bool pitchEnabled) {
    g_rollBits = FloatToBits(lateral);
    g_yawBits = FloatToBits(yaw);
    g_pitchBits = FloatToBits(pitch);
    g_rotOverrideEnabled = enabled ? 1 : 0;
    g_rollOverrideEnabled = (enabled && lateralEnabled) ? 1 : 0;
    const bool externalMouseOwner = ExternalMouseSteeringOwnerActive();
    g_yawOverrideEnabled = (enabled && yawEnabled && !externalMouseOwner) ? 1 : 0;
    g_pitchOverrideEnabled = (enabled && pitchEnabled && !externalMouseOwner) ? 1 : 0;

    // Vertical strafe lane: skip if reverse override owns it.
    if (!g_reverseOwnsVertStrafe) {
        g_vertStrafeBits = FloatToBits(vertical);
        g_vertStrafeOverrideEnabled = (enabled && verticalEnabled) ? 1 : 0;
    }
}

// ---- Source-object aim injection ----
void ThrottleHook::SetSourceObjectAim(float yaw, float pitch, bool enabled) {
    if (ExternalMouseSteeringOwnerActive()) {
        enabled = false;
    }
    g_sourceAimYawBits   = FloatToBits(yaw);
    g_sourceAimPitchBits = FloatToBits(pitch);
    g_sourceAimEnabled   = enabled ? 1 : 0;

    if (!enabled) return;

    uintptr_t src = g_capturedSourceR13;
    if (!src) return;

    // Time-based blending: compute dt since last call to produce smooth
    // motion regardless of poll rate vs game frame rate mismatch.
    static auto s_lastAimTime = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - s_lastAimTime).count();
    dt = std::clamp(dt, 0.0001f, 0.1f); // survive hitches
    s_lastAimTime = now;

    // Chase rate: how fast we blend toward the target. Higher = snappier.
    // At 60.0f, we reach ~95% of target in ~50ms which is perceptually instant
    // but spread across multiple game frames to avoid single-frame spikes.
    constexpr float kChaseRate = 60.0f;
    float alpha = 1.0f - std::exp(-kChaseRate * dt);

    // Write directly to the source object mouse accumulator lanes.
    // +0x4C = yaw mouse accumulator, +0x50 = pitch mouse accumulator (scale: -200.0..+200.0).
    // These work regardless of controller mode, unlike the gamepad input lanes (+0x44/+0x48).
    // Uses exponential chase blending to interpolate smoothly and avoid frame-skip judder.
    // Guarded with SEH to survive stale pointers between reacquire cycles.
    __try {
        float curYaw   = *(volatile float*)(src + 0x4C);
        float curPitch = *(volatile float*)(src + 0x50);

        float newYaw   = curYaw   + alpha * (yaw   - curYaw);
        float newPitch = curPitch + alpha * (pitch - curPitch);

        // Clamp to safe accumulator range
        newYaw   = std::clamp(newYaw,   -400.0f, 400.0f);
        newPitch = std::clamp(newPitch, -400.0f, 400.0f);

        *(float*)(src + 0x4C) = newYaw;
        *(float*)(src + 0x50) = newPitch;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

bool ThrottleHook::IsSourcePtrValid() {
    return g_capturedSourceR13 != 0;
}

void ThrottleHook::SetReverseOverride(bool enabled) {
    if (enabled) {
        // Claim the vertical strafe gate for reverse — blocks SetRotationalOverride
        g_reverseOwnsVertStrafe = 1;

        // 1. Lock the vertical strafe gate to -1.0
        g_vertStrafeBits = FloatToBits(-1.0f);
        g_vertStrafeOverrideEnabled = 1;

        // 2. Write upstream decel intent to source+0x3C
        uintptr_t src = g_capturedSourceR13;
        if (src) {
            __try {
                *(volatile float*)(src + 0x3C) = -1.0f;
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    } else {
        // Release the vertical strafe gate — SetRotationalOverride can reclaim it.
        // Only clear the override flag if reverse was actually owning the lane;
        // otherwise SetRotationalOverride already set it this frame and we must
        // not clobber it.
        const bool wasReverseActive = g_reverseOwnsVertStrafe != 0;
        if (wasReverseActive) {
            g_vertStrafeOverrideEnabled = 0;
        }
        g_reverseOwnsVertStrafe = 0;

        // Clear upstream decel intent ONLY on the reverse-active -> inactive edge.
        // The neutral-stick throttle branch calls SetReverseOverride(false) every
        // tick, so writing +0x3C unconditionally zero-stomps this shared source-
        // object lane at poll rate. On foot (with the captured pointer still live)
        // that clobbers the game's own use of the lane and cancels sprint. Once
        // reverse is released, leave the lane to the game.
        if (wasReverseActive) {
            uintptr_t src = g_capturedSourceR13;
            if (src) {
                __try {
                    *(volatile float*)(src + 0x3C) = 0.0f;
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
        }
    }
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
    HookLog(std::format("=== {} {} hook installation ===", Plugin::Name, Plugin::VersionString));

    uintptr_t moduleBase = (uintptr_t)GetModuleHandle(NULL);
    int hookCount = 0;

    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!GetTextSection(textStart, textSize)) {
        HookLog("CRITICAL: Could not find .text section!");
        return false;
    }

    g_rotationalWriterHookInstalled.store(
        InstallRotationalGates(textStart, textSize, moduleBase),
        std::memory_order_release);


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

        // --- Throttle Effective (6C) — independently silenced via g_silence6CEnabled ---
        {{0xC5, 0x00, 0x11, 0x00, 0x6C}, "x?x?x", "VMOVSS-STORE-6C"},
        {{0xF3, 0x0F, 0x11, 0x00, 0x6C}, "xxxx?x", "MOVSS-STORE-6C"},

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

                // --- RELAXED 6DOF SCANNER ---
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
                    bool is6C = (pat.name.find("-6C") != std::string::npos);
                    volatile bool* silFlag = (isStore && is6C) ? &g_silence6CEnabled : nullptr;
                    if (InstallTrampoline5(textStart + i, orig, label, isStore, silFlag)) {
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
