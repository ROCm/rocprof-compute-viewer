// MIT License
//
// Copyright (c) 2024-2025 Advanced Micro Devices, Inc. All rights reserved.
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
#include "highlight.h"

// ============================================================================
// Color Utility Tests - arithmetic operations with clamping
// ============================================================================

TEST(ColorTest, ConstructorClipsOutOfRangeValues)
{
    Color c(300, -50, 256);
    EXPECT_EQ(c.red(), 255);
    EXPECT_EQ(c.green(), 0);
    EXPECT_EQ(c.blue(), 255);
}

TEST(ColorTest, ScalarMultiplicationWithClipping)
{
    Color c(200, 100, 50);
    Color result = c * 2.0f;

    EXPECT_EQ(result.red(), 255); // Clipped
    EXPECT_EQ(result.green(), 200);
    EXPECT_EQ(result.blue(), 100);
}

TEST(ColorTest, ScalarDivision)
{
    Color c(100, 200, 150);
    Color result = c / 2.0f;

    EXPECT_EQ(result.red(), 50);
    EXPECT_EQ(result.green(), 100);
    EXPECT_EQ(result.blue(), 75);
}

TEST(ColorTest, ColorAdditionWithClipping)
{
    Color c1(200, 100, 50);
    Color c2(100, 50, 25);
    Color result = c1 + c2;

    EXPECT_EQ(result.red(), 255); // Clipped
    EXPECT_EQ(result.green(), 150);
    EXPECT_EQ(result.blue(), 75);
}

TEST(ColorTest, ColorMultiplication)
{
    Color c1(100, 100, 100);
    Color c2(2, 1, 2);
    Color result = c1 * c2;

    EXPECT_EQ(result.red(), 200);
    EXPECT_EQ(result.green(), 100);
    EXPECT_EQ(result.blue(), 200);
}

TEST(ColorTest, ClipFunction)
{
    EXPECT_EQ(Color::clip(-10.0f), 0.0f);
    EXPECT_EQ(Color::clip(128.0f), 128.0f);
    EXPECT_EQ(Color::clip(300.0f), 255.0f);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
