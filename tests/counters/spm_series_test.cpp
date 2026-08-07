#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

#include "analysis/spm_series.h"

using DerivedCounter::Shape;
using DerivedCounter::Tensor;

TEST(SpmSeries, BuildsPerXccTimestampDeltas)
{
    SpmData spm;
    spm.sample_count = 3;
    spm.timestamps = {100, 110, 130, 105, 125, 125};
    spm.sample_valid = {0b11111};

    EXPECT_EQ(SpmSeries::timestampDeltas(spm), (std::vector<float>{15, 10, 20, 20, 20, 20}));
}

TEST(SpmSeries, PlacesValuesAtIntervalStarts)
{
    Tensor values(Shape(1, 1, 1, 3), std::vector<float>{99, 1, 2});
    Tensor clock(Shape(1, 1, 1, 3), std::vector<float>{10, 20, 30});

    const auto points = SpmSeries::makePlotSeries(values, clock);

    ASSERT_EQ(points.size(), 4u);
    EXPECT_FLOAT_EQ(points[0].time, 0);
    EXPECT_FLOAT_EQ(points[0].value, 99);
    EXPECT_FLOAT_EQ(points[1].time, 10);
    EXPECT_FLOAT_EQ(points[1].value, 1);
    EXPECT_FLOAT_EQ(points[2].time, 20);
    EXPECT_FLOAT_EQ(points[2].value, 2);
    EXPECT_FLOAT_EQ(points[3].time, 30);
    EXPECT_FLOAT_EQ(points[3].value, 2);
}

TEST(SpmSeries, SumsSeAndCuAxesForPlotting)
{
    Tensor values(Shape(1, 2, 2, 3), std::vector<float>{1, 2, 3, 10, 20, 30, 100, 200, 300, 1000, 2000, 3000});

    Tensor clock(Shape(1, 1, 1, 3), std::vector<float>{10, 20, 30});
    const auto points = SpmSeries::makePlotSeries(values, clock);

    ASSERT_EQ(points.size(), 4u);
    EXPECT_FLOAT_EQ(points[0].value, 1111);
    EXPECT_FLOAT_EQ(points[1].value, 2222);
    EXPECT_FLOAT_EQ(points[2].value, 3333);
}

TEST(SpmSeries, MergesXccUpdatesAndKeepsLastValues)
{
    Tensor values(Shape(2, 1, 1, 3), std::vector<float>{0, 1, 2, 0, 10, 20});
    Tensor clock(Shape(2, 1, 1, 3), std::vector<float>{10, 20, 30, 15, 25, 35});

    const auto points = SpmSeries::makePlotSeries(values, clock);

    ASSERT_EQ(points.size(), 8u);
    EXPECT_EQ(points[0].time, -1);
    EXPECT_EQ(points[0].value, 0);
    EXPECT_EQ(points[3].time, 10);
    EXPECT_EQ(points[3].value, 1);
    EXPECT_EQ(points[4].time, 15);
    EXPECT_EQ(points[4].value, 11);
    EXPECT_EQ(points[5].time, 20);
    EXPECT_EQ(points[5].value, 12);
    EXPECT_EQ(points[6].time, 25);
    EXPECT_EQ(points[6].value, 22);
    EXPECT_EQ(points[7].time, 35);
    EXPECT_EQ(points[7].value, 22);
}

TEST(SpmSeries, CoalescesSimultaneousXccUpdates)
{
    Tensor values(Shape(2, 1, 1, 2), std::vector<float>{0, 1, 0, 10});
    Tensor clock(Shape(2, 1, 1, 2), std::vector<float>{10, 20, 10, 20});

    const auto points = SpmSeries::makePlotSeries(values, clock);

    ASSERT_EQ(points.size(), 4u);
    EXPECT_EQ(points[2].time, 10);
    EXPECT_EQ(points[2].value, 11);
    EXPECT_EQ(points[3].time, 20);
    EXPECT_EQ(points[3].value, 11);
}

TEST(SpmSeries, SelectedXccUsesItsOriginalClock)
{
    Tensor values(Shape(3, 1, 1, 3), std::vector<float>{0, 10, 10, 0, 20, 20, 0, 30, 30});
    Tensor clock(Shape(3, 1, 1, 3), std::vector<float>{0, 10, 20, 5, 15, 25, 7, 17, 27});

    DerivedCounter::DerivedCounterManager manager;
    manager.context().setCounter("TCP", std::make_shared<Tensor>(values));
    manager.loadDefinitions("TCP_XCC2 := select[TCP, 2, axis=XCC]");
    const Tensor selected = *manager.evaluate("TCP_XCC2");
    ASSERT_EQ(selected.xccIndices(), (std::vector<size_t>{2}));

    const auto selected_points = SpmSeries::makePlotSeries(selected, clock);
    const auto all_points = SpmSeries::makePlotSeries(values, clock);

    ASSERT_GE(selected_points.size(), 3u);
    EXPECT_EQ(selected_points[0].time, -3);
    EXPECT_EQ(selected_points[0].value, 0);
    EXPECT_EQ(selected_points[1].time, 7);
    EXPECT_EQ(selected_points[1].value, 30);

    const auto all_at_xcc2_update =
        std::find_if(all_points.begin(), all_points.end(), [](const auto& point) { return point.time == 7; });
    ASSERT_NE(all_at_xcc2_update, all_points.end());
    EXPECT_GE(all_at_xcc2_update->value, selected_points[0].value);
}

TEST(SpmSeries, SelectedXccUsesItsOriginalSampleCount)
{
    Tensor values(Shape(3, 1, 1, 4), 1);
    Tensor clock(
        Shape(3, 1, 1, 4), std::vector<float>{0, 1, 2, 3, 10, 11, 12, 12, 20, 21, 21, 21}, std::vector<uint64_t>{0x37F}
    );
    const Tensor selected = values.select(2, DerivedCounter::Axis::XCC);
    const auto points = SpmSeries::makePlotSeries(selected, clock);

    ASSERT_EQ(points.size(), 3u);
    EXPECT_EQ(points.back().time, 21);
}
