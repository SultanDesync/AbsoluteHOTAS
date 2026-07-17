#include "PCH.h"

#include "UIHookInternal.h"

#include "RuntimePaths.h"

#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

namespace UIHook::Detail {


void WaitForGpu() {
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
void CleanupRenderTargets() {
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

    const UINT oldBackBufferCount = g_numBackBuffers;
    g_numBackBuffers = desc.BufferCount;
    g_backBufferFormat = desc.BufferDesc.Format;
    g_lastSwapWidth = desc.BufferDesc.Width;
    g_lastSwapHeight = desc.BufferDesc.Height;

    // Allocate arrays if needed
    if (g_backBuffers) delete[] g_backBuffers;
    if (g_rtvHandles) delete[] g_rtvHandles;
    if (g_cmdAllocators) {
        // Release old allocators
        for (UINT i = 0; i < oldBackBufferCount; i++) {
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
        RestoreGameCursorState();
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

static void LogOverlaySubmission(IDXGISwapChain* swapChain, ID3D12CommandQueue* queue,
    UINT backBufferIdx)
{
    UILog(std::format("Overlay command list submitted: swapChain=0x{:X}, queue=0x{:X}, "
        "backBuffer={}, targetSwapChain=0x{:X}.",
        reinterpret_cast<uintptr_t>(swapChain), reinterpret_cast<uintptr_t>(queue),
        backBufferIdx, reinterpret_cast<uintptr_t>(g_targetSwapChain)));
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
                if (g_logNextOverlaySubmit.exchange(false, std::memory_order_relaxed)) {
                    LogOverlaySubmission(pSwapChain, pQueue, backBufferIdx);
                }
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
    ID3D12CommandQueue* selectedQueue = SelectPresentQueue(pSwapChain);
    ID3D12CommandQueue* activeQueue = g_gameCommandQueue.load(std::memory_order_relaxed);
    if (selectedQueue && selectedQueue != activeQueue) {
        if (g_initialized.load(std::memory_order_relaxed)) {
            HandleRenderException();
        }
        g_gameCommandQueue.store(selectedQueue, std::memory_order_relaxed);
        UILog(std::format("Present selected swap chain 0x{:X} with queue 0x{:X} ({}).",
            reinterpret_cast<uintptr_t>(pSwapChain), reinterpret_cast<uintptr_t>(selectedQueue),
            FindAssociatedQueue(pSwapChain) ? "creation association" : "temporal fallback"));
    }

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

HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    HandlePresent(pSwapChain);
    return OriginalPresentFor(pSwapChain)(pSwapChain, SyncInterval, Flags);
}

// IDXGISwapChain1::Present1 — same overlay handling, different present entry point.
// IDXGISwapChain1 derives from IDXGISwapChain, so the upcast to HandlePresent is safe.
HRESULT STDMETHODCALLTYPE HookedPresent1(IDXGISwapChain1* pSwapChain, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS* pPresentParameters) {
    HandlePresent(pSwapChain);
    return OriginalPresent1For(pSwapChain)(pSwapChain, SyncInterval, PresentFlags, pPresentParameters);
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
        hr = OriginalResizeBuffersFor(pSwapChain)(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HandleResizeBuffersException();
    }
    return hr;
}

// ============================================================================
// ResizeBuffers Hook
// ============================================================================
HRESULT STDMETHODCALLTYPE HookedResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    // Force-close the overlay and clear the captured queue unconditionally.
    // Frame Generation reconfigures the presentation pipeline on enable/disable,
    // invalidating both our D3D12 resources and the command queue pointer.
    if (g_isOpen.exchange(false)) RestoreGameCursorState();
    // Keep the authoritative creation-time swap-chain/queue association. Clearing
    // it here used to make RTSS's next DIRECT submission win the fallback race.

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

}  // namespace UIHook::Detail
