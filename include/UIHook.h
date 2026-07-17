#pragma once

#include <atomic>

namespace UIHook {
    // Install the D3D12 Present hook. Call once from SFSEPluginLoad.
    bool Install();

    // Cleanup hooks on shutdown.
    void Shutdown();

    // Toggle the ImGui overlay on/off.
    void ToggleUI();

    // Query whether the overlay is currently visible.
    bool IsUIOpen();

    // Called each frame by the Present hook to render ImGui.
    // Users register a draw callback via SetDrawCallback.
    using DrawCallback = void(*)();
    void SetDrawCallback(DrawCallback cb);

    // Called before an ordinary toggle request closes the overlay. Returning
    // false keeps it open so the client can present a save/discard decision.
    using CloseGuardCallback = bool(*)();
    void SetCloseGuardCallback(CloseGuardCallback cb);
}
