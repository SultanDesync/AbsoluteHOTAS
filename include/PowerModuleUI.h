#pragma once

#include <string_view>

namespace PowerModuleUI {
void Initialize();
bool Available();
void Draw();
bool Dirty();
bool Save();
void Discard();
void CancelTransientInteractions();
std::string_view StatusText();
} // namespace PowerModuleUI
