#pragma once

#include <cstdint>
#include <string>
#include <optional>
#include <chrono>
#include <shared_mutex>
#include <unordered_map>
#include <string_view>

namespace fs {

class SizeCache {
public:
    // Meyers singleton
    static SizeCache& instance();
    
    // Get cached size, returns nullopt if not found or expired
    std::optional<uint64_t> get(std::wstring_view path);
    
    // Store size with current timestamp
    void put(std::wstring_view path, uint64_t size);
    
    // Remove specific entry
    void invalidate(std::wstring_view path);
    
    // Remove all entries
    void clear();
    
    // Approximate memory usage in bytes
    size_t memory_usage() const;
    
    // Number of entries
    size_t entry_count() const;
    
    // Check if entry exists and is not stale
    bool is_fresh(std::wstring_view path) const;

private:
    SizeCache() = default;
    ~SizeCache() = default;
    SizeCache(const SizeCache&) = delete;
    SizeCache& operator=(const SizeCache&) = delete;
    
    // Normalize path for cache key (lowercase, strip trailing backslash, forward->back slash)
    static std::wstring normalize_path(std::wstring_view path);
    
    // Evict oldest entries when over capacity
    void evict_if_needed();
    
    struct CacheEntry {
        uint64_t size;
        std::chrono::steady_clock::time_point timestamp;
        std::chrono::steady_clock::time_point last_access;
    };
    
    // Constants
    static constexpr size_t MAX_ENTRIES = 100000;
    static constexpr auto TTL = std::chrono::minutes(5);
    static constexpr size_t MAX_MEMORY_BYTES = 50 * 1024 * 1024; // 50MB
    
    mutable std::shared_mutex m_mutex; // reader-writer lock
    std::unordered_map<std::wstring, CacheEntry> m_cache;
};

} // namespace fs
