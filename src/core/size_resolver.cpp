#include "core/size_resolver.h"
#include "logging.h"
#include "core/size_cache.h"
#include "core/size_formatter.h"
#include "providers/everything_client.h"
#include "providers/folder_scanner.h"

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <propkey.h>
#include <propvarutil.h>
#include <objbase.h>
#include <atomic>

namespace fs::core {

// ============================================================================
// Singleton
// ============================================================================

SizeResolver& SizeResolver::instance() {
    static SizeResolver s_instance;
    return s_instance;
}

// ============================================================================
// Path Extraction — COM interface chain to get filesystem path from PIDL
// ============================================================================
//
// Chain: pCFSFolder → IShellFolder2 → BindToObject(pidlChild) → IShellFolder2
//        → IPersistFolder2 → GetCurFolder() → PIDL → SHGetPathFromIDListEx → path
//
// Every COM pointer is released on each step. No ComPtr used — explicit Release()
// for minimal overhead in this hot path.

std::optional<std::wstring> SizeResolver::extract_path(void* pCFSFolder,
                                                        const ITEMID_CHILD* pidlChild) {
    FS_SCOPED_TIMER(FS_MOD_RESOLVER, "extract_path");

    if (!pCFSFolder || !pidlChild) {
        return std::nullopt;
    }

    // Step 1: QI pCFSFolder → IShellFolder2
    IShellFolder2* pFolder = nullptr;
    HRESULT hr = reinterpret_cast<IUnknown*>(pCFSFolder)->QueryInterface(
        IID_IShellFolder2, reinterpret_cast<void**>(&pFolder));
    if (FAILED(hr) || !pFolder) {
        FS_TRACE(FS_MOD_RESOLVER, "QI IShellFolder2 failed: 0x%lx", hr);
        return std::nullopt;
    }

    // Step 2: BindToObject to get child folder interface
    IShellFolder2* pChildFolder = nullptr;
    hr = pFolder->BindToObject(pidlChild, nullptr, IID_IShellFolder2,
                               reinterpret_cast<void**>(&pChildFolder));
    pFolder->Release();
    if (FAILED(hr) || !pChildFolder) {
        // Expected for files — BindToObject only works on folders
        FS_TRACE(FS_MOD_RESOLVER, "BindToObject failed (likely a file): 0x%lx", hr);
        return std::nullopt;
    }

    // Step 3: QI → IPersistFolder2 to get the folder's absolute PIDL
    IPersistFolder2* pPersist = nullptr;
    hr = pChildFolder->QueryInterface(IID_IPersistFolder2,
                                      reinterpret_cast<void**>(&pPersist));
    pChildFolder->Release();
    if (FAILED(hr) || !pPersist) {
        FS_TRACE(FS_MOD_RESOLVER, "QI IPersistFolder2 failed: 0x%lx", hr);
        return std::nullopt;
    }

    // Step 4: GetCurFolder → absolute PIDL
    PIDLIST_ABSOLUTE pidlAbsolute = nullptr;
    hr = pPersist->GetCurFolder(&pidlAbsolute);
    pPersist->Release();
    if (FAILED(hr) || !pidlAbsolute) {
        FS_TRACE(FS_MOD_RESOLVER, "GetCurFolder failed: 0x%lx", hr);
        return std::nullopt;
    }

    // Step 5: PIDL → filesystem path
    wchar_t pathBuf[1024]; // MAX_PATH * 2+ for long paths
    BOOL ok = SHGetPathFromIDListEx(pidlAbsolute, pathBuf, ARRAYSIZE(pathBuf),
                                     GPFIDL_DEFAULT);
    CoTaskMemFree(pidlAbsolute);
    if (!ok || pathBuf[0] == L'\0') {
        FS_TRACE(FS_MOD_RESOLVER, "SHGetPathFromIDListEx failed (virtual folder?)");
        return std::nullopt;
    }

    // Step 6: Strip extended-length prefix
    std::wstring path(pathBuf);
    if (path.starts_with(L"\\\\?\\")) {
        path = path.substr(4);
    }

    FS_TRACE(FS_MOD_RESOLVER, "Extracted path: %ls", path.c_str());
    return path;
}

// ============================================================================
// Everything Availability Backoff
// ============================================================================

bool SizeResolver::should_try_everything() {
    if (m_everythingAvailable.load(std::memory_order_relaxed)) {
        return true;
    }

    // Check if backoff period has elapsed
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(m_retryMutex);
    if (now - m_lastEverythingRetry >= EVERYTHING_RETRY_INTERVAL) {
        m_everythingAvailable.store(true, std::memory_order_relaxed);
        FS_DEBUG(FS_MOD_RESOLVER, "Everything backoff expired, will retry");
        return true;
    }

    return false;
}

void SizeResolver::mark_everything_unavailable() {
    std::lock_guard<std::mutex> lock(m_retryMutex);
    m_everythingAvailable.store(false, std::memory_order_relaxed);
    m_lastEverythingRetry = std::chrono::steady_clock::now();
    FS_WARN(FS_MOD_RESOLVER, "Everything marked unavailable, backoff %lld seconds",
            static_cast<long long>(EVERYTHING_RETRY_INTERVAL.count()));
}

// ============================================================================
// Reparse Point Resolution — junctions, symlinks, OneDrive placeholders
// ============================================================================

std::optional<std::wstring> SizeResolver::resolve_real_path(const std::wstring& path) {
    // Open the directory without following reparse points (backup semantics
    // lets us open directories; FILE_FLAG_BACKUP_SEMANTICS is required).
    HANDLE hFile = CreateFileW(
        path.c_str(),
        FILE_READ_EA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, // required for directories
        nullptr);

    if (hFile == INVALID_HANDLE_VALUE) return std::nullopt;

    wchar_t resolved[32768] = {};
    DWORD result = GetFinalPathNameByHandleW(
        hFile, resolved, static_cast<DWORD>(std::size(resolved)),
        FILE_NAME_NORMALIZED);
    CloseHandle(hFile);

    if (result == 0 || result >= std::size(resolved)) return std::nullopt;

    std::wstring canonical(resolved);
    // Strip the \\?\ extended-path prefix added by GetFinalPathNameByHandle
    if (canonical.starts_with(L"\\\\?\\")) canonical = canonical.substr(4);

    return canonical;
}

// ============================================================================
// Size Resolution Pipeline: Cache → Everything (NTFS) → Scanner (non-NTFS)
// Reparse points are resolved before querying Everything so that junctions
// and symlinks map to a path that Everything has indexed.
// ============================================================================

std::optional<uint64_t> SizeResolver::resolve_size(const std::wstring& path) {
    FS_SCOPED_TIMER(FS_MOD_RESOLVER, "resolve_size");

    // 1. Cache check (fast path — sub-microsecond for hits)
    auto cached = fs::SizeCache::instance().get(path);
    if (cached.has_value()) {
        FS_TRACE(FS_MOD_RESOLVER, "Cache hit: %ls = %llu bytes", path.c_str(),
                 cached.value());
        return cached;
    }

    // 2. If the path is a reparse point (junction / symlink), resolve it so
    //    Everything can find the real directory.  Skip UNC paths — resolution
    //    can be slow for unmapped network shares.
    std::wstring effectivePath = path;
    if (!path.starts_with(L"\\\\")) {
        WIN32_FILE_ATTRIBUTE_DATA fad = {};
        if (GetFileAttributesEx(path.c_str(), GetFileExInfoStandard, &fad) &&
            (fad.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            auto resolved = resolve_real_path(path);
            if (resolved && *resolved != path) {
                FS_TRACE(FS_MOD_RESOLVER, "Reparse: %ls -> %ls",
                         path.c_str(), resolved->c_str());
                effectivePath = std::move(*resolved);
            }
        }
    }

    // 3. Determine drive filesystem type (use effectivePath's drive)
    wchar_t driveLetter = L'\0';
    if (effectivePath.length() >= 2 && effectivePath[1] == L':') {
        driveLetter = towupper(effectivePath[0]);
    }

    if (driveLetter == L'\0') {
        FS_TRACE(FS_MOD_RESOLVER, "No drive letter in path: %ls", effectivePath.c_str());
        return std::nullopt; // UNC paths, etc. — not supported
    }

    bool ntfs = fs::providers::FolderScanner::is_ntfs(driveLetter);

    if (ntfs) {
        // 4a. NTFS → Everything (pre-indexed, ~3ms per query)
        if (!should_try_everything()) {
            FS_TRACE(FS_MOD_RESOLVER, "Everything in backoff, skipping: %ls", path.c_str());
            return std::nullopt;
        }

        auto size = fs::EverythingClient::instance().get_folder_size(effectivePath);
        if (size.has_value()) {
            fs::SizeCache::instance().put(path, size.value()); // cache under original path
            FS_TRACE(FS_MOD_RESOLVER, "Everything: %ls = %llu bytes", path.c_str(),
                     size.value());
            return size;
        }

        // Everything failed — enter backoff, show blank for NTFS
        mark_everything_unavailable();
        return std::nullopt;
    } else {
        // 4b. Non-NTFS → recursive scanner (200ms timeout)
        auto size = fs::providers::FolderScanner::instance().scan_sync(
            effectivePath, std::chrono::milliseconds(200));
        if (size.has_value()) {
            fs::SizeCache::instance().put(path, size.value());
            FS_TRACE(FS_MOD_RESOLVER, "Scanner: %ls = %llu bytes", path.c_str(),
                     size.value());
            return size;
        }

        FS_TRACE(FS_MOD_RESOLVER, "Scanner timeout/fail: %ls", path.c_str());
        return std::nullopt;
    }
}

// ============================================================================
// resolve_and_inject — Called from hooked_GetSize
// ============================================================================

bool SizeResolver::resolve_and_inject(void* pCFSFolder, const ITEMID_CHILD* pidlChild,
                                       PROPVARIANT* pv) {
    static std::atomic<int> s_resolve_calls{0};
    int call_num = s_resolve_calls.fetch_add(1) + 1;

    // Extract filesystem path from COM interfaces
    auto pathOpt = extract_path(pCFSFolder, pidlChild);
    if (!pathOpt.has_value()) {
        if (call_num <= 5) {
            fs::log::diagnostic_logf("resolve_and_inject #%d: extract_path returned nullopt (file, not folder)", call_num);
        }
        return false; // Not a filesystem folder — fall through
    }

    if (call_num <= 10) {
        fs::log::diagnostic_logf("resolve_and_inject #%d: path=%ls", call_num, pathOpt.value().c_str());
    }

    // Resolve folder size
    auto sizeOpt = resolve_size(pathOpt.value());
    if (!sizeOpt.has_value()) {
        if (call_num <= 10) {
            fs::log::diagnostic_logf("resolve_and_inject #%d: resolve_size returned nullopt", call_num);
        }
        return false; // No size available — show blank
    }

    // Inject into PROPVARIANT: VT_UI8 with folder size
    pv->vt = VT_UI8;
    pv->uhVal.QuadPart = sizeOpt.value();
    if (call_num <= 10) {
        fs::log::diagnostic_logf("resolve_and_inject #%d: INJECTED %llu bytes for %ls",
                                 call_num, sizeOpt.value(), pathOpt.value().c_str());
    }
    return true;
}

// ============================================================================
// format_size_display — Called from hooked_PSFormatForDisplayAlloc
// ============================================================================

HRESULT SizeResolver::format_size_display(REFPROPERTYKEY key, REFPROPVARIANT propvar,
                                           PROPDESC_FORMAT_FLAGS pdff,
                                           PWSTR* ppszDisplay) {
    (void)pdff;

    // Only intercept PKEY_Size
    if (!IsEqualPropertyKey(key, PKEY_Size)) {
        return E_FAIL; // Not our property — let original handle it
    }

    // Only format if there's a value (VT_UI8)
    if (propvar.vt != VT_UI8) {
        return E_FAIL; // Empty or unexpected type — let original handle
    }

    uint64_t bytes = propvar.uhVal.QuadPart;
    std::wstring formatted = fs::format_size_for_column(bytes);

    // Allocate output with CoTaskMemAlloc (Explorer expects this allocator)
    size_t cbSize = (formatted.length() + 1) * sizeof(wchar_t);
    PWSTR pOut = static_cast<PWSTR>(CoTaskMemAlloc(cbSize));
    if (!pOut) {
        FS_ERROR(FS_MOD_RESOLVER, "CoTaskMemAlloc failed for format output");
        return E_OUTOFMEMORY;
    }

    wcscpy_s(pOut, formatted.length() + 1, formatted.c_str());
    *ppszDisplay = pOut;

    FS_TRACE(FS_MOD_RESOLVER, "Formatted %llu bytes as: %ls", bytes, formatted.c_str());
    return S_OK;
}

} // namespace fs::core
