#pragma once
#include <cstdint>
#include <string>

namespace fs {

// Format bytes into human-readable string
// 0 -> "0 bytes", 1023 -> "1,023 bytes", 1536 -> "1.5 KB", etc.
std::wstring format_size(uint64_t bytes);

// Shorter format for column display
// Uses fewer decimal places for compact display
std::wstring format_size_for_column(uint64_t bytes);

} // namespace fs
