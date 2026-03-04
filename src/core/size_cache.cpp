#include "core/size_cache.h"
#include "logging.h"
#include <windows.h>
#include <algorithm>
#include <vector>
#include <cctype>

namespace fs {

SizeCache& SizeCache::instance() {
    static SizeCache s_instance;
    return s_instance;
}

std::wstring SizeCache::normalize_path(std::wstring_view path) {
    std::wstring normalized(path);
    
    // Convert to lowercase
    for (auto& c : normalized) {
        c = towlower(c);
    }
    
    // Replace forward slashes with backslashes
    for (auto& c : normalized) {
        if (c == L'/') {
            c = L'\\';
        }
    }
    
    // Strip trailing backslash unless it's a root (e.g., "C:\")
    if (normalized.length() > 3 && normalized.back() == L'\\') {
        normalized.pop_back();
    }
    
    return normalized;
}

std::optional<uint64_t> SizeCache::get(std::wstring_view path) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    
    std::wstring normalized = normalize_path(path);
    auto it = m_cache.find(normalized);
    
    if (it == m_cache.end()) {
        FS_TRACE(FS_MOD_CACHE, "Cache miss for path");
        return std::nullopt;
    }
    
    const auto& entry = it->second;
    auto now = std::chrono::steady_clock::now();
    
    // Check if expired
    if (now - entry.timestamp > TTL) {
        FS_TRACE(FS_MOD_CACHE, "Cache entry expired for path");
        return entry.size; // Return value but log as stale
    }
    
    // Update last_access timestamp
    it->second.last_access = now;
    
    FS_TRACE(FS_MOD_CACHE, "Cache hit for path");
    return entry.size;
}

void SizeCache::put(std::wstring_view path, uint64_t size) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    
    std::wstring normalized = normalize_path(path);
    auto now = std::chrono::steady_clock::now();
    
    m_cache[normalized] = {size, now, now};
    
    evict_if_needed();
    
    FS_TRACE(FS_MOD_CACHE, "Cached size for path: %llu bytes", size);
}

void SizeCache::invalidate(std::wstring_view path) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    
    std::wstring normalized = normalize_path(path);
    auto it = m_cache.find(normalized);
    
    if (it != m_cache.end()) {
        m_cache.erase(it);
        FS_TRACE(FS_MOD_CACHE, "Invalidated cache entry for path");
    }
}

void SizeCache::clear() {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    
    size_t count = m_cache.size();
    m_cache.clear();
    
    FS_TRACE(FS_MOD_CACHE, "Cleared cache: %zu entries removed", count);
}

size_t SizeCache::memory_usage() const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    
    // Estimate: entry_count * (avg_key_size + sizeof(CacheEntry) + overhead)
    // avg_key_size ≈ 100 chars * sizeof(wchar_t) = 200 bytes
    // sizeof(CacheEntry) ≈ 48 bytes (uint64_t + 2 time_points)
    // overhead ≈ 64 bytes per entry (hash map overhead)
    
    size_t count = m_cache.size();
    size_t avg_key_size = 200; // 100 wchar_t chars
    size_t entry_overhead = sizeof(CacheEntry) + 64;
    
    return count * (avg_key_size + entry_overhead);
}

size_t SizeCache::entry_count() const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_cache.size();
}

bool SizeCache::is_fresh(std::wstring_view path) const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    
    std::wstring normalized = normalize_path(path);
    auto it = m_cache.find(normalized);
    
    if (it == m_cache.end()) {
        return false;
    }
    
    const auto& entry = it->second;
    auto now = std::chrono::steady_clock::now();
    
    return (now - entry.timestamp) <= TTL;
}

void SizeCache::evict_if_needed() {
    // Called with lock already held — must NOT call public locking methods
    
    // Check if over capacity by entry count
    if (m_cache.size() > MAX_ENTRIES) {
        // Remove 10% of oldest entries
        size_t to_remove = m_cache.size() / 10;
        if (to_remove == 0) to_remove = 1;
        
        // Find oldest entries by last_access
        std::vector<std::pair<std::wstring, std::chrono::steady_clock::time_point>> entries;
        for (const auto& [key, entry] : m_cache) {
            entries.push_back({key, entry.last_access});
        }
        
        // Sort by last_access (oldest first)
        std::sort(entries.begin(), entries.end(),
                  [](const auto& a, const auto& b) {
                      return a.second < b.second;
                  });
        
        // Remove oldest entries
        for (size_t i = 0; i < to_remove && i < entries.size(); ++i) {
            m_cache.erase(entries[i].first);
        }
        
        FS_TRACE(FS_MOD_CACHE, "Evicted %zu entries due to MAX_ENTRIES limit", to_remove);
    }
    
    // Inline memory estimate (avoid calling memory_usage() which would deadlock)
    size_t count = m_cache.size();
    size_t estimated_mem = count * (200 + sizeof(CacheEntry) + 64);
    
    if (estimated_mem > MAX_MEMORY_BYTES) {
        size_t to_remove = count / 10;
        if (to_remove == 0) to_remove = 1;
        
        std::vector<std::pair<std::wstring, std::chrono::steady_clock::time_point>> entries;
        for (const auto& [key, entry] : m_cache) {
            entries.push_back({key, entry.last_access});
        }
        
        std::sort(entries.begin(), entries.end(),
                  [](const auto& a, const auto& b) {
                      return a.second < b.second;
                  });
        
        for (size_t i = 0; i < to_remove && i < entries.size(); ++i) {
            m_cache.erase(entries[i].first);
        }
        
        FS_TRACE(FS_MOD_CACHE, "Evicted %zu entries due to memory limit", to_remove);
    }
}

} // namespace fs
