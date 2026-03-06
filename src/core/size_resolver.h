#pragma once

#include <windows.h>
#include <shlobj.h>
#include <propsys.h>
#include <propkey.h>
#include <string>
#include <optional>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace fs::core {

// Central orchestrator: resolves folder sizes by querying providers
// (cache → Everything → scanner) and injects them into Explorer's property pipeline.
class SizeResolver {
public:
    static SizeResolver& instance();

    // Called from hooked_GetSize when VT_EMPTY detected (folder with no size).
    // Extracts path from shell folder + child PIDL, resolves size, injects into PROPVARIANT.
    // Returns true if size was injected into pv, false to keep original (blank).
    bool resolve_and_inject(void* pCFSFolder, const ITEMID_CHILD* pidlChild,
                            PROPVARIANT* pv);

    // Called from hooked_PSFormat to intercept PKEY_Size formatting.
    // Returns S_OK with ppszDisplay set if we formatted, E_FAIL to use original.
    HRESULT format_size_display(REFPROPERTYKEY key, REFPROPVARIANT propvar,
                                PROPDESC_FORMAT_FLAGS pdff, PWSTR* ppszDisplay);

    // Non-copyable
    SizeResolver(const SizeResolver&) = delete;
    SizeResolver& operator=(const SizeResolver&) = delete;

private:
    SizeResolver() = default;
    ~SizeResolver();

    // Extract filesystem path from CFSFolder + child PIDL via COM interfaces
    std::optional<std::wstring> extract_path(void* pCFSFolder, const ITEMID_CHILD* pidlChild);

    // Resolve folder size on the background worker: cache → Everything (NTFS) → scanner.
    std::optional<uint64_t> resolve_size(const std::wstring& path);

    // Fast path for the Explorer hook: cache lookup only, no provider I/O.
    std::optional<uint64_t> get_cached_size(const std::wstring& path);

    // Queue a background lookup if one is not already in flight.
    bool queue_background_resolution(const std::wstring& path);
    void ensure_worker_started();
    void worker_main();
    static std::wstring normalize_request_path(std::wstring_view path);
    static void notify_shell_item_changed(const std::wstring& path);

    // Follow reparse points / junctions / symlinks to get the canonical path.
    // Returns nullopt if not a reparse point, resolution fails, or path unchanged.
    std::optional<std::wstring> resolve_real_path(const std::wstring& path);

    // Everything availability backoff
    bool should_try_everything();
    void mark_everything_unavailable();

    std::atomic<bool> m_everythingAvailable{true};
    std::chrono::steady_clock::time_point m_lastEverythingRetry{};
    std::mutex m_retryMutex;
    static constexpr auto EVERYTHING_RETRY_INTERVAL = std::chrono::seconds(30);

    std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    std::deque<std::wstring> m_queue;
    std::unordered_set<std::wstring> m_inFlight;
    std::unordered_map<std::wstring, std::chrono::steady_clock::time_point> m_recentFailures;
    std::thread m_worker;
    bool m_stopWorker{false};

    static constexpr size_t MAX_PENDING_REQUESTS = 64;
    static constexpr auto FAILURE_RETRY_INTERVAL = std::chrono::seconds(15);
};

} // namespace fs::core
