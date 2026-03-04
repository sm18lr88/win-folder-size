#pragma once
#include <string>
#include <optional>
#include <mutex>
#include <atomic>
#include <chrono>
#include <windows.h>

namespace fs {

class EverythingClient {
public:
    // Meyers singleton
    static EverythingClient& instance();
    
    // Connect to Everything service via named pipe
    bool connect();
    
    // Query folder size (returns nullopt on error/not-found)
    std::optional<uint64_t> get_folder_size(std::wstring_view path);
    
    // Disconnect from Everything
    void disconnect();
    
    // Check connection status
    bool is_connected() const;

private:
    EverythingClient() = default;
    ~EverythingClient();
    EverythingClient(const EverythingClient&) = delete;
    EverythingClient& operator=(const EverythingClient&) = delete;
    
    // Try to connect/reconnect if needed (called with lock held)
    bool ensure_connected();
    
    // Implementation methods (called with lock held, no locking)
    bool connect_impl();
    void disconnect_impl();
    
    // Convert wide string path to UTF-8
    static std::string to_utf8(std::wstring_view wstr);
    
    // Pipe I/O helpers (called with lock held)
    // These loop to handle partial writes/reads with overlapped I/O + timeout
    bool write_pipe(const void* data, DWORD size);
    bool read_pipe(void* buffer, DWORD size);
    
    HANDLE m_pipe = INVALID_HANDLE_VALUE;
    HANDLE m_sendEvent = nullptr;   // Manual-reset event for overlapped writes
    HANDLE m_recvEvent = nullptr;   // Manual-reset event for overlapped reads
    mutable std::mutex m_mutex;
    std::atomic<bool> m_connected{false};
    
    // Reconnection backoff
    std::chrono::steady_clock::time_point m_lastConnectAttempt{};
    static constexpr auto RECONNECT_INTERVAL = std::chrono::seconds(5);
    static constexpr DWORD IO_TIMEOUT_MS = 3000;  // 3 second timeout for pipe I/O
};

} // namespace fs
