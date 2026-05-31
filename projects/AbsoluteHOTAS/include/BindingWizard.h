#pragma once

namespace BindingWizard {
    // Initialize the binding wizard (register draw callback with UIHook).
    void Initialize();

    // The ImGui draw function called each frame by UIHook.
    void Draw();
}
