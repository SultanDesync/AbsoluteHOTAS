#include "PCH.h"

#include "UIHookInternal.h"

#include <imgui.h>
#include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace UIHook::Detail {


UINT GetToggleUIMessage() {
    static UINT msg = RegisterWindowMessageW(L"AbsoluteHOTAS_ToggleUI");
    return msg;
}

// ============================================================================
// WndProc Hook
// ============================================================================
LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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


void RestoreGameCursorState() {
    while (ShowCursor(FALSE) >= 0) {}
    if (g_hadClipRect) ClipCursor(&g_savedClipRect);
    g_hadClipRect = false;
}

}  // namespace UIHook::Detail
