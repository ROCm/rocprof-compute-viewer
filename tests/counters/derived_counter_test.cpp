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

#include "derived_counter.h"
#include <gtest/gtest.h>
#include <cmath>

using namespace DerivedCounter;

// Helper to check floating point equality
constexpr float kEpsilon = 1e-5f;

// ============================================================================
// Test Axis Conversion
// ============================================================================

TEST(AxisConversionTest, FromString)
{
    EXPECT_EQ(axisFromString("XCC"), Axis::XCC);
    EXPECT_EQ(axisFromString("xcc"), Axis::XCC);
    EXPECT_EQ(axisFromString("0"), Axis::XCC);

    EXPECT_EQ(axisFromString("SE"), Axis::SE);
    EXPECT_EQ(axisFromString("se"), Axis::SE);
    EXPECT_EQ(axisFromString("1"), Axis::SE);

    EXPECT_EQ(axisFromString("CU"), Axis::CU);
    EXPECT_EQ(axisFromString("cu"), Axis::CU);
    EXPECT_EQ(axisFromString("2"), Axis::CU);

    EXPECT_EQ(axisFromString("TIME"), Axis::Time);
    EXPECT_EQ(axisFromString("Time"), Axis::Time);
    EXPECT_EQ(axisFromString("time"), Axis::Time);
    EXPECT_EQ(axisFromString("SAMPLES"), Axis::Time);
    EXPECT_EQ(axisFromString("3"), Axis::Time);
}

TEST(AxisConversionTest, ToString)
{
    EXPECT_EQ(axisToString(Axis::XCC), "XCC");
    EXPECT_EQ(axisToString(Axis::SE), "SE");
    EXPECT_EQ(axisToString(Axis::CU), "CU");
    EXPECT_EQ(axisToString(Axis::Time), "TIME");
}

// ============================================================================
// Test Shape
// ============================================================================

TEST(ShapeTest, DefaultConstruction)
{
    Shape s1;
    EXPECT_EQ(s1.getXCC(), 1);
    EXPECT_EQ(s1.getSE(), 1);
    EXPECT_EQ(s1.getCU(), 1);
    EXPECT_EQ(s1.getSamples(), 1);
    EXPECT_EQ(s1.totalSize(), 1);
    EXPECT_TRUE(s1.isScalar());
}

TEST(ShapeTest, ParameterizedConstruction)
{
    Shape s2(2, 4, 8, 100);
    EXPECT_EQ(s2.totalSize(), 2 * 4 * 8 * 100);
    EXPECT_FALSE(s2.isScalar());
}

TEST(ShapeTest, DimensionSizes)
{
    Shape s(2, 4, 8, 100);
    EXPECT_EQ(s.dimSize(Axis::XCC), 2);
    EXPECT_EQ(s.dimSize(Axis::SE), 4);
    EXPECT_EQ(s.dimSize(Axis::CU), 8);
    EXPECT_EQ(s.dimSize(Axis::Time), 100);
}

TEST(ShapeTest, ReducedShapes)
{
    Shape s(2, 4, 8, 100);
    Shape reduced = s.reducedShape({Axis::XCC, Axis::SE});
    EXPECT_EQ(reduced.getXCC(), 1);
    EXPECT_EQ(reduced.getSE(), 1);
    EXPECT_EQ(reduced.getCU(), 8);
    EXPECT_EQ(reduced.getSamples(), 100);
}

TEST(ShapeTest, AllReduction)
{
    Shape s(2, 4, 8, 100);
    Shape allReduced = s.reducedShape({Axis::All});
    EXPECT_TRUE(allReduced.isScalar());
}

TEST(ShapeTest, Broadcasting)
{
    Shape a(2, 1, 8, 100);
    Shape b(1, 4, 1, 100);
    EXPECT_TRUE(Shape::areBroadcastable(a, b));

    Shape broadcast = Shape::broadcastShape(a, b);
    EXPECT_EQ(broadcast.getXCC(), 2);
    EXPECT_EQ(broadcast.getSE(), 4);
    EXPECT_EQ(broadcast.getCU(), 8);
    EXPECT_EQ(broadcast.getSamples(), 100);
}

// ============================================================================
// Test Tensor Basic Operations
// ============================================================================

TEST(TensorBasicTest, ScalarTensor)
{
    Tensor scalar(42.0f);
    EXPECT_TRUE(scalar.isScalar());
    EXPECT_NEAR(scalar.scalar(), 42.0f, kEpsilon);
}

TEST(TensorBasicTest, FilledTensor)
{
    Shape shape(2, 2, 2, 3);
    Tensor t1(shape, 1.0f);
    EXPECT_EQ(t1.size(), 24);
    for (size_t i = 0; i < t1.size(); ++i) EXPECT_NEAR(t1[i], 1.0f, kEpsilon);
}

TEST(TensorBasicTest, IndexedAccess)
{
    Shape shape(2, 2, 2, 3);
    Tensor t2(shape, 0.0f);
    t2.at(0, 0, 0, 0) = 1.0f;
    t2.at(1, 1, 1, 2) = 99.0f;
    EXPECT_NEAR(t2.at(0, 0, 0, 0), 1.0f, kEpsilon);
    EXPECT_NEAR(t2.at(1, 1, 1, 2), 99.0f, kEpsilon);
}

TEST(TensorBasicTest, LinearIndexConversion)
{
    Shape shape(2, 2, 2, 3);
    Tensor t(shape, 0.0f);
    size_t idx = t.linearIndex(1, 1, 1, 2);
    size_t xcc, se, cu, sample;
    t.multiIndex(idx, xcc, se, cu, sample);
    EXPECT_EQ(xcc, 1);
    EXPECT_EQ(se, 1);
    EXPECT_EQ(cu, 1);
    EXPECT_EQ(sample, 2);
}

// ============================================================================
// Test Tensor Arithmetic
// ============================================================================

class TensorArithmeticTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        shape = Shape(2, 2, 1, 4);
        data1.resize(16);
        data2.resize(16);
        for (int i = 0; i < 16; ++i)
        {
            data1[i] = static_cast<float>(i);
            data2[i] = 2.0f;
        }
        t1 = Tensor(shape, data1);
        t2 = Tensor(shape, data2);
    }

    Shape shape;
    std::vector<float> data1, data2;
    Tensor t1, t2;
};

TEST_F(TensorArithmeticTest, Addition)
{
    Tensor sum = t1 + t2;
    for (int i = 0; i < 16; ++i) EXPECT_NEAR(sum[i], i + 2.0f, kEpsilon);
}

TEST_F(TensorArithmeticTest, Subtraction)
{
    Tensor diff = t1 - t2;
    for (int i = 0; i < 16; ++i) EXPECT_NEAR(diff[i], i - 2.0f, kEpsilon);
}

TEST_F(TensorArithmeticTest, Multiplication)
{
    Tensor prod = t1 * t2;
    for (int i = 0; i < 16; ++i) EXPECT_NEAR(prod[i], i * 2.0f, kEpsilon);
}

TEST_F(TensorArithmeticTest, Division)
{
    Tensor div = t1 / t2;
    for (int i = 0; i < 16; ++i) EXPECT_NEAR(div[i], i / 2.0f, kEpsilon);
}

TEST_F(TensorArithmeticTest, ScalarMultiplication)
{
    Tensor scaled = t1 * 3.0f;
    for (int i = 0; i < 16; ++i) EXPECT_NEAR(scaled[i], i * 3.0f, kEpsilon);
}

TEST_F(TensorArithmeticTest, ScalarAddition)
{
    Tensor added = t1 + 10.0f;
    for (int i = 0; i < 16; ++i) EXPECT_NEAR(added[i], i + 10.0f, kEpsilon);
}

TEST_F(TensorArithmeticTest, Negation)
{
    Tensor neg = -t1;
    for (int i = 0; i < 16; ++i) EXPECT_NEAR(neg[i], static_cast<float>(-i), kEpsilon);
}

// ============================================================================
// Test Broadcasting
// ============================================================================

TEST(BroadcastingTest, ScalarBroadcast)
{
    Shape fullShape(2, 2, 2, 4);
    Tensor full(fullShape, 1.0f);
    Tensor scalar(5.0f);

    Tensor result = full + scalar;
    EXPECT_EQ(result.shape(), fullShape);
    for (size_t i = 0; i < result.size(); ++i) EXPECT_NEAR(result[i], 6.0f, kEpsilon);
}

TEST(BroadcastingTest, TimeVectorBroadcast)
{
    Shape fullShape(2, 2, 2, 4);
    Tensor full(fullShape, 1.0f);

    Shape timeShape(1, 1, 1, 4);
    std::vector<float> timeData = {1, 2, 3, 4};
    Tensor timeVec(timeShape, timeData);

    Tensor result = full * timeVec;
    EXPECT_EQ(result.shape(), fullShape);

    for (size_t xcc = 0; xcc < 2; ++xcc)
    {
        for (size_t se = 0; se < 2; ++se)
        {
            for (size_t cu = 0; cu < 2; ++cu)
            {
                for (size_t t = 0; t < 4; ++t) EXPECT_NEAR(result.at(xcc, se, cu, t), timeData[t], kEpsilon);
            }
        }
    }
}

TEST(BroadcastingTest, DifferentShapesBroadcast)
{
    Shape shapeA(2, 1, 1, 4);
    Shape shapeB(1, 3, 1, 4);
    Tensor a(shapeA, 2.0f);
    Tensor b(shapeB, 3.0f);

    Tensor result = a + b;
    Shape expectedShape(2, 3, 1, 4);
    EXPECT_EQ(result.shape(), expectedShape);
    for (size_t i = 0; i < result.size(); ++i) EXPECT_NEAR(result[i], 5.0f, kEpsilon);
}

// ============================================================================
// Test Reductions
// ============================================================================

class ReductionTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        shape = Shape(2, 2, 1, 3);
        data.resize(12);
        for (int i = 0; i < 12; ++i)
        {
            data[i] = static_cast<float>(i + 1); // 1, 2, 3, ..., 12
        }
        t = Tensor(shape, data);
    }

    Shape shape;
    std::vector<float> data;
    Tensor t;
};

TEST_F(ReductionTest, SumAll)
{
    Tensor sumAll = t.sumAll();
    EXPECT_TRUE(sumAll.isScalar());
    EXPECT_NEAR(sumAll.scalar(), 78.0f, kEpsilon); // 1+2+...+12 = 78
}

TEST_F(ReductionTest, MeanAll)
{
    Tensor meanAll = t.meanAll();
    EXPECT_TRUE(meanAll.isScalar());
    EXPECT_NEAR(meanAll.scalar(), 6.5f, kEpsilon); // 78/12
}

TEST_F(ReductionTest, MaxAll)
{
    Tensor maxAll = t.maxAll();
    EXPECT_TRUE(maxAll.isScalar());
    EXPECT_NEAR(maxAll.scalar(), 12.0f, kEpsilon);
}

TEST_F(ReductionTest, MinAll)
{
    Tensor minAll = t.minAll();
    EXPECT_TRUE(minAll.isScalar());
    EXPECT_NEAR(minAll.scalar(), 1.0f, kEpsilon);
}

TEST_F(ReductionTest, SumOverTimeAxis)
{
    Tensor sumTime = t.sum(Axis::Time);
    EXPECT_EQ(sumTime.shape(), Shape(2, 2, 1, 1));
    EXPECT_NEAR(sumTime.at(0, 0, 0, 0), 6.0f, kEpsilon);  // 1+2+3
    EXPECT_NEAR(sumTime.at(0, 1, 0, 0), 15.0f, kEpsilon); // 4+5+6
    EXPECT_NEAR(sumTime.at(1, 0, 0, 0), 24.0f, kEpsilon); // 7+8+9
    EXPECT_NEAR(sumTime.at(1, 1, 0, 0), 33.0f, kEpsilon); // 10+11+12
}

TEST_F(ReductionTest, SumOverXCCAxis)
{
    Tensor sumXCC = t.sum(Axis::XCC);
    EXPECT_EQ(sumXCC.shape(), Shape(1, 2, 1, 3));
    EXPECT_NEAR(sumXCC.at(0, 0, 0, 0), 8.0f, kEpsilon);  // 1+7
    EXPECT_NEAR(sumXCC.at(0, 0, 0, 1), 10.0f, kEpsilon); // 2+8
    EXPECT_NEAR(sumXCC.at(0, 0, 0, 2), 12.0f, kEpsilon); // 3+9
}

TEST_F(ReductionTest, SumOverMultipleAxes)
{
    Tensor sumXccSe = t.sum({Axis::XCC, Axis::SE});
    EXPECT_EQ(sumXccSe.shape(), Shape(1, 1, 1, 3));
    EXPECT_NEAR(sumXccSe.at(0, 0, 0, 0), 22.0f, kEpsilon); // 1+4+7+10
    EXPECT_NEAR(sumXccSe.at(0, 0, 0, 1), 26.0f, kEpsilon); // 2+5+8+11
    EXPECT_NEAR(sumXccSe.at(0, 0, 0, 2), 30.0f, kEpsilon); // 3+6+9+12
}

TEST_F(ReductionTest, MeanOverTimeAxis)
{
    Tensor meanTime = t.mean(Axis::Time);
    EXPECT_NEAR(meanTime.at(0, 0, 0, 0), 2.0f, kEpsilon);  // (1+2+3)/3
    EXPECT_NEAR(meanTime.at(0, 1, 0, 0), 5.0f, kEpsilon);  // (4+5+6)/3
    EXPECT_NEAR(meanTime.at(1, 0, 0, 0), 8.0f, kEpsilon);  // (7+8+9)/3
    EXPECT_NEAR(meanTime.at(1, 1, 0, 0), 11.0f, kEpsilon); // (10+11+12)/3
}

// ============================================================================
// Test Select
// ============================================================================

class SelectTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Create a 2x2x1x4 tensor with values 1-16
        shape = Shape(2, 2, 1, 4);
        data.resize(16);
        for (int i = 0; i < 16; ++i) data[i] = static_cast<float>(i + 1);
        t = Tensor(shape, data);
        // Layout: xcc=0,se=0: [1,2,3,4], xcc=0,se=1: [5,6,7,8]
        //         xcc=1,se=0: [9,10,11,12], xcc=1,se=1: [13,14,15,16]
    }

    Shape shape;
    std::vector<float> data;
    Tensor t;
};

TEST_F(SelectTest, SelectTimeIndex)
{
    // Select time index 1 (second sample)
    Tensor selected = t.select(1, Axis::Time);
    EXPECT_EQ(selected.shape(), Shape(2, 2, 1, 1));
    EXPECT_NEAR(selected.at(0, 0, 0, 0), 2.0f, kEpsilon);  // xcc=0,se=0,time=1
    EXPECT_NEAR(selected.at(0, 1, 0, 0), 6.0f, kEpsilon);  // xcc=0,se=1,time=1
    EXPECT_NEAR(selected.at(1, 0, 0, 0), 10.0f, kEpsilon); // xcc=1,se=0,time=1
    EXPECT_NEAR(selected.at(1, 1, 0, 0), 14.0f, kEpsilon); // xcc=1,se=1,time=1
}

TEST_F(SelectTest, SelectXCCIndex)
{
    // Select XCC index 1
    Tensor selected = t.select(1, Axis::XCC);
    EXPECT_EQ(selected.shape(), Shape(1, 2, 1, 4));
    EXPECT_NEAR(selected.at(0, 0, 0, 0), 9.0f, kEpsilon);  // xcc=1,se=0,time=0
    EXPECT_NEAR(selected.at(0, 0, 0, 1), 10.0f, kEpsilon); // xcc=1,se=0,time=1
    EXPECT_NEAR(selected.at(0, 1, 0, 0), 13.0f, kEpsilon); // xcc=1,se=1,time=0
    EXPECT_NEAR(selected.at(0, 1, 0, 3), 16.0f, kEpsilon); // xcc=1,se=1,time=3
}

TEST_F(SelectTest, SelectSEIndex)
{
    // Select SE index 0
    Tensor selected = t.select(0, Axis::SE);
    EXPECT_EQ(selected.shape(), Shape(2, 1, 1, 4));
    EXPECT_NEAR(selected.at(0, 0, 0, 0), 1.0f, kEpsilon);  // xcc=0,se=0,time=0
    EXPECT_NEAR(selected.at(0, 0, 0, 3), 4.0f, kEpsilon);  // xcc=0,se=0,time=3
    EXPECT_NEAR(selected.at(1, 0, 0, 0), 9.0f, kEpsilon);  // xcc=1,se=0,time=0
    EXPECT_NEAR(selected.at(1, 0, 0, 3), 12.0f, kEpsilon); // xcc=1,se=0,time=3
}

TEST_F(SelectTest, SelectLastTimeIndex)
{
    // Select the last time index (3)
    Tensor selected = t.select(3, Axis::Time);
    EXPECT_EQ(selected.shape(), Shape(2, 2, 1, 1));
    EXPECT_NEAR(selected.at(0, 0, 0, 0), 4.0f, kEpsilon);
    EXPECT_NEAR(selected.at(0, 1, 0, 0), 8.0f, kEpsilon);
    EXPECT_NEAR(selected.at(1, 0, 0, 0), 12.0f, kEpsilon);
    EXPECT_NEAR(selected.at(1, 1, 0, 0), 16.0f, kEpsilon);
}

TEST_F(SelectTest, SelectNegativeIndex)
{
    // Select time index -1 (last sample, equivalent to index 3)
    Tensor selected = t.select(-1, Axis::Time);
    EXPECT_EQ(selected.shape(), Shape(2, 2, 1, 1));
    EXPECT_NEAR(selected.at(0, 0, 0, 0), 4.0f, kEpsilon);  // xcc=0,se=0,time=3
    EXPECT_NEAR(selected.at(0, 1, 0, 0), 8.0f, kEpsilon);  // xcc=0,se=1,time=3
    EXPECT_NEAR(selected.at(1, 0, 0, 0), 12.0f, kEpsilon); // xcc=1,se=0,time=3
    EXPECT_NEAR(selected.at(1, 1, 0, 0), 16.0f, kEpsilon); // xcc=1,se=1,time=3

    // Select time index -2 (second to last, equivalent to index 2)
    Tensor selected2 = t.select(-2, Axis::Time);
    EXPECT_EQ(selected2.shape(), Shape(2, 2, 1, 1));
    EXPECT_NEAR(selected2.at(0, 0, 0, 0), 3.0f, kEpsilon);  // xcc=0,se=0,time=2
    EXPECT_NEAR(selected2.at(0, 1, 0, 0), 7.0f, kEpsilon);  // xcc=0,se=1,time=2
    EXPECT_NEAR(selected2.at(1, 0, 0, 0), 11.0f, kEpsilon); // xcc=1,se=0,time=2
    EXPECT_NEAR(selected2.at(1, 1, 0, 0), 15.0f, kEpsilon); // xcc=1,se=1,time=2

    // Select XCC index -1 (last XCC, equivalent to index 1)
    Tensor selected3 = t.select(-1, Axis::XCC);
    EXPECT_EQ(selected3.shape(), Shape(1, 2, 1, 4));
    EXPECT_NEAR(selected3.at(0, 0, 0, 0), 9.0f, kEpsilon);  // xcc=1,se=0,time=0
    EXPECT_NEAR(selected3.at(0, 1, 0, 3), 16.0f, kEpsilon); // xcc=1,se=1,time=3
}

TEST_F(SelectTest, SelectNegativeIndexOutOfRange)
{
    // Selecting an out-of-range negative index should throw
    EXPECT_THROW(t.select(-5, Axis::Time), std::runtime_error); // Time has 4 elements
    EXPECT_THROW(t.select(-3, Axis::XCC), std::runtime_error);  // XCC has 2 elements
    EXPECT_THROW(t.select(-3, Axis::SE), std::runtime_error);   // SE has 2 elements
}

TEST_F(SelectTest, SelectOutOfRange)
{
    // Selecting an out-of-range index should throw
    EXPECT_THROW(t.select(4, Axis::Time), std::runtime_error);
    EXPECT_THROW(t.select(2, Axis::XCC), std::runtime_error);
    EXPECT_THROW(t.select(2, Axis::SE), std::runtime_error);
}

// ============================================================================
// Test SelectRange
// ============================================================================

class SelectRangeTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Create a 2x2x1x8 tensor with values 1-32
        shape = Shape(2, 2, 1, 8);
        data.resize(32);
        for (int i = 0; i < 32; ++i) data[i] = static_cast<float>(i + 1);
        t = Tensor(shape, data);
        // Layout: xcc=0,se=0: [1,2,3,4,5,6,7,8], xcc=0,se=1: [9,10,11,12,13,14,15,16]
        //         xcc=1,se=0: [17,18,19,20,21,22,23,24], xcc=1,se=1: [25,26,27,28,29,30,31,32]
    }

    Shape shape;
    std::vector<float> data;
    Tensor t;
};

TEST_F(SelectRangeTest, SelectRangeTimeStep1)
{
    // Select time indices 2-5 with step 1
    Tensor selected = t.selectRange(2, 5, 1, Axis::Time);
    EXPECT_EQ(selected.shape(), Shape(2, 2, 1, 3));
    // xcc=0,se=0: indices 2,3,4 -> values 3,4,5
    EXPECT_NEAR(selected.at(0, 0, 0, 0), 3.0f, kEpsilon);
    EXPECT_NEAR(selected.at(0, 0, 0, 1), 4.0f, kEpsilon);
    EXPECT_NEAR(selected.at(0, 0, 0, 2), 5.0f, kEpsilon);
}

TEST_F(SelectRangeTest, SelectRangeTimeStep2)
{
    // Select time indices 0,2,4,6 (0:8:2)
    Tensor selected = t.selectRange(0, 8, 2, Axis::Time);
    EXPECT_EQ(selected.shape(), Shape(2, 2, 1, 4));
    // xcc=0,se=0: indices 0,2,4,6 -> values 1,3,5,7
    EXPECT_NEAR(selected.at(0, 0, 0, 0), 1.0f, kEpsilon);
    EXPECT_NEAR(selected.at(0, 0, 0, 1), 3.0f, kEpsilon);
    EXPECT_NEAR(selected.at(0, 0, 0, 2), 5.0f, kEpsilon);
    EXPECT_NEAR(selected.at(0, 0, 0, 3), 7.0f, kEpsilon);
}

TEST_F(SelectRangeTest, SelectRangeTimeStep3)
{
    // Select time indices 1,4,7 (1:8:3)
    Tensor selected = t.selectRange(1, 8, 3, Axis::Time);
    EXPECT_EQ(selected.shape(), Shape(2, 2, 1, 3));
    // xcc=0,se=0: indices 1,4,7 -> values 2,5,8
    EXPECT_NEAR(selected.at(0, 0, 0, 0), 2.0f, kEpsilon);
    EXPECT_NEAR(selected.at(0, 0, 0, 1), 5.0f, kEpsilon);
    EXPECT_NEAR(selected.at(0, 0, 0, 2), 8.0f, kEpsilon);
}

TEST_F(SelectRangeTest, SelectRangeXCC)
{
    // Select XCC index 0 only (0:1:1) - should be equivalent to select single
    Tensor selected = t.selectRange(0, 2, 1, Axis::XCC);
    EXPECT_EQ(selected.shape(), Shape(2, 2, 1, 8));
    // All data should be preserved since we selected both XCC indices
    EXPECT_NEAR(selected.at(0, 0, 0, 0), 1.0f, kEpsilon);
    EXPECT_NEAR(selected.at(1, 0, 0, 0), 17.0f, kEpsilon);
}

TEST_F(SelectRangeTest, SelectRangePartialXCC)
{
    // Select only XCC index 1 (1:2:1)
    Tensor selected = t.selectRange(1, 2, 1, Axis::XCC);
    EXPECT_EQ(selected.shape(), Shape(1, 2, 1, 8));
    // Only xcc=1 data remains
    EXPECT_NEAR(selected.at(0, 0, 0, 0), 17.0f, kEpsilon);
    EXPECT_NEAR(selected.at(0, 1, 0, 0), 25.0f, kEpsilon);
}

TEST_F(SelectRangeTest, SelectRangeOutOfRange)
{
    // Start out of range
    EXPECT_THROW(t.selectRange(10, 15, 1, Axis::Time), std::runtime_error);
    // Stop out of range
    EXPECT_THROW(t.selectRange(0, 100, 1, Axis::Time), std::runtime_error);
    // Step of zero
    EXPECT_THROW(t.selectRange(0, 4, 0, Axis::Time), std::runtime_error);
    // Start >= stop
    EXPECT_THROW(t.selectRange(5, 5, 1, Axis::Time), std::runtime_error);
    EXPECT_THROW(t.selectRange(5, 3, 1, Axis::Time), std::runtime_error);
}

// ============================================================================
// Test Remove
// ============================================================================

class RemoveTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Create a 2x2x1x4 tensor with values 1-16
        shape = Shape(2, 2, 1, 4);
        data.resize(16);
        for (int i = 0; i < 16; ++i) data[i] = static_cast<float>(i + 1);
        t = Tensor(shape, data);
        // Layout: xcc=0,se=0: [1,2,3,4], xcc=0,se=1: [5,6,7,8]
        //         xcc=1,se=0: [9,10,11,12], xcc=1,se=1: [13,14,15,16]
    }

    Shape shape;
    std::vector<float> data;
    Tensor t;
};

TEST_F(RemoveTest, RemoveFirstTimeIndex)
{
    // Remove time index 0
    Tensor result = t.remove(0, Axis::Time);
    EXPECT_EQ(result.shape(), Shape(2, 2, 1, 3));
    // xcc=0,se=0 should now be [2,3,4]
    EXPECT_NEAR(result.at(0, 0, 0, 0), 2.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 1), 3.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 2), 4.0f, kEpsilon);
}

TEST_F(RemoveTest, RemoveLastTimeIndexNegative)
{
    // Remove last time index using -1
    Tensor result = t.remove(-1, Axis::Time);
    EXPECT_EQ(result.shape(), Shape(2, 2, 1, 3));
    // xcc=0,se=0 should now be [1,2,3]
    EXPECT_NEAR(result.at(0, 0, 0, 0), 1.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 1), 2.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 2), 3.0f, kEpsilon);
}

TEST_F(RemoveTest, RemoveSecondToLastNegative)
{
    // Remove second-to-last using -2
    Tensor result = t.remove(-2, Axis::Time);
    EXPECT_EQ(result.shape(), Shape(2, 2, 1, 3));
    // xcc=0,se=0 should now be [1,2,4] (removed 3)
    EXPECT_NEAR(result.at(0, 0, 0, 0), 1.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 1), 2.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 2), 4.0f, kEpsilon);
}

TEST_F(RemoveTest, RemoveMiddleTimeIndex)
{
    // Remove time index 1
    Tensor result = t.remove(1, Axis::Time);
    EXPECT_EQ(result.shape(), Shape(2, 2, 1, 3));
    // xcc=0,se=0 should now be [1,3,4]
    EXPECT_NEAR(result.at(0, 0, 0, 0), 1.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 1), 3.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 2), 4.0f, kEpsilon);
}

TEST_F(RemoveTest, RemoveXCCIndex)
{
    // Remove XCC index 0
    Tensor result = t.remove(0, Axis::XCC);
    EXPECT_EQ(result.shape(), Shape(1, 2, 1, 4));
    // Only xcc=1 data remains: se=0: [9,10,11,12], se=1: [13,14,15,16]
    EXPECT_NEAR(result.at(0, 0, 0, 0), 9.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 1, 0, 0), 13.0f, kEpsilon);
}

TEST_F(RemoveTest, RemoveOutOfRange)
{
    EXPECT_THROW(t.remove(4, Axis::Time), std::runtime_error);
    EXPECT_THROW(t.remove(-5, Axis::Time), std::runtime_error);
    EXPECT_THROW(t.remove(2, Axis::XCC), std::runtime_error);
}

TEST_F(RemoveTest, RemoveFromSizeOneAxis)
{
    // CU axis has size 1, cannot remove
    EXPECT_THROW(t.remove(0, Axis::CU), std::runtime_error);
}

// ============================================================================
// Test Delta
// ============================================================================

class DeltaTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Create a 1x1x1x4 tensor with values [1, 3, 6, 10]
        // Deltas should be [2, 3, 4]
        shape = Shape(1, 1, 1, 4);
        data = {1.0f, 3.0f, 6.0f, 10.0f};
        t = Tensor(shape, data);
    }

    Shape shape;
    std::vector<float> data;
    Tensor t;
};

TEST_F(DeltaTest, DeltaAlongTime)
{
    Tensor result = t.delta(Axis::Time);
    EXPECT_EQ(result.shape(), Shape(1, 1, 1, 3));
    EXPECT_NEAR(result.at(0, 0, 0, 0), 2.0f, kEpsilon); // 3 - 1
    EXPECT_NEAR(result.at(0, 0, 0, 1), 3.0f, kEpsilon); // 6 - 3
    EXPECT_NEAR(result.at(0, 0, 0, 2), 4.0f, kEpsilon); // 10 - 6
}

TEST_F(DeltaTest, DeltaAlongXCC)
{
    // Create a 2x1x1x2 tensor
    Shape shape2(2, 1, 1, 2);
    std::vector<float> data2 = {1.0f, 2.0f, 5.0f, 8.0f}; // xcc=0: [1,2], xcc=1: [5,8]
    Tensor t2(shape2, data2);

    Tensor result = t2.delta(Axis::XCC);
    EXPECT_EQ(result.shape(), Shape(1, 1, 1, 2));
    EXPECT_NEAR(result.at(0, 0, 0, 0), 4.0f, kEpsilon); // 5 - 1
    EXPECT_NEAR(result.at(0, 0, 0, 1), 6.0f, kEpsilon); // 8 - 2
}

TEST_F(DeltaTest, DeltaOnSizeOneAxis)
{
    // Cannot compute delta on axis with size 1
    EXPECT_THROW(t.delta(Axis::XCC), std::runtime_error);
    EXPECT_THROW(t.delta(Axis::SE), std::runtime_error);
}

TEST_F(DeltaTest, DeltaEquivalentToRemoveDifference)
{
    // delta[x, axis=TIME] should equal remove[x, 0, axis=TIME] - remove[x, -1, axis=TIME]
    Tensor delta_result = t.delta(Axis::Time);
    Tensor remove_first = t.remove(0, Axis::Time); // [3, 6, 10]
    Tensor remove_last = t.remove(-1, Axis::Time); // [1, 3, 6]
    Tensor diff_result = remove_first - remove_last;

    EXPECT_EQ(delta_result.shape(), diff_result.shape());
    for (size_t i = 0; i < delta_result.size(); ++i) EXPECT_NEAR(delta_result[i], diff_result[i], kEpsilon);
}

// ============================================================================
// Test Parser
// ============================================================================

TEST(ParserTest, SimpleExpressionParsing)
{
    Parser parser;
    ExprPtr expr = parser.parseExpression("42");
    CounterContext ctx;
    Tensor result = expr->evaluate(ctx);
    EXPECT_NEAR(result.scalar(), 42.0f, kEpsilon);
}

TEST(ParserTest, BinaryOperationPrecedence)
{
    Parser parser;
    CounterContext ctx;

    ExprPtr expr2 = parser.parseExpression("10 + 20 * 3");
    EXPECT_NEAR(expr2->evaluate(ctx).scalar(), 70.0f, kEpsilon); // 10 + 60

    ExprPtr expr3 = parser.parseExpression("(10 + 20) * 3");
    EXPECT_NEAR(expr3->evaluate(ctx).scalar(), 90.0f, kEpsilon); // 30 * 3
}

TEST(ParserTest, VariableExpressions)
{
    Parser parser;
    CounterContext ctx;
    ctx.setCounter("x", std::make_shared<Tensor>(5.0f));
    ctx.setCounter("y", std::make_shared<Tensor>(3.0f));

    ExprPtr expr = parser.parseExpression("x + y * 2");
    EXPECT_NEAR(expr->evaluate(ctx).scalar(), 11.0f, kEpsilon); // 5 + 6
}

TEST(ParserTest, CounterNameWithColon)
{
    Parser parser;
    CounterContext ctx;
    ctx.setCounter("mycounter:3", std::make_shared<Tensor>(7.0f));
    ctx.setCounter("other:name:2", std::make_shared<Tensor>(3.0f));

    ExprPtr expr = parser.parseExpression("mycounter:3 + other:name:2");
    EXPECT_NEAR(expr->evaluate(ctx).scalar(), 10.0f, kEpsilon); // 7 + 3
}

TEST(ParserTest, UnaryMinus)
{
    Parser parser;
    CounterContext ctx;
    ctx.setCounter("x", std::make_shared<Tensor>(5.0f));
    ctx.setCounter("y", std::make_shared<Tensor>(3.0f));

    ExprPtr expr = parser.parseExpression("-x + y");
    EXPECT_NEAR(expr->evaluate(ctx).scalar(), -2.0f, kEpsilon); // -5 + 3
}

// ============================================================================
// Test Reduction Parsing
// ============================================================================

class ReductionParsingTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Shape shape(2, 3, 1, 4);
        std::vector<float> data(24);
        for (int i = 0; i < 24; ++i) data[i] = static_cast<float>(i + 1);
        ctx.setCounter("counter_A", std::make_shared<Tensor>(shape, data));
    }

    Parser parser;
    CounterContext ctx;
};

TEST_F(ReductionParsingTest, SumWithoutAxis)
{
    ExprPtr expr = parser.parseExpression("sum[counter_A]");
    Tensor result = expr->evaluate(ctx);
    EXPECT_TRUE(result.isScalar());
    EXPECT_NEAR(result.scalar(), 300.0f, kEpsilon); // 1+2+...+24 = 300
}

TEST_F(ReductionParsingTest, SumWithSingleAxis)
{
    ExprPtr expr = parser.parseExpression("sum[counter_A, axis=Time]");
    Tensor result = expr->evaluate(ctx);
    EXPECT_EQ(result.shape(), Shape(2, 3, 1, 1));
}

TEST_F(ReductionParsingTest, SumWithMultipleAxes)
{
    ExprPtr expr = parser.parseExpression("sum[counter_A, axis=[XCC, SE]]");
    Tensor result = expr->evaluate(ctx);
    EXPECT_EQ(result.shape(), Shape(1, 1, 1, 4));
}

TEST_F(ReductionParsingTest, Mean)
{
    ExprPtr expr = parser.parseExpression("mean[counter_A]");
    Tensor result = expr->evaluate(ctx);
    EXPECT_NEAR(result.scalar(), 12.5f, kEpsilon); // 300/24
}

TEST_F(ReductionParsingTest, Max)
{
    ExprPtr expr = parser.parseExpression("max[counter_A]");
    Tensor result = expr->evaluate(ctx);
    EXPECT_NEAR(result.scalar(), 24.0f, kEpsilon);
}

TEST_F(ReductionParsingTest, Min)
{
    ExprPtr expr = parser.parseExpression("min[counter_A]");
    Tensor result = expr->evaluate(ctx);
    EXPECT_NEAR(result.scalar(), 1.0f, kEpsilon);
}

TEST_F(ReductionParsingTest, AxisByNumber)
{
    ExprPtr expr = parser.parseExpression("sum[counter_A, axis=3]"); // 3 = Time
    Tensor result = expr->evaluate(ctx);
    EXPECT_EQ(result.shape(), Shape(2, 3, 1, 1));
}

// ============================================================================
// Test Select Parsing
// ============================================================================

class SelectParsingTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // counter_A: 2x2x1x4 tensor with values 1-16
        Shape shapeA(2, 2, 1, 4);
        std::vector<float> dataA(16);
        for (int i = 0; i < 16; ++i) dataA[i] = static_cast<float>(i + 1);
        ctx.setCounter("counter_A", std::make_shared<Tensor>(shapeA, dataA));
    }

    Parser parser;
    CounterContext ctx;
};

TEST_F(SelectParsingTest, SelectWithTimeAxis)
{
    ExprPtr expr = parser.parseExpression("select[counter_A, 1, axis=TIME]");
    Tensor result = expr->evaluate(ctx);
    EXPECT_EQ(result.shape(), Shape(2, 2, 1, 1));
    EXPECT_NEAR(result.at(0, 0, 0, 0), 2.0f, kEpsilon); // time index 1 from [1,2,3,4]
    EXPECT_NEAR(result.at(0, 1, 0, 0), 6.0f, kEpsilon); // time index 1 from [5,6,7,8]
}

TEST_F(SelectParsingTest, SelectWithXCCAxis)
{
    ExprPtr expr = parser.parseExpression("select[counter_A, 0, axis=XCC]");
    Tensor result = expr->evaluate(ctx);
    EXPECT_EQ(result.shape(), Shape(1, 2, 1, 4));
    EXPECT_NEAR(result.at(0, 0, 0, 0), 1.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 1, 0, 0), 5.0f, kEpsilon);
}

TEST_F(SelectParsingTest, SelectWithNumericAxis)
{
    ExprPtr expr = parser.parseExpression("select[counter_A, 2, axis=3]"); // 3 = Time
    Tensor result = expr->evaluate(ctx);
    EXPECT_EQ(result.shape(), Shape(2, 2, 1, 1));
    EXPECT_NEAR(result.at(0, 0, 0, 0), 3.0f, kEpsilon); // time index 2 from [1,2,3,4]
}

TEST_F(SelectParsingTest, SelectDefaultAxis)
{
    // Default axis is TIME if not specified
    ExprPtr expr = parser.parseExpression("select[counter_A, 0]");
    Tensor result = expr->evaluate(ctx);
    EXPECT_EQ(result.shape(), Shape(2, 2, 1, 1));
    EXPECT_NEAR(result.at(0, 0, 0, 0), 1.0f, kEpsilon); // time index 0
}

TEST_F(SelectParsingTest, SelectInExpression)
{
    // Select can be used in arithmetic expressions
    ExprPtr expr = parser.parseExpression("select[counter_A, 0, axis=TIME] * 2");
    Tensor result = expr->evaluate(ctx);
    EXPECT_NEAR(result.at(0, 0, 0, 0), 2.0f, kEpsilon);  // 1 * 2
    EXPECT_NEAR(result.at(0, 1, 0, 0), 10.0f, kEpsilon); // 5 * 2
}

TEST_F(SelectParsingTest, SelectWithLowercaseAxis)
{
    ExprPtr expr = parser.parseExpression("select[counter_A, 1, axis=time]");
    Tensor result = expr->evaluate(ctx);
    EXPECT_EQ(result.shape(), Shape(2, 2, 1, 1));
    EXPECT_NEAR(result.at(0, 0, 0, 0), 2.0f, kEpsilon);
}

TEST_F(SelectParsingTest, SelectThenSum)
{
    // Select a time slice, then sum over XCC
    ExprPtr expr = parser.parseExpression("sum[select[counter_A, 0, axis=TIME], axis=XCC]");
    Tensor result = expr->evaluate(ctx);
    EXPECT_EQ(result.shape(), Shape(1, 2, 1, 1));
    EXPECT_NEAR(result.at(0, 0, 0, 0), 10.0f, kEpsilon); // 1 + 9 (xcc=0 and xcc=1 at time=0, se=0)
    EXPECT_NEAR(result.at(0, 1, 0, 0), 18.0f, kEpsilon); // 5 + 13 (xcc=0 and xcc=1 at time=0, se=1)
}

TEST_F(SelectParsingTest, SelectNegativeIndex)
{
    // Select last time index using -1
    ExprPtr expr = parser.parseExpression("select[counter_A, -1, axis=TIME]");
    Tensor result = expr->evaluate(ctx);
    EXPECT_EQ(result.shape(), Shape(2, 2, 1, 1));
    EXPECT_NEAR(result.at(0, 0, 0, 0), 4.0f, kEpsilon);  // last element of [1,2,3,4]
    EXPECT_NEAR(result.at(0, 1, 0, 0), 8.0f, kEpsilon);  // last element of [5,6,7,8]
    EXPECT_NEAR(result.at(1, 0, 0, 0), 12.0f, kEpsilon); // last element of [9,10,11,12]
    EXPECT_NEAR(result.at(1, 1, 0, 0), 16.0f, kEpsilon); // last element of [13,14,15,16]

    // Select second to last time index using -2
    ExprPtr expr2 = parser.parseExpression("select[counter_A, -2, axis=TIME]");
    Tensor result2 = expr2->evaluate(ctx);
    EXPECT_EQ(result2.shape(), Shape(2, 2, 1, 1));
    EXPECT_NEAR(result2.at(0, 0, 0, 0), 3.0f, kEpsilon);  // index 2 of [1,2,3,4]
    EXPECT_NEAR(result2.at(0, 1, 0, 0), 7.0f, kEpsilon);  // index 2 of [5,6,7,8]
    EXPECT_NEAR(result2.at(1, 0, 0, 0), 11.0f, kEpsilon); // index 2 of [9,10,11,12]
    EXPECT_NEAR(result2.at(1, 1, 0, 0), 15.0f, kEpsilon); // index 2 of [13,14,15,16]

    // Select last XCC using -1
    ExprPtr expr3 = parser.parseExpression("select[counter_A, -1, axis=XCC]");
    Tensor result3 = expr3->evaluate(ctx);
    EXPECT_EQ(result3.shape(), Shape(1, 2, 1, 4));
    EXPECT_NEAR(result3.at(0, 0, 0, 0), 9.0f, kEpsilon);  // xcc=1,se=0,time=0
    EXPECT_NEAR(result3.at(0, 1, 0, 3), 16.0f, kEpsilon); // xcc=1,se=1,time=3
}

// ============================================================================
// Test Select Range Parsing
// ============================================================================

class SelectRangeParsingTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // counter_A: 2x2x1x8 tensor with values 1-32
        Shape shapeA(2, 2, 1, 8);
        std::vector<float> dataA(32);
        for (int i = 0; i < 32; ++i) dataA[i] = static_cast<float>(i + 1);
        ctx.setCounter("counter_A", std::make_shared<Tensor>(shapeA, dataA));
        // Layout: xcc=0,se=0: [1,2,3,4,5,6,7,8], xcc=0,se=1: [9,10,11,12,13,14,15,16]
        //         xcc=1,se=0: [17,18,19,20,21,22,23,24], xcc=1,se=1: [25,26,27,28,29,30,31,32]
    }

    Parser parser;
    CounterContext ctx;
};

TEST_F(SelectRangeParsingTest, SelectRangeWithStep1)
{
    // select[counter_A, 2:5, axis=TIME] -> indices 2,3,4
    ExprPtr expr = parser.parseExpression("select[counter_A, 2:5, axis=TIME]");
    Tensor result = expr->evaluate(ctx);
    EXPECT_EQ(result.shape(), Shape(2, 2, 1, 3));
    // xcc=0,se=0: indices 2,3,4 -> values 3,4,5
    EXPECT_NEAR(result.at(0, 0, 0, 0), 3.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 1), 4.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 2), 5.0f, kEpsilon);
}

TEST_F(SelectRangeParsingTest, SelectRangeWithStep2)
{
    // select[counter_A, 0:8:2, axis=TIME] -> indices 0,2,4,6
    ExprPtr expr = parser.parseExpression("select[counter_A, 0:8:2, axis=TIME]");
    Tensor result = expr->evaluate(ctx);
    EXPECT_EQ(result.shape(), Shape(2, 2, 1, 4));
    // xcc=0,se=0: indices 0,2,4,6 -> values 1,3,5,7
    EXPECT_NEAR(result.at(0, 0, 0, 0), 1.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 1), 3.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 2), 5.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 3), 7.0f, kEpsilon);
}

TEST_F(SelectRangeParsingTest, SelectRangeWithStep3)
{
    // select[counter_A, 1:8:3, axis=TIME] -> indices 1,4,7
    ExprPtr expr = parser.parseExpression("select[counter_A, 1:8:3, axis=TIME]");
    Tensor result = expr->evaluate(ctx);
    EXPECT_EQ(result.shape(), Shape(2, 2, 1, 3));
    // xcc=0,se=0: indices 1,4,7 -> values 2,5,8
    EXPECT_NEAR(result.at(0, 0, 0, 0), 2.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 1), 5.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 2), 8.0f, kEpsilon);
}

TEST_F(SelectRangeParsingTest, SelectRangeDefaultAxis)
{
    // Default axis is TIME
    ExprPtr expr = parser.parseExpression("select[counter_A, 0:4]");
    Tensor result = expr->evaluate(ctx);
    EXPECT_EQ(result.shape(), Shape(2, 2, 1, 4));
    EXPECT_NEAR(result.at(0, 0, 0, 0), 1.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 3), 4.0f, kEpsilon);
}

TEST_F(SelectRangeParsingTest, SelectRangeWithXCCAxis)
{
    // select[counter_A, 0:2, axis=XCC] -> all XCC indices
    ExprPtr expr = parser.parseExpression("select[counter_A, 0:2, axis=XCC]");
    Tensor result = expr->evaluate(ctx);
    EXPECT_EQ(result.shape(), Shape(2, 2, 1, 8));
}

TEST_F(SelectRangeParsingTest, SelectRangeInExpression)
{
    // Select range can be used in arithmetic expressions
    ExprPtr expr = parser.parseExpression("sum[select[counter_A, 0:4, axis=TIME]]");
    Tensor result = expr->evaluate(ctx);
    // Sum of first 4 time indices for each xcc,se combination
    // xcc=0,se=0: 1+2+3+4=10, xcc=0,se=1: 9+10+11+12=42
    // xcc=1,se=0: 17+18+19+20=74, xcc=1,se=1: 25+26+27+28=106
    // Total = 10+42+74+106 = 232
    EXPECT_TRUE(result.isScalar());
    EXPECT_NEAR(result.scalar(), 232.0f, kEpsilon);
}

TEST_F(SelectRangeParsingTest, SelectRangeEvenOddPattern)
{
    // Get even indices: 0,2,4,6
    ExprPtr evenExpr = parser.parseExpression("select[counter_A, 0:8:2, axis=TIME]");
    Tensor evenResult = evenExpr->evaluate(ctx);
    EXPECT_EQ(evenResult.shape(), Shape(2, 2, 1, 4));

    // Get odd indices: 1,3,5,7
    ExprPtr oddExpr = parser.parseExpression("select[counter_A, 1:8:2, axis=TIME]");
    Tensor oddResult = oddExpr->evaluate(ctx);
    EXPECT_EQ(oddResult.shape(), Shape(2, 2, 1, 4));

    // xcc=0,se=0: even=[1,3,5,7], odd=[2,4,6,8]
    EXPECT_NEAR(evenResult.at(0, 0, 0, 0), 1.0f, kEpsilon);
    EXPECT_NEAR(evenResult.at(0, 0, 0, 1), 3.0f, kEpsilon);
    EXPECT_NEAR(oddResult.at(0, 0, 0, 0), 2.0f, kEpsilon);
    EXPECT_NEAR(oddResult.at(0, 0, 0, 1), 4.0f, kEpsilon);
}

// ============================================================================
// Test Remove Parsing
// ============================================================================

class RemoveParsingTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // counter_A: 2x2x1x4 tensor with values 1-16
        Shape shapeA(2, 2, 1, 4);
        std::vector<float> dataA(16);
        for (int i = 0; i < 16; ++i) dataA[i] = static_cast<float>(i + 1);
        ctx.setCounter("counter_A", std::make_shared<Tensor>(shapeA, dataA));
    }

    Parser parser;
    CounterContext ctx;
};

TEST_F(RemoveParsingTest, RemoveWithTimeAxis)
{
    ExprPtr expr = parser.parseExpression("remove[counter_A, 0, axis=TIME]");
    Tensor result = expr->evaluate(ctx);
    EXPECT_EQ(result.shape(), Shape(2, 2, 1, 3));
    EXPECT_NEAR(result.at(0, 0, 0, 0), 2.0f, kEpsilon); // removed 1, now starts at 2
}

TEST_F(RemoveParsingTest, RemoveLastWithNegativeIndex)
{
    ExprPtr expr = parser.parseExpression("remove[counter_A, -1, axis=TIME]");
    Tensor result = expr->evaluate(ctx);
    EXPECT_EQ(result.shape(), Shape(2, 2, 1, 3));
    EXPECT_NEAR(result.at(0, 0, 0, 2), 3.0f, kEpsilon); // last is now 3 (removed 4)
}

TEST_F(RemoveParsingTest, RemoveSecondToLast)
{
    ExprPtr expr = parser.parseExpression("remove[counter_A, -2, axis=TIME]");
    Tensor result = expr->evaluate(ctx);
    EXPECT_EQ(result.shape(), Shape(2, 2, 1, 3));
    // [1,2,3,4] -> [1,2,4] (removed 3)
    EXPECT_NEAR(result.at(0, 0, 0, 0), 1.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 1), 2.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 2), 4.0f, kEpsilon);
}

TEST_F(RemoveParsingTest, RemoveDefaultAxis)
{
    // Default axis is TIME if not specified
    ExprPtr expr = parser.parseExpression("remove[counter_A, 0]");
    Tensor result = expr->evaluate(ctx);
    EXPECT_EQ(result.shape(), Shape(2, 2, 1, 3));
}

TEST_F(RemoveParsingTest, RemoveWithXCCAxis)
{
    ExprPtr expr = parser.parseExpression("remove[counter_A, 1, axis=XCC]");
    Tensor result = expr->evaluate(ctx);
    EXPECT_EQ(result.shape(), Shape(1, 2, 1, 4));
    EXPECT_NEAR(result.at(0, 0, 0, 0), 1.0f, kEpsilon); // only xcc=0 remains
}

// ============================================================================
// Test Delta Parsing
// ============================================================================

class DeltaParsingTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // counter_A: 1x1x1x4 tensor with values [1, 3, 6, 10]
        Shape shapeA(1, 1, 1, 4);
        std::vector<float> dataA = {1.0f, 3.0f, 6.0f, 10.0f};
        ctx.setCounter("counter_A", std::make_shared<Tensor>(shapeA, dataA));
    }

    Parser parser;
    CounterContext ctx;
};

TEST_F(DeltaParsingTest, DeltaWithTimeAxis)
{
    ExprPtr expr = parser.parseExpression("delta[counter_A, axis=TIME]");
    Tensor result = expr->evaluate(ctx);
    EXPECT_EQ(result.shape(), Shape(1, 1, 1, 3));
    EXPECT_NEAR(result.at(0, 0, 0, 0), 2.0f, kEpsilon); // 3 - 1
    EXPECT_NEAR(result.at(0, 0, 0, 1), 3.0f, kEpsilon); // 6 - 3
    EXPECT_NEAR(result.at(0, 0, 0, 2), 4.0f, kEpsilon); // 10 - 6
}

TEST_F(DeltaParsingTest, DeltaDefaultAxis)
{
    // Default axis is TIME if not specified
    ExprPtr expr = parser.parseExpression("delta[counter_A]");
    Tensor result = expr->evaluate(ctx);
    EXPECT_EQ(result.shape(), Shape(1, 1, 1, 3));
    EXPECT_NEAR(result.at(0, 0, 0, 0), 2.0f, kEpsilon);
}

TEST_F(DeltaParsingTest, DeltaInExpression)
{
    // Delta can be used in arithmetic expressions
    ExprPtr expr = parser.parseExpression("delta[counter_A] * 2");
    Tensor result = expr->evaluate(ctx);
    EXPECT_NEAR(result.at(0, 0, 0, 0), 4.0f, kEpsilon); // 2 * 2
    EXPECT_NEAR(result.at(0, 0, 0, 1), 6.0f, kEpsilon); // 3 * 2
}

TEST_F(DeltaParsingTest, SumOfDelta)
{
    // Sum of deltas = last - first
    ExprPtr expr = parser.parseExpression("sum[delta[counter_A]]");
    Tensor result = expr->evaluate(ctx);
    EXPECT_TRUE(result.isScalar());
    EXPECT_NEAR(result.scalar(), 9.0f, kEpsilon); // 2+3+4 = 9, also 10-1 = 9
}

// ============================================================================
// Test Definition Parsing
// ============================================================================

TEST(DefinitionParsingTest, SingleDefinition)
{
    Parser parser;
    auto def = parser.parseDefinition("mydef := 10 + 20");
    EXPECT_EQ(def.name, "mydef");
    CounterContext ctx;
    EXPECT_NEAR(def.expression->evaluate(ctx).scalar(), 30.0f, kEpsilon);
}

TEST(DefinitionParsingTest, DefinitionWithReduction)
{
    Parser parser;
    auto def = parser.parseDefinition("summed := sum[x, axis=Time]");
    EXPECT_EQ(def.name, "summed");
}

TEST(DefinitionParsingTest, MultipleDefinitionsFromFile)
{
    Parser parser;
    std::string content = R"(
# This is a comment
mydef1 := sum[counter_A, axis=[XCC,SE]]
mydef2 := sum[counter_B, axis=[XCC,SE]]

# Another comment
myratio := 25 * mydef1 / mydef2 + RCLOCK
)";

    auto defs = parser.parseFile(content);
    EXPECT_EQ(defs.size(), 3);
    EXPECT_EQ(defs[0].name, "mydef1");
    EXPECT_EQ(defs[1].name, "mydef2");
    EXPECT_EQ(defs[2].name, "myratio");
}

// ============================================================================
// Test Full Integration
// ============================================================================

TEST(IntegrationTest, FullWorkflow)
{
    DerivedCounterManager manager;

    Shape fullShape(2, 4, 8, 100);
    std::vector<float> dataA(fullShape.totalSize());
    std::vector<float> dataB(fullShape.totalSize());

    for (size_t i = 0; i < dataA.size(); ++i)
    {
        dataA[i] = static_cast<float>(i % 100);
        dataB[i] = static_cast<float>((i % 50) + 1);
    }

    manager.context().setCounter("counter_A", std::make_shared<Tensor>(fullShape, dataA));
    manager.context().setCounter("counter_B", std::make_shared<Tensor>(fullShape, dataB));

    std::vector<float> sclock(100), rclock(100);
    for (int i = 0; i < 100; ++i)
    {
        sclock[i] = i * 1000.0f;
        rclock[i] = i * 0.001f;
    }
    Shape clockShape(1, 1, 1, 100);
    manager.context().setCounter("SCLOCK", std::make_shared<Tensor>(clockShape, sclock));
    manager.context().setCounter("RCLOCK", std::make_shared<Tensor>(clockShape, rclock));

    std::string definitions = R"(
mydef1 := sum[counter_A, axis=[XCC,SE]]
mydef2 := sum[counter_B, axis=[XCC,SE]]
myratio := 25 * mydef1 / mydef2 + RCLOCK
)";

    manager.loadDefinitions(definitions);

    auto names = manager.derivedCounterNames();
    EXPECT_EQ(names.size(), 3);

    auto mydef1 = manager.evaluate("mydef1");
    EXPECT_EQ(mydef1->shape(), Shape(1, 1, 8, 100));

    auto mydef2 = manager.evaluate("mydef2");
    EXPECT_EQ(mydef2->shape(), Shape(1, 1, 8, 100));

    auto myratio = manager.evaluate("myratio");
    EXPECT_EQ(myratio->shape(), Shape(1, 1, 8, 100));

    auto numXcc = manager.context().getCounter("NUM_XCC");
    EXPECT_NEAR(numXcc->scalar(), 2.0f, kEpsilon);

    auto numSamples = manager.context().getCounter("NUM_SAMPLES");
    EXPECT_NEAR(numSamples->scalar(), 100.0f, kEpsilon);
}

TEST(IntegrationTest, SelectInDefinitions)
{
    DerivedCounterManager manager;

    // Create a 2x2x1x4 tensor
    Shape shape(2, 2, 1, 4);
    std::vector<float> data(16);
    for (int i = 0; i < 16; ++i) data[i] = static_cast<float>(i + 1);
    manager.context().setCounter("counter_A", std::make_shared<Tensor>(shape, data));

    std::string definitions = R"(
# Select time index 0 across all XCC and SE
time_slice_0 := select[counter_A, 0, axis=TIME]

# Select XCC index 1
xcc_slice_1 := select[counter_A, 1, axis=XCC]

# Combine select with sum
sum_at_time_1 := sum[select[counter_A, 1, axis=TIME], axis=[XCC, SE]]
)";

    manager.loadDefinitions(definitions);

    auto names = manager.derivedCounterNames();
    EXPECT_EQ(names.size(), 3);

    // time_slice_0 should be shape (2,2,1,1) with values 1, 5, 9, 13
    auto slice0 = manager.evaluate("time_slice_0");
    EXPECT_EQ(slice0->shape(), Shape(2, 2, 1, 1));
    EXPECT_NEAR(slice0->at(0, 0, 0, 0), 1.0f, kEpsilon);
    EXPECT_NEAR(slice0->at(0, 1, 0, 0), 5.0f, kEpsilon);
    EXPECT_NEAR(slice0->at(1, 0, 0, 0), 9.0f, kEpsilon);
    EXPECT_NEAR(slice0->at(1, 1, 0, 0), 13.0f, kEpsilon);

    // xcc_slice_1 should be shape (1,2,1,4) with values 9-16
    auto xccSlice = manager.evaluate("xcc_slice_1");
    EXPECT_EQ(xccSlice->shape(), Shape(1, 2, 1, 4));
    EXPECT_NEAR(xccSlice->at(0, 0, 0, 0), 9.0f, kEpsilon);
    EXPECT_NEAR(xccSlice->at(0, 1, 0, 0), 13.0f, kEpsilon);

    // sum_at_time_1 should sum values at time index 1: 2+6+10+14 = 32
    auto sumAt1 = manager.evaluate("sum_at_time_1");
    EXPECT_TRUE(sumAt1->isScalar());
    EXPECT_NEAR(sumAt1->scalar(), 32.0f, kEpsilon);
}

// ============================================================================
// Test Normalization Example
// ============================================================================

TEST(NormalizationTest, GlobalNormalization)
{
    DerivedCounterManager manager;

    Shape shape(2, 2, 1, 4);
    std::vector<float> data(16);
    for (int i = 0; i < 16; ++i)
    {
        data[i] = (i + 1) * 10.0f; // 10, 20, 30, ..., 160
    }

    manager.context().setCounter("counter", std::make_shared<Tensor>(shape, data));

    manager.loadDefinitions("normalized := counter / max[counter]");

    auto normalized = manager.evaluate("normalized");
    EXPECT_EQ(normalized->shape(), shape);

    for (int i = 0; i < 16; ++i)
    {
        float expected = (i + 1) * 10.0f / 160.0f;
        EXPECT_NEAR((*normalized)[i], expected, kEpsilon);
    }
}

TEST(NormalizationTest, PerTimeStepNormalization)
{
    DerivedCounterManager manager;

    Shape shape(2, 2, 1, 4);
    std::vector<float> data(16);
    for (int i = 0; i < 16; ++i) { data[i] = (i + 1) * 10.0f; }

    manager.context().setCounter("counter", std::make_shared<Tensor>(shape, data));

    manager.loadDefinitions("normalized_time := counter / max[counter, axis=Time]");

    auto normTime = manager.evaluate("normalized_time");
    EXPECT_EQ(normTime->shape(), shape);

    // xcc=0,se=0: values 10,20,30,40 -> max=40 -> 0.25,0.5,0.75,1.0
    EXPECT_NEAR(normTime->at(0, 0, 0, 0), 10.0f / 40.0f, kEpsilon);
    EXPECT_NEAR(normTime->at(0, 0, 0, 1), 20.0f / 40.0f, kEpsilon);
    EXPECT_NEAR(normTime->at(0, 0, 0, 2), 30.0f / 40.0f, kEpsilon);
    EXPECT_NEAR(normTime->at(0, 0, 0, 3), 40.0f / 40.0f, kEpsilon);
}

// ============================================================================
// Test Complex Expressions
// ============================================================================

TEST(ComplexExpressionsTest, DerivedCounterChaining)
{
    constexpr size_t NSAMPLES = 17;
    DerivedCounterManager manager;

    Shape shape(2, 3, 2, NSAMPLES);
    std::vector<float> dataA(shape.totalSize()), dataB(shape.totalSize());
    for (size_t i = 0; i < dataA.size(); ++i)
    {
        dataA[i] = static_cast<float>(i + 1);
        dataB[i] = static_cast<float>((i + 1) * 2);
    }

    manager.context().setCounter("A", std::make_shared<Tensor>(shape, dataA));
    manager.context().setCounter("B", std::make_shared<Tensor>(shape, dataB));

    std::vector<float> rclock(NSAMPLES);
    for (int i = 0; i < NSAMPLES; ++i) rclock[i] = i * 0.1f;

    Shape clockShape(1, 1, 1, NSAMPLES);
    manager.context().setCounter("RCLOCK", std::make_shared<Tensor>(clockShape, rclock));
    manager.loadDefinitions(R"(
spatial_sum_A := sum[A, axis=[XCC, SE, CU]]
spatial_sum_B := sum[B, axis=[XCC, SE, CU]]

ratio := spatial_sum_A / spatial_sum_B
weighted_ratio := ratio * RCLOCK

scaled := 100 * mean[A, axis=Time] + 50
)");

    auto spatialA = manager.evaluate("spatial_sum_A");
    EXPECT_EQ(spatialA->shape(), Shape(1, 1, 1, NSAMPLES));

    auto spatialB = manager.evaluate("spatial_sum_B");
    EXPECT_EQ(spatialB->shape(), Shape(1, 1, 1, NSAMPLES));

    auto ratio = manager.evaluate("ratio");
    // A/B should be 0.5 since B = 2*A
    for (size_t i = 0; i < ratio->size(); ++i) EXPECT_NEAR((*ratio)[i], 0.5f, kEpsilon);

    auto weightedRatio = manager.evaluate("weighted_ratio");
    EXPECT_EQ(weightedRatio->shape(), Shape(1, 1, 1, NSAMPLES));

    auto scaled = manager.evaluate("scaled");
    EXPECT_EQ(scaled->shape(), Shape(2, 3, 2, 1));
}

// ============================================================================
// Test Edge Cases
// ============================================================================

TEST(EdgeCasesTest, NestedParentheses)
{
    Parser parser;
    ExprPtr expr = parser.parseExpression("((10 + 5) * (3 - 1))");
    CounterContext ctx;
    EXPECT_NEAR(expr->evaluate(ctx).scalar(), 30.0f, kEpsilon);
}

TEST(EdgeCasesTest, MultipleUnaryMinuses)
{
    Parser parser;
    ExprPtr expr = parser.parseExpression("--5");
    CounterContext ctx;
    EXPECT_NEAR(expr->evaluate(ctx).scalar(), 5.0f, kEpsilon);
}

TEST(EdgeCasesTest, DivisionByScalarTensor)
{
    Parser parser;
    CounterContext ctx;
    ctx.setCounter("x", std::make_shared<Tensor>(10.0f));
    ExprPtr expr = parser.parseExpression("x / 2");
    EXPECT_NEAR(expr->evaluate(ctx).scalar(), 5.0f, kEpsilon);
}

TEST(EdgeCasesTest, ScientificNotation)
{
    Parser parser;
    ExprPtr expr = parser.parseExpression("1e3 + 2.5e-1");
    CounterContext ctx;
    EXPECT_NEAR(expr->evaluate(ctx).scalar(), 1000.25f, kEpsilon);
}

// ============================================================================
// Test Linear Function
// ============================================================================

TEST(LinearTest, BasicLinearTimeAxis)
{
    // linear(6, axis=Time) creates {0, 1, 2, 3, 4, 5}
    Tensor t = linear(6, Axis::Time);
    EXPECT_EQ(t.shape(), Shape(1, 1, 1, 6));
    for (size_t i = 0; i < 6; ++i) EXPECT_NEAR(t.at(0, 0, 0, i), static_cast<float>(i), kEpsilon);
}

TEST(LinearTest, LinearXCCAxis)
{
    Tensor t = linear(4, Axis::XCC);
    EXPECT_EQ(t.shape(), Shape(4, 1, 1, 1));
    for (size_t i = 0; i < 4; ++i) EXPECT_NEAR(t.at(i, 0, 0, 0), static_cast<float>(i), kEpsilon);
}

TEST(LinearTest, LinearSEAxis)
{
    Tensor t = linear(3, Axis::SE);
    EXPECT_EQ(t.shape(), Shape(1, 3, 1, 1));
    for (size_t i = 0; i < 3; ++i) EXPECT_NEAR(t.at(0, i, 0, 0), static_cast<float>(i), kEpsilon);
}

TEST(LinearTest, LinearCUAxis)
{
    Tensor t = linear(8, Axis::CU);
    EXPECT_EQ(t.shape(), Shape(1, 1, 8, 1));
    for (size_t i = 0; i < 8; ++i) EXPECT_NEAR(t.at(0, 0, i, 0), static_cast<float>(i), kEpsilon);
}

TEST(LinearTest, ParseLinearExpression)
{
    Parser parser;
    CounterContext ctx;
    ExprPtr expr = parser.parseExpression("linear(5, axis=Time)");
    Tensor t = expr->evaluate(ctx);

    EXPECT_EQ(t.shape(), Shape(1, 1, 1, 5));
    for (size_t i = 0; i < 5; ++i) EXPECT_NEAR(t.at(0, 0, 0, i), static_cast<float>(i), kEpsilon);
}

TEST(LinearTest, ParseLinearWithXCC)
{
    Parser parser;
    CounterContext ctx;
    ExprPtr expr = parser.parseExpression("linear(3, axis=XCC)");
    Tensor t = expr->evaluate(ctx);

    EXPECT_EQ(t.shape(), Shape(3, 1, 1, 1));
    for (size_t i = 0; i < 3; ++i) EXPECT_NEAR(t.at(i, 0, 0, 0), static_cast<float>(i), kEpsilon);
}

TEST(LinearTest, LinearInDerivedCounter)
{
    DerivedCounterManager manager;
    manager.loadDefinitions("indices := linear(10, axis=Time)");

    auto indices = manager.evaluate("indices");
    EXPECT_EQ(indices->shape(), Shape(1, 1, 1, 10));
    for (size_t i = 0; i < 10; ++i) EXPECT_NEAR(indices->at(0, 0, 0, i), static_cast<float>(i), kEpsilon);
}

TEST(LinearTest, LinearWithArithmetic)
{
    Parser parser;
    CounterContext ctx;
    ExprPtr expr = parser.parseExpression("linear(4, axis=Time) * 2 + 1");
    Tensor t = expr->evaluate(ctx);

    EXPECT_EQ(t.shape(), Shape(1, 1, 1, 4));
    // Should be {1, 3, 5, 7}
    EXPECT_NEAR(t.at(0, 0, 0, 0), 1.0f, kEpsilon);
    EXPECT_NEAR(t.at(0, 0, 0, 1), 3.0f, kEpsilon);
    EXPECT_NEAR(t.at(0, 0, 0, 2), 5.0f, kEpsilon);
    EXPECT_NEAR(t.at(0, 0, 0, 3), 7.0f, kEpsilon);
}

// ============================================================================
// Test Comparison Operators
// ============================================================================

TEST(ComparisonTest, EqualityTensor)
{
    Tensor a(Shape(1, 1, 1, 4), {1.0f, 2.0f, 3.0f, 4.0f});
    Tensor b(Shape(1, 1, 1, 4), {1.0f, 5.0f, 3.0f, 6.0f});

    Tensor result = a == b;
    EXPECT_EQ(result.shape(), Shape(1, 1, 1, 4));
    EXPECT_NEAR(result.at(0, 0, 0, 0), 1.0f, kEpsilon); // 1 == 1
    EXPECT_NEAR(result.at(0, 0, 0, 1), 0.0f, kEpsilon); // 2 != 5
    EXPECT_NEAR(result.at(0, 0, 0, 2), 1.0f, kEpsilon); // 3 == 3
    EXPECT_NEAR(result.at(0, 0, 0, 3), 0.0f, kEpsilon); // 4 != 6
}

TEST(ComparisonTest, EqualityScalar)
{
    Tensor a(Shape(1, 1, 1, 4), {1.0f, 2.0f, 2.0f, 4.0f});
    Tensor result = a == 2.0f;
    EXPECT_NEAR(result.at(0, 0, 0, 0), 0.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 1), 1.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 2), 1.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 3), 0.0f, kEpsilon);
}

TEST(ComparisonTest, LessThan)
{
    Tensor a(Shape(1, 1, 1, 4), {1.0f, 2.0f, 3.0f, 4.0f});
    Tensor result = a < 3.0f;
    EXPECT_NEAR(result.at(0, 0, 0, 0), 1.0f, kEpsilon); // 1 < 3
    EXPECT_NEAR(result.at(0, 0, 0, 1), 1.0f, kEpsilon); // 2 < 3
    EXPECT_NEAR(result.at(0, 0, 0, 2), 0.0f, kEpsilon); // 3 < 3 = false
    EXPECT_NEAR(result.at(0, 0, 0, 3), 0.0f, kEpsilon); // 4 < 3 = false
}

TEST(ComparisonTest, GreaterThan)
{
    Tensor a(Shape(1, 1, 1, 4), {1.0f, 2.0f, 3.0f, 4.0f});
    Tensor result = a > 2.0f;
    EXPECT_NEAR(result.at(0, 0, 0, 0), 0.0f, kEpsilon); // 1 > 2 = false
    EXPECT_NEAR(result.at(0, 0, 0, 1), 0.0f, kEpsilon); // 2 > 2 = false
    EXPECT_NEAR(result.at(0, 0, 0, 2), 1.0f, kEpsilon); // 3 > 2
    EXPECT_NEAR(result.at(0, 0, 0, 3), 1.0f, kEpsilon); // 4 > 2
}

TEST(ComparisonTest, LessOrEqual)
{
    Tensor a(Shape(1, 1, 1, 4), {1.0f, 2.0f, 3.0f, 4.0f});
    Tensor result = a <= 2.0f;
    EXPECT_NEAR(result.at(0, 0, 0, 0), 1.0f, kEpsilon); // 1 <= 2
    EXPECT_NEAR(result.at(0, 0, 0, 1), 1.0f, kEpsilon); // 2 <= 2
    EXPECT_NEAR(result.at(0, 0, 0, 2), 0.0f, kEpsilon); // 3 <= 2 = false
    EXPECT_NEAR(result.at(0, 0, 0, 3), 0.0f, kEpsilon); // 4 <= 2 = false
}

TEST(ComparisonTest, GreaterOrEqual)
{
    Tensor a(Shape(1, 1, 1, 4), {1.0f, 2.0f, 3.0f, 4.0f});
    Tensor result = a >= 2.0f;
    EXPECT_NEAR(result.at(0, 0, 0, 0), 0.0f, kEpsilon); // 1 >= 2 = false
    EXPECT_NEAR(result.at(0, 0, 0, 1), 1.0f, kEpsilon); // 2 >= 2
    EXPECT_NEAR(result.at(0, 0, 0, 2), 1.0f, kEpsilon); // 3 >= 2
    EXPECT_NEAR(result.at(0, 0, 0, 3), 1.0f, kEpsilon); // 4 >= 2
}

TEST(ComparisonTest, ScalarOnLeftSide)
{
    Tensor a(Shape(1, 1, 1, 4), {1.0f, 2.0f, 3.0f, 4.0f});

    // 2.0f < a means check if 2 < each element
    Tensor result = 2.0f < a;
    EXPECT_NEAR(result.at(0, 0, 0, 0), 0.0f, kEpsilon); // 2 < 1 = false
    EXPECT_NEAR(result.at(0, 0, 0, 1), 0.0f, kEpsilon); // 2 < 2 = false
    EXPECT_NEAR(result.at(0, 0, 0, 2), 1.0f, kEpsilon); // 2 < 3
    EXPECT_NEAR(result.at(0, 0, 0, 3), 1.0f, kEpsilon); // 2 < 4
}

TEST(ComparisonTest, TensorToTensorComparison)
{
    Tensor a(Shape(1, 1, 1, 4), {1.0f, 5.0f, 3.0f, 4.0f});
    Tensor b(Shape(1, 1, 1, 4), {2.0f, 3.0f, 3.0f, 5.0f});

    Tensor result = a < b;
    EXPECT_NEAR(result.at(0, 0, 0, 0), 1.0f, kEpsilon); // 1 < 2
    EXPECT_NEAR(result.at(0, 0, 0, 1), 0.0f, kEpsilon); // 5 < 3 = false
    EXPECT_NEAR(result.at(0, 0, 0, 2), 0.0f, kEpsilon); // 3 < 3 = false
    EXPECT_NEAR(result.at(0, 0, 0, 3), 1.0f, kEpsilon); // 4 < 5
}

TEST(ComparisonTest, ComparisonWithBroadcasting)
{
    Tensor a(Shape(2, 1, 1, 3), {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f});
    Tensor threshold(2.5f); // Scalar

    Tensor result = a > threshold;
    EXPECT_EQ(result.shape(), Shape(2, 1, 1, 3));
    // XCC=0: {1, 2, 3} > 2.5 = {0, 0, 1}
    EXPECT_NEAR(result.at(0, 0, 0, 0), 0.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 1), 0.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 2), 1.0f, kEpsilon);
    // XCC=1: {4, 5, 6} > 2.5 = {1, 1, 1}
    EXPECT_NEAR(result.at(1, 0, 0, 0), 1.0f, kEpsilon);
    EXPECT_NEAR(result.at(1, 0, 0, 1), 1.0f, kEpsilon);
    EXPECT_NEAR(result.at(1, 0, 0, 2), 1.0f, kEpsilon);
}

// ============================================================================
// Test Logical Operators
// ============================================================================

TEST(LogicalTest, OrOperator)
{
    Tensor a(Shape(1, 1, 1, 4), {0.0f, 1.0f, 0.0f, 1.0f});
    Tensor b(Shape(1, 1, 1, 4), {0.0f, 0.0f, 1.0f, 1.0f});

    Tensor result = a | b;
    EXPECT_NEAR(result.at(0, 0, 0, 0), 0.0f, kEpsilon); // 0 | 0 = 0
    EXPECT_NEAR(result.at(0, 0, 0, 1), 1.0f, kEpsilon); // 1 | 0 = 1
    EXPECT_NEAR(result.at(0, 0, 0, 2), 1.0f, kEpsilon); // 0 | 1 = 1
    EXPECT_NEAR(result.at(0, 0, 0, 3), 1.0f, kEpsilon); // 1 | 1 = 1
}

TEST(LogicalTest, AndOperator)
{
    Tensor a(Shape(1, 1, 1, 4), {0.0f, 1.0f, 0.0f, 1.0f});
    Tensor b(Shape(1, 1, 1, 4), {0.0f, 0.0f, 1.0f, 1.0f});

    Tensor result = a & b;
    EXPECT_NEAR(result.at(0, 0, 0, 0), 0.0f, kEpsilon); // 0 & 0 = 0
    EXPECT_NEAR(result.at(0, 0, 0, 1), 0.0f, kEpsilon); // 1 & 0 = 0
    EXPECT_NEAR(result.at(0, 0, 0, 2), 0.0f, kEpsilon); // 0 & 1 = 0
    EXPECT_NEAR(result.at(0, 0, 0, 3), 1.0f, kEpsilon); // 1 & 1 = 1
}

TEST(LogicalTest, NonZeroAsTrue)
{
    // Any non-zero value should be treated as true
    Tensor a(Shape(1, 1, 1, 4), {0.0f, 5.0f, -1.0f, 0.001f});
    Tensor b(Shape(1, 1, 1, 4), {0.0f, 2.0f, 3.0f, 0.0f});

    Tensor result = a & b;
    EXPECT_NEAR(result.at(0, 0, 0, 0), 0.0f, kEpsilon); // 0 & 0 = 0
    EXPECT_NEAR(result.at(0, 0, 0, 1), 1.0f, kEpsilon); // 5 & 2 = 1 (both non-zero)
    EXPECT_NEAR(result.at(0, 0, 0, 2), 1.0f, kEpsilon); // -1 & 3 = 1
    EXPECT_NEAR(result.at(0, 0, 0, 3), 0.0f, kEpsilon); // 0.001 & 0 = 0
}

TEST(LogicalTest, OrWithScalar)
{
    Tensor a(Shape(1, 1, 1, 4), {0.0f, 1.0f, 0.0f, 1.0f});
    Tensor result = a | 1.0f;
    // Everything should be 1 (true | anything = true)
    for (size_t i = 0; i < 4; ++i) EXPECT_NEAR(result.at(0, 0, 0, i), 1.0f, kEpsilon);
}

TEST(LogicalTest, AndWithScalar)
{
    Tensor a(Shape(1, 1, 1, 4), {0.0f, 1.0f, 0.0f, 1.0f});
    Tensor result = a & 0.0f;
    // Everything should be 0 (anything & false = false)
    for (size_t i = 0; i < 4; ++i) { EXPECT_NEAR(result.at(0, 0, 0, i), 0.0f, kEpsilon); }
}

TEST(LogicalTest, LogicalWithBroadcasting)
{
    Tensor a(Shape(2, 1, 1, 2), {1.0f, 0.0f, 0.0f, 1.0f});
    Tensor b(Shape(1, 1, 1, 2), {1.0f, 1.0f});

    Tensor result = a & b;
    EXPECT_EQ(result.shape(), Shape(2, 1, 1, 2));
    // XCC=0: {1,0} & {1,1} = {1,0}
    EXPECT_NEAR(result.at(0, 0, 0, 0), 1.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 1), 0.0f, kEpsilon);
    // XCC=1: {0,1} & {1,1} = {0,1}
    EXPECT_NEAR(result.at(1, 0, 0, 0), 0.0f, kEpsilon);
    EXPECT_NEAR(result.at(1, 0, 0, 1), 1.0f, kEpsilon);
}

// ============================================================================
// Test Comparison and Logical Parsing
// ============================================================================

TEST(ParserComparisonTest, ParseEquality)
{
    Parser parser;
    CounterContext ctx;
    ctx.setCounter("x", std::make_shared<Tensor>(Shape(1, 1, 1, 3), std::vector<float>{1.0f, 2.0f, 3.0f}));

    ExprPtr expr = parser.parseExpression("x == 2");
    Tensor result = expr->evaluate(ctx);

    EXPECT_EQ(result.shape(), Shape(1, 1, 1, 3));
    EXPECT_NEAR(result.at(0, 0, 0, 0), 0.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 1), 1.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 2), 0.0f, kEpsilon);
}

TEST(ParserComparisonTest, ParseLessThan)
{
    Parser parser;
    CounterContext ctx;
    ctx.setCounter("x", std::make_shared<Tensor>(Shape(1, 1, 1, 4), std::vector<float>{1.0f, 2.0f, 3.0f, 4.0f}));

    ExprPtr expr = parser.parseExpression("x < 3");
    Tensor result = expr->evaluate(ctx);

    EXPECT_NEAR(result.at(0, 0, 0, 0), 1.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 1), 1.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 2), 0.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 3), 0.0f, kEpsilon);
}

TEST(ParserComparisonTest, ParseGreaterOrEqual)
{
    Parser parser;
    CounterContext ctx;
    ctx.setCounter("x", std::make_shared<Tensor>(Shape(1, 1, 1, 4), std::vector<float>{1.0f, 2.0f, 3.0f, 4.0f}));

    ExprPtr expr = parser.parseExpression("x >= 2");
    Tensor result = expr->evaluate(ctx);

    EXPECT_NEAR(result.at(0, 0, 0, 0), 0.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 1), 1.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 2), 1.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 3), 1.0f, kEpsilon);
}

TEST(ParserLogicalTest, ParseOr)
{
    Parser parser;
    CounterContext ctx;
    ctx.setCounter("a", std::make_shared<Tensor>(Shape(1, 1, 1, 4), std::vector<float>{0.0f, 1.0f, 0.0f, 1.0f}));
    ctx.setCounter("b", std::make_shared<Tensor>(Shape(1, 1, 1, 4), std::vector<float>{0.0f, 0.0f, 1.0f, 1.0f}));

    ExprPtr expr = parser.parseExpression("a | b");
    Tensor result = expr->evaluate(ctx);

    EXPECT_NEAR(result.at(0, 0, 0, 0), 0.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 1), 1.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 2), 1.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 3), 1.0f, kEpsilon);
}

TEST(ParserLogicalTest, ParseAnd)
{
    Parser parser;
    CounterContext ctx;
    ctx.setCounter("a", std::make_shared<Tensor>(Shape(1, 1, 1, 4), std::vector<float>{0.0f, 1.0f, 0.0f, 1.0f}));
    ctx.setCounter("b", std::make_shared<Tensor>(Shape(1, 1, 1, 4), std::vector<float>{0.0f, 0.0f, 1.0f, 1.0f}));

    ExprPtr expr = parser.parseExpression("a & b");
    Tensor result = expr->evaluate(ctx);

    EXPECT_NEAR(result.at(0, 0, 0, 0), 0.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 1), 0.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 2), 0.0f, kEpsilon);
    EXPECT_NEAR(result.at(0, 0, 0, 3), 1.0f, kEpsilon);
}

TEST(ParserLogicalTest, ComplexExpression)
{
    // Test: (x > 1) & (x < 4)
    Parser parser;
    CounterContext ctx;
    ctx.setCounter("x", std::make_shared<Tensor>(Shape(1, 1, 1, 5), std::vector<float>{0.0f, 1.0f, 2.0f, 3.0f, 4.0f}));

    ExprPtr expr = parser.parseExpression("(x > 1) & (x < 4)");
    Tensor result = expr->evaluate(ctx);

    EXPECT_NEAR(result.at(0, 0, 0, 0), 0.0f, kEpsilon); // 0: not > 1
    EXPECT_NEAR(result.at(0, 0, 0, 1), 0.0f, kEpsilon); // 1: not > 1
    EXPECT_NEAR(result.at(0, 0, 0, 2), 1.0f, kEpsilon); // 2: > 1 and < 4
    EXPECT_NEAR(result.at(0, 0, 0, 3), 1.0f, kEpsilon); // 3: > 1 and < 4
    EXPECT_NEAR(result.at(0, 0, 0, 4), 0.0f, kEpsilon); // 4: not < 4
}

TEST(ParserLogicalTest, OrPrecedence)
{
    // Test precedence: a | b & c should be parsed as a | (b & c)
    // But we implement OR lower precedence than AND, so it's a | (b & c)
    Parser parser;
    CounterContext ctx;
    ctx.setCounter("a", std::make_shared<Tensor>(1.0f));
    ctx.setCounter("b", std::make_shared<Tensor>(0.0f));
    ctx.setCounter("c", std::make_shared<Tensor>(0.0f));

    ExprPtr expr = parser.parseExpression("a | b & c");
    Tensor result = expr->evaluate(ctx);
    // a | (b & c) = 1 | (0 & 0) = 1 | 0 = 1
    EXPECT_NEAR(result.scalar(), 1.0f, kEpsilon);
}

TEST(ParserLogicalTest, ComparisonInLogical)
{
    // Test: x == 2 | x == 4
    Parser parser;
    CounterContext ctx;
    ctx.setCounter("x", std::make_shared<Tensor>(Shape(1, 1, 1, 5), std::vector<float>{1.0f, 2.0f, 3.0f, 4.0f, 5.0f}));

    ExprPtr expr = parser.parseExpression("x == 2 | x == 4");
    Tensor result = expr->evaluate(ctx);

    EXPECT_NEAR(result.at(0, 0, 0, 0), 0.0f, kEpsilon); // 1
    EXPECT_NEAR(result.at(0, 0, 0, 1), 1.0f, kEpsilon); // 2
    EXPECT_NEAR(result.at(0, 0, 0, 2), 0.0f, kEpsilon); // 3
    EXPECT_NEAR(result.at(0, 0, 0, 3), 1.0f, kEpsilon); // 4
    EXPECT_NEAR(result.at(0, 0, 0, 4), 0.0f, kEpsilon); // 5
}

// ============================================================================
// Test Combined Linear with Comparison/Logical
// ============================================================================

TEST(LinearComparisonTest, FilterWithLinear)
{
    // Create a mask for indices >= 2
    Parser parser;
    CounterContext ctx;

    ExprPtr expr = parser.parseExpression("linear(5, axis=Time) >= 2");
    Tensor result = expr->evaluate(ctx);

    EXPECT_EQ(result.shape(), Shape(1, 1, 1, 5));
    EXPECT_NEAR(result.at(0, 0, 0, 0), 0.0f, kEpsilon); // 0 >= 2 = false
    EXPECT_NEAR(result.at(0, 0, 0, 1), 0.0f, kEpsilon); // 1 >= 2 = false
    EXPECT_NEAR(result.at(0, 0, 0, 2), 1.0f, kEpsilon); // 2 >= 2 = true
    EXPECT_NEAR(result.at(0, 0, 0, 3), 1.0f, kEpsilon); // 3 >= 2 = true
    EXPECT_NEAR(result.at(0, 0, 0, 4), 1.0f, kEpsilon); // 4 >= 2 = true
}

TEST(LinearComparisonTest, RangeMask)
{
    // Create a mask for indices in range [1, 3)
    Parser parser;
    CounterContext ctx;

    ExprPtr expr = parser.parseExpression("(linear(5, axis=Time) >= 1) & (linear(5, axis=Time) < 3)");
    Tensor result = expr->evaluate(ctx);

    EXPECT_NEAR(result.at(0, 0, 0, 0), 0.0f, kEpsilon); // 0: false
    EXPECT_NEAR(result.at(0, 0, 0, 1), 1.0f, kEpsilon); // 1: true
    EXPECT_NEAR(result.at(0, 0, 0, 2), 1.0f, kEpsilon); // 2: true
    EXPECT_NEAR(result.at(0, 0, 0, 3), 0.0f, kEpsilon); // 3: false
    EXPECT_NEAR(result.at(0, 0, 0, 4), 0.0f, kEpsilon); // 4: false
}

TEST(DerivedCounterIntegrationTest, LinearWithComparisonAndLogical)
{
    DerivedCounterManager manager;
    manager.loadDefinitions(R"(
indices := linear(6, axis=Time)
mask := (indices >= 2) & (indices <= 4)
)");

    auto indices = manager.evaluate("indices");
    EXPECT_EQ(indices->shape(), Shape(1, 1, 1, 6));

    auto mask = manager.evaluate("mask");
    EXPECT_EQ(mask->shape(), Shape(1, 1, 1, 6));
    EXPECT_NEAR(mask->at(0, 0, 0, 0), 0.0f, kEpsilon); // 0
    EXPECT_NEAR(mask->at(0, 0, 0, 1), 0.0f, kEpsilon); // 1
    EXPECT_NEAR(mask->at(0, 0, 0, 2), 1.0f, kEpsilon); // 2
    EXPECT_NEAR(mask->at(0, 0, 0, 3), 1.0f, kEpsilon); // 3
    EXPECT_NEAR(mask->at(0, 0, 0, 4), 1.0f, kEpsilon); // 4
    EXPECT_NEAR(mask->at(0, 0, 0, 5), 0.0f, kEpsilon); // 5
}

// ============================================================================
// Parser and DerivedCounterManager integration tests
// ============================================================================

TEST(ParserTest, ParsesReductionWithAxisList)
{
    DerivedCounterManager mgr;

    // Provide at least one raw counter so builtin scalars are available if needed.
    Shape shape(1, 1, 1, 4);
    auto a = std::make_shared<Tensor>(shape, std::vector<float>{1.0f, 2.0f, 3.0f, 4.0f});
    mgr.context().setCounter("A", a);

    mgr.loadDefinitions("S := sum[A, axis=[TIME]]\n");
    auto s = mgr.evaluate("S");
    ASSERT_TRUE(s);
    EXPECT_TRUE(s->isScalar());
    EXPECT_NEAR(s->scalar(), 10.0f, kEpsilon);
}

TEST(DerivedCounterManagerTest, DetectsCyclicDefinitions)
{
    DerivedCounterManager mgr;
    // Raw counter present to avoid unrelated builtin scalar failures.
    Shape shape(1, 1, 1, 1);
    mgr.context().setCounter("X", std::make_shared<Tensor>(shape, 0.0f));

    mgr.loadDefinitions("A := B\nB := A\n");
    EXPECT_THROW({ (void) mgr.evaluate("A"); }, std::runtime_error);
}