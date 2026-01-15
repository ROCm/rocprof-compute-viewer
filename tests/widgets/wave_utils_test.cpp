// MIT License
//
// Copyright (c) 2024-2026 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <gtest/gtest.h>

// Include the extracted utilities - no Qt dependencies needed!
#include "util/wave_utils.h"

// ============================================================================
// WaveUtils::mipShiftLeft / mipShiftRight Tests
// ============================================================================

TEST(MipShiftTest, LeftShiftPositiveLevel)
{
    // mipmap_level=2 means shift left by 2 (multiply by 4)
    EXPECT_EQ(WaveUtils::mipShiftLeft(10, 2), 40);
    EXPECT_EQ(WaveUtils::mipShiftLeft(1, 3), 8);
    EXPECT_EQ(WaveUtils::mipShiftLeft(5, 0), 5);
}

TEST(MipShiftTest, LeftShiftNegativeLevel)
{
    // mipmap_level=-2 means shift right by 2 (divide by 4)
    EXPECT_EQ(WaveUtils::mipShiftLeft(40, -2), 10);
    EXPECT_EQ(WaveUtils::mipShiftLeft(8, -3), 1);
}

TEST(MipShiftTest, RightShiftPositiveLevel)
{
    EXPECT_EQ(WaveUtils::mipShiftRight(40, 2), 10);
    EXPECT_EQ(WaveUtils::mipShiftRight(8, 3), 1);
    EXPECT_EQ(WaveUtils::mipShiftRight(5, 0), 5);
}

TEST(MipShiftTest, RightShiftNegativeLevel)
{
    // Negative level inverts the operation
    EXPECT_EQ(WaveUtils::mipShiftRight(10, -2), 40);
    EXPECT_EQ(WaveUtils::mipShiftRight(1, -3), 8);
}

TEST(MipShiftTest, ZeroValue)
{
    EXPECT_EQ(WaveUtils::mipShiftLeft(0, 5), 0);
    EXPECT_EQ(WaveUtils::mipShiftRight(0, 5), 0);
}

// ============================================================================
// WaveUtils::getTokenSize Tests
// ============================================================================

TEST(TokenSizeTest, NaviWaveMultiplier)
{
    // Navi uses 12/2 = 6x multiplier, others use 3/2 = 1.5x
    int64_t naviSize = WaveUtils::getTokenSize(100, 0, true);
    int64_t otherSize = WaveUtils::getTokenSize(100, 0, false);

    EXPECT_EQ(naviSize, 600);  // 100 * 12 / 2
    EXPECT_EQ(otherSize, 150); // 100 * 3 / 2
}

TEST(TokenSizeTest, MipmapLevelAffectsSize)
{
    // Higher mipmap level reduces the result
    int64_t level0 = WaveUtils::getTokenSize(100, 0, false);
    int64_t level1 = WaveUtils::getTokenSize(100, 1, false);
    int64_t level2 = WaveUtils::getTokenSize(100, 2, false);

    EXPECT_GT(level0, level1);
    EXPECT_GT(level1, level2);
}

TEST(TokenSizeTest, RoundingBehavior)
{
    // Test that rounding is applied correctly
    // rounding = mipShiftLeft(1, mipmap_level) >> 1
    // For mipmap_level=1: rounding = (1 << 1) >> 1 = 1
    int64_t size = WaveUtils::getTokenSize(3, 1, false);
    // (3 * 3 / 2 + 1) >> 1 = (4 + 1) >> 1 = 2
    EXPECT_EQ(size, 2);
}

// ============================================================================
// WaveUtils::slotHeightReduction Tests
// ============================================================================

TEST(SlotHeightTest, ReductionAtDifferentLevels)
{
    EXPECT_EQ(WaveUtils::slotHeightReduction(0), 3);
    EXPECT_EQ(WaveUtils::slotHeightReduction(1), 1);
    EXPECT_EQ(WaveUtils::slotHeightReduction(2), 0);
    EXPECT_EQ(WaveUtils::slotHeightReduction(3), -1);
}

TEST(SlotHeightTest, NegativeLevelReturnsThree)
{
    EXPECT_EQ(WaveUtils::slotHeightReduction(-1), 3);
    EXPECT_EQ(WaveUtils::slotHeightReduction(-5), 3);
}

// ============================================================================
// SourceUtils::getFilename Tests
// ============================================================================

TEST(SourceUtilsTest, GetFilenameFromLinePath)
{
    EXPECT_EQ(SourceUtils::getFilename("/path/to/file.cpp:42"), "/path/to/file.cpp");
    EXPECT_EQ(SourceUtils::getFilename("file.cpp:1"), "file.cpp");
    EXPECT_EQ(SourceUtils::getFilename("test.h:999"), "test.h");
}

TEST(SourceUtilsTest, GetFilenameNoLineNumber)
{
    // No colon - return original
    EXPECT_EQ(SourceUtils::getFilename("/path/to/file.cpp"), "/path/to/file.cpp");
    EXPECT_EQ(SourceUtils::getFilename("file.cpp"), "file.cpp");
}

TEST(SourceUtilsTest, GetFilenameEmptyString) { EXPECT_EQ(SourceUtils::getFilename(""), ""); }

TEST(SourceUtilsTest, GetFilenameMultipleColons)
{
    // Should use last colon (rfind)
    EXPECT_EQ(SourceUtils::getFilename("C:/path/file.cpp:10"), "C:/path/file.cpp");
}

// ============================================================================
// SourceUtils::getLineNumber Tests
// ============================================================================

TEST(SourceUtilsTest, GetLineNumberFromPath)
{
    EXPECT_EQ(SourceUtils::getLineNumber("/path/to/file.cpp:42"), 42);
    EXPECT_EQ(SourceUtils::getLineNumber("file.cpp:1"), 1);
    EXPECT_EQ(SourceUtils::getLineNumber("test.h:999"), 999);
}

TEST(SourceUtilsTest, GetLineNumberNoColon)
{
    EXPECT_EQ(SourceUtils::getLineNumber("file.cpp"), -1);
    EXPECT_EQ(SourceUtils::getLineNumber(""), -1);
}

TEST(SourceUtilsTest, GetLineNumberInvalidNumber)
{
    EXPECT_EQ(SourceUtils::getLineNumber("file.cpp:abc"), -1);
    EXPECT_EQ(SourceUtils::getLineNumber("file.cpp:"), -1);
}

// ============================================================================
// SourceUtils::getBasename Tests
// ============================================================================

TEST(SourceUtilsTest, GetBasenameFromPath)
{
    EXPECT_EQ(SourceUtils::getBasename("/path/to/file.cpp"), "file.cpp");
    EXPECT_EQ(SourceUtils::getBasename("/a/b/c/d.h"), "d.h");
    EXPECT_EQ(SourceUtils::getBasename("./local/test.cpp"), "test.cpp");
}

TEST(SourceUtilsTest, GetBasenameNoSlash)
{
    EXPECT_EQ(SourceUtils::getBasename("file.cpp"), "file.cpp");
    EXPECT_EQ(SourceUtils::getBasename("test"), "test");
}

TEST(SourceUtilsTest, GetBasenameTrailingSlash)
{
    // Edge case: path ends with slash - returns original since no basename exists
    EXPECT_EQ(SourceUtils::getBasename("/path/to/"), "/path/to/");
}

// ============================================================================
// LatencyData Tests
// ============================================================================

TEST(LatencyDataTest, DefaultConstruction)
{
    LatencyData lat;
    EXPECT_EQ(lat.latency, 0);
    EXPECT_EQ(lat.stalled, 0);
}

TEST(LatencyDataTest, AdditionOperator)
{
    LatencyData a{100, 30};
    LatencyData b{50, 20};

    a += b;

    EXPECT_EQ(a.latency, 150);
    EXPECT_EQ(a.stalled, 50);
}

TEST(LatencyDataTest, ChainedAddition)
{
    LatencyData total{0, 0};
    total += LatencyData{10, 5};
    total += LatencyData{20, 10};
    total += LatencyData{30, 15};

    EXPECT_EQ(total.latency, 60);
    EXPECT_EQ(total.stalled, 30);
}

TEST(LatencyDataTest, Equality)
{
    LatencyData a{100, 50};
    LatencyData b{100, 50};
    LatencyData c{100, 60};

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
