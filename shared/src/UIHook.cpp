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
    // Always log UIHook messages — these are diagnostics, not per-frame spam
    RuntimePaths::AppendLogAlways("[UIHook]", msg);
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

// The game's command queue — captured via ExecuteCommandLists hook
static ID3D12CommandQueue*          g_gameCommandQueue = nullptr;

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

// WndProc
static HWND    g_hWnd = nullptr;
static WNDPROC g_origWndProc = nullptr;

// Hook function pointers
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
using ExecuteCommandListsFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue* pQueue, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists);

static PresentFn             g_origPresent = nullptr;
static ResizeBuffersFn       g_origResizeBuffers = nullptr;
static ExecuteCommandListsFn g_origExecuteCommandLists = nullptr;

// ============================================================================
// WndProc Hook
// ============================================================================
static LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
        if (msg == WM_MOUSEMOVE) return 0;

        // Block all keyboard messages
        if (msg >= WM_KEYFIRST && msg <= WM_KEYLAST) return 0;
        if (msg == WM_CHAR || msg == WM_SYSCHAR) return 0;
        if (msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) return 0;

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
    if (!g_gameCommandQueue || !g_fence || !g_fenceEvent) return;
    g_fenceValue++;
    g_gameCommandQueue->Signal(g_fence, g_fenceValue);
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

    UILog("Render targets created: " + std::to_string(g_numBackBuffers) + " back buffers, format=" + std::to_string((int)g_backBufferFormat));
    return true;
}

// ============================================================================
// ExecuteCommandLists Hook — captures the game's command queue
// ============================================================================
static void STDMETHODCALLTYPE HookedExecuteCommandLists(ID3D12CommandQueue* pQueue, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists) {
    // Capture the first DIRECT queue we see — that's the game's rendering queue
    if (!g_gameCommandQueue) {
        D3D12_COMMAND_QUEUE_DESC desc = pQueue->GetDesc();
        if (desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
            g_gameCommandQueue = pQueue;
            UILog("Captured game command queue: 0x" + std::format("{:X}", reinterpret_cast<uintptr_t>(pQueue)));
        }
    }
    g_origExecuteCommandLists(pQueue, NumCommandLists, ppCommandLists);
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
    // abandoned by HandleRenderException (which nulls D3D12 resources
    // without calling ImGui shutdown to avoid secondary crashes).
    if (ImGui::GetCurrentContext()) {
        UILog("Cleaning up stale ImGui context from previous session.");
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
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

    // Use AppendLogAlways so this is visible even with logging disabled
    RuntimePaths::AppendLogAlways("[UIHook]",
        "D3D12 render exception caught — overlay disabled. Press Ctrl+Alt+B to reinit.");

    g_isOpen.store(false);
    g_initialized.store(false);

    // Null out all resource pointers WITHOUT releasing them.
    // The OS/driver will reclaim GPU resources when the process exits or
    // when InitImGui creates new ones (old refs get orphaned).
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
}

// ============================================================================
// RenderOverlayFrame
// ============================================================================
// __try requires no C++ objects with destructors in the function scope.
// All cleanup is delegated to HandleRenderException().
static void RenderOverlayFrame(IDXGISwapChain* pSwapChain) {
    __try {
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
            g_gameCommandQueue->ExecuteCommandLists(1, ppCmdLists);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HandleRenderException();
    }
}

// ============================================================================
// Present Hook
// ============================================================================
static HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
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
                UILog("Swap chain state changed (" +
                    std::to_string(g_lastSwapWidth) + "x" + std::to_string(g_lastSwapHeight) +
                    " -> " + std::to_string(curDesc.BufferDesc.Width) + "x" +
                    std::to_string(curDesc.BufferDesc.Height) +
                    ") — forcing overlay reinit.");
                HandleRenderException();
            }

            // Detect HWND change → re-hook WndProc
            if (curDesc.OutputWindow && curDesc.OutputWindow != g_hWnd) {
                UILog("HWND changed in Present — re-hooking WndProc.");
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
            if (g_gameCommandQueue) {
                InitImGui(pSwapChain);
            }
        }

        if (g_initialized.load(std::memory_order_relaxed) && g_isOpen.load(std::memory_order_relaxed)) {
            RenderOverlayFrame(pSwapChain);
        }
    } catch (...) {
        HandleRenderException();
    }

    return g_origPresent(pSwapChain, SyncInterval, Flags);
}

// ============================================================================
// ResizeBuffers Hook
// ============================================================================
static HRESULT STDMETHODCALLTYPE HookedResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    RuntimePaths::AppendLogAlways("[UIHook]",
        "ResizeBuffers called: " + std::to_string(Width) + "x" + std::to_string(Height));

    // Force-close the overlay — the resize invalidates all our D3D12 state.
    // Don't try to WaitForGpu, Release, or call ImGui shutdown here:
    // after a mode transition the fence/device may be stale and would throw.
    g_isOpen.store(false);

    if (g_initialized.load()) {
        g_initialized.store(false);

        // Abandon all resource pointers (minor leak, reclaimed on reinit or exit).
        // This is the same approach as HandleRenderException — safe from exceptions.
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

        RuntimePaths::AppendLogAlways("[UIHook]",
            "Overlay state abandoned for resize — will reinit on next Ctrl+Alt+B.");
    }

    // Call the game's original ResizeBuffers — this MUST always execute.
    HRESULT hr = g_origResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);

    // Re-hook WndProc if the window handle changed (display mode transitions
    // can recreate the HWND, leaving our hotkey hook on the dead window).
    if (SUCCEEDED(hr)) {
        DXGI_SWAP_CHAIN_DESC scDesc{};
        pSwapChain->GetDesc(&scDesc);
        if (scDesc.OutputWindow && scDesc.OutputWindow != g_hWnd) {
            RuntimePaths::AppendLogAlways("[UIHook]", "HWND changed after ResizeBuffers — re-hooking WndProc.");
            if (g_origWndProc && g_hWnd) {
                SetWindowLongPtrW(g_hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_origWndProc));
            }
            g_hWnd = scDesc.OutputWindow;
            g_origWndProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(g_hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(HookedWndProc)));
        }
    }

    // g_initialized is now false. The next HookedPresent will call InitImGui
    // which rebuilds everything from scratch using the new swap chain state.
    return hr;
}

// ============================================================================
// Vtable Discovery via Dummy Device
// ============================================================================
static bool GetVtablePointers(void** outPresent, void** outResizeBuffers, void** outExecuteCommandLists) {
    // Create a temporary DXGI factory + device + swap chain to harvest vtable pointers
    IDXGIFactory4* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        UILog("ERROR: Failed to create DXGI factory for vtable discovery.");
        return false;
    }

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
// Public API
// ============================================================================
bool UIHook::Install() {
    UILog("Installing D3D12 hooks...");

    void* pPresent = nullptr;
    void* pResizeBuffers = nullptr;
    void* pExecuteCommandLists = nullptr;

    if (!GetVtablePointers(&pPresent, &pResizeBuffers, &pExecuteCommandLists)) {
        UILog("ERROR: Vtable discovery failed. UI overlay disabled.");
        return false;
    }

    if (MH_Initialize() != MH_OK) {
        UILog("ERROR: MH_Initialize failed.");
        return false;
    }

    if (MH_CreateHook(pPresent, &HookedPresent, reinterpret_cast<void**>(&g_origPresent)) != MH_OK) {
        UILog("ERROR: Failed to create Present hook.");
        return false;
    }

    if (MH_CreateHook(pResizeBuffers, &HookedResizeBuffers, reinterpret_cast<void**>(&g_origResizeBuffers)) != MH_OK) {
        UILog("ERROR: Failed to create ResizeBuffers hook.");
        return false;
    }

    if (MH_CreateHook(pExecuteCommandLists, &HookedExecuteCommandLists, reinterpret_cast<void**>(&g_origExecuteCommandLists)) != MH_OK) {
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

        UILog("UI overlay OPENED — cursor unclipped, input captured.");
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

        UILog("UI overlay CLOSED — cursor restored, input released.");
    }
}

bool UIHook::IsUIOpen() {
    return g_isOpen.load(std::memory_order_relaxed);
}

void UIHook::SetDrawCallback(DrawCallback cb) {
    g_drawCallback = cb;
}
