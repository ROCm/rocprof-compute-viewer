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

    EXPECT_EQ(data.counters.size(), 7u);
    EXPECT_EQ(data.sample_count, 11u);
    EXPECT_EQ(data.sample_counts, (std::vector<size_t>{9, 9, 9, 10, 10, 10, 11, 11}));

    EXPECT_FLOAT_EQ(data.clock.at(0 * data.sample_count), 86.0f);
    EXPECT_FLOAT_EQ(data.clock.at(1 * data.sample_count), 0.0f);
    EXPECT_EQ(data.timestamps.at(0 * data.sample_count), 25079783077210u);
    EXPECT_EQ(data.timestamps.at(1 * data.sample_count), 25079783077124u);

    const auto& sq_cycles = findCounter(data, "SQ_CYCLES");
    EXPECT_EQ(sq_cycles.xcc_count, 8u);
    EXPECT_EQ(sq_cycles.se_count, 4u);
    EXPECT_EQ(sq_cycles.instance_count, 1u);
    EXPECT_FLOAT_EQ(sq_cycles.values.at(index(sq_cycles, data.sample_count, 0, 0, 0, 0)), 16411.0f);

    const auto& ta_busy = findCounter(data, "TA_TA_BUSY");
    EXPECT_EQ(ta_busy.xcc_count, 8u);
    EXPECT_EQ(ta_busy.se_count, 4u);
    EXPECT_EQ(ta_busy.instance_count, 11u);

    const auto& tcc_hit = findCounter(data, "TCC_HIT");
    EXPECT_EQ(tcc_hit.xcc_count, 8u);
    EXPECT_EQ(tcc_hit.se_count, 1u);
    EXPECT_EQ(tcc_hit.instance_count, 16u);
}

TEST(SpmJson, AlignsClockWithSqCyclesAndFirstRealtimeRecord)
{
    SpmData data = loadSpmJson(SPM_TEST_DATA);
    std::map<int, std::vector<realtime_record_t>> realtime{
        {0, {{12680, 25079783078590, 0}}}
    };

    ASSERT_TRUE(alignSpmClock(data, realtime));

    const size_t xcc = 6;
    const size_t before = 1;
    const size_t after = 2;
    const uint64_t before_timestamp = data.timestamps.at(xcc * data.sample_count + before);
    const uint64_t after_timestamp = data.timestamps.at(xcc * data.sample_count + after);
    const float before_clock = data.clock.at(xcc * data.sample_count + before);
    const float after_clock = data.clock.at(xcc * data.sample_count + after);
    const double fraction = double(25079783078590u - before_timestamp) / double(after_timestamp - before_timestamp);

    EXPECT_NEAR(before_clock + fraction * (after_clock - before_clock), 12680.0, 0.5);
    EXPECT_NEAR(after_clock - before_clock, 16385.0, 0.5);
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
    std::map<int, std::vector<realtime_record_t>> realtime{
        {0, {{12680, 25079783078590, 0}}  },
        {1, {{4077640, 25079783274738, 0}}}
    };

    ASSERT_TRUE(alignSpmClock(data, realtime));

    const long double slope =
        static_cast<long double>(4077640 - 12680) / static_cast<long double>(25079783274738u - 25079783078590u);
    const long double expected = 12680.0L - static_cast<long double>(25079783078590u - 25079783077210u) * slope;
    EXPECT_NEAR(data.clock.at(0), static_cast<double>(expected), 0.5);
}
