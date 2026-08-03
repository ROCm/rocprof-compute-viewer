#include <gtest/gtest.h>

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
