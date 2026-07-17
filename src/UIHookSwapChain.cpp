#include "PCH.h"

#include "UIHookInternal.h"

namespace UIHook::Detail {

static IUnknown* GetComIdentity(IUnknown* object);


static void InstallInstanceRenderHooks(IDXGISwapChain1* swapChain) {
    if (!swapChain) return;
    IUnknown* identity = GetComIdentity(swapChain);
    if (!identity) return;

    std::lock_guard<std::mutex> lock(g_instanceHooksMutex);
    if (g_instanceHooks.contains(identity)) {
        identity->Release();
        return;
    }

    // IDXGISwapChain4 has 41 slots. A private table avoids modifying DXGI's
    // process-wide vtable and remains attached if RTSS later repatches the
    // canonical function prologue.
    constexpr size_t kSwapChain4VtableSlots = 41;
    void*** objectVtable = reinterpret_cast<void***>(swapChain);
    void** current = *objectVtable;
    void** shadow = new void*[kSwapChain4VtableSlots];
    std::copy_n(current, kSwapChain4VtableSlots, shadow);

    InstanceRenderHooks hooks{};
    hooks.present = reinterpret_cast<PresentFn>(current[8]);
    hooks.resizeBuffers = reinterpret_cast<ResizeBuffersFn>(current[13]);
    hooks.present1 = reinterpret_cast<Present1Fn>(current[22]);
    hooks.shadowVtable = shadow;
    shadow[8] = reinterpret_cast<void*>(&HookedPresent);
    shadow[13] = reinterpret_cast<void*>(&HookedResizeBuffers);
    shadow[22] = reinterpret_cast<void*>(&HookedPresent1);

    g_instanceHooks.emplace(identity, hooks);
    InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(objectVtable), shadow);
    UILog(std::format("Installed per-instance render hooks on swap chain 0x{:X} (identity 0x{:X}).",
        reinterpret_cast<uintptr_t>(swapChain), reinterpret_cast<uintptr_t>(identity)));
    identity->Release();
}

static InstanceRenderHooks FindInstanceRenderHooks(IUnknown* swapChain) {
    InstanceRenderHooks hooks{};
    IUnknown* identity = GetComIdentity(swapChain);
    if (!identity) return hooks;
    {
        std::lock_guard<std::mutex> lock(g_instanceHooksMutex);
        auto it = g_instanceHooks.find(identity);
        if (it != g_instanceHooks.end()) hooks = it->second;
    }
    identity->Release();
    return hooks;
}

PresentFn OriginalPresentFor(IDXGISwapChain* swapChain) {
    PresentFn fn = FindInstanceRenderHooks(swapChain).present;
    return reinterpret_cast<void*>(fn) == g_presentTarget ? g_origPresent : (fn ? fn : g_origPresent);
}

Present1Fn OriginalPresent1For(IDXGISwapChain1* swapChain) {
    Present1Fn fn = FindInstanceRenderHooks(swapChain).present1;
    return reinterpret_cast<void*>(fn) == g_present1Target ? g_origPresent1 : (fn ? fn : g_origPresent1);
}

ResizeBuffersFn OriginalResizeBuffersFor(IDXGISwapChain* swapChain) {
    ResizeBuffersFn fn = FindInstanceRenderHooks(swapChain).resizeBuffers;
    return reinterpret_cast<void*>(fn) == g_resizeBuffersTarget ? g_origResizeBuffers : (fn ? fn : g_origResizeBuffers);
}

// Re-entrancy guard: RenderOverlayFrame calls ExecuteCommandLists on the game queue to
// submit overlay draw commands. Without this flag, HookedExecuteCommandLists would see
// our own submission and could overwrite the captured queue or emit spurious log lines.
static thread_local bool g_inOverlaySubmit = false;

static IUnknown* GetComIdentity(IUnknown* object) {
    if (!object) return nullptr;
    IUnknown* identity = nullptr;
    if (FAILED(object->QueryInterface(IID_PPV_ARGS(&identity)))) return nullptr;
    return identity;
}

static void AssociateSwapChainQueue(IDXGISwapChain* swapChain, ID3D12CommandQueue* queue) {
    IUnknown* identity = GetComIdentity(swapChain);
    if (!identity || !queue) {
        if (identity) identity->Release();
        return;
    }

    queue->AddRef();
    {
        std::lock_guard<std::mutex> lock(g_swapChainQueuesMutex);
        auto [it, inserted] = g_swapChainQueues.emplace(identity, queue);
        if (!inserted) {
            it->second->Release();
            it->second = queue;
        }
    }

    UILog(std::format("Associated swap chain 0x{:X} (identity 0x{:X}) with D3D12 queue 0x{:X}.",
        reinterpret_cast<uintptr_t>(swapChain), reinterpret_cast<uintptr_t>(identity),
        reinterpret_cast<uintptr_t>(queue)));
    identity->Release();
}

ID3D12CommandQueue* FindAssociatedQueue(IDXGISwapChain* swapChain) {
    IUnknown* identity = GetComIdentity(swapChain);
    if (!identity) return nullptr;

    ID3D12CommandQueue* queue = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_swapChainQueuesMutex);
        auto it = g_swapChainQueues.find(identity);
        if (it != g_swapChainQueues.end()) queue = it->second;
    }
    identity->Release();
    return queue;
}

static bool QueueMatchesSwapChainDevice(ID3D12CommandQueue* queue, IDXGISwapChain* swapChain) {
    if (!queue || !swapChain) return false;
    ID3D12Device* queueDevice = nullptr;
    ID3D12Device* swapDevice = nullptr;
    const bool ok = SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&queueDevice))) &&
                    SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(&swapDevice))) &&
                    queueDevice == swapDevice;
    if (queueDevice) queueDevice->Release();
    if (swapDevice) swapDevice->Release();
    return ok;
}

ID3D12CommandQueue* SelectPresentQueue(IDXGISwapChain* swapChain) {
    if (ID3D12CommandQueue* queue = FindAssociatedQueue(swapChain)) return queue;
    ID3D12CommandQueue* recent = g_recentDirectQueue.load(std::memory_order_relaxed);
    return QueueMatchesSwapChainDevice(recent, swapChain) ? recent : nullptr;
}

void ReleaseSwapChainQueueAssociations() {
    std::lock_guard<std::mutex> lock(g_swapChainQueuesMutex);
    for (auto& [identity, queue] : g_swapChainQueues) {
        (void)identity;
        queue->Release();
    }
    g_swapChainQueues.clear();
}


HRESULT STDMETHODCALLTYPE HookedCreateSwapChainForHwnd(
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
    HRESULT hr = g_origCreateSwapChainForHwnd(
        pThis, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
    if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain && pDevice) {
        ID3D12CommandQueue* queue = nullptr;
        if (SUCCEEDED(pDevice->QueryInterface(IID_PPV_ARGS(&queue)))) {
            // Associate the chain returned by the complete hook stack. An earlier
            // injector may have replaced the native chain with its proxy object.
            AssociateSwapChainQueue(*ppSwapChain, queue);
            queue->Release();
        }
        InstallInstanceRenderHooks(*ppSwapChain);
    }
    // Selection belongs to HandlePresent, where the returned swap-chain identity
    // can be matched to its queue. Do not preserve the legacy process-wide latch.
    g_gameCommandQueue.store(nullptr, std::memory_order_relaxed);
    return hr;
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
void STDMETHODCALLTYPE HookedExecuteCommandLists(ID3D12CommandQueue* pQueue, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists) {
    if (!g_inOverlaySubmit) {
        D3D12_COMMAND_QUEUE_DESC desc = pQueue->GetDesc();
        if (desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
            g_recentDirectQueue.store(pQueue, std::memory_order_relaxed);
        }
    }
    g_origExecuteCommandLists(pQueue, NumCommandLists, ppCommandLists);
}

// ============================================================================
// ImGui Safe Teardown
// ============================================================================


bool GetVtablePointers(void** outPresent, void** outPresent1, void** outResizeBuffers, void** outExecuteCommandLists, void** outCreateSwapChainForHwnd) {
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
bool LooksHooked(void* fn, std::string& firstBytesOut) {
    if (!fn) return false;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(fn);
    firstBytesOut = std::format("{:02X} {:02X} {:02X}",
        static_cast<unsigned>(p[0]), static_cast<unsigned>(p[1]), static_cast<unsigned>(p[2]));
    if (p[0] == 0xE9) return true;                  // jmp rel32
    if (p[0] == 0xEB) return true;                  // jmp rel8
    if (p[0] == 0xFF && p[1] == 0x25) return true;  // jmp qword ptr [rip+disp32]
    return false;
}

void* ResolveInitialJump(void* fn) {
    if (!fn) return nullptr;
    const auto* p = reinterpret_cast<const unsigned char*>(fn);
    if (p[0] == 0xE9) {
        const auto displacement = *reinterpret_cast<const std::int32_t*>(p + 1);
        return const_cast<unsigned char*>(p + 5 + displacement);
    }
    if (p[0] == 0xEB) {
        const auto displacement = *reinterpret_cast<const std::int8_t*>(p + 1);
        return const_cast<unsigned char*>(p + 2 + displacement);
    }
    if (p[0] == 0xFF && p[1] == 0x25) {
        const auto displacement = *reinterpret_cast<const std::int32_t*>(p + 2);
        auto slot = reinterpret_cast<void* const*>(p + 6 + displacement);
        return *slot;
    }
    return nullptr;
}

std::string ModuleForAddress(void* address) {
    if (!address) return "<unresolved>";
    MEMORY_BASIC_INFORMATION memory{};
    if (!VirtualQuery(address, &memory, sizeof(memory)) || !memory.AllocationBase) {
        return "<unknown>";
    }
    char path[MAX_PATH]{};
    if (!GetModuleFileNameA(static_cast<HMODULE>(memory.AllocationBase), path,
        static_cast<DWORD>(std::size(path)))) {
        return "<anonymous executable memory>";
    }
    return std::filesystem::path(path).filename().string();
}

}  // namespace UIHook::Detail
