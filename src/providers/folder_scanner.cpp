#include "folder_scanner.h"
#include "logging.h"
#include <windows.h>
#include <algorithm>

namespace fs::providers {

FolderScanner& FolderScanner::instance() {
    static FolderScanner s_instance;
    return s_instance;
}

FolderScanner::~FolderScanner() {
    cancel_all();
}

std::shared_ptr<std::atomic<bool>> FolderScanner::create_cancel_token() {
    auto cancel_token = std::make_shared<std::atomic<bool>>(false);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cancelTokens.push_back(cancel_token);
    }
    return cancel_token;
}

void FolderScanner::remove_cancel_token(const std::shared_ptr<std::atomic<bool>>& cancel_token) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::find(m_cancelTokens.begin(), m_cancelTokens.end(), cancel_token);
    if (it != m_cancelTokens.end()) {
        m_cancelTokens.erase(it);
    }
}

FolderScanner::ScanResult FolderScanner::scan_recursive(const std::wstring& path, const std::atomic<bool>& cancel_token) {
    if (cancel_token.load()) {
        return {0, ScanStatus::Cancelled};
    }

    uint64_t total_size = 0;
    WIN32_FIND_DATAW find_data;
    HANDLE find_handle = INVALID_HANDLE_VALUE;

    std::wstring directory_prefix = path;
    if (!directory_prefix.empty() && directory_prefix.back() != L'\\') {
        directory_prefix += L'\\';
    }

    std::wstring search_pattern = directory_prefix;
    search_pattern += L'*';

    FS_TRACE(FS_MOD_SCANNER, "Scanning directory: %ls", path.c_str());

    // Use FindFirstFileExW with optimizations
    find_handle = FindFirstFileExW(
        search_pattern.c_str(),
        FindExInfoBasic,  // Skip short names for performance
        &find_data,
        FindExSearchNameMatch,
        nullptr,
        FIND_FIRST_EX_LARGE_FETCH  // Optimize for large directories
    );

    if (find_handle == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        if (error == ERROR_ACCESS_DENIED) {
            FS_WARN(FS_MOD_SCANNER, "Access denied scanning: %ls", path.c_str());
        } else {
            FS_ERROR(FS_MOD_SCANNER, "Failed to open directory %ls (error: %lu)", path.c_str(), error);
        }
        return {0, ScanStatus::Incomplete};
    }

    do {
        // Check cancellation token between entries
        if (cancel_token.load()) {
            FindClose(find_handle);
            return {total_size, ScanStatus::Cancelled};
        }

        // Skip . and ..
        if (wcscmp(find_data.cFileName, L".") == 0 || wcscmp(find_data.cFileName, L"..") == 0) {
            continue;
        }

        // Skip reparse points (junctions, symlinks) to avoid infinite recursion
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            FS_TRACE(FS_MOD_SCANNER, "Skipping reparse point: %ls", find_data.cFileName);
            continue;
        }

        std::wstring full_path = directory_prefix;
        full_path += find_data.cFileName;

        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // Recurse into subdirectory
            ScanResult subdir_result = scan_recursive(full_path, cancel_token);
            total_size += subdir_result.size;
            FS_TRACE(FS_MOD_SCANNER, "Subdir %ls: %llu bytes", find_data.cFileName, subdir_result.size);
            if (!subdir_result.completed()) {
                FindClose(find_handle);
                return {total_size, subdir_result.status};
            }
        } else {
            // Accumulate file size
            uint64_t file_size = (static_cast<uint64_t>(find_data.nFileSizeHigh) << 32) | find_data.nFileSizeLow;
            total_size += file_size;
            FS_TRACE(FS_MOD_SCANNER, "File %ls: %llu bytes", find_data.cFileName, file_size);
        }

    } while (FindNextFileW(find_handle, &find_data));

    DWORD find_next_error = GetLastError();
    FindClose(find_handle);

    if (find_next_error != ERROR_NO_MORE_FILES) {
        FS_ERROR(FS_MOD_SCANNER, "Directory enumeration failed for %ls (error: %lu)", path.c_str(), find_next_error);
        return {total_size, ScanStatus::Incomplete};
    }

    FS_DEBUG(FS_MOD_SCANNER, "Scan complete for %ls: %llu bytes", path.c_str(), total_size);
    return {total_size, ScanStatus::Complete};
}

std::future<FolderScanner::ScanResult> FolderScanner::scan_async_result(std::wstring_view path) {
    auto cancel_token = create_cancel_token();

    return std::async(std::launch::async, [this, path_str = std::wstring(path), cancel_token]() -> ScanResult {
        FS_DEBUG(FS_MOD_SCANNER, "Starting async scan: %ls", path_str.c_str());
        FS_SCOPED_TIMER(FS_MOD_SCANNER, "scan_async");

        ScanResult result = scan_recursive(path_str, *cancel_token);
        remove_cancel_token(cancel_token);
        return result;
    });
}

std::future<std::optional<uint64_t>> FolderScanner::scan_async(std::wstring_view path) {
    auto cancel_token = create_cancel_token();

    return std::async(std::launch::async, [this, path_str = std::wstring(path), cancel_token]() -> std::optional<uint64_t> {
        FS_DEBUG(FS_MOD_SCANNER, "Starting async scan: %ls", path_str.c_str());
        FS_SCOPED_TIMER(FS_MOD_SCANNER, "scan_async");

        ScanResult result = scan_recursive(path_str, *cancel_token);
        remove_cancel_token(cancel_token);
        if (!result.completed()) {
            return std::nullopt;
        }

        return result.size;
    });
}

FolderScanner::ScanResult FolderScanner::scan_sync_result(std::wstring_view path, std::chrono::milliseconds timeout) {
    auto future = scan_async_result(path);

    // Wait for the result with timeout
    auto status = future.wait_for(timeout);

    if (status == std::future_status::timeout) {
        FS_WARN(FS_MOD_SCANNER, "Scan timeout for path: %ls", path.data());
        cancel_all();
        return {0, ScanStatus::Cancelled};
    }

    // Get the result
    try {
        return future.get();
    } catch (const std::exception& e) {
        FS_ERROR(FS_MOD_SCANNER, "Scan exception: %s", e.what());
        return {0, ScanStatus::Incomplete};
    }
}

std::optional<uint64_t> FolderScanner::scan_sync(std::wstring_view path, std::chrono::milliseconds timeout) {
    ScanResult result = scan_sync_result(path, timeout);
    if (!result.completed()) {
        return std::nullopt;
    }

    return result.size;
}

void FolderScanner::cancel_all() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& token : m_cancelTokens) {
        token->store(true);
    }
    m_cancelTokens.clear();
}

bool FolderScanner::is_ntfs(wchar_t drive_letter) {
    // Build root path: "C:\"
    wchar_t root_path[4] = {drive_letter, L':', L'\\', L'\0'};

    // Buffer for filesystem name (at least MAX_PATH)
    wchar_t fs_name[MAX_PATH] = {0};

    BOOL result = GetVolumeInformationW(
        root_path,
        nullptr,  // volume name not needed
        0,        // volume name buffer size
        nullptr,  // serial number not needed
        nullptr,  // max component length not needed
        nullptr,  // flags not needed
        fs_name,  // filesystem name
        MAX_PATH  // filesystem name buffer size
    );

    if (!result) {
        DWORD error = GetLastError();
        FS_WARN(FS_MOD_SCANNER, "Failed to get volume info for drive %c: (error: %lu)", drive_letter, error);
        return false;
    }

    bool is_ntfs = (wcscmp(fs_name, L"NTFS") == 0);
    FS_INFO(FS_MOD_SCANNER, "Drive %c: filesystem is %ls (NTFS: %s)", drive_letter, fs_name, is_ntfs ? "yes" : "no");

    return is_ntfs;
}

} // namespace fs::providers
