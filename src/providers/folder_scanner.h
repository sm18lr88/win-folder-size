#pragma once
#include <windows.h>
#include <string>
#include <string_view>
#include <optional>
#include <future>
#include <atomic>
#include <vector>
#include <mutex>
#include <chrono>
#include <memory>

namespace fs::providers {

class FolderScanner {
public:
    static FolderScanner& instance();  // Meyers singleton

    // Async scan — returns a future with the folder size
    std::future<std::optional<uint64_t>> scan_async(std::wstring_view path);

    // Blocking scan with timeout
    std::optional<uint64_t> scan_sync(std::wstring_view path, std::chrono::milliseconds timeout = std::chrono::seconds(10));

    // Cancel all pending scans
    void cancel_all();

    // Check if a drive is NTFS (NTFS uses Everything, non-NTFS uses scanner)
    static bool is_ntfs(wchar_t drive_letter);

    // Non-copyable
    FolderScanner(const FolderScanner&) = delete;
    FolderScanner& operator=(const FolderScanner&) = delete;

private:
    FolderScanner() = default;
    ~FolderScanner();

    // Internal recursive scan
    static uint64_t scan_recursive(const std::wstring& path, const std::atomic<bool>& cancel_token);

    std::mutex m_mutex;
    std::vector<std::shared_ptr<std::atomic<bool>>> m_cancelTokens;
};

} // namespace fs::providers
