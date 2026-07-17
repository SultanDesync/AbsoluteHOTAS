#pragma once

#include "UIHook.h"

#include <d3d12.h>
#include <dxgi1_4.h>

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>

namespace UIHook::Detail {

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT,
                                               const DXGI_PRESENT_PARAMETERS*);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT,
                                                    UINT, DXGI_FORMAT, UINT);
using ExecuteCommandListsFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT,
                                                       ID3D12CommandList* const*);
using CreateSwapChainForHwndFn = HRESULT(STDMETHODCALLTYPE*)(
    IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);

struct InstanceRenderHooks {
    PresentFn present = nullptr;
    Present1Fn present1 = nullptr;
    ResizeBuffersFn resizeBuffers = nullptr;
    void** shadowVtable = nullptr;
};

extern std::atomic<bool> g_isOpen;
extern CloseGuardCallback g_closeGuardCallback;
extern std::atomic<bool> g_initialized;
extern std::atomic<bool> g_logNextOverlaySubmit;
extern std::mutex g_initMutex;
extern DrawCallback g_drawCallback;
extern RECT g_savedClipRect;
extern bool g_hadClipRect;

extern ID3D12Device* g_d3dDevice;
extern ID3D12DescriptorHeap* g_srvDescHeap;
extern ID3D12CommandAllocator** g_cmdAllocators;
extern ID3D12GraphicsCommandList* g_cmdList;
extern std::atomic<ID3D12CommandQueue*> g_gameCommandQueue;
extern std::atomic<ID3D12CommandQueue*> g_recentDirectQueue;

extern std::mutex g_swapChainQueuesMutex;
extern std::unordered_map<IUnknown*, ID3D12CommandQueue*> g_swapChainQueues;

extern ID3D12Fence* g_fence;
extern HANDLE g_fenceEvent;
extern UINT64 g_fenceValue;
extern UINT g_numBackBuffers;
extern ID3D12Resource** g_backBuffers;
extern D3D12_CPU_DESCRIPTOR_HANDLE* g_rtvHandles;
extern ID3D12DescriptorHeap* g_rtvDescHeap;
extern DXGI_FORMAT g_backBufferFormat;
extern UINT g_lastSwapWidth;
extern UINT g_lastSwapHeight;
extern IDXGISwapChain* g_targetSwapChain;

extern HWND g_hWnd;
extern WNDPROC g_origWndProc;

extern PresentFn g_origPresent;
extern Present1Fn g_origPresent1;
extern ResizeBuffersFn g_origResizeBuffers;
extern ExecuteCommandListsFn g_origExecuteCommandLists;
extern CreateSwapChainForHwndFn g_origCreateSwapChainForHwnd;
extern void* g_presentTarget;
extern void* g_present1Target;
extern void* g_resizeBuffersTarget;

extern std::mutex g_instanceHooksMutex;
extern std::unordered_map<IUnknown*, InstanceRenderHooks> g_instanceHooks;
extern thread_local bool g_inOverlaySubmit;

void UILog(const std::string& message);

PresentFn OriginalPresentFor(IDXGISwapChain* swapChain);
Present1Fn OriginalPresent1For(IDXGISwapChain1* swapChain);
ResizeBuffersFn OriginalResizeBuffersFor(IDXGISwapChain* swapChain);
ID3D12CommandQueue* FindAssociatedQueue(IDXGISwapChain* swapChain);
ID3D12CommandQueue* SelectPresentQueue(IDXGISwapChain* swapChain);
void ReleaseSwapChainQueueAssociations();

HRESULT STDMETHODCALLTYPE HookedCreateSwapChainForHwnd(
    IDXGIFactory2* factory, IUnknown* device, HWND window,
    const DXGI_SWAP_CHAIN_DESC1* desc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreenDesc,
    IDXGIOutput* restrictToOutput, IDXGISwapChain1** swapChain);
void STDMETHODCALLTYPE HookedExecuteCommandLists(
    ID3D12CommandQueue* queue, UINT commandListCount,
    ID3D12CommandList* const* commandLists);

HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* swapChain,
                                        UINT syncInterval, UINT flags);
HRESULT STDMETHODCALLTYPE HookedPresent1(
    IDXGISwapChain1* swapChain, UINT syncInterval, UINT flags,
    const DXGI_PRESENT_PARAMETERS* parameters);
HRESULT STDMETHODCALLTYPE HookedResizeBuffers(
    IDXGISwapChain* swapChain, UINT bufferCount, UINT width, UINT height,
    DXGI_FORMAT format, UINT flags);

UINT GetToggleUIMessage();
LRESULT CALLBACK HookedWndProc(HWND window, UINT message, WPARAM wParam,
                               LPARAM lParam);
void RestoreGameCursorState();

void WaitForGpu();
void CleanupRenderTargets();

bool GetVtablePointers(void** present, void** present1, void** resizeBuffers,
                       void** executeCommandLists,
                       void** createSwapChainForHwnd);
bool LooksHooked(void* function, std::string& firstBytes);
void* ResolveInitialJump(void* function);
std::string ModuleForAddress(void* address);

}  // namespace UIHook::Detail
