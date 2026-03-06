#pragma once
#include <cstdint>
#include <limits>
#include <string>

namespace fs {

inline constexpr uint64_t kPendingSizeSentinel = (std::numeric_limits<uint64_t>::max)();

// Format bytes into human-readable string
// 0 -> "0 bytes", 1023 -> "1,023 bytes", 1536 -> "1.5 KB", etc.
std::wstring format_size(uint64_t bytes);

// Shorter format for column display
// Uses fewer decimal places for compact display
std::wstring format_size_for_column(uint64_t bytes);

// Shell column formatter with support for transient placeholder values.
bool is_pending_size(uint64_t bytes);
std::wstring format_size_for_shell_column(uint64_t bytes);

} // namespace fs
