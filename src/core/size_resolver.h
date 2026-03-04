#pragma once

#include <windows.h>
#include <shlobj.h>
#include <propsys.h>
#include <propkey.h>
#include <string>
#include <optional>
#include <atomic>
#include <chrono>
#include <mutex>

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
    ~SizeResolver() = default;

    // Extract filesystem path from CFSFolder + child PIDL via COM interfaces
    std::optional<std::wstring> extract_path(void* pCFSFolder, const ITEMID_CHILD* pidlChild);

    // Resolve folder size: cache → Everything (NTFS) → scanner (non-NTFS)
    std::optional<uint64_t> resolve_size(const std::wstring& path);

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
};

} // namespace fs::core
