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

uint64_t FolderScanner::scan_recursive(const std::wstring& path, const std::atomic<bool>& cancel_token) {
    if (cancel_token.load()) {
        return 0;
    }

    uint64_t total_size = 0;
    WIN32_FIND_DATAW find_data;
    HANDLE find_handle = INVALID_HANDLE_VALUE;

    // Build search pattern: path + \*
    std::wstring search_pattern = path;
    if (!search_pattern.empty() && search_pattern.back() != L'\\') {
        search_pattern += L'\\';
    }
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
        return 0;
    }

    do {
        // Check cancellation token between entries
        if (cancel_token.load()) {
            FindClose(find_handle);
            return total_size;
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

        std::wstring full_path = path;
        if (!full_path.empty() && full_path.back() != L'\\') {
            full_path += L'\\';
        }
        full_path += find_data.cFileName;

        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // Recurse into subdirectory
            uint64_t subdir_size = scan_recursive(full_path, cancel_token);
            total_size += subdir_size;
            FS_TRACE(FS_MOD_SCANNER, "Subdir %ls: %llu bytes", find_data.cFileName, subdir_size);
        } else {
            // Accumulate file size
            uint64_t file_size = (static_cast<uint64_t>(find_data.nFileSizeHigh) << 32) | find_data.nFileSizeLow;
            total_size += file_size;
            FS_TRACE(FS_MOD_SCANNER, "File %ls: %llu bytes", find_data.cFileName, file_size);
        }

    } while (FindNextFileW(find_handle, &find_data));

    FindClose(find_handle);

    FS_DEBUG(FS_MOD_SCANNER, "Scan complete for %ls: %llu bytes", path.c_str(), total_size);
    return total_size;
}

std::future<std::optional<uint64_t>> FolderScanner::scan_async(std::wstring_view path) {
    // Create a shared cancel token
    auto cancel_token = std::make_shared<std::atomic<bool>>(false);

    // Store it for potential cancellation
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cancelTokens.push_back(cancel_token);
    }

    // Launch async scan
    auto future = std::async(std::launch::async, [this, path_str = std::wstring(path), cancel_token]() -> std::optional<uint64_t> {
        FS_DEBUG(FS_MOD_SCANNER, "Starting async scan: %ls", path_str.c_str());
        
        FS_SCOPED_TIMER(FS_MOD_SCANNER, "scan_async");
        
        uint64_t result = scan_recursive(path_str, *cancel_token);
        
        // Clean up the cancel token from the vector
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = std::find(m_cancelTokens.begin(), m_cancelTokens.end(), cancel_token);
            if (it != m_cancelTokens.end()) {
                m_cancelTokens.erase(it);
            }
        }

        return result;
    });

    return future;
}

std::optional<uint64_t> FolderScanner::scan_sync(std::wstring_view path, std::chrono::milliseconds timeout) {
    auto future = scan_async(path);

    // Wait for the result with timeout
    auto status = future.wait_for(timeout);

    if (status == std::future_status::timeout) {
        FS_WARN(FS_MOD_SCANNER, "Scan timeout for path: %ls", path.data());
        cancel_all();
        return std::nullopt;
    }

    // Get the result
    try {
        return future.get();
    } catch (const std::exception& e) {
        FS_ERROR(FS_MOD_SCANNER, "Scan exception: %s", e.what());
        return std::nullopt;
    }
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
