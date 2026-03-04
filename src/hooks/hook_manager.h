#pragma once

#include <windows.h>
#include <dbghelp.h>
#include <propsys.h>
#include <propvarutil.h>
#include <shlobj.h>
#include <propkey.h>
#include <mutex>

namespace fs::hooks {

// ============================================================================
// RecursiveOpGuard - RAII scoped guard for recursive operation tracking
// ============================================================================
class RecursiveOpGuard {
    static thread_local int s_depth;

public:
    RecursiveOpGuard() noexcept { ++s_depth; }
    ~RecursiveOpGuard() noexcept { --s_depth; }

    static bool active() noexcept { return s_depth > 0; }

    // Non-copyable, non-movable
    RecursiveOpGuard(const RecursiveOpGuard&) = delete;
    RecursiveOpGuard& operator=(const RecursiveOpGuard&) = delete;
};

// ============================================================================
// HookManager - Meyers singleton for managing Detours hooks
// ============================================================================
class HookManager {
public:
    // Get singleton instance
    static HookManager& instance();

    // Install all hooks (symbol resolution + Detours attachment)
    bool install_hooks();

    // Remove all hooks and cleanup
    void remove_hooks();

    // Check if hooks are currently active
    bool is_active() const;

    // Non-copyable
    HookManager(const HookManager&) = delete;
    HookManager& operator=(const HookManager&) = delete;

private:
    HookManager() = default;
    ~HookManager() = default;

    bool m_active = false;
    std::once_flag m_initFlag;

    // Hook function pointers (originals)
    // These are file-static in the .cpp file, not stored here
};

} // namespace fs::hooks
