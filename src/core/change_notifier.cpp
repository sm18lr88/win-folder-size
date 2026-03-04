#include "core/change_notifier.h"
#include "core/size_cache.h"
#include "logging.h"

#include <windows.h>
#include <shlobj.h>
#include <string>
#include <atomic>

// Window-class name for the hidden notification window.
static constexpr wchar_t kWndClass[] = L"FolderSizeChangeNotifier";

// The shell notification message we register for.
// WM_APP is 0x8000; using WM_APP+1 avoids clashing with other WM_USER apps.
static constexpr UINT kNotifyMsg = WM_APP + 1;

// Provided by dll_main.cpp
extern HINSTANCE GetDllInstance() noexcept;

namespace fs::core {

// ============================================================================
// Singleton
// ============================================================================

ChangeNotifier& ChangeNotifier::instance() {
    static ChangeNotifier s_instance;
    return s_instance;
}

// ============================================================================
// Cache invalidation helper
// ============================================================================

// Invalidate path AND every ancestor up to the drive root, so that
// re-navigating a parent folder shows updated sizes.
void ChangeNotifier::invalidate_path_and_ancestors(const std::wstring& path) {
    std::wstring p = path;
    // Strip trailing backslash (but keep "C:\")
    while (p.size() > 3 && p.back() == L'\\') p.pop_back();

    while (!p.empty()) {
        fs::SizeCache::instance().invalidate(p);
        FS_TRACE("NOTIFIER", "Invalidated cache: %ls", p.c_str());

        size_t lastSep = p.rfind(L'\\');
        if (lastSep == std::wstring::npos) break;

        // Stop at drive root ("C:\")
        if (lastSep <= 2) {
            // Invalidate the drive root entry too
            std::wstring root = p.substr(0, lastSep + 1); // "C:\"
            fs::SizeCache::instance().invalidate(root);
            break;
        }
        p = p.substr(0, lastSep);
    }
}

// ============================================================================
// Background thread
// ============================================================================

DWORD WINAPI ChangeNotifier::thread_proc(LPVOID pParam) {
    ChangeNotifier* self = static_cast<ChangeNotifier*>(pParam);

    // -------------------------------------------------------------------------
    // Register a minimal window class for our hidden message-only window.
    // -------------------------------------------------------------------------
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance     = GetDllInstance();
    wc.lpszClassName = kWndClass;
    RegisterClassExW(&wc); // OK if already registered

    HWND hwnd = CreateWindowExW(
        0, kWndClass, nullptr, 0,
        0, 0, 0, 0,
        HWND_MESSAGE,       // message-only window (never visible)
        nullptr,
        GetDllInstance(),
        nullptr);

    if (!hwnd) {
        fs::log::diagnostic_logf("ChangeNotifier: CreateWindowEx failed err=%lu",
                                 GetLastError());
        return 1;
    }

    // -------------------------------------------------------------------------
    // Register for shell change notifications from all paths (desktop root,
    // recursive so we capture changes on every drive and UNC share).
    // SHCNRF_NewDelivery: notifications delivered as lockable handles (safer
    // for multi-process scenarios than raw PIDLs in message params).
    // -------------------------------------------------------------------------
    PIDLIST_ABSOLUTE pidlDesktop = nullptr;
    SHGetSpecialFolderLocation(nullptr, CSIDL_DESKTOP, &pidlDesktop);

    SHChangeNotifyEntry entry = {};
    entry.pidl       = pidlDesktop; // nullptr also works but explicit is clearer
    entry.fRecursive = TRUE;

    ULONG ulRegID = SHChangeNotifyRegister(
        hwnd,
        SHCNRF_ShellLevel | SHCNRF_NewDelivery,
        SHCNE_UPDATEDIR  |
        SHCNE_RMDIR      |
        SHCNE_MKDIR      |
        SHCNE_CREATE     |
        SHCNE_DELETE     |
        SHCNE_RENAMEITEM |
        SHCNE_RENAMEFOLDER,
        kNotifyMsg,
        1, &entry);

    if (pidlDesktop) CoTaskMemFree(pidlDesktop);

    if (!ulRegID) {
        fs::log::diagnostic_log("ChangeNotifier: SHChangeNotifyRegister failed");
        DestroyWindow(hwnd);
        UnregisterClassW(kWndClass, GetDllInstance());
        return 1;
    }

    fs::log::diagnostic_log("ChangeNotifier: registered for shell notifications");

    // -------------------------------------------------------------------------
    // Message loop — pump until the stop event is signaled.
    // -------------------------------------------------------------------------
    while (true) {
        DWORD wait = MsgWaitForMultipleObjects(
            1, &self->m_stop_event,
            FALSE, INFINITE, QS_ALLINPUT);

        if (wait == WAIT_OBJECT_0) {
            // Stop event signaled — exit cleanly
            break;
        }

        // Drain all pending messages
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == kNotifyMsg) {
                // With SHCNRF_NewDelivery: wParam = lock handle, lParam = DWORD dwProcID
                PIDLIST_ABSOLUTE* ppidls = nullptr;
                LONG lEvent = 0;
                HANDLE hLock = SHChangeNotification_Lock(
                    reinterpret_cast<HANDLE>(msg.wParam),
                    static_cast<DWORD>(msg.lParam),
                    &ppidls,
                    &lEvent);

                if (hLock && ppidls) {
                    // ppidls[0] = affected path, ppidls[1] = new path (for renames)
                    wchar_t path[MAX_PATH] = {};
                    if (ppidls[0] && SHGetPathFromIDListW(ppidls[0], path) && path[0]) {
                        invalidate_path_and_ancestors(path);
                        FS_DEBUG("NOTIFIER", "Shell event 0x%lx: %ls", lEvent, path);
                    }
                    // For renames, also invalidate the new name
                    if ((lEvent & (SHCNE_RENAMEITEM | SHCNE_RENAMEFOLDER)) && ppidls[1]) {
                        wchar_t newPath[MAX_PATH] = {};
                        if (SHGetPathFromIDListW(ppidls[1], newPath) && newPath[0]) {
                            invalidate_path_and_ancestors(newPath);
                        }
                    }
                    SHChangeNotification_Unlock(hLock);
                }
            } else {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }

    // -------------------------------------------------------------------------
    // Cleanup
    // -------------------------------------------------------------------------
    SHChangeNotifyDeregister(ulRegID);
    DestroyWindow(hwnd);
    UnregisterClassW(kWndClass, GetDllInstance());

    fs::log::diagnostic_log("ChangeNotifier: thread exited cleanly");
    return 0;
}

// ============================================================================
// Public API
// ============================================================================

void ChangeNotifier::start() {
    if (m_thread) return; // already running

    m_stop_event = CreateEventW(nullptr, TRUE /*manual reset*/, FALSE, nullptr);
    if (!m_stop_event) {
        fs::log::diagnostic_logf("ChangeNotifier: CreateEvent failed err=%lu",
                                 GetLastError());
        return;
    }

    m_thread = CreateThread(nullptr, 0, thread_proc, this, 0, nullptr);
    if (!m_thread) {
        fs::log::diagnostic_logf("ChangeNotifier: CreateThread failed err=%lu",
                                 GetLastError());
        CloseHandle(m_stop_event);
        m_stop_event = nullptr;
        return;
    }

    fs::log::diagnostic_log("ChangeNotifier: background thread started");
}

void ChangeNotifier::stop() {
    if (!m_thread) return;

    SetEvent(m_stop_event);
    // Give the thread up to 5 seconds to flush its message queue and exit.
    WaitForSingleObject(m_thread, 5000);

    CloseHandle(m_thread);
    CloseHandle(m_stop_event);
    m_thread     = nullptr;
    m_stop_event = nullptr;

    fs::log::diagnostic_log("ChangeNotifier: stopped");
}

} // namespace fs::core
