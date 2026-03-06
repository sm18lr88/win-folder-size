#include "size_formatter.h"
#include "logging.h"
#include <cstdio>
#include <cwchar>

namespace fs {

// Helper to format a number with thousand separators
static std::wstring format_with_commas(uint64_t value) {
    wchar_t buffer[32];
    swprintf_s(buffer, sizeof(buffer) / sizeof(wchar_t), L"%llu", value);
    
    std::wstring result(buffer);
    std::wstring with_commas;
    
    int count = 0;
    for (int i = static_cast<int>(result.length()) - 1; i >= 0; --i) {
        if (count == 3) {
            with_commas = L',' + with_commas;
            count = 0;
        }
        with_commas = result[i] + with_commas;
        ++count;
    }
    
    return with_commas;
}

std::wstring format_size(uint64_t bytes) {
    FS_TRACE(FS_MOD_FORMAT, "Formatting %llu bytes", bytes);
    
    wchar_t buffer[64];
    
    // 0 bytes
    if (bytes == 0) {
        return L"0 bytes";
    }
    
    // 1-1023 bytes
    if (bytes < 1024) {
        std::wstring formatted = format_with_commas(bytes);
        swprintf_s(buffer, sizeof(buffer) / sizeof(wchar_t), L"%s bytes", formatted.c_str());
        return buffer;
    }
    
    // 1 KB - 1 MB (1024 to 1,048,575)
    if (bytes < 1048576) {
        double kb = bytes / 1024.0;
        swprintf_s(buffer, sizeof(buffer) / sizeof(wchar_t), L"%.1f KB", kb);
        return buffer;
    }
    
    // 1 MB - 1 GB (1,048,576 to 1,073,741,823)
    if (bytes < 1073741824) {
        double mb = bytes / 1048576.0;
        swprintf_s(buffer, sizeof(buffer) / sizeof(wchar_t), L"%.1f MB", mb);
        return buffer;
    }
    
    // 1 GB - 1 TB (1,073,741,824 to 1,099,511,627,775)
    if (bytes < 1099511627776ULL) {
        double gb = bytes / 1073741824.0;
        swprintf_s(buffer, sizeof(buffer) / sizeof(wchar_t), L"%.2f GB", gb);
        return buffer;
    }
    
    // 1 TB and above
    double tb = bytes / 1099511627776.0;
    swprintf_s(buffer, sizeof(buffer) / sizeof(wchar_t), L"%.2f TB", tb);
    return buffer;
}

std::wstring format_size_for_column(uint64_t bytes) {
    FS_TRACE(FS_MOD_FORMAT, "Formatting %llu bytes for column", bytes);
    
    wchar_t buffer[64];
    
    // 0 bytes
    if (bytes == 0) {
        return L"0 bytes";
    }
    
    // 1-1023 bytes
    if (bytes < 1024) {
        std::wstring formatted = format_with_commas(bytes);
        swprintf_s(buffer, sizeof(buffer) / sizeof(wchar_t), L"%s bytes", formatted.c_str());
        return buffer;
    }
    
    // 1 KB - 1 MB (1024 to 1,048,575) - no decimal
    if (bytes < 1048576) {
        double kb = bytes / 1024.0;
        swprintf_s(buffer, sizeof(buffer) / sizeof(wchar_t), L"%.0f KB", kb);
        return buffer;
    }
    
    // 1 MB - 1 GB (1,048,576 to 1,073,741,823) - 1 decimal
    if (bytes < 1073741824) {
        double mb = bytes / 1048576.0;
        swprintf_s(buffer, sizeof(buffer) / sizeof(wchar_t), L"%.1f MB", mb);
        return buffer;
    }
    
    // 1 GB - 1 TB (1,073,741,824 to 1,099,511,627,775) - 2 decimals
    if (bytes < 1099511627776ULL) {
        double gb = bytes / 1073741824.0;
        swprintf_s(buffer, sizeof(buffer) / sizeof(wchar_t), L"%.2f GB", gb);
        return buffer;
    }
    
    // 1 TB and above - 2 decimals
    double tb = bytes / 1099511627776.0;
    swprintf_s(buffer, sizeof(buffer) / sizeof(wchar_t), L"%.2f TB", tb);
    return buffer;
}

bool is_pending_size(uint64_t bytes) {
    return bytes == kPendingSizeSentinel;
}

std::wstring format_size_for_shell_column(uint64_t bytes) {
    if (is_pending_size(bytes)) {
        return L"Pending";
    }

    return format_size_for_column(bytes);
}

} // namespace fs
