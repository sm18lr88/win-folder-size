#pragma once

namespace fs::core {

// Call once to start background initialization (hook installation, etc.)
// Safe to call multiple times — uses std::call_once internally.
void initialize_foldersize();

// Call on DLL_PROCESS_DETACH to cleanly remove hooks and flush caches.
// Must NOT be called from DllMain with loader lock held if it does heavy work.
// In practice, we keep this lightweight: just set flags and let things unwind.
void shutdown_foldersize();

// Returns true if hooks are installed and ready
bool is_initialized();

} // namespace fs::core
