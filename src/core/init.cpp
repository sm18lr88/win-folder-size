#include "core/init.h"
#include "logging.h"
#include "hooks/hook_manager.h"
#include "core/size_cache.h"
#include <mutex>
#include <thread>
#include <atomic>
#include "providers/everything_client.h"
#include "providers/folder_scanner.h"

namespace fs::core {

static std::once_flag s_initFlag;
static std::atomic<bool> s_initialized{false};
static std::atomic<bool> s_shutdownRequested{false};

static void background_init() {
    fs::log::diagnostic_log("Background init thread started");
    FS_INFO(FS_MOD_INIT, "Background initialization started");

    if (s_shutdownRequested.load()) {
        FS_WARN(FS_MOD_INIT, "Shutdown requested before init completed, aborting");
        return;
    }

    bool result = fs::hooks::HookManager::instance().install_hooks();
    if (result) {
        fs::log::diagnostic_log("Hooks installed successfully");
        s_initialized.store(true);
        FS_INFO(FS_MOD_INIT, "Hooks installed successfully — folder sizes active");
    } else {
        fs::log::diagnostic_log("Hook installation FAILED");
        FS_WARN(FS_MOD_INIT, "Hook installation failed — running in pass-through mode");
    }
}

void initialize_foldersize() {
    std::call_once(s_initFlag, []() {
        FS_INFO(FS_MOD_INIT, "Launching background initialization thread");
        std::thread(background_init).detach();
    });
}

void shutdown_foldersize() {
    FS_INFO(FS_MOD_INIT, "Shutdown requested");
    s_shutdownRequested.store(true);

    if (s_initialized.load()) {
        // Remove hooks
        fs::hooks::HookManager::instance().remove_hooks();
        s_initialized.store(false);
        FS_INFO(FS_MOD_INIT, "Hooks removed, shutdown complete");
    }

    // Disconnect Everything client
    fs::EverythingClient::instance().disconnect();

    // Cancel pending scans
    fs::providers::FolderScanner::instance().cancel_all();

    // Clear cache
    fs::SizeCache::instance().clear();
}

bool is_initialized() {
    return s_initialized.load();
}

} // namespace fs::core
