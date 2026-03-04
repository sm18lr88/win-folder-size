#pragma once
#include <windows.h>

namespace fs::hooks {

// Hooks RegQueryValueExW from kernelbase.dll to inject System.Size into
// Explorer's shell property format strings, enabling folder sizes in
// Tiles, Content, Details-pane, and Status-bar views.
//
// Must be called from within a DetourTransaction (install_hooks / remove_hooks).
void install_reg_query_hook();
void remove_reg_query_hook();

} // namespace fs::hooks
