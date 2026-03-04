#include <gtest/gtest.h>
#include "core/size_formatter.h"

namespace fs {

class SizeFormatterTest : public ::testing::Test {
protected:
    // format_size uses 1 decimal for KB/MB, 2 decimals for GB/TB
    // format_size_for_column uses 0 decimals for KB, 1 for MB, 2 for GB/TB
};

// ============================================================================
// format_size() Tests
// ============================================================================

TEST_F(SizeFormatterTest, FormatSizeZero) {
    EXPECT_EQ(format_size(0), L"0 bytes");
}

TEST_F(SizeFormatterTest, FormatSizeOneByteRange) {
    EXPECT_EQ(format_size(1), L"1 bytes");
    EXPECT_EQ(format_size(512), L"512 bytes");
    EXPECT_EQ(format_size(1023), L"1,023 bytes");
}

TEST_F(SizeFormatterTest, FormatSizeExactlyOneKB) {
    EXPECT_EQ(format_size(1024), L"1.0 KB");
}

TEST_F(SizeFormatterTest, FormatSizeOnePointFiveKB) {
    EXPECT_EQ(format_size(1536), L"1.5 KB");
}

TEST_F(SizeFormatterTest, FormatSizeKBRange) {
    EXPECT_EQ(format_size(2048), L"2.0 KB");
    EXPECT_EQ(format_size(10240), L"10.0 KB");
    EXPECT_EQ(format_size(512000), L"500.0 KB");
    EXPECT_EQ(format_size(1048575), L"1024.0 KB");  // Just under 1 MB
}

TEST_F(SizeFormatterTest, FormatSizeExactlyOneMB) {
    EXPECT_EQ(format_size(1048576), L"1.0 MB");
}

TEST_F(SizeFormatterTest, FormatSizeMBRange) {
    EXPECT_EQ(format_size(2097152), L"2.0 MB");
    EXPECT_EQ(format_size(10485760), L"10.0 MB");
    EXPECT_EQ(format_size(536870912), L"512.0 MB");
    EXPECT_EQ(format_size(1073741823), L"1024.0 MB");  // Just under 1 GB
}

TEST_F(SizeFormatterTest, FormatSizeExactlyOneGB) {
    EXPECT_EQ(format_size(1073741824), L"1.00 GB");
}

TEST_F(SizeFormatterTest, FormatSizeGBRange) {
    EXPECT_EQ(format_size(2147483648ULL), L"2.00 GB");
    EXPECT_EQ(format_size(10737418240ULL), L"10.00 GB");
    EXPECT_EQ(format_size(536870912000ULL), L"500.00 GB");
    EXPECT_EQ(format_size(1099511627775ULL), L"1024.00 GB");  // Just under 1 TB
}

TEST_F(SizeFormatterTest, FormatSizeExactlyOneTB) {
    EXPECT_EQ(format_size(1099511627776ULL), L"1.00 TB");
}

TEST_F(SizeFormatterTest, FormatSizeTBRange) {
    EXPECT_EQ(format_size(2199023255552ULL), L"2.00 TB");
    EXPECT_EQ(format_size(10995116277760ULL), L"10.00 TB");
    EXPECT_EQ(format_size(1099511627776000ULL), L"1000.00 TB");
}

TEST_F(SizeFormatterTest, FormatSizeLargeValue) {
    // 5.5 TB
    EXPECT_EQ(format_size(6043707973632ULL), L"5.50 TB");
}

// ============================================================================
// format_size_for_column() Tests
// ============================================================================

TEST_F(SizeFormatterTest, FormatSizeForColumnZero) {
    EXPECT_EQ(format_size_for_column(0), L"0 bytes");
}

TEST_F(SizeFormatterTest, FormatSizeForColumnByteRange) {
    EXPECT_EQ(format_size_for_column(1), L"1 bytes");
    EXPECT_EQ(format_size_for_column(512), L"512 bytes");
    EXPECT_EQ(format_size_for_column(1023), L"1,023 bytes");
}

TEST_F(SizeFormatterTest, FormatSizeForColumnKBRange) {
    // Column format uses 0 decimals for KB
    EXPECT_EQ(format_size_for_column(1024), L"1 KB");
    EXPECT_EQ(format_size_for_column(1536), L"2 KB");  // Rounds to 2
    EXPECT_EQ(format_size_for_column(2048), L"2 KB");
    EXPECT_EQ(format_size_for_column(10240), L"10 KB");
}

TEST_F(SizeFormatterTest, FormatSizeForColumnMBRange) {
    // Column format uses 1 decimal for MB
    EXPECT_EQ(format_size_for_column(1048576), L"1.0 MB");
    EXPECT_EQ(format_size_for_column(2097152), L"2.0 MB");
    EXPECT_EQ(format_size_for_column(10485760), L"10.0 MB");
}

TEST_F(SizeFormatterTest, FormatSizeForColumnGBRange) {
    // Column format uses 2 decimals for GB
    EXPECT_EQ(format_size_for_column(1073741824), L"1.00 GB");
    EXPECT_EQ(format_size_for_column(2147483648ULL), L"2.00 GB");
    EXPECT_EQ(format_size_for_column(10737418240ULL), L"10.00 GB");
}

TEST_F(SizeFormatterTest, FormatSizeForColumnTBRange) {
    // Column format uses 2 decimals for TB
    EXPECT_EQ(format_size_for_column(1099511627776ULL), L"1.00 TB");
    EXPECT_EQ(format_size_for_column(2199023255552ULL), L"2.00 TB");
}

TEST_F(SizeFormatterTest, FormatSizeForColumnIsShorterThanFormatSize) {
    // Verify that column format is indeed shorter (fewer decimals)
    uint64_t test_size = 1536;  // 1.5 KB
    std::wstring full = format_size(test_size);
    std::wstring column = format_size_for_column(test_size);
    
    // Full format: "1.5 KB", Column format: "2 KB"
    EXPECT_LT(column.length(), full.length());
}

}  // namespace fs
