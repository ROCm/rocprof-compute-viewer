#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

#include "data/spm_json.h"

namespace
{
const SpmCounterData& findCounter(const SpmData& data, const std::string& name)
{
    for (const auto& counter : data.counters)
        if (counter.name == name) return counter;
    throw std::runtime_error("counter not found");
}

size_t index(const SpmCounterData& counter, size_t sample_count, size_t xcc, size_t se, size_t instance, size_t sample)
{
    return ((xcc * counter.se_count + se) * counter.instance_count + instance) * sample_count + sample;
}
} // namespace

TEST(SpmJson, LoadsPerXccClocksAndCounterDimensions)
{
    const std::filesystem::path path = SPM_TEST_DATA;
    SpmData data = loadSpmJson(path.string());

    EXPECT_EQ(data.counters.size(), 2u);
    EXPECT_EQ(data.sample_count, 3u);
    EXPECT_EQ(data.sample_counts, (std::vector<size_t>{3, 2}));

    EXPECT_FLOAT_EQ(data.clock.at(0 * data.sample_count), 0.0f);
    EXPECT_FLOAT_EQ(data.clock.at(1 * data.sample_count), 2.0f);
    EXPECT_EQ(data.timestamps.at(0 * data.sample_count), 100u);
    EXPECT_EQ(data.timestamps.at(1 * data.sample_count), 102u);

    const auto& sq_cycles = findCounter(data, "SQ_CYCLES");
    EXPECT_EQ(sq_cycles.xcc_count, 2u);
    EXPECT_EQ(sq_cycles.se_count, 1u);
    EXPECT_EQ(sq_cycles.instance_count, 1u);
    EXPECT_FLOAT_EQ(sq_cycles.values.at(index(sq_cycles, data.sample_count, 0, 0, 0, 0)), 11.0f);

    const auto& test_counter = findCounter(data, "TEST_COUNTER");
    EXPECT_EQ(test_counter.xcc_count, 2u);
    EXPECT_EQ(test_counter.se_count, 1u);
    EXPECT_EQ(test_counter.instance_count, 2u);
    EXPECT_FLOAT_EQ(test_counter.values.at(index(test_counter, data.sample_count, 1, 0, 1, 1)), 10.0f);
}

TEST(SpmJson, AlignsNearestSampleWithSqCyclesAndFirstRealtimeRecord)
{
    SpmData data = loadSpmJson(SPM_TEST_DATA);
    std::vector<SpmClockAnchor> realtime{
        {0, 1000, 108}
    };

    ASSERT_TRUE(alignSpmClock(data, realtime));

    // 110 and 112 are nearest to 108, then corrected using the local rate.
    EXPECT_NEAR(data.clock.at(0 * data.sample_count + 1), 1002.0, 0.01);
    EXPECT_NEAR(data.clock.at(1 * data.sample_count + 1), 1004.0, 0.01);
    EXPECT_NEAR(data.clock.at(0 * data.sample_count + 0), 992.0, 0.01);
}

TEST(SpmJson, ChoosesPreviousSampleWhenItIsNearest)
{
    SpmData data = loadSpmJson(SPM_TEST_DATA);
    std::vector<SpmClockAnchor> realtime{
        {0, 1000, 104}
    };

    ASSERT_TRUE(alignSpmClock(data, realtime));

    EXPECT_NEAR(data.clock.at(0 * data.sample_count + 0), 996.0, 0.01);
    EXPECT_NEAR(data.clock.at(1 * data.sample_count + 0), 998.0, 0.01);
}

TEST(SpmJson, UsesNearestSamplesSqCyclesRate)
{
    SpmData data;
    data.sample_count = 3;
    data.sample_counts = {3};
    data.timestamps = {100, 200, 300};
    data.clock = {0, 0, 0};

    SpmCounterData sq_cycles;
    sq_cycles.name = "SQ_CYCLES";
    sq_cycles.values = {0, 10'000, 50'000};
    data.counters.push_back(std::move(sq_cycles));

    std::vector<SpmClockAnchor> realtime{
        {0, 1000, 249}
    };
    ASSERT_TRUE(alignSpmClock(data, realtime));

    // Timestamp 200 is nearest. Its own interval rate is 100 cycles/tick;
    // using the following sample's rate would introduce a 19,600-cycle error.
    EXPECT_NEAR(data.clock.at(1), -3900.0, 0.01);
    EXPECT_NEAR(data.clock.at(2), 46'100.0, 0.01);
}

TEST(SpmJson, DetectsRealtimeRangeOverlap)
{
    SpmData data = loadSpmJson(SPM_TEST_DATA);
    std::vector<SpmClockAnchor> in_range{
        {0, 1000, 108}
    };
    std::vector<SpmClockAnchor> out_of_range{
        {0, 1000, 1000}
    };

    EXPECT_TRUE(spmClockRangesOverlap(data, in_range));
    EXPECT_FALSE(spmClockRangesOverlap(data, out_of_range));
}

TEST(SpmJson, UsesRealtimeInterpolationWithoutSqCycles)
{
    SpmData data = loadSpmJson(SPM_TEST_DATA);
    data.counters.erase(
        std::remove_if(
            data.counters.begin(),
            data.counters.end(),
            [](const SpmCounterData& counter) { return counter.name == "SQ_CYCLES"; }
        ),
        data.counters.end()
    );
    std::vector<SpmClockAnchor> realtime{
        {0, 1000, 108},
        {0, 1020, 128}
    };

    ASSERT_TRUE(alignSpmClock(data, realtime));

    EXPECT_NEAR(data.clock.at(0), 992.0, 0.01);
}

TEST(SpmJson, RequiresSameSeForRealtimeInterpolation)
{
    SpmData data = loadSpmJson(SPM_TEST_DATA);
    data.counters.erase(
        std::remove_if(
            data.counters.begin(),
            data.counters.end(),
            [](const SpmCounterData& counter) { return counter.name == "SQ_CYCLES"; }
        ),
        data.counters.end()
    );

    std::vector<SpmClockAnchor> realtime{
        {0, 1000, 108},
        {1, 1020, 128}
    };
    EXPECT_FALSE(alignSpmClock(data, realtime));
}
