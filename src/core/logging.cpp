#include "logging.h"
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <mutex>
#include <atomic>
#include <fstream>

namespace fs::log {

// Global state
static std::mutex g_log_mutex;
static std::atomic<bool> g_initialized(false);
static HANDLE g_log_file = INVALID_HANDLE_VALUE;
static LARGE_INTEGER g_perf_freq = {0};

// Helper to get level name
static const char* get_level_name(int level) {
    switch (level) {
    case FS_LOG_TRACE:
        return "TRACE";
    case FS_LOG_DEBUG:
        return "DEBUG";
    case FS_LOG_INFO:
        return "INFO";
    case FS_LOG_WARN:
        return "WARN";
    case FS_LOG_ERROR:
        return "ERROR";
    case FS_LOG_FATAL:
        return "FATAL";
    default:
        return "UNKNOWN";
    }
}

// Helper to get current timestamp
static void get_timestamp(char* buf, size_t bufsize) {
    SYSTEMTIME st;
    GetSystemTime(&st);
    snprintf(buf, bufsize, "%04u-%02u-%02u %02u:%02u:%02u.%03u", st.wYear, st.wMonth,
             st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

// Helper to get current timestamp (wide)
static void get_timestamp_w(wchar_t* buf, size_t bufsize) {
    SYSTEMTIME st;
    GetSystemTime(&st);
    swprintf_s(buf, bufsize, L"%04u-%02u-%02u %02u:%02u:%02u.%03u", st.wYear, st.wMonth,
               st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

// Helper to format HRESULT description
static void format_hresult(long hr, char* buf, size_t bufsize) {
    wchar_t wide_buf[256] = {0};
    DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD result = FormatMessageW(flags, nullptr, (DWORD)hr, 0, wide_buf, 256, nullptr);

    if (result > 0) {
        // Convert wide to ANSI
        WideCharToMultiByte(CP_ACP, 0, wide_buf, -1, buf, (int)bufsize, nullptr, nullptr);
        // Remove trailing newline
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') {
            buf[len - 1] = '\0';
        }
    } else {
        snprintf(buf, bufsize, "Unknown error");
    }
}

// Helper to format Win32 error description
static void format_win32_error(unsigned long error, char* buf, size_t bufsize) {
    wchar_t wide_buf[256] = {0};
    DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD result = FormatMessageW(flags, nullptr, error, 0, wide_buf, 256, nullptr);

    if (result > 0) {
        // Convert wide to ANSI
        WideCharToMultiByte(CP_ACP, 0, wide_buf, -1, buf, (int)bufsize, nullptr, nullptr);
        // Remove trailing newline
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') {
            buf[len - 1] = '\0';
        }
    } else {
        snprintf(buf, bufsize, "Unknown error");
    }
}

void log_message(int level, const char* module, const char* file, int line,
                 const char* func, _Printf_format_string_ const char* fmt, ...) {
    if (level < FS_LOG_LEVEL) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_log_mutex);

    char timestamp[32];
    get_timestamp(timestamp, sizeof(timestamp));

    DWORD tid = GetCurrentThreadId();

    // Extract filename from full path
    const char* filename = strrchr(file, '\\');
    if (!filename) {
        filename = strrchr(file, '/');
    }
    if (!filename) {
        filename = file;
    } else {
        filename++; // Skip the separator
    }

    // Format the message
    char message[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    // Build full log line
    char logline[4096];
    snprintf(logline, sizeof(logline), "[FolderSize][%s][%s] %s tid=%lu %s:%d %s() %s\n",
             module, get_level_name(level), timestamp, tid, filename, line, func, message);

    // Output to debugger
    OutputDebugStringA(logline);

    // Write to file in debug builds
#ifdef FOLDERSIZE_DEBUG
    if (g_log_file != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(g_log_file, logline, (DWORD)strlen(logline), &written, nullptr);
    }
#endif
}

void log_message_w(int level, const char* module, const char* file, int line,
                   const char* func, _Printf_format_string_ const wchar_t* fmt, ...) {
    if (level < FS_LOG_LEVEL) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_log_mutex);

    wchar_t timestamp[32];
    get_timestamp_w(timestamp, sizeof(timestamp) / sizeof(wchar_t));

    DWORD tid = GetCurrentThreadId();

    // Extract filename from full path
    const char* filename = strrchr(file, '\\');
    if (!filename) {
        filename = strrchr(file, '/');
    }
    if (!filename) {
        filename = file;
    } else {
        filename++; // Skip the separator
    }

    // Format the message
    wchar_t message[2048];
    va_list args;
    va_start(args, fmt);
    vswprintf_s(message, sizeof(message) / sizeof(wchar_t), fmt, args);
    va_end(args);

    // Build full log line
    wchar_t logline[4096];
    swprintf_s(logline, sizeof(logline) / sizeof(wchar_t),
               L"[FolderSize][%hs][%hs] %s tid=%lu %hs:%d %hs() %s\n", module,
               get_level_name(level), timestamp, tid, filename, line, func, message);

    // Output to debugger
    OutputDebugStringW(logline);

    // Write to file in debug builds
#ifdef FOLDERSIZE_DEBUG
    if (g_log_file != INVALID_HANDLE_VALUE) {
        // Convert to ANSI for file write
        char ansi_line[4096];
        WideCharToMultiByte(CP_ACP, 0, logline, -1, ansi_line, sizeof(ansi_line), nullptr,
                            nullptr);
        DWORD written;
        WriteFile(g_log_file, ansi_line, (DWORD)strlen(ansi_line), &written, nullptr);
    }
#endif
}

void log_hresult(const char* module, const char* file, int line, const char* func,
                 const char* call, long hr) {
    char error_desc[256];
    format_hresult(hr, error_desc, sizeof(error_desc));
    log_message(FS_LOG_ERROR, module, file, line, func,
               "%s failed: HRESULT=0x%08X (%s)", call, (unsigned int)hr, error_desc);
}

void log_win32_error(const char* module, const char* file, int line, const char* func,
                     const char* call, unsigned long error) {
    char error_desc[256];
    format_win32_error(error, error_desc, sizeof(error_desc));
    log_message(FS_LOG_ERROR, module, file, line, func,
               "%s failed: error=%lu (%s)", call, error, error_desc);
}

void init_logging() {
    std::lock_guard<std::mutex> lock(g_log_mutex);

    if (g_initialized.exchange(true)) {
        return; // Already initialized
    }

    // Initialize performance counter frequency
    QueryPerformanceFrequency(&g_perf_freq);

    FS_INFO(FS_MOD_INIT, "Logging system initialized");

#ifdef FOLDERSIZE_DEBUG
    // Open debug log file
    char temp_path[MAX_PATH];
    if (GetTempPathA(sizeof(temp_path), temp_path)) {
        char log_path[MAX_PATH];
        snprintf(log_path, sizeof(log_path), "%sfoldersize-debug.log", temp_path);

        g_log_file = CreateFileA(log_path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (g_log_file != INVALID_HANDLE_VALUE) {
            FS_INFO(FS_MOD_INIT, "Debug log file opened: %s", log_path);
        }
    }
#endif
}

void shutdown_logging() {
    std::lock_guard<std::mutex> lock(g_log_mutex);

    FS_INFO(FS_MOD_LIFECYCLE, "Logging system shutting down");

    if (g_log_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_log_file);
        g_log_file = INVALID_HANDLE_VALUE;
    }

    g_initialized = false;
}

void flush() {
    std::lock_guard<std::mutex> lock(g_log_mutex);

    if (g_log_file != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(g_log_file);
    }
}

// ScopedTimer implementation
ScopedTimer::ScopedTimer(const char* module, const char* name)
    : m_module(module), m_name(name) {
    QueryPerformanceCounter(&m_start);
}

ScopedTimer::~ScopedTimer() {
    LARGE_INTEGER end;
    QueryPerformanceCounter(&end);

    if (g_perf_freq.QuadPart > 0) {
        double elapsed_ms =
            (double)(end.QuadPart - m_start.QuadPart) * 1000.0 / g_perf_freq.QuadPart;
        FS_DEBUG(m_module, "%s completed in %.3f ms", m_name, elapsed_ms);
    }
}

} // namespace fs::log

namespace fs::log {

void diagnostic_log(const char* message) {
    // Get DLL directory from our own module address
    char dll_path[MAX_PATH];
    HMODULE hMod = nullptr;
    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)&diagnostic_log, &hMod)) {
        return;
    }
    if (!GetModuleFileNameA(hMod, dll_path, MAX_PATH)) return;

    // Replace DLL filename with log filename
    char* last_slash = strrchr(dll_path, '\\');
    if (!last_slash) return;
    *(last_slash + 1) = '\0';

    char log_path[MAX_PATH];
    snprintf(log_path, sizeof(log_path), "%sfoldersize-diag.log", dll_path);

    // Open in append mode, write, close immediately
    HANDLE hFile = CreateFileA(log_path, FILE_APPEND_DATA,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;

    SYSTEMTIME st;
    GetSystemTime(&st);
    char line[2048];
    int len = snprintf(line, sizeof(line),
                       "[%04u-%02u-%02u %02u:%02u:%02u.%03u] [tid=%lu] %s\n",
                       st.wYear, st.wMonth, st.wDay,
                       st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                       GetCurrentThreadId(), message);
    if (len > 0) {
        DWORD written;
        WriteFile(hFile, line, (DWORD)len, &written, nullptr);
    }
    CloseHandle(hFile);
}

void diagnostic_logf(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    diagnostic_log(buf);
}

} // namespace fs::log
