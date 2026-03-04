#include <gtest/gtest.h>
#include "core/size_cache.h"
#include <thread>
#include <vector>
#include <chrono>

namespace fs {

class SizeCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clear cache before each test
        SizeCache::instance().clear();
    }
    
    void TearDown() override {
        // Clean up after each test
        SizeCache::instance().clear();
    }
};

// ============================================================================
// Basic Put/Get Tests
// ============================================================================

TEST_F(SizeCacheTest, PutAndGet) {
    SizeCache& cache = SizeCache::instance();
    
    // Put a value
    cache.put(L"C:\\test\\path", 1024);
    
    // Get it back
    auto result = cache.get(L"C:\\test\\path");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 1024);
}

TEST_F(SizeCacheTest, GetNonExistentPath) {
    SizeCache& cache = SizeCache::instance();
    
    // Try to get a path that was never put
    auto result = cache.get(L"C:\\nonexistent\\path");
    EXPECT_FALSE(result.has_value());
}

TEST_F(SizeCacheTest, PutMultipleValues) {
    SizeCache& cache = SizeCache::instance();
    
    cache.put(L"C:\\path1", 1024);
    cache.put(L"C:\\path2", 2048);
    cache.put(L"C:\\path3", 4096);
    
    EXPECT_EQ(cache.get(L"C:\\path1").value(), 1024);
    EXPECT_EQ(cache.get(L"C:\\path2").value(), 2048);
    EXPECT_EQ(cache.get(L"C:\\path3").value(), 4096);
}

TEST_F(SizeCacheTest, OverwriteExistingValue) {
    SizeCache& cache = SizeCache::instance();
    
    cache.put(L"C:\\test\\path", 1024);
    EXPECT_EQ(cache.get(L"C:\\test\\path").value(), 1024);
    
    // Overwrite with new value
    cache.put(L"C:\\test\\path", 2048);
    EXPECT_EQ(cache.get(L"C:\\test\\path").value(), 2048);
}

// ============================================================================
// Path Normalization Tests
// ============================================================================

TEST_F(SizeCacheTest, PathNormalizationLowercase) {
    SizeCache& cache = SizeCache::instance();
    
    // Put with uppercase
    cache.put(L"C:\\TEST\\PATH", 1024);
    
    // Get with lowercase - should find it due to normalization
    auto result = cache.get(L"c:\\test\\path");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 1024);
}

TEST_F(SizeCacheTest, PathNormalizationForwardSlashes) {
    SizeCache& cache = SizeCache::instance();
    
    // Put with backslashes
    cache.put(L"C:\\test\\path", 1024);
    
    // Get with forward slashes - should find it due to normalization
    auto result = cache.get(L"C:/test/path");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 1024);
}

TEST_F(SizeCacheTest, PathNormalizationMixed) {
    SizeCache& cache = SizeCache::instance();
    
    // Put with mixed case and slashes
    cache.put(L"C:\\Test/Path", 1024);
    
    // Get with different case and slashes
    auto result = cache.get(L"c:/TEST\\path");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 1024);
}

TEST_F(SizeCacheTest, PathNormalizationTrailingBackslash) {
    SizeCache& cache = SizeCache::instance();
    
    // Put without trailing backslash
    cache.put(L"C:\\test\\path", 1024);
    
    // Get with trailing backslash - should find it (trailing backslash stripped)
    auto result = cache.get(L"C:\\test\\path\\");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 1024);
}

TEST_F(SizeCacheTest, PathNormalizationRootDrive) {
    SizeCache& cache = SizeCache::instance();
    
    // Root paths should keep their trailing backslash
    cache.put(L"C:\\", 1024);
    
    // Should be able to retrieve with or without trailing backslash
    auto result = cache.get(L"C:\\");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 1024);
}

// ============================================================================
// Invalidation Tests
// ============================================================================

TEST_F(SizeCacheTest, InvalidateRemovesEntry) {
    SizeCache& cache = SizeCache::instance();
    
    cache.put(L"C:\\test\\path", 1024);
    EXPECT_TRUE(cache.get(L"C:\\test\\path").has_value());
    
    // Invalidate the entry
    cache.invalidate(L"C:\\test\\path");
    
    // Should no longer be found
    EXPECT_FALSE(cache.get(L"C:\\test\\path").has_value());
}

TEST_F(SizeCacheTest, InvalidateNonExistentPath) {
    SizeCache& cache = SizeCache::instance();
    
    // Should not crash when invalidating non-existent path
    cache.invalidate(L"C:\\nonexistent\\path");
    
    // Cache should still be empty
    EXPECT_EQ(cache.entry_count(), 0);
}

TEST_F(SizeCacheTest, InvalidateWithNormalization) {
    SizeCache& cache = SizeCache::instance();
    
    cache.put(L"C:\\test\\path", 1024);
    
    // Invalidate with different case/slashes
    cache.invalidate(L"c:/TEST\\PATH");
    
    // Should be gone
    EXPECT_FALSE(cache.get(L"C:\\test\\path").has_value());
}

// ============================================================================
// Clear Tests
// ============================================================================

TEST_F(SizeCacheTest, ClearRemovesAllEntries) {
    SizeCache& cache = SizeCache::instance();
    
    cache.put(L"C:\\path1", 1024);
    cache.put(L"C:\\path2", 2048);
    cache.put(L"C:\\path3", 4096);
    
    EXPECT_EQ(cache.entry_count(), 3);
    
    // Clear all entries
    cache.clear();
    
    EXPECT_EQ(cache.entry_count(), 0);
    EXPECT_FALSE(cache.get(L"C:\\path1").has_value());
    EXPECT_FALSE(cache.get(L"C:\\path2").has_value());
    EXPECT_FALSE(cache.get(L"C:\\path3").has_value());
}

TEST_F(SizeCacheTest, ClearEmptyCache) {
    SizeCache& cache = SizeCache::instance();
    
    // Should not crash when clearing empty cache
    cache.clear();
    EXPECT_EQ(cache.entry_count(), 0);
}

// ============================================================================
// Freshness Tests
// ============================================================================

TEST_F(SizeCacheTest, IsFreshAfterPut) {
    SizeCache& cache = SizeCache::instance();
    
    cache.put(L"C:\\test\\path", 1024);
    
    // Should be fresh immediately after put
    EXPECT_TRUE(cache.is_fresh(L"C:\\test\\path"));
}

TEST_F(SizeCacheTest, IsFreshNonExistentPath) {
    SizeCache& cache = SizeCache::instance();
    
    // Non-existent path is not fresh
    EXPECT_FALSE(cache.is_fresh(L"C:\\nonexistent\\path"));
}

// ============================================================================
// Concurrent Access Tests
// ============================================================================

TEST_F(SizeCacheTest, ConcurrentPutGet) {
    SizeCache& cache = SizeCache::instance();
    const int NUM_THREADS = 8;
    const int OPERATIONS_PER_THREAD = 100;
    
    std::vector<std::thread> threads;
    
    // Create threads that put and get values
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&cache, t]() {
            for (int i = 0; i < OPERATIONS_PER_THREAD; ++i) {
                std::wstring path = L"C:\\thread_" + std::to_wstring(t) + L"\\path_" + std::to_wstring(i);
                uint64_t size = static_cast<uint64_t>(t * 1000 + i);
                
                // Put value
                cache.put(path, size);
                
                // Get value back
                auto result = cache.get(path);
                EXPECT_TRUE(result.has_value());
                EXPECT_EQ(result.value(), size);
            }
        });
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Verify all entries are in cache
    EXPECT_EQ(cache.entry_count(), NUM_THREADS * OPERATIONS_PER_THREAD);
}

TEST_F(SizeCacheTest, ConcurrentMixedOperations) {
    SizeCache& cache = SizeCache::instance();
    const int NUM_THREADS = 4;
    const int OPERATIONS_PER_THREAD = 50;
    
    std::vector<std::thread> threads;
    
    // Create threads that do mixed operations
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&cache, t]() {
            for (int i = 0; i < OPERATIONS_PER_THREAD; ++i) {
                std::wstring path = L"C:\\mixed_" + std::to_wstring(t) + L"\\path_" + std::to_wstring(i);
                uint64_t size = static_cast<uint64_t>(t * 1000 + i);
                
                // Put
                cache.put(path, size);
                
                // Get
                auto result = cache.get(path);
                EXPECT_TRUE(result.has_value());
                
                // Check freshness
                bool fresh = cache.is_fresh(path);
                EXPECT_TRUE(fresh);
                
                // Invalidate every 3rd entry
                if (i % 3 == 0) {
                    cache.invalidate(path);
                }
            }
        });
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Cache should have some entries (those not invalidated)
    EXPECT_GT(cache.entry_count(), 0);
}

TEST_F(SizeCacheTest, ConcurrentReadWrite) {
    SizeCache& cache = SizeCache::instance();
    const int NUM_READERS = 4;
    const int NUM_WRITERS = 2;
    const int OPERATIONS = 100;
    
    // Pre-populate cache
    for (int i = 0; i < 10; ++i) {
        cache.put(L"C:\\shared\\path_" + std::to_wstring(i), i * 1000);
    }
    
    std::vector<std::thread> threads;
    
    // Reader threads
    for (int r = 0; r < NUM_READERS; ++r) {
        threads.emplace_back([&cache]() {
            for (int i = 0; i < OPERATIONS; ++i) {
                for (int j = 0; j < 10; ++j) {
                    auto result = cache.get(L"C:\\shared\\path_" + std::to_wstring(j));
                    // May or may not have value depending on writer timing
                }
            }
        });
    }
    
    // Writer threads
    for (int w = 0; w < NUM_WRITERS; ++w) {
        threads.emplace_back([&cache, w]() {
            for (int i = 0; i < OPERATIONS; ++i) {
                int idx = (w * OPERATIONS + i) % 10;
                cache.put(L"C:\\shared\\path_" + std::to_wstring(idx), 
                         static_cast<uint64_t>(w * 10000 + i));
            }
        });
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Cache should have entries
    EXPECT_GT(cache.entry_count(), 0);
}

}  // namespace fs
