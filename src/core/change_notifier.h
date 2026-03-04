#pragma once
#include <windows.h>
#include <string>

namespace fs::core {

// Runs a background thread with a hidden message window that receives
// SHChangeNotifyRegister notifications.  On directory-change events the
// affected path (and all its ancestors) are invalidated from SizeCache,
// ensuring Explorer shows fresh folder sizes after file-system mutations.
//
// This is our unique feature vs. Windhawk, which has no cache invalidation.
class ChangeNotifier {
public:
    static ChangeNotifier& instance();

    // Spawn the background thread.  No-op if already running.
    void start();

    // Signal the thread to stop and wait for it to exit (≤5 s timeout).
    void stop();

    // Non-copyable
    ChangeNotifier(const ChangeNotifier&) = delete;
    ChangeNotifier& operator=(const ChangeNotifier&) = delete;

private:
    ChangeNotifier() = default;
    ~ChangeNotifier() = default;

    static DWORD WINAPI thread_proc(LPVOID pParam);
    static void invalidate_path_and_ancestors(const std::wstring& path);

    HANDLE m_thread     = nullptr;
    HANDLE m_stop_event = nullptr;
};

} // namespace fs::core
