#include "PCH.h"

#include "AbsoluteControlSubscriber.h"
#include "UIHook.h"
#include "UIHookInternal.h"

#include "RuntimePaths.h"

#include <MinHook.h>

#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace UIHook::Detail {

std::atomic<bool> g_isOpen{false};
std::atomic<bool> g_available{false};
std::atomic<bool> g_faulted{false};
CloseGuardCallback g_closeGuardCallback = nullptr;
std::atomic<bool> g_initialized{false};
std::atomic<bool> g_logNextOverlaySubmit{false};
std::mutex g_initMutex;
DrawCallback g_drawCallback = nullptr;
RECT g_savedClipRect{};
bool g_hadClipRect = false;

ID3D12Device* g_d3dDevice = nullptr;
ID3D12DescriptorHeap* g_srvDescHeap = nullptr;
ID3D12CommandAllocator** g_cmdAllocators = nullptr;
ID3D12GraphicsCommandList* g_cmdList = nullptr;
std::atomic<ID3D12CommandQueue*> g_gameCommandQueue{nullptr};
std::atomic<ID3D12CommandQueue*> g_recentDirectQueue{nullptr};

std::mutex g_swapChainQueuesMutex;
std::unordered_map<IUnknown*, ID3D12CommandQueue*> g_swapChainQueues;

ID3D12Fence* g_fence = nullptr;
HANDLE g_fenceEvent = nullptr;
UINT64 g_fenceValue = 0;
UINT64* g_frameFenceValues = nullptr;
UINT g_numBackBuffers = 0;
ID3D12Resource** g_backBuffers = nullptr;
D3D12_CPU_DESCRIPTOR_HANDLE* g_rtvHandles = nullptr;
ID3D12DescriptorHeap* g_rtvDescHeap = nullptr;
DXGI_FORMAT g_backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
UINT g_lastSwapWidth = 0;
UINT g_lastSwapHeight = 0;
IDXGISwapChain* g_targetSwapChain = nullptr;

HWND g_hWnd = nullptr;
WNDPROC g_origWndProc = nullptr;

PresentFn g_origPresent = nullptr;
Present1Fn g_origPresent1 = nullptr;
ResizeBuffersFn g_origResizeBuffers = nullptr;
ExecuteCommandListsFn g_origExecuteCommandLists = nullptr;
CreateSwapChainForHwndFn g_origCreateSwapChainForHwnd = nullptr;
void* g_presentTarget = nullptr;
void* g_present1Target = nullptr;
void* g_resizeBuffersTarget = nullptr;

thread_local bool g_inOverlaySubmit = false;

void UILog(const std::string& msg) {
    // D3D12 hook/overlay setup and errors — gated by bEnableLog.
    RuntimePaths::Log("[UIHook]", msg);
}

}  // namespace UIHook::Detail

namespace UIHook {

using namespace Detail;

static bool IsExecutableAddress(void* address) {
    if (!address) return false;
    MEMORY_BASIC_INFORMATION memory{};
    if (!VirtualQuery(address, &memory, sizeof(memory)) || memory.State != MEM_COMMIT) {
        return false;
    }
    const DWORD protection = memory.Protect & 0xFF;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ
        || protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

static void* SelectCooperativeHookTarget(const char* label, void* canonicalTarget) {
    std::string firstBytes;
    if (!LooksHooked(canonicalTarget, firstBytes)) return canonicalTarget;

    void* downstreamTarget = ResolveInitialJump(canonicalTarget);
    if (!downstreamTarget || downstreamTarget == canonicalTarget
        || !IsExecutableAddress(downstreamTarget)) {
        UILog(std::format(
            "WARNING: {} has an existing detour, but its destination is not a usable hook target; using the canonical entry.",
            label));
        return canonicalTarget;
    }

    UILog(std::format(
        "Compatibility chain for {} will hook the existing detour destination 0x{:X} ({}) instead of competing for canonical entry 0x{:X}.",
        label, reinterpret_cast<uintptr_t>(downstreamTarget),
        ModuleForAddress(downstreamTarget), reinterpret_cast<uintptr_t>(canonicalTarget)));
    return downstreamTarget;
}

static bool CreateCooperativeHook(
    const char* label,
    void* canonicalTarget,
    void* preferredTarget,
    void* detour,
    void** original,
    void** actualTarget)
{
    void* selectedTarget = preferredTarget;
    MH_STATUS status = MH_CreateHook(selectedTarget, detour, original);
    if (status != MH_OK && selectedTarget != canonicalTarget) {
        UILog(std::format(
            "WARNING: Could not hook the existing {} detour destination (MinHook status {}); falling back to the canonical entry.",
            label, static_cast<int>(status)));
        selectedTarget = canonicalTarget;
        status = MH_CreateHook(selectedTarget, detour, original);
    }
    if (status != MH_OK) {
        UILog(std::format("ERROR: Failed to create {} hook (MinHook status {}).",
            label, static_cast<int>(status)));
        return false;
    }

    if (actualTarget) *actualTarget = selectedTarget;
    UILog(std::format("Prepared {} hook at 0x{:X}{}.", label,
        reinterpret_cast<uintptr_t>(selectedTarget),
        selectedTarget == canonicalTarget ? " (canonical entry)" : " (existing-hook destination)"));
    return true;
}


bool Install() {
    g_available.store(false, std::memory_order_relaxed);
    g_faulted.store(false, std::memory_order_relaxed);
    UILog("Installing D3D12 hooks...");

    void* pPresent = nullptr;
    void* pPresent1 = nullptr;
    void* pResizeBuffers = nullptr;
    void* pExecuteCommandLists = nullptr;
    void* pCreateSwapChainForHwnd = nullptr;

    if (!GetVtablePointers(&pPresent, &pPresent1, &pResizeBuffers, &pExecuteCommandLists, &pCreateSwapChainForHwnd)) {
        UILog("ERROR: Vtable discovery failed. UI overlay disabled.");
        return false;
    }
    // Diagnose prior render-chain hooks BEFORE we patch anything ourselves.
    // Another layer hooking these entry points first is the common cause of
    // "cursor works but the overlay never renders." We still install normally;
    // this just turns a silent failure into an actionable log line.
    {
        struct HookTarget { const char* name; void* fn; };
        const HookTarget targets[] = {
            { "IDXGISwapChain::Present",                 pPresent },
            { "IDXGISwapChain1::Present1",               pPresent1 },
            { "IDXGISwapChain::ResizeBuffers",           pResizeBuffers },
            { "ID3D12CommandQueue::ExecuteCommandLists", pExecuteCommandLists },
            { "IDXGIFactory2::CreateSwapChainForHwnd",   pCreateSwapChainForHwnd },
        };
        int priorHooks = 0;
        std::string firstBytes;
        for (const auto& t : targets) {
            if (LooksHooked(t.fn, firstBytes)) {
                ++priorHooks;
                void* destination = ResolveInitialJump(t.fn);
                UILog(std::string("Note: render entry already hooked by another layer: ") + t.name
                    + " (first bytes: " + firstBytes + ", destination module: "
                    + ModuleForAddress(destination) + ").");
            }
        }
        if (priorHooks > 0) {
            UILog("Note: " + std::to_string(priorHooks) + " D3D12/DXGI render entry point(s) are already hooked "
                  "by another layer. AbsoluteHOTAS will attempt to chain behind the existing detours; Steam, "
                  "Discord, RTSS, capture tools, and frame-generation layers commonly share these methods. If "
                  "the workbench still does not render, disable frame generation (NVIDIA Smooth Motion / DLSS-G, "
                  "AMD AFMF / FSR Frame Generation), capture/overlay tools, or driver filters. "
                  "See docs/reference/overlay-hook-compatibility.md.");
        }
    }

    // If another overlay owns the public DXGI/D3D12 prologue, chaining behind its
    // existing detour is more durable than replacing that prologue and racing its
    // self-repair logic. Canonical entry hooking remains the default for an
    // unclaimed method and the fallback if a downstream stub cannot be detoured.
    void* presentHookTarget = SelectCooperativeHookTarget("Present", pPresent);
    void* present1HookTarget = SelectCooperativeHookTarget("Present1", pPresent1);
    void* resizeBuffersHookTarget = SelectCooperativeHookTarget("ResizeBuffers", pResizeBuffers);
    void* executeCommandListsHookTarget = SelectCooperativeHookTarget(
        "ExecuteCommandLists", pExecuteCommandLists);
    void* createSwapChainHookTarget = SelectCooperativeHookTarget(
        "CreateSwapChainForHwnd", pCreateSwapChainForHwnd);

    if (MH_Initialize() != MH_OK) {
        UILog("ERROR: MH_Initialize failed.");
        return false;
    }

    // Primary queue capture — fires at swap chain creation, before other injectors
    // can create private helper queues that would fool ExecuteCommandLists.
    if (!CreateCooperativeHook(
        "CreateSwapChainForHwnd", pCreateSwapChainForHwnd, createSwapChainHookTarget,
        reinterpret_cast<void*>(&HookedCreateSwapChainForHwnd),
        reinterpret_cast<void**>(&g_origCreateSwapChainForHwnd), nullptr)) {
        MH_Uninitialize();
        return false;
    }

    if (!CreateCooperativeHook(
        "Present", pPresent, presentHookTarget, reinterpret_cast<void*>(&HookedPresent),
        reinterpret_cast<void**>(&g_origPresent), &g_presentTarget)) {
        MH_Uninitialize();
        return false;
    }

    // Present1 uses the same implementation chain for the active DXGI layer, so
    // this hook covers swapchains presenting through that layer. Non-fatal if it
    // fails — Present-only hooking still works for the common case.
    if (!CreateCooperativeHook(
        "Present1", pPresent1, present1HookTarget, reinterpret_cast<void*>(&HookedPresent1),
        reinterpret_cast<void**>(&g_origPresent1), &g_present1Target)) {
        UILog("WARNING: Failed to create Present1 hook (Present-only overlay still active).");
    }

    if (!CreateCooperativeHook(
        "ResizeBuffers", pResizeBuffers, resizeBuffersHookTarget,
        reinterpret_cast<void*>(&HookedResizeBuffers),
        reinterpret_cast<void**>(&g_origResizeBuffers), &g_resizeBuffersTarget)) {
        MH_Uninitialize();
        return false;
    }

    // Fallback queue capture + re-entrancy guard (see HookedExecuteCommandLists).
    if (!CreateCooperativeHook(
        "ExecuteCommandLists", pExecuteCommandLists, executeCommandListsHookTarget,
        reinterpret_cast<void*>(&HookedExecuteCommandLists),
        reinterpret_cast<void**>(&g_origExecuteCommandLists), nullptr)) {
        MH_Uninitialize();
        return false;
    }

    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        UILog("ERROR: Failed to enable hooks.");
        MH_Uninitialize();
        return false;
    }

    g_available.store(true, std::memory_order_release);
    UILog("D3D12 hooks installed successfully. Press Ctrl+Alt+B to toggle overlay.");
    return true;
}

void Shutdown() {
    g_available.store(false, std::memory_order_release);
    if (g_isOpen.exchange(false) && g_initialized.load(std::memory_order_acquire)) {
        RestoreGameCursorState();
    }
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();

    if (g_initialized.load()) {
        WaitForGpu();
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }

    // Restore WndProc
    if (g_origWndProc && g_hWnd) {
        SetWindowLongPtrW(g_hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_origWndProc));
    }

    CleanupRenderTargets();
    if (g_rtvDescHeap) { g_rtvDescHeap->Release(); g_rtvDescHeap = nullptr; }
    if (g_srvDescHeap) { g_srvDescHeap->Release(); g_srvDescHeap = nullptr; }
    if (g_cmdList)      { g_cmdList->Release();      g_cmdList = nullptr; }
    if (g_cmdAllocators) {
        for (UINT i = 0; i < g_numBackBuffers; i++) {
            if (g_cmdAllocators[i]) g_cmdAllocators[i]->Release();
        }
        delete[] g_cmdAllocators;
        g_cmdAllocators = nullptr;
    }
    if (g_fence)        { g_fence->Release();        g_fence = nullptr; }
    if (g_fenceEvent)   { CloseHandle(g_fenceEvent); g_fenceEvent = nullptr; }
    if (g_d3dDevice)    { g_d3dDevice->Release();    g_d3dDevice = nullptr; }

    delete[] g_backBuffers;  g_backBuffers = nullptr;
    delete[] g_rtvHandles;   g_rtvHandles = nullptr;
    delete[] g_frameFenceValues; g_frameFenceValues = nullptr;
    g_gameCommandQueue.store(nullptr, std::memory_order_relaxed);
    g_recentDirectQueue.store(nullptr, std::memory_order_relaxed);
    ReleaseSwapChainQueueAssociations();
}

void ToggleUI() {
    if (!g_available.load(std::memory_order_acquire)) return;
    if (g_faulted.load(std::memory_order_acquire)) {
        UILog("Workbench toggle ignored: renderer disabled for this session; manual configuration remains active.");
        return;
    }

    if (g_hWnd && GetCurrentThreadId() != GetWindowThreadProcessId(g_hWnd, nullptr)) {
        PostMessageW(g_hWnd, GetToggleUIMessage(), 0, 0);
        return;
    }

    bool wasOpen = g_isOpen.load();
    if (!wasOpen && AbsoluteControlSubscriber::IsHostOpen()) {
        UILog("Workbench open ignored while Absolute Control owns the editing frontend.");
        return;
    }
    if (wasOpen && g_closeGuardCallback && !g_closeGuardCallback()) return;
    bool nowOpen = !wasOpen;
    g_isOpen.store(nowOpen);

    if (nowOpen) {
        g_logNextOverlaySubmit.store(true, std::memory_order_relaxed);
        UILog(std::format("Overlay toggle requested: OPEN (initialized={}, swapChain=0x{:X}, queue=0x{:X}).",
            g_initialized.load(std::memory_order_relaxed), reinterpret_cast<uintptr_t>(g_targetSwapChain),
            reinterpret_cast<uintptr_t>(g_gameCommandQueue.load(std::memory_order_relaxed))));
        if (!g_initialized.load(std::memory_order_acquire)) {
            UILog("Workbench renderer initialization deferred to the next game Present.");
            return;
        }
        // --- Opening the overlay ---
        // Save the game's cursor clip rect so we can restore it on close
        g_hadClipRect = (GetClipCursor(&g_savedClipRect) != 0);
        ClipCursor(nullptr);  // Unclip so the mouse can reach our UI

        // Show the system cursor (games typically hide it)
        // ShowCursor uses a reference counter; keep incrementing until visible
        while (ShowCursor(TRUE) < 0) {}

        // Tell ImGui to draw its own software cursor (reliable in fullscreen)
        if (g_initialized.load()) {
            ImGuiIO& io = ImGui::GetIO();
            io.MouseDrawCursor = true;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        }
    } else {
        UILog("Overlay toggle requested: CLOSE.");
        if (!g_initialized.load(std::memory_order_acquire)) return;
        // --- Closing the overlay ---
        // Hide the software cursor
        if (g_initialized.load()) {
            ImGuiIO& io = ImGui::GetIO();
            io.MouseDrawCursor = false;
        }

        RestoreGameCursorState();
    }
}

bool IsUIOpen() {
    return g_available.load(std::memory_order_relaxed)
        && !g_faulted.load(std::memory_order_relaxed)
        && g_isOpen.load(std::memory_order_relaxed);
}

void SetDrawCallback(DrawCallback cb) {
    g_drawCallback = cb;
}

void SetCloseGuardCallback(CloseGuardCallback cb) {
    g_closeGuardCallback = cb;
}

}  // namespace UIHook
