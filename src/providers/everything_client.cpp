#include "everything_client.h"
#include "everything_ipc.h"
#include "logging.h"
#include <windows.h>
#include <chrono>

namespace fs {

EverythingClient& EverythingClient::instance() {
    static EverythingClient s_instance;
    return s_instance;
}

EverythingClient::~EverythingClient() {
    disconnect();
}

bool EverythingClient::connect() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return connect_impl();
}

bool EverythingClient::connect_impl() {
    // Called with lock held
    if (m_pipe != INVALID_HANDLE_VALUE) {
        FS_DEBUG(FS_MOD_EVERYTHING, "Already connected to Everything service");
        return true;
    }
    
    // Pipe names matching SDK3 reference (from Everything3.c):
    //   Everything3_ConnectW(nullptr) -> "\\.\.\\PIPE\\Everything IPC"
    //   Everything3_ConnectW(L"1.5a") -> "\\.\.\\PIPE\\Everything IPC (1.5a)"
    // Also try service pipe as last resort (seen via pipe enumeration on some systems)
    const wchar_t* pipe_names[] = {
        ipc::EVERYTHING_PIPE_NAME,              // "Everything IPC" (unnamed)
        ipc::EVERYTHING_PIPE_NAME_1_5A,          // "Everything IPC (1.5a)"
        ipc::EVERYTHING_PIPE_NAME_SERVICE,        // "Everything Service (1.5a)" fallback
    };
    
    for (const auto* pipe_name : pipe_names) {
        fs::log::diagnostic_logf("Trying pipe: %ls", pipe_name);
        
        // SDK3 retries on ERROR_PIPE_BUSY (all instances occupied)
        for (int retry = 0; retry < 3; ++retry) {
            HANDLE pipe = CreateFileW(
                pipe_name,
                GENERIC_READ | GENERIC_WRITE,
                0,          // No sharing (SDK3 uses 0)
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED,  // SDK3 uses overlapped I/O
                nullptr
            );
            
            if (pipe != INVALID_HANDLE_VALUE) {
                // Create I/O events (manual-reset, initially non-signaled)
                HANDLE sendEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                HANDLE recvEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                if (!sendEvt || !recvEvt) {
                    DWORD evtErr = GetLastError();
                    fs::log::diagnostic_logf("CreateEvent failed: err=%lu", evtErr);
                    if (sendEvt) CloseHandle(sendEvt);
                    if (recvEvt) CloseHandle(recvEvt);
                    CloseHandle(pipe);
                    break; // Event creation failure — try next pipe name
                }
                
                m_pipe = pipe;
                m_sendEvent = sendEvt;
                m_recvEvent = recvEvt;
                m_connected = true;
                fs::log::diagnostic_logf("Everything pipe connected: %ls", pipe_name);
                FS_INFO(FS_MOD_EVERYTHING, "Connected to Everything service via pipe: %ls", pipe_name);
                return true;
            }
            
            DWORD err = GetLastError();
            if (err == ERROR_PIPE_BUSY) {
                // SDK3 pattern: Everything creates new pipe servers immediately, retry
                fs::log::diagnostic_logf("Pipe '%ls' busy, retrying (%d/3)", pipe_name, retry + 1);
                Sleep(10);
                continue;
            }
            
            fs::log::diagnostic_logf("Pipe '%ls' failed: err=%lu", pipe_name, err);
            break; // Not PIPE_BUSY — try next pipe name
        }
    }
    
    FS_WARN(FS_MOD_EVERYTHING, "Could not connect to Everything service (service may not be running)");
    m_connected = false;
    return false;
}

bool EverythingClient::ensure_connected() {
    // Called with lock held
    if (m_connected.load()) {
        return true;
    }
    
    // Check reconnection backoff
    auto now = std::chrono::steady_clock::now();
    if (now - m_lastConnectAttempt < RECONNECT_INTERVAL) {
        FS_TRACE(FS_MOD_EVERYTHING, "Reconnection backoff active, skipping connect attempt");
        return false;
    }
    
    m_lastConnectAttempt = now;
    return connect_impl();
}

std::string EverythingClient::to_utf8(std::wstring_view wstr) {
    if (wstr.empty()) {
        return std::string();
    }
    
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (size_needed <= 0) {
        FS_ERROR(FS_MOD_EVERYTHING, "WideCharToMultiByte failed to calculate size");
        return std::string();
    }
    
    std::string result(size_needed, '\0');
    int size_converted = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), &result[0], size_needed, nullptr, nullptr);
    if (size_converted <= 0) {
        FS_ERROR(FS_MOD_EVERYTHING, "WideCharToMultiByte failed to convert string");
        return std::string();
    }
    
    return result;
}

std::optional<uint64_t> EverythingClient::get_folder_size(std::wstring_view path) {
    FS_SCOPED_TIMER(FS_MOD_EVERYTHING, "get_folder_size");
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    fs::log::diagnostic_logf("Everything query: %ls", path.data());
    
    if (!ensure_connected()) {
        fs::log::diagnostic_log("Everything: not connected, skipping");
        FS_TRACE(FS_MOD_EVERYTHING, "Not connected to Everything service");
        return std::nullopt;
    }
    
    // Convert path to UTF-8
    std::string utf8_path = to_utf8(path);
    if (utf8_path.empty() && !path.empty()) {
        FS_ERROR(FS_MOD_EVERYTHING, "Failed to convert path to UTF-8");
        return std::nullopt;
    }
    
    // Build request header (NO null terminator in payload size)
    ipc::Everything3Message header;
    header.code = ipc::EVERYTHING3_COMMAND_GET_FOLDER_SIZE;
    header.size = static_cast<uint32_t>(utf8_path.size());
    fs::log::diagnostic_logf("Sending header: code=%u size=%u path='%s'", header.code, header.size, utf8_path.c_str());
    
    // Send: header + payload (SDK3 sends header first, then payload separately)
    if (!write_pipe(&header, sizeof(header))) {
        fs::log::diagnostic_log("FAIL: write_pipe header");
        disconnect_impl();
        return std::nullopt;
    }
    fs::log::diagnostic_log("Header sent OK");
    
    if (header.size > 0) {
        if (!write_pipe(utf8_path.data(), header.size)) {
            fs::log::diagnostic_log("FAIL: write_pipe payload");
            disconnect_impl();
            return std::nullopt;
        }
        fs::log::diagnostic_log("Payload sent OK");
    }
    
    FS_TRACE(FS_MOD_EVERYTHING, "Sent request: code=%u, payload_size=%u", header.code, header.size);
    
    // Read response header (8 bytes)
    ipc::Everything3Message resp_header = {};
    if (!read_pipe(&resp_header, sizeof(resp_header))) {
        fs::log::diagnostic_log("FAIL: read_pipe response header");
        disconnect_impl();
        return std::nullopt;
    }
    
    fs::log::diagnostic_logf("Everything response code=%u size=%u", resp_header.code, resp_header.size);
    FS_TRACE(FS_MOD_EVERYTHING, "Response header: code=%u, size=%u", resp_header.code, resp_header.size);
    
    // Check response code (SDK3: 200 = OK, 100 = OK_MORE_DATA)
    if (resp_header.code != ipc::EVERYTHING3_RESPONSE_OK &&
        resp_header.code != ipc::EVERYTHING3_RESPONSE_OK_MORE_DATA) {
        if (resp_header.code == ipc::EVERYTHING3_RESPONSE_NOT_FOUND) {
            FS_DEBUG(FS_MOD_EVERYTHING, "Folder not found in Everything index");
        } else {
            FS_WARN(FS_MOD_EVERYTHING, "Everything returned error code: %u", resp_header.code);
        }
        // Skip any payload data in error response
        if (resp_header.size > 0 && resp_header.size <= 4096) {
            std::vector<char> skip_buf(resp_header.size);
            read_pipe(skip_buf.data(), resp_header.size);
        }
        return std::nullopt;
    }
    
    // Read payload (uint64_t folder_size)
    if (resp_header.size != sizeof(uint64_t)) {
        FS_ERROR(FS_MOD_EVERYTHING, "Unexpected response size: %u (expected %zu)", resp_header.size, sizeof(uint64_t));
        // Skip unexpected payload
        if (resp_header.size > 0 && resp_header.size <= 4096) {
            std::vector<char> skip_buf(resp_header.size);
            read_pipe(skip_buf.data(), resp_header.size);
        }
        return std::nullopt;
    }
    
    uint64_t folder_size = 0;
    if (!read_pipe(&folder_size, sizeof(folder_size))) {
        FS_ERROR(FS_MOD_EVERYTHING, "Failed to read folder size data");
        disconnect_impl();
        return std::nullopt;
    }
    
    // Check for sentinel value (UINT64_MAX = not found)
    if (folder_size == ipc::EVERYTHING_FOLDER_NOT_FOUND) {
        FS_DEBUG(FS_MOD_EVERYTHING, "Folder not found in Everything index (sentinel)");
        return std::nullopt;
    }
    
    FS_DEBUG(FS_MOD_EVERYTHING, "Query successful: %llu bytes", folder_size);
    return folder_size;
}

void EverythingClient::disconnect() {
    std::lock_guard<std::mutex> lock(m_mutex);
    disconnect_impl();
}

void EverythingClient::disconnect_impl() {
    // Called with lock held
    if (m_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(m_pipe);
        m_pipe = INVALID_HANDLE_VALUE;
    }
    if (m_sendEvent) {
        CloseHandle(m_sendEvent);
        m_sendEvent = nullptr;
    }
    if (m_recvEvent) {
        CloseHandle(m_recvEvent);
        m_recvEvent = nullptr;
    }
    m_connected = false;
    FS_DEBUG(FS_MOD_EVERYTHING, "Disconnected from Everything service");
}

bool EverythingClient::is_connected() const {
    return m_connected.load();
}

// Overlapped write with loop for partial writes — matches SDK3 _everything3_send pattern
bool EverythingClient::write_pipe(const void* data, DWORD size) {
    const BYTE* p = static_cast<const BYTE*>(data);
    DWORD remaining = size;
    
    while (remaining > 0) {
        OVERLAPPED ov = {};
        ov.hEvent = m_sendEvent;
        
        DWORD written = 0;
        if (WriteFile(m_pipe, p, remaining, &written, &ov)) {
            // Completed synchronously
            if (written == 0) {
                fs::log::diagnostic_log("write_pipe: WriteFile returned 0 bytes (disconnected)");
                return false;
            }
            p += written;
            remaining -= written;
            continue;
        }
        
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING || err == ERROR_IO_INCOMPLETE) {
            // Wait for completion with timeout
            DWORD wait = WaitForSingleObject(m_sendEvent, IO_TIMEOUT_MS);
            if (wait != WAIT_OBJECT_0) {
                fs::log::diagnostic_logf("write_pipe: wait timeout/fail (wait=%lu)", wait);
                CancelIo(m_pipe);
                return false;
            }
            
            if (!GetOverlappedResult(m_pipe, &ov, &written, FALSE)) {
                DWORD ovErr = GetLastError();
                fs::log::diagnostic_logf("write_pipe: GetOverlappedResult failed err=%lu", ovErr);
                return false;
            }
            
            if (written == 0) {
                fs::log::diagnostic_log("write_pipe: overlapped completed with 0 bytes");
                return false;
            }
            
            p += written;
            remaining -= written;
            continue;
        }
        
        // Real error
        fs::log::diagnostic_logf("write_pipe FAILED: remaining=%lu err=%lu (0x%lx)", remaining, err, err);
        return false;
    }
    
    return true;
}

// Overlapped read with loop for partial reads — matches SDK3 _everything3_recv_data pattern
bool EverythingClient::read_pipe(void* buffer, DWORD size) {
    BYTE* p = static_cast<BYTE*>(buffer);
    DWORD remaining = size;
    
    while (remaining > 0) {
        OVERLAPPED ov = {};
        ov.hEvent = m_recvEvent;
        
        // SDK3 reads in 64KB chunks max
        DWORD chunk = (remaining <= 65536) ? remaining : 65536;
        
        DWORD bytesRead = 0;
        if (ReadFile(m_pipe, p, chunk, &bytesRead, &ov)) {
            // Completed synchronously
            if (bytesRead == 0) {
                fs::log::diagnostic_log("read_pipe: ReadFile returned 0 bytes (disconnected)");
                return false;
            }
            p += bytesRead;
            remaining -= bytesRead;
            continue;
        }
        
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING || err == ERROR_IO_INCOMPLETE) {
            // Wait for completion with timeout
            DWORD wait = WaitForSingleObject(m_recvEvent, IO_TIMEOUT_MS);
            if (wait != WAIT_OBJECT_0) {
                fs::log::diagnostic_logf("read_pipe: wait timeout/fail (wait=%lu)", wait);
                CancelIo(m_pipe);
                return false;
            }
            
            if (!GetOverlappedResult(m_pipe, &ov, &bytesRead, FALSE)) {
                DWORD ovErr = GetLastError();
                fs::log::diagnostic_logf("read_pipe: GetOverlappedResult failed err=%lu", ovErr);
                return false;
            }
            
            if (bytesRead == 0) {
                fs::log::diagnostic_log("read_pipe: overlapped completed with 0 bytes");
                return false;
            }
            
            p += bytesRead;
            remaining -= bytesRead;
            continue;
        }
        
        // Real error
        fs::log::diagnostic_logf("read_pipe FAILED: remaining=%lu err=%lu (0x%lx)", remaining, err, err);
        return false;
    }
    
    return true;
}

} // namespace fs
