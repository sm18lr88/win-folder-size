#include <gtest/gtest.h>
#include "providers/folder_scanner.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace fs::providers {

class FolderScannerTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::error_code error;
        m_root = std::filesystem::temp_directory_path(error) /
            (L"foldersize_scanner_test_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
             std::to_wstring(GetTickCount64()));
        ASSERT_FALSE(error);
        ASSERT_TRUE(std::filesystem::create_directory(m_root, error));
        ASSERT_FALSE(error);
    }

    void TearDown() override {
        std::error_code error;
        std::filesystem::remove_all(m_root, error);
    }

    static void WriteFileWithSize(const std::filesystem::path& path, std::uint64_t size) {
        std::ofstream file(path, std::ios::binary);
        ASSERT_TRUE(file.is_open());

        for (std::uint64_t index = 0; index < size; ++index) {
            file.put(static_cast<char>('A' + (index % 26)));
        }

        ASSERT_TRUE(file.good());
    }

    std::filesystem::path m_root;
};

// ============================================================================
// Completion-Aware Scan Tests
// ============================================================================

TEST_F(FolderScannerTest, ScanSyncResultReturnsCompleteExactSizeForTemporaryDirectory) {
    const auto nested_dir = m_root / L"nested";
    std::error_code error;
    ASSERT_TRUE(std::filesystem::create_directory(nested_dir, error));
    ASSERT_FALSE(error);

    WriteFileWithSize(m_root / L"root.bin", 7);
    WriteFileWithSize(nested_dir / L"child.bin", 11);

    auto result = FolderScanner::instance().scan_sync_result(m_root.wstring(), std::chrono::seconds(2));

    EXPECT_TRUE(result.completed());
    EXPECT_EQ(result.status, FolderScanner::ScanStatus::Complete);
    EXPECT_EQ(result.size, 18ULL);
}

TEST_F(FolderScannerTest, ScanSyncResultReturnsIncompleteForMissingDirectory) {
    const auto missing_dir = m_root / L"missing";

    auto result = FolderScanner::instance().scan_sync_result(missing_dir.wstring(), std::chrono::seconds(2));

    EXPECT_FALSE(result.completed());
    EXPECT_EQ(result.status, FolderScanner::ScanStatus::Incomplete);
}

TEST_F(FolderScannerTest, ScanSyncCompatibilityWrapperReturnsNulloptForIncompleteResult) {
    const auto missing_dir = m_root / L"missing";

    auto result = FolderScanner::instance().scan_sync(missing_dir.wstring(), std::chrono::seconds(2));

    EXPECT_FALSE(result.has_value());
}

} // namespace fs::providers
