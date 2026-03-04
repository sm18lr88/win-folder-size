#pragma once
#pragma warning(disable: 4127)  // Suppress C4127 for compile-time constant conditions in logging macros

#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <mutex>
#include <chrono>
#include <atomic>

// Log levels
#define FS_LOG_TRACE 0
#define FS_LOG_DEBUG 1
#define FS_LOG_INFO 2
#define FS_LOG_WARN 3
#define FS_LOG_ERROR 4
#define FS_LOG_FATAL 5

// Module tags
#define FS_MOD_COM "COM"
#define FS_MOD_HOOK "HOOK"
#define FS_MOD_CACHE "CACHE"
#define FS_MOD_EVERYTHING "EVERYTHING"
#define FS_MOD_SCANNER "SCANNER"
#define FS_MOD_RESOLVER "RESOLVER"
#define FS_MOD_INIT "INIT"
#define FS_MOD_LIFECYCLE "LIFECYCLE"
#define FS_MOD_FORMAT "FORMAT"

// Default log level based on build type
#ifndef FS_LOG_LEVEL
#ifdef FOLDERSIZE_DEBUG
#define FS_LOG_LEVEL FS_LOG_TRACE
#else
#define FS_LOG_LEVEL FS_LOG_WARN
#endif
#endif

namespace fs::log {

// Core logging functions
void log_message(int level, const char* module, const char* file, int line,
                 const char* func, _Printf_format_string_ const char* fmt, ...);

void log_message_w(int level, const char* module, const char* file, int line,
                   const char* func, _Printf_format_string_ const wchar_t* fmt, ...);

void log_hresult(const char* module, const char* file, int line, const char* func,
                 const char* call, long hr);

void log_win32_error(const char* module, const char* file, int line, const char* func,
                     const char* call, unsigned long error);

void init_logging();
void shutdown_logging();
void flush();

// Scoped timer for performance profiling
class ScopedTimer {
public:
    ScopedTimer(const char* module, const char* name);
    ~ScopedTimer();

private:
    const char* m_module;
    const char* m_name;
    LARGE_INTEGER m_start;
};

} // namespace fs::log

// Always-on diagnostic log — safe to call from DllMain (no C++ objects)
// Writes to foldersize-diag.log next to the DLL
namespace fs::log {
void diagnostic_log(const char* message);
void diagnostic_logf(_Printf_format_string_ const char* fmt, ...);
} // namespace fs::log

// Logging macros - ANSI variants
#define FS_TRACE(mod, fmt, ...)                                                    \
    do {                                                                           \
        if (FS_LOG_LEVEL <= FS_LOG_TRACE) {                                       \
            fs::log::log_message(FS_LOG_TRACE, mod, __FILE__, __LINE__, __func__, \
                                 fmt, ##__VA_ARGS__);                             \
        }                                                                          \
    } while (0)

#define FS_DEBUG(mod, fmt, ...)                                                    \
    do {                                                                           \
        if (FS_LOG_LEVEL <= FS_LOG_DEBUG) {                                       \
            fs::log::log_message(FS_LOG_DEBUG, mod, __FILE__, __LINE__, __func__, \
                                 fmt, ##__VA_ARGS__);                             \
        }                                                                          \
    } while (0)

#define FS_INFO(mod, fmt, ...)                                                    \
    do {                                                                           \
        if (FS_LOG_LEVEL <= FS_LOG_INFO) {                                        \
            fs::log::log_message(FS_LOG_INFO, mod, __FILE__, __LINE__, __func__,  \
                                 fmt, ##__VA_ARGS__);                             \
        }                                                                          \
    } while (0)

#define FS_WARN(mod, fmt, ...)                                                    \
    do {                                                                           \
        if (FS_LOG_LEVEL <= FS_LOG_WARN) {                                        \
            fs::log::log_message(FS_LOG_WARN, mod, __FILE__, __LINE__, __func__,  \
                                 fmt, ##__VA_ARGS__);                             \
        }                                                                          \
    } while (0)

#define FS_ERROR(mod, fmt, ...)                                                   \
    do {                                                                           \
        if (FS_LOG_LEVEL <= FS_LOG_ERROR) {                                       \
            fs::log::log_message(FS_LOG_ERROR, mod, __FILE__, __LINE__, __func__, \
                                 fmt, ##__VA_ARGS__);                             \
        }                                                                          \
    } while (0)

#define FS_FATAL(mod, fmt, ...)                                                   \
    do {                                                                           \
        if (FS_LOG_LEVEL <= FS_LOG_FATAL) {                                       \
            fs::log::log_message(FS_LOG_FATAL, mod, __FILE__, __LINE__, __func__, \
                                 fmt, ##__VA_ARGS__);                             \
        }                                                                          \
    } while (0)

// Logging macros - Wide-string variants
#define FS_TRACEW(mod, fmt, ...)                                                   \
    do {                                                                           \
        if (FS_LOG_LEVEL <= FS_LOG_TRACE) {                                       \
            fs::log::log_message_w(FS_LOG_TRACE, mod, __FILE__, __LINE__, __func__, \
                                   fmt, ##__VA_ARGS__);                           \
        }                                                                          \
    } while (0)

#define FS_DEBUGW(mod, fmt, ...)                                                   \
    do {                                                                           \
        if (FS_LOG_LEVEL <= FS_LOG_DEBUG) {                                       \
            fs::log::log_message_w(FS_LOG_DEBUG, mod, __FILE__, __LINE__, __func__, \
                                   fmt, ##__VA_ARGS__);                           \
        }                                                                          \
    } while (0)

#define FS_INFOW(mod, fmt, ...)                                                    \
    do {                                                                           \
        if (FS_LOG_LEVEL <= FS_LOG_INFO) {                                        \
            fs::log::log_message_w(FS_LOG_INFO, mod, __FILE__, __LINE__, __func__,  \
                                   fmt, ##__VA_ARGS__);                           \
        }                                                                          \
    } while (0)

#define FS_WARNW(mod, fmt, ...)                                                    \
    do {                                                                           \
        if (FS_LOG_LEVEL <= FS_LOG_WARN) {                                        \
            fs::log::log_message_w(FS_LOG_WARN, mod, __FILE__, __LINE__, __func__,  \
                                   fmt, ##__VA_ARGS__);                           \
        }                                                                          \
    } while (0)

#define FS_ERRORW(mod, fmt, ...)                                                   \
    do {                                                                           \
        if (FS_LOG_LEVEL <= FS_LOG_ERROR) {                                       \
            fs::log::log_message_w(FS_LOG_ERROR, mod, __FILE__, __LINE__, __func__, \
                                   fmt, ##__VA_ARGS__);                           \
        }                                                                          \
    } while (0)

#define FS_FATALW(mod, fmt, ...)                                                   \
    do {                                                                           \
        if (FS_LOG_LEVEL <= FS_LOG_FATAL) {                                       \
            fs::log::log_message_w(FS_LOG_FATAL, mod, __FILE__, __LINE__, __func__, \
                                   fmt, ##__VA_ARGS__);                           \
        }                                                                          \
    } while (0)

// HRESULT helper macro
#define FS_CHECK_HR(mod, call, hr)                                                 \
    do {                                                                           \
        if (FAILED(hr)) {                                                          \
            fs::log::log_hresult(mod, __FILE__, __LINE__, __func__, #call, hr);   \
        }                                                                          \
    } while (0)

// Win32 error helper macro
#define FS_CHECK_WIN32(mod, call)                                                  \
    do {                                                                           \
        unsigned long err = GetLastError();                                       \
        if (err != ERROR_SUCCESS) {                                               \
            fs::log::log_win32_error(mod, __FILE__, __LINE__, __func__, #call, err); \
        }                                                                          \
    } while (0)

// Scoped timer macro
#if FS_LOG_LEVEL <= FS_LOG_DEBUG
#define FS_SCOPED_TIMER(mod, name) fs::log::ScopedTimer _timer(mod, name)
#else
#define FS_SCOPED_TIMER(mod, name) ((void)0)
#endif
