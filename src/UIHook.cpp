#include "PCH.h"

#include "UIHook.h"
#include "RuntimePaths.h"

// MinHook
#include <MinHook.h>

// ImGui
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>

// D3D12 / DXGI
#include <d3d12.h>
#include <dxgi1_4.h>

#include <atomic>
#include <mutex>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

// Forward declare the Win32 ImGui WndProc handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void UILog(const std::string& msg) {
    // D3D12 hook/overlay setup and errors — gated by bEnableLog.
    RuntimePaths::Log("[UIHook]", msg);
}

// ============================================================================
// State
// ============================================================================
static std::atomic<bool>   g_isOpen{ false };
static std::atomic<bool>   g_initialized{ false };
static std::mutex          g_initMutex;
static UIHook::DrawCallback g_drawCallback = nullptr;
static RECT                g_savedClipRect{};
static bool                g_hadClipRect = false;

// D3D12 resources owned by us
static ID3D12Device*                g_d3dDevice = nullptr;
static ID3D12DescriptorHeap*        g_srvDescHeap = nullptr;
static ID3D12CommandAllocator**     g_cmdAllocators = nullptr;
static ID3D12GraphicsCommandList*   g_cmdList = nullptr;

// The game's command queue — captured via CreateSwapChainForHwnd (primary) or
// ExecuteCommandLists (fallback). Held as a non-owning reference; the game retains ownership.
static std::atomic<ID3D12CommandQueue*> g_gameCommandQueue{nullptr};

// Fence for GPU synchronization
static ID3D12Fence*                 g_fence = nullptr;
static HANDLE                       g_fenceEvent = nullptr;
static UINT64                       g_fenceValue = 0;

// Per-frame render targets (dynamically sized)
static UINT                         g_numBackBuffers = 0;
static ID3D12Resource**             g_backBuffers = nullptr;
static D3D12_CPU_DESCRIPTOR_HANDLE* g_rtvHandles = nullptr;
static ID3D12DescriptorHeap*        g_rtvDescHeap = nullptr;
static DXGI_FORMAT                  g_backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
static UINT                         g_lastSwapWidth = 0;
static UINT                         g_lastSwapHeight = 0;
// Non-owning pointer to the swapchain our current render targets were built from.
// Under frame generation / proxy swapchains the presented swapchain can differ
// from the one we initialized on; we rebind targets to whichever is presenting.
static IDXGISwapChain*              g_targetSwapChain = nullptr;

// WndProc
static HWND    g_hWnd = nullptr;
static WNDPROC g_origWndProc = nullptr;

// Hook function pointers
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
// IDXGISwapChain1::Present1 (vtable index 22). Games and injected layers (capture
// suites, frame-gen/proxy swapchains) may present through Present1 instead of
// Present; hooking only Present would miss them, leaving the overlay invisible.
using Present1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1* pSwapChain, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS* pPresentParameters);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
using ExecuteCommandListsFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue* pQueue, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists);
// IDXGIFactory2::CreateSwapChainForHwnd — pDevice (2nd arg) is required by D3D12 spec to be
// the ID3D12CommandQueue that the swap chain uses for presentation. This gives us the
// definitively correct queue at creation time, before any other graphics injector can
// create private helper queues that would fool the heuristic ExecuteCommandLists capture.
using CreateSwapChainForHwndFn = HRESULT(STDMETHODCALLTYPE*)(
    IDXGIFactory2*, IUnknown*, HWND,
    const DXGI_SWAP_CHAIN_DESC1*,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*,
    IDXGIOutput*, IDXGISwapChain1**);

static PresentFn                g_origPresent = nullptr;
static Present1Fn               g_origPresent1 = nullptr;
static ResizeBuffersFn          g_origResizeBuffers = nullptr;
static ExecuteCommandListsFn    g_origExecuteCommandLists = nullptr;
static CreateSwapChainForHwndFn g_origCreateSwapChainForHwnd = nullptr;

// Re-entrancy guard: RenderOverlayFrame calls ExecuteCommandLists on the game queue to
// submit overlay draw commands. Without this flag, HookedExecuteCommandLists would see
// our own submission and could overwrite the captured queue or emit spurious log lines.
static thread_local bool g_inOverlaySubmit = false;

static UINT GetToggleUIMessage() {
    static UINT msg = RegisterWindowMessageW(L"AbsoluteHOTAS_ToggleUI");
    return msg;
}

// ============================================================================
// WndProc Hook
// ============================================================================
static LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == GetToggleUIMessage()) {
        UIHook::ToggleUI();
        return 0;
    }

    // Toggle on Ctrl+Alt+B
    if (msg == WM_KEYDOWN && wParam == 'B') {
        bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool alt  = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
        if (ctrl && alt) {
            UIHook::ToggleUI();
            return 0;
        }
    }

    if (g_isOpen.load(std::memory_order_relaxed)) {
        // Let ImGui process the message first
        ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);

        // Block raw input (Starfield uses this for mouse/keyboard)
        if (msg == WM_INPUT) return 0;

        // Block all mouse messages
        if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) return 0;

        // Block all keyboard messages
        if (msg >= WM_KEYFIRST && msg <= WM_KEYLAST) return 0;

        // Block activation/focus changes that the game might use
        if (msg == WM_ACTIVATEAPP || msg == WM_ACTIVATE) return 0;

        // Prevent the game from re-clipping the cursor while we're open
        if (msg == WM_SETCURSOR) {
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            return TRUE;
        }
    }

    return CallWindowProcW(g_origWndProc, hWnd, msg, wParam, lParam);
}

// ============================================================================
// GPU Fence Wait
// ============================================================================
static void WaitForGpu() {
    ID3D12CommandQueue* pQueue = g_gameCommandQueue.load(std::memory_order_relaxed);
    if (!pQueue || !g_fence || !g_fenceEvent) return;
    g_fenceValue++;
    pQueue->Signal(g_fence, g_fenceValue);
    if (g_fence->GetCompletedValue() < g_fenceValue) {
        g_fence->SetEventOnCompletion(g_fenceValue, g_fenceEvent);
        WaitForSingleObject(g_fenceEvent, 2000); // 2s timeout
    }
}

// ============================================================================
// Render Target Management
// ============================================================================
static void CleanupRenderTargets() {
    if (!g_backBuffers) return;
    for (UINT i = 0; i < g_numBackBuffers; i++) {
        if (g_backBuffers[i]) {
            g_backBuffers[i]->Release();
            g_backBuffers[i] = nullptr;
        }
    }
}

static bool CreateRenderTargets(IDXGISwapChain* pSwapChain) {
    DXGI_SWAP_CHAIN_DESC desc{};
    pSwapChain->GetDesc(&desc);

    g_numBackBuffers = desc.BufferCount;
    g_backBufferFormat = desc.BufferDesc.Format;
    g_lastSwapWidth = desc.BufferDesc.Width;
    g_lastSwapHeight = desc.BufferDesc.Height;

    // Allocate arrays if needed
    if (g_backBuffers) delete[] g_backBuffers;
    if (g_rtvHandles) delete[] g_rtvHandles;
    if (g_cmdAllocators) {
        // Release old allocators
        for (UINT i = 0; i < g_numBackBuffers; i++) {
            if (g_cmdAllocators[i]) g_cmdAllocators[i]->Release();
        }
        delete[] g_cmdAllocators;
    }

    g_backBuffers = new ID3D12Resource*[g_numBackBuffers]();
    g_rtvHandles = new D3D12_CPU_DESCRIPTOR_HANDLE[g_numBackBuffers]();
    g_cmdAllocators = new ID3D12CommandAllocator*[g_numBackBuffers]();

    // Create RTV heap
    if (g_rtvDescHeap) { g_rtvDescHeap->Release(); g_rtvDescHeap = nullptr; }
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDesc.NumDescriptors = g_numBackBuffers;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(g_d3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&g_rtvDescHeap)))) {
            UILog("ERROR: Failed to create RTV descriptor heap.");
            return false;
        }
    }

    SIZE_T rtvIncrementSize = g_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvBase = g_rtvDescHeap->GetCPUDescriptorHandleForHeapStart();

    for (UINT i = 0; i < g_numBackBuffers; i++) {
        g_rtvHandles[i].ptr = rtvBase.ptr + i * rtvIncrementSize;

        ID3D12Resource* pBackBuffer = nullptr;
        if (SUCCEEDED(pSwapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer)))) {
            g_d3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, g_rtvHandles[i]);
            g_backBuffers[i] = pBackBuffer;
        }

        // Create per-frame command allocator
        if (FAILED(g_d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_cmdAllocators[i])))) {
            UILog("ERROR: Failed to create command allocator for frame " + std::to_string(i));
            return false;
        }
    }

    g_targetSwapChain = pSwapChain;
    UILog("Render targets created: " + std::to_string(g_numBackBuffers) + " back buffers, format=" + std::to_string((int)g_backBufferFormat));
    return true;
}

// ============================================================================
// CreateSwapChainForHwnd Hook — primary, guaranteed command queue capture
// ============================================================================
// IDXGIFactory2::CreateSwapChainForHwnd requires pDevice to be the specific
// ID3D12CommandQueue used for all Present submissions on this swap chain.
// Hooking here gives us the definitive queue at creation time, before any
// other graphics injector (ReShade, ENB, DLSS helpers, etc.) can create
// private DIRECT queues that would fool the ExecuteCommandLists heuristic.
static HRESULT STDMETHODCALLTYPE HookedCreateSwapChainForHwnd(
    IDXGIFactory2* pThis,
    IUnknown*      pDevice,
    HWND           hWnd,
    const DXGI_SWAP_CHAIN_DESC1*           pDesc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
    IDXGIOutput*   pRestrictToOutput,
    IDXGISwapChain1** ppSwapChain)
{
    if (!g_gameCommandQueue.load(std::memory_order_relaxed) && pDevice) {
        ID3D12CommandQueue* pQueue = nullptr;
        if (SUCCEEDED(pDevice->QueryInterface(IID_PPV_ARGS(&pQueue)))) {
            ID3D12CommandQueue* expected = nullptr;
            if (g_gameCommandQueue.compare_exchange_strong(expected, pQueue)) {
                // Release our QueryInterface ref — the game retains ownership; we hold a non-owning ref.
                pQueue->Release();
                UILog("Captured game command queue from CreateSwapChainForHwnd: 0x"
                    + std::format("{:X}", reinterpret_cast<uintptr_t>(pQueue)));
            } else {
                pQueue->Release();
            }
        } else {
            // pDevice was not an ID3D12CommandQueue — unusual configuration.
            // The ExecuteCommandLists fallback will capture the queue instead.
            UILog("WARNING: CreateSwapChainForHwnd pDevice is not an ID3D12CommandQueue "
                  "\u2014 queue capture deferred to ExecuteCommandLists fallback.");
        }
    }
    return g_origCreateSwapChainForHwnd(
        pThis, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
}

// ============================================================================
// ExecuteCommandLists Hook — fallback queue capture + re-entrancy guard
// ============================================================================
// Primary capture is now HookedCreateSwapChainForHwnd. This hook serves two roles:
//   1. Fallback: catches rare edge cases where the swap chain was created before
//      our CreateSwapChainForHwnd hook became live (very early injection scenarios).
//   2. Re-entrancy safety: our own RenderOverlayFrame calls ExecuteCommandLists to
//      submit the overlay command list; g_inOverlaySubmit prevents that from
//      overwriting the captured queue or emitting spurious fallback log entries.
static void STDMETHODCALLTYPE HookedExecuteCommandLists(ID3D12CommandQueue* pQueue, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists) {
    if (!g_inOverlaySubmit && !g_gameCommandQueue.load(std::memory_order_relaxed)) {
        D3D12_COMMAND_QUEUE_DESC desc = pQueue->GetDesc();
        if (desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
            ID3D12CommandQueue* expected = nullptr;
            if (g_gameCommandQueue.compare_exchange_strong(expected, pQueue)) {
                UILog("Captured game command queue from ExecuteCommandLists (fallback): 0x"
                    + std::format("{:X}", reinterpret_cast<uintptr_t>(pQueue)));
            }
        }
    }
    g_origExecuteCommandLists(pQueue, NumCommandLists, ppCommandLists);
}

// ============================================================================
// ImGui Safe Teardown
// ============================================================================
static bool TryImGuiBackendShutdown() {
    __try {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void SafeShutdownStaleImGui() {
    if (ImGui::GetCurrentContext()) {
        if (!TryImGuiBackendShutdown()) {
            RuntimePaths::Log("[UIHook]", "Exception in ImGui_ImplDX12_Shutdown. Forcing context destruction.");
        }
        ImGui::DestroyContext();
    }
}

// ============================================================================
// ImGui Initialization (called once from first Present after queue is captured)
// ============================================================================
static bool InitImGui(IDXGISwapChain* pSwapChain) {
    std::lock_guard<std::mutex> lock(g_initMutex);
    if (g_initialized.load()) return true;

    // We need the game's command queue before we can initialize
    if (!g_gameCommandQueue) return false;

    UILog("Initializing ImGui D3D12 overlay...");

    // Clean up any stale ImGui context from a previous session that was
    // abandoned by HandleRenderException.
    if (ImGui::GetCurrentContext()) {
        UILog("Cleaning up stale ImGui context from previous session.");
        SafeShutdownStaleImGui();
    }

    // Restore old WndProc if we previously hooked it (prevents double-hooking)
    if (g_origWndProc && g_hWnd) {
        SetWindowLongPtrW(g_hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_origWndProc));
        g_origWndProc = nullptr;
    }

    // Get the D3D12 device from the swap chain
    if (FAILED(pSwapChain->GetDevice(IID_PPV_ARGS(&g_d3dDevice)))) {
        UILog("ERROR: Failed to get ID3D12Device from swap chain.");
        return false;
    }

    // Get the HWND from the swap chain
    DXGI_SWAP_CHAIN_DESC desc{};
    pSwapChain->GetDesc(&desc);
    g_hWnd = desc.OutputWindow;
    g_backBufferFormat = desc.BufferDesc.Format;
    g_numBackBuffers = desc.BufferCount;
    g_lastSwapWidth = desc.BufferDesc.Width;
    g_lastSwapHeight = desc.BufferDesc.Height;

    UILog("Swap chain: " + std::to_string(desc.BufferDesc.Width) + "x" + std::to_string(desc.BufferDesc.Height)
        + " format=" + std::to_string((int)g_backBufferFormat)
        + " buffers=" + std::to_string(g_numBackBuffers));

    // Create SRV descriptor heap for ImGui fonts
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = 1;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(g_d3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&g_srvDescHeap)))) {
            UILog("ERROR: Failed to create SRV descriptor heap.");
            return false;
        }
    }

    // Create render targets and per-frame allocators
    if (!CreateRenderTargets(pSwapChain)) {
        UILog("ERROR: Failed to create render targets.");
        return false;
    }

    // Create command list (uses first allocator initially)
    if (FAILED(g_d3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_cmdAllocators[0], nullptr, IID_PPV_ARGS(&g_cmdList)))) {
        UILog("ERROR: Failed to create command list.");
        return false;
    }
    g_cmdList->Close();

    // Create fence for GPU synchronization
    if (FAILED(g_d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)))) {
        UILog("ERROR: Failed to create fence.");
        return false;
    }
    if (g_fenceEvent) {
        CloseHandle(g_fenceEvent);
    }
    g_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_fenceValue = 0;

    // Setup ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // Don't save layout to disk

    // Dark theme with custom colors
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(12.0f, 12.0f);
    style.FramePadding = ImVec2(8.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);

    // Custom color scheme
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]        = ImVec4(0.08f, 0.08f, 0.12f, 0.94f);
    colors[ImGuiCol_TitleBg]         = ImVec4(0.10f, 0.10f, 0.18f, 1.00f);
    colors[ImGuiCol_TitleBgActive]   = ImVec4(0.15f, 0.15f, 0.28f, 1.00f);
    colors[ImGuiCol_FrameBg]         = ImVec4(0.12f, 0.12f, 0.20f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]  = ImVec4(0.20f, 0.20f, 0.35f, 1.00f);
    colors[ImGuiCol_Button]          = ImVec4(0.20f, 0.25f, 0.45f, 1.00f);
    colors[ImGuiCol_ButtonHovered]   = ImVec4(0.30f, 0.35f, 0.60f, 1.00f);
    colors[ImGuiCol_ButtonActive]    = ImVec4(0.15f, 0.20f, 0.40f, 1.00f);
    colors[ImGuiCol_Header]          = ImVec4(0.20f, 0.25f, 0.45f, 0.60f);
    colors[ImGuiCol_HeaderHovered]   = ImVec4(0.30f, 0.35f, 0.60f, 0.80f);
    colors[ImGuiCol_Tab]             = ImVec4(0.15f, 0.18f, 0.32f, 1.00f);
    colors[ImGuiCol_TabHovered]      = ImVec4(0.30f, 0.35f, 0.60f, 1.00f);
    colors[ImGuiCol_TabSelected]     = ImVec4(0.22f, 0.27f, 0.50f, 1.00f);

    // Init Win32 backend
    ImGui_ImplWin32_Init(g_hWnd);

    // Init DX12 backend — use the actual back buffer format
    ImGui_ImplDX12_Init(
        g_d3dDevice,
        static_cast<int>(g_numBackBuffers),
        g_backBufferFormat,
        g_srvDescHeap,
        g_srvDescHeap->GetCPUDescriptorHandleForHeapStart(),
        g_srvDescHeap->GetGPUDescriptorHandleForHeapStart()
    );

    // Hook WndProc
    g_origWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(HookedWndProc)));

    g_initialized.store(true);
    UILog("ImGui D3D12 overlay initialized successfully.");
    return true;
}

// ============================================================================
// RenderOverlayFrame — Exception Recovery
// ============================================================================
static bool TryReleaseResources() {
    __try {
        if (g_backBuffers) {
            for (UINT i = 0; i < g_numBackBuffers; i++) {
                if (g_backBuffers[i]) g_backBuffers[i]->Release();
            }
            delete[] g_backBuffers;
        }
        if (g_rtvHandles) delete[] g_rtvHandles;
        if (g_rtvDescHeap) g_rtvDescHeap->Release();
        if (g_srvDescHeap) g_srvDescHeap->Release();
        if (g_cmdList) g_cmdList->Release();
        if (g_cmdAllocators) {
            for (UINT i = 0; i < g_numBackBuffers; i++) {
                if (g_cmdAllocators[i]) g_cmdAllocators[i]->Release();
            }
            delete[] g_cmdAllocators;
        }
        if (g_fence) g_fence->Release();
        if (g_fenceEvent) CloseHandle(g_fenceEvent);
        if (g_d3dDevice) g_d3dDevice->Release();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Separated so the __try function (RenderOverlayFrame) has zero C++ objects
// requiring unwinding, satisfying MSVC's C2712 constraint.
static void HandleRenderException() {
    // CRITICAL: Do NOT call WaitForGpu, ImGui_ImplDX12_Shutdown, or any
    // D3D12/COM methods here. The resources are likely corrupted after a
    // display mode transition, and touching them throws a SECOND exception
    // from inside this handler, which crashes the game.
    //
    // We accept a minor GPU resource leak. InitImGui will recreate everything
    // from scratch on the next Ctrl+Alt+B press.

    RuntimePaths::Log("[UIHook]",
        "D3D12 render exception caught — overlay disabled. Press Ctrl+Alt+B to reinit.");

    // Restore Win32 cursor state if the overlay was open when the exception fired.
    // ToggleUI's closing branch normally handles this, but HandleRenderException
    // bypasses ToggleUI entirely — leaving the system cursor visible and unclipped.
    // Win32 cursor calls are safe here: no D3D12/COM dependency, no throw risk.
    if (g_isOpen.load()) {
        // Hide the system cursor (mirrors ToggleUI's closing path).
        while (ShowCursor(FALSE) >= 0) {}
        if (g_hadClipRect) {
            ClipCursor(&g_savedClipRect);
            g_hadClipRect = false;
        }
    }

    g_isOpen.store(false);
    g_initialized.store(false);

    // Attempt safe COM release in a separate block. Releasing references
    // is valid even if device is lost, but proxy objects might crash.
    if (!TryReleaseResources()) {
        RuntimePaths::Log("[UIHook]", "Exception during resource release, proxy object is dead. Leaking remaining resources.");
    }

    g_backBuffers = nullptr;
    g_rtvHandles = nullptr;
    g_rtvDescHeap = nullptr;
    g_srvDescHeap = nullptr;
    g_cmdList = nullptr;
    g_cmdAllocators = nullptr;
    g_fence = nullptr;
    g_fenceEvent = nullptr;
    g_d3dDevice = nullptr;
    g_numBackBuffers = 0;
    g_targetSwapChain = nullptr;
    // Clear the captured queue — it is stale after a render exception and must
    // be recaptured before InitImGui is called again.
    g_gameCommandQueue.store(nullptr, std::memory_order_relaxed);
}

// ============================================================================
// RenderOverlayFrame
// ============================================================================
// __try requires no C++ objects with destructors in the function scope.
// All cleanup is delegated to HandleRenderException().
// Rebind render targets to the swapchain currently being presented. Under frame
// generation (FSR3 FG, DLSS-G) or any injector that wraps the swapchain, the
// presented one can differ from the one we initialized on, which would draw the
// overlay into an off-screen buffer. Kept out of RenderOverlayFrame so its
// std::string logging doesn't live in that function's __try scope (MSVC C2712).
// Only reached while the overlay is open (a menu), so rebuild churn never touches
// gameplay.
static bool RebindRenderTargetsIfNeeded(IDXGISwapChain* pSwapChain) {
    if (pSwapChain == g_targetSwapChain) return true;
    WaitForGpu();
    CleanupRenderTargets();
    if (!CreateRenderTargets(pSwapChain)) {
        UILog("ERROR: Failed to rebind render targets to presented swapchain.");
        return false;
    }
    return true;
}

static void RenderOverlayFrame(IDXGISwapChain* pSwapChain) {
    __try {
        if (!RebindRenderTargetsIfNeeded(pSwapChain)) return;

        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (g_drawCallback) {
            g_drawCallback();
        }

        ImGui::Render();

        UINT backBufferIdx = 0;
        IDXGISwapChain3* pSwapChain3 = nullptr;
        if (SUCCEEDED(pSwapChain->QueryInterface(IID_PPV_ARGS(&pSwapChain3)))) {
            backBufferIdx = pSwapChain3->GetCurrentBackBufferIndex();
            pSwapChain3->Release();
        }

        if (backBufferIdx < g_numBackBuffers && g_backBuffers[backBufferIdx] && g_cmdAllocators[backBufferIdx]) {
            g_cmdAllocators[backBufferIdx]->Reset();
            g_cmdList->Reset(g_cmdAllocators[backBufferIdx], nullptr);

            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = g_backBuffers[backBufferIdx];
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            g_cmdList->ResourceBarrier(1, &barrier);

            g_cmdList->OMSetRenderTargets(1, &g_rtvHandles[backBufferIdx], FALSE, nullptr);
            g_cmdList->SetDescriptorHeaps(1, &g_srvDescHeap);

            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_cmdList);

            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
            g_cmdList->ResourceBarrier(1, &barrier);

            g_cmdList->Close();

            ID3D12CommandList* ppCmdLists[] = { g_cmdList };
            // Set re-entrancy flag so HookedExecuteCommandLists ignores this submission.
            g_inOverlaySubmit = true;
            ID3D12CommandQueue* pQueue = g_gameCommandQueue.load(std::memory_order_relaxed);
            if (pQueue) {
                pQueue->ExecuteCommandLists(1, ppCmdLists);
            }
            g_inOverlaySubmit = false;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HandleRenderException();
    }
}

// ============================================================================
// Present Hook
// ============================================================================
// Shared overlay handling for every present path (Present and Present1). Runs the
// swap-chain state monitor, lazy init, and overlay render. The caller forwards to
// the appropriate original function afterward.
static void HandlePresent(IDXGISwapChain* pSwapChain) {
    // Monitor swap chain state every frame — catches display mode changes that
    // bypass our ResizeBuffers hook (e.g., ResizeBuffers1, swap chain recreation).
    if (g_initialized.load(std::memory_order_relaxed)) {
        DXGI_SWAP_CHAIN_DESC curDesc{};
        if (SUCCEEDED(pSwapChain->GetDesc(&curDesc))) {
            // Detect resolution/format/buffer count change → invalidate
            if (curDesc.BufferCount != g_numBackBuffers ||
                curDesc.BufferDesc.Format != g_backBufferFormat ||
                curDesc.BufferDesc.Width != g_lastSwapWidth ||
                curDesc.BufferDesc.Height != g_lastSwapHeight) {
                HandleRenderException();
            }

            // Detect HWND change → re-hook WndProc
            if (curDesc.OutputWindow && curDesc.OutputWindow != g_hWnd) {
                if (g_origWndProc && g_hWnd) {
                    SetWindowLongPtrW(g_hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_origWndProc));
                }
                g_hWnd = curDesc.OutputWindow;
                g_origWndProc = reinterpret_cast<WNDPROC>(
                    SetWindowLongPtrW(g_hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(HookedWndProc)));
            }
        }
    }

    // Belt-and-suspenders: C++ try/catch for C++ exceptions (0xE06D7363)
    // which MSVC /EHsc may not route to the __try/__except in RenderOverlayFrame.
    try {
        if (!g_initialized.load(std::memory_order_relaxed)) {
            if (g_gameCommandQueue.load(std::memory_order_relaxed)) {
                InitImGui(pSwapChain);
            }
        }

        if (g_initialized.load(std::memory_order_relaxed) && g_isOpen.load(std::memory_order_relaxed)) {
            RenderOverlayFrame(pSwapChain);
        }
    } catch (...) {
        HandleRenderException();
    }
}

static HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    HandlePresent(pSwapChain);
    return g_origPresent(pSwapChain, SyncInterval, Flags);
}

// IDXGISwapChain1::Present1 — same overlay handling, different present entry point.
// IDXGISwapChain1 derives from IDXGISwapChain, so the upcast to HandlePresent is safe.
static HRESULT STDMETHODCALLTYPE HookedPresent1(IDXGISwapChain1* pSwapChain, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS* pPresentParameters) {
    HandlePresent(pSwapChain);
    return g_origPresent1(pSwapChain, SyncInterval, PresentFlags, pPresentParameters);
}

// ============================================================================
// ResizeBuffers — SEH-safe forwarder
// ============================================================================
// Extracted to satisfy MSVC C2712: a function containing __try must have no
// C++ objects with destructors in scope. Logging is delegated to the exception
// handler to keep InvokeOrigResizeBuffers free of std::string temporaries.
static void HandleResizeBuffersException() {
    RuntimePaths::Log("[UIHook]",
        "WARNING: ResizeBuffers threw — overlay disabled, game may continue.");
}

static HRESULT InvokeOrigResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount,
    UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    HRESULT hr = DXGI_ERROR_INVALID_CALL;
    __try {
        hr = g_origResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HandleResizeBuffersException();
    }
    return hr;
}

// ============================================================================
// ResizeBuffers Hook
// ============================================================================
static HRESULT STDMETHODCALLTYPE HookedResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    // Force-close the overlay and clear the captured queue unconditionally.
    // Frame Generation reconfigures the presentation pipeline on enable/disable,
    // invalidating both our D3D12 resources and the command queue pointer.
    g_isOpen.store(false);
    g_gameCommandQueue.store(nullptr, std::memory_order_relaxed);  // force fresh recapture on next Present

    if (g_initialized.load()) {
        g_initialized.store(false);

        // Release back buffer COM references BEFORE calling ResizeBuffers.
        // DXGI requires all GetBuffer-acquired refs to be released first;
        // failure returns DXGI_ERROR_INVALID_CALL. Frame Generation wrappers
        // (especially AMD FSR3 built-in) do not handle that HRESULT defensively
        // and crash writing through a null pointer.
        CleanupRenderTargets();         // releases g_backBuffers[i] COM refs
        delete[] g_backBuffers;         g_backBuffers   = nullptr;
        delete[] g_rtvHandles;          g_rtvHandles    = nullptr;

        // RTV heap — safe to release here (no pending GPU work routed through it;
        // DXGI callers are expected to be GPU-idle before ResizeBuffers).
        if (g_rtvDescHeap) { g_rtvDescHeap->Release(); g_rtvDescHeap = nullptr; }

        // Remaining objects may have in-flight GPU work — null without release.
        // Minor leak; reclaimed by InitImGui on reinit or by the OS at exit.
        g_srvDescHeap   = nullptr;
        g_cmdList       = nullptr;
        g_cmdAllocators = nullptr;
        g_fence         = nullptr;
        if (g_fenceEvent) { CloseHandle(g_fenceEvent); g_fenceEvent = nullptr; }
        g_d3dDevice     = nullptr;
        g_numBackBuffers = 0;
    }

    // Forward to the real ResizeBuffers. Back buffer refs are fully released so
    // DXGI will accept the call. InvokeOrigResizeBuffers catches any remaining
    // structured exceptions (GPU device lost, proxy swap chain edge cases, etc.).
    HRESULT hr = InvokeOrigResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);

    // Re-hook WndProc if the window handle changed (display mode transitions
    // can recreate the HWND, leaving our hotkey hook on the dead window).
    if (SUCCEEDED(hr)) {
        DXGI_SWAP_CHAIN_DESC scDesc{};
        pSwapChain->GetDesc(&scDesc);
        if (scDesc.OutputWindow && scDesc.OutputWindow != g_hWnd) {
            if (g_origWndProc && g_hWnd) {
                SetWindowLongPtrW(g_hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_origWndProc));
            }
            g_hWnd = scDesc.OutputWindow;
            g_origWndProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(g_hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(HookedWndProc)));
        }
    }

    // g_gameCommandQueue is now null. The next HookedPresent will wait for
    // CreateSwapChainForHwnd or ExecuteCommandLists to recapture the queue,
    // then call InitImGui to rebuild all overlay state from scratch.
    return hr;
}

// ============================================================================
// Vtable Discovery via Dummy Device
// ============================================================================
static bool GetVtablePointers(void** outPresent, void** outPresent1, void** outResizeBuffers, void** outExecuteCommandLists, void** outCreateSwapChainForHwnd) {
    // Create a temporary DXGI factory + device + swap chain to harvest vtable pointers
    IDXGIFactory4* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        UILog("ERROR: Failed to create DXGI factory for vtable discovery.");
        return false;
    }

    // Harvest CreateSwapChainForHwnd from the factory vtable.
    // IDXGIFactory2::CreateSwapChainForHwnd is at vtable index 15 (SDK-verified):
    //   IUnknown(0-2) + IDXGIObject(3-6) + IDXGIFactory(7-11) + IDXGIFactory1(12-13)
    //   + IDXGIFactory2::IsWindowedStereoEnabled(14) + CreateSwapChainForHwnd(15)
    // IDXGIFactory4 inherits IDXGIFactory2, so this index is stable regardless of which
    // factory version the game uses internally.
    void** factoryVtable = *reinterpret_cast<void***>(factory);
    *outCreateSwapChainForHwnd = factoryVtable[15];

    ID3D12Device* tempDevice = nullptr;
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&tempDevice)))) {
        factory->Release();
        UILog("ERROR: Failed to create temp D3D12 device.");
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* tempQueue = nullptr;
    if (FAILED(tempDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&tempQueue)))) {
        tempDevice->Release();
        factory->Release();
        UILog("ERROR: Failed to create temp command queue.");
        return false;
    }

    // Harvest ExecuteCommandLists vtable pointer (index 10 on ID3D12CommandQueue)
    void** queueVtable = *reinterpret_cast<void***>(tempQueue);
    *outExecuteCommandLists = queueVtable[10];

    // Create a dummy window for the swap chain
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"AbsoluteHOTAS_DummyWnd";
    RegisterClassExW(&wc);
    HWND hDummy = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);

    DXGI_SWAP_CHAIN_DESC1 swapDesc{};
    swapDesc.Width = 100;
    swapDesc.Height = 100;
    swapDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.BufferCount = 2;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    IDXGISwapChain1* tempSwapChain1 = nullptr;
    HRESULT hr = factory->CreateSwapChainForHwnd(tempQueue, hDummy, &swapDesc, nullptr, nullptr, &tempSwapChain1);

    if (FAILED(hr) || !tempSwapChain1) {
        tempQueue->Release();
        tempDevice->Release();
        factory->Release();
        DestroyWindow(hDummy);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        UILog("ERROR: Failed to create temp swap chain.");
        return false;
    }

    // Harvest Present and ResizeBuffers vtable pointers
    void** swapVtable = *reinterpret_cast<void***>(tempSwapChain1);
    *outPresent = swapVtable[8];        // IDXGISwapChain::Present
    *outPresent1 = swapVtable[22];      // IDXGISwapChain1::Present1
    *outResizeBuffers = swapVtable[13]; // IDXGISwapChain::ResizeBuffers

    UILog("Vtable discovery complete.");

    // Cleanup
    tempSwapChain1->Release();
    tempQueue->Release();
    tempDevice->Release();
    factory->Release();
    DestroyWindow(hDummy);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return true;
}

// ============================================================================
// Prior-hook detection
// ============================================================================
// Detect whether a render entry point has already been inline-hooked by another
// injector (frame generation, upscaler, capture, or overlay software) before we
// install our own. Inline hooks open the function with a jmp — E9 (rel32), EB
// (rel8), or FF 25 (indirect [rip]) — whereas a normal prologue does not.
// Vtable-swizzle / proxy-object hooks are not visible this way; this is a
// best-effort self-serve diagnostic, not a guarantee. See
// docs/reference/overlay-hook-compatibility.md.
static bool LooksHooked(void* fn, std::string& firstBytesOut) {
    if (!fn) return false;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(fn);
    firstBytesOut = std::format("{:02X} {:02X} {:02X}",
        static_cast<unsigned>(p[0]), static_cast<unsigned>(p[1]), static_cast<unsigned>(p[2]));
    if (p[0] == 0xE9) return true;                  // jmp rel32
    if (p[0] == 0xEB) return true;                  // jmp rel8
    if (p[0] == 0xFF && p[1] == 0x25) return true;  // jmp qword ptr [rip+disp32]
    return false;
}

// ============================================================================
// Public API
// ============================================================================
bool UIHook::Install() {
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
                UILog(std::string("Note: render entry already hooked by another layer: ") + t.name + " (first bytes: " + firstBytes + ").");
            }
        }
        if (priorHooks > 0) {
            UILog("Note: " + std::to_string(priorHooks) + " D3D12/DXGI render entry point(s) are already hooked "
                  "by another layer. This is usually harmless — Steam/Discord/RTSS overlays and frame-gen layers "
                  "commonly coexist with the overlay. It only matters if Ctrl+Alt+B shows a cursor but no UI; in "
                  "that case disable frame generation (NVIDIA Smooth Motion / DLSS-G, AMD AFMF / FSR Frame "
                  "Generation), capture/overlay tools, or driver filters. "
                  "See docs/reference/overlay-hook-compatibility.md.");
        }
    }

    if (MH_Initialize() != MH_OK) {
        UILog("ERROR: MH_Initialize failed.");
        return false;
    }

    // Primary queue capture — fires at swap chain creation, before other injectors
    // can create private helper queues that would fool ExecuteCommandLists.
    if (MH_CreateHook(pCreateSwapChainForHwnd, &HookedCreateSwapChainForHwnd,
        reinterpret_cast<void**>(&g_origCreateSwapChainForHwnd)) != MH_OK) {
        UILog("ERROR: Failed to create CreateSwapChainForHwnd hook.");
        return false;
    }

    if (MH_CreateHook(pPresent, &HookedPresent, reinterpret_cast<void**>(&g_origPresent)) != MH_OK) {
        UILog("ERROR: Failed to create Present hook.");
        return false;
    }

    // Present1 shares its vtable slot across all IDXGISwapChain1 instances, so this
    // one hook covers any swapchain that presents via Present1. Non-fatal if it
    // fails — Present-only hooking still works for the common case.
    if (MH_CreateHook(pPresent1, &HookedPresent1, reinterpret_cast<void**>(&g_origPresent1)) != MH_OK) {
        UILog("WARNING: Failed to create Present1 hook (Present-only overlay still active).");
    }

    if (MH_CreateHook(pResizeBuffers, &HookedResizeBuffers, reinterpret_cast<void**>(&g_origResizeBuffers)) != MH_OK) {
        UILog("ERROR: Failed to create ResizeBuffers hook.");
        return false;
    }

    // Fallback queue capture + re-entrancy guard (see HookedExecuteCommandLists).
    if (MH_CreateHook(pExecuteCommandLists, &HookedExecuteCommandLists,
        reinterpret_cast<void**>(&g_origExecuteCommandLists)) != MH_OK) {
        UILog("ERROR: Failed to create ExecuteCommandLists hook.");
        return false;
    }

    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        UILog("ERROR: Failed to enable hooks.");
        return false;
    }

    UILog("D3D12 hooks installed successfully. Press Ctrl+Alt+B to toggle overlay.");
    return true;
}

void UIHook::Shutdown() {
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
}

void UIHook::ToggleUI() {
    if (g_hWnd && GetCurrentThreadId() != GetWindowThreadProcessId(g_hWnd, nullptr)) {
        PostMessageW(g_hWnd, GetToggleUIMessage(), 0, 0);
        return;
    }

    bool wasOpen = g_isOpen.load();
    bool nowOpen = !wasOpen;
    g_isOpen.store(nowOpen);

    if (nowOpen) {
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
        // --- Closing the overlay ---
        // Hide the software cursor
        if (g_initialized.load()) {
            ImGuiIO& io = ImGui::GetIO();
            io.MouseDrawCursor = false;
        }

        // Hide the system cursor again
        while (ShowCursor(FALSE) >= 0) {}

        // Restore the game's cursor clip rect
        if (g_hadClipRect) {
            ClipCursor(&g_savedClipRect);
        }
    }
}

bool UIHook::IsUIOpen() {
    return g_isOpen.load(std::memory_order_relaxed);
}

void UIHook::SetDrawCallback(DrawCallback cb) {
    g_drawCallback = cb;
}
