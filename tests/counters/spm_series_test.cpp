#include <gtest/gtest.h>

#include "analysis/spm_series.h"

using DerivedCounter::Shape;
using DerivedCounter::Tensor;

TEST(SpmSeries, BuildsPerXccTimestampDeltas)
{
    SpmData spm;
    spm.sample_count = 3;
    spm.sample_counts = {3, 2};
    spm.timestamps = {100, 110, 130, 105, 125, 125};

    EXPECT_EQ(SpmSeries::timestampDeltas(spm), (std::vector<float>{15, 10, 20, 20, 20, 20}));
}

TEST(SpmSeries, PlacesValuesAtIntervalStarts)
{
    Tensor values(Shape(1, 1, 1, 3), std::vector<float>{99, 1, 2});
    Tensor clock(Shape(1, 1, 1, 3), std::vector<float>{10, 20, 30});

    const auto points = SpmSeries::interval(values, clock, 0, 0, 0, 3);

    ASSERT_EQ(points.size(), 3u);
    EXPECT_FLOAT_EQ(points[0].time, 10);
    EXPECT_FLOAT_EQ(points[0].value, 1);
    EXPECT_FLOAT_EQ(points[1].time, 20);
    EXPECT_FLOAT_EQ(points[1].value, 2);
    EXPECT_FLOAT_EQ(points[2].time, 30);
    EXPECT_FLOAT_EQ(points[2].value, 2);
}

TEST(SpmSeries, SumsSeAndCuAxesForPlotting)
{
    Tensor values(
        Shape(1, 2, 2, 3),
        std::vector<float>{1, 2, 3, 10, 20, 30, 100, 200, 300, 1000, 2000, 3000}
    );

    const Tensor summed = SpmSeries::sumSpatialForPlot(values);

    EXPECT_EQ(summed.shape(), Shape(1, 1, 1, 3));
    EXPECT_FLOAT_EQ(summed.at(0, 0, 0, 0), 1111);
    EXPECT_FLOAT_EQ(summed.at(0, 0, 0, 1), 2222);
    EXPECT_FLOAT_EQ(summed.at(0, 0, 0, 2), 3333);
}

TEST(SpmSeries, MergesXccUpdatesAndKeepsLastValues)
{
    Tensor values(Shape(2, 1, 1, 3), std::vector<float>{0, 1, 2, 0, 10, 20});
    Tensor clock(Shape(2, 1, 1, 3), std::vector<float>{10, 20, 30, 15, 25, 35});

    const auto points = SpmSeries::mergeXcc(values, clock, {3, 3}, 3, 0, 0);

    ASSERT_EQ(points.size(), 6u);
    EXPECT_EQ(points[0].time, 9);
    EXPECT_EQ(points[0].value, 0);
    EXPECT_EQ(points[1].time, 10);
    EXPECT_EQ(points[1].value, 1);
    EXPECT_EQ(points[2].time, 15);
    EXPECT_EQ(points[2].value, 11);
    EXPECT_EQ(points[3].time, 20);
    EXPECT_EQ(points[3].value, 12);
    EXPECT_EQ(points[4].time, 25);
    EXPECT_EQ(points[4].value, 22);
    EXPECT_EQ(points[5].time, 35);
    EXPECT_EQ(points[5].value, 22);
}

TEST(SpmSeries, CoalescesSimultaneousXccUpdates)
{
    Tensor values(Shape(2, 1, 1, 2), std::vector<float>{0, 1, 0, 10});
    Tensor clock(Shape(2, 1, 1, 2), std::vector<float>{10, 20, 10, 20});

    const auto points = SpmSeries::mergeXcc(values, clock, {2, 2}, 2, 0, 0);

    ASSERT_EQ(points.size(), 3u);
    EXPECT_EQ(points[1].time, 10);
    EXPECT_EQ(points[1].value, 11);
    EXPECT_EQ(points[2].time, 20);
    EXPECT_EQ(points[2].value, 11);
}
