#include <gtest/gtest.h>

#include "data/datastore.h"
#include "data/shaderdata.h"
#include "util/memtracker.h"

int MemTracker::count = 0;
std::unordered_map<std::string, int> MemTracker::classes;

TEST(DataStoreTimeOffsets, ShiftsCountersAndRealtimeShaderClocks)
{
    DataStore store;

    counter_record_t counter{};
    counter.time = 10;
    counter.cu = 3;
    counter.bank = 0;
    counter.values = {1.0f, 2.0f, 3.0f, 4.0f};
    store.counters_by_se[1][0].push_back(counter);

    realtime_record_t realtime{};
    realtime.shader_clock = 100;
    realtime.realtime_clock = 5000;
    store.realtime_by_se[1].push_back(realtime);

    store.applyTimeOffsets({{1, 42}});

    ASSERT_EQ(store.counters_by_se.at(1).at(0).size(), 1u);
    EXPECT_EQ(store.counters_by_se.at(1).at(0).front().time, 52);

    ASSERT_EQ(store.realtime_by_se.at(1).size(), 1u);
    EXPECT_EQ(store.realtime_by_se.at(1).front().shader_clock, 142);
    EXPECT_EQ(store.realtime_by_se.at(1).front().realtime_clock, 5000u);
}

TEST(DataStoreRealtimeAlignment, DoesNotPartiallyShiftCounterSEsMissingRealtime)
{
    DataStore store;

    realtime_record_t se0_a{};
    se0_a.shader_clock = 10;
    se0_a.realtime_clock = 1000;
    realtime_record_t se0_b{};
    se0_b.shader_clock = 110;
    se0_b.realtime_clock = 1100;
    store.realtime_by_se[0] = {se0_a, se0_b};

    realtime_record_t se1_a{};
    se1_a.shader_clock = 10;
    se1_a.realtime_clock = 1100;
    realtime_record_t se1_b{};
    se1_b.shader_clock = 110;
    se1_b.realtime_clock = 1200;
    store.realtime_by_se[1] = {se1_a, se1_b};

    counter_record_t counter{};
    counter.time = 20;
    store.counters_by_se[2][0].push_back(counter);

    EXPECT_FALSE(store.applyRealtimeAlignment());
    EXPECT_FALSE(store.realtime_alignment_applied);
    EXPECT_EQ(store.counters_by_se.at(2).at(0).front().time, 20);
}
