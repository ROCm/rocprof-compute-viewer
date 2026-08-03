#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>

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

TEST(SpmJson, AlignsClockWithSqCyclesAndFirstRealtimeRecord)
{
    SpmData data = loadSpmJson(SPM_TEST_DATA);
    std::vector<SpmClockAnchor> realtime{
        {0, 1000, 108}
    };

    ASSERT_TRUE(alignSpmClock(data, realtime));

    const size_t xcc = 0;
    const size_t before = 0;
    const size_t after = 1;
    const uint64_t before_timestamp = data.timestamps.at(xcc * data.sample_count + before);
    const uint64_t after_timestamp = data.timestamps.at(xcc * data.sample_count + after);
    const float before_clock = data.clock.at(xcc * data.sample_count + before);
    const float after_clock = data.clock.at(xcc * data.sample_count + after);
    const double fraction = double(108u - before_timestamp) / double(after_timestamp - before_timestamp);

    EXPECT_NEAR(before_clock + fraction * (after_clock - before_clock), 1000.0, 0.01);
    EXPECT_NEAR(after_clock - before_clock, 10.0, 0.01);
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
        {1, 1020, 128}
    };

    ASSERT_TRUE(alignSpmClock(data, realtime));

    EXPECT_NEAR(data.clock.at(0), 992.0, 0.01);
}
