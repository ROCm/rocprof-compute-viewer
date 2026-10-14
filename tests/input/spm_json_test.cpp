#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "analysis/spm_series.h"
#include "data/spm_json.h"
#include "json/include/nlohmann/json.hpp"

namespace
{
const SpmCounterData& findCounter(const SpmData& data, const std::string& name)
{
    for (const auto& counter : data.counters)
        if (counter.name == name) return counter;
    throw std::runtime_error("counter not found");
}

size_t index(const SpmCounterData& counter, size_t sample_count, size_t xcc, size_t se, size_t instance, size_t sample)
{ return ((xcc * counter.se_count + se) * counter.instance_count + instance) * sample_count + sample; }

class EditableSpmCapture
{
public:
    nlohmann::json root;
    explicit EditableSpmCapture(const char* fixture = SPM_NAVI4_TEST_DATA)
    {
        std::ifstream input(fixture);
        input >> root;
        static std::atomic<size_t> serial{0};
        path = std::filesystem::temp_directory_path() /
               ("rcv_spm_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
                std::to_string(serial++) + ".json");
    }
    ~EditableSpmCapture()
    {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
    auto& tool() { return root.at("rocprofiler-sdk-tool").front(); }
    auto& batches() { return tool().at("callback_records").at("spm_counter_collection"); }
    SpmData load()
    {
        {
            std::ofstream output(path);
            output << root;
        }
        return loadSpmJson(path.string());
    }

private:
    std::filesystem::path path;
};

DerivedCounter::Tensor tensor(const SpmCounterData& counter, size_t samples)
{
    return DerivedCounter::Tensor(
        {counter.xcc_count, counter.se_count, counter.instance_count, samples}, counter.values, counter.valid
    );
}

} // namespace

TEST(SpmJson, LoadsPerXccClocksAndCounterDimensions)
{
    const std::filesystem::path path = SPM_TEST_DATA;
    SpmData data = loadSpmJson(path.string());

    EXPECT_EQ(data.counters.size(), 2u);
    EXPECT_EQ(data.sample_count, 3u);
    EXPECT_EQ(data.validSamples(0), 3u);
    EXPECT_EQ(data.validSamples(1), 2u);

    EXPECT_FLOAT_EQ(data.clock.at(0 * data.sample_count), 0.0f);
    EXPECT_FLOAT_EQ(data.clock.at(1 * data.sample_count), 2.0f);
    EXPECT_EQ(data.timestamps.at(0 * data.sample_count), 100u);
    EXPECT_EQ(data.timestamps.at(1 * data.sample_count), 102u);
    EXPECT_TRUE(Validity::test(data.sample_valid, 1 * data.sample_count + 1));
    EXPECT_FALSE(Validity::test(data.sample_valid, 1 * data.sample_count + 2));

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
    EXPECT_FALSE(Validity::test(test_counter.valid, index(test_counter, data.sample_count, 1, 0, 1, 2)));
}

TEST(SpmJson, Gfx9XccCaptureKeepsNonzeroSeExpressionsAndUnitArrayStride)
{
    EditableSpmCapture capture(SPM_TEST_DATA);
    capture.tool()["agents"] = {
        {{"id", {{"handle", 42}}}, {"name", "gfx942"}}
    };
    for (auto& counter : capture.tool()["counters"])
    {
        for (auto& dimension : counter["dimensions"])
            if (dimension["name"] == "DIMENSION_SHADER_ENGINE") dimension["instance_size"] = 2;
        for (auto& instance : counter["instances"])
        {
            bool xcc1 = false;
            for (const auto& dimension : instance["dimensions"])
                if (dimension["dimension_name"] == "DIMENSION_XCC" && dimension["index"] == 1) xcc1 = true;
            if (xcc1)
                for (auto& dimension : instance["dimensions"])
                    if (dimension["dimension_name"] == "DIMENSION_SHADER_ENGINE") dimension["index"] = 1;
        }
    }
    const auto data = capture.load();
    const auto& counter = findCounter(data, "TEST_COUNTER");
    EXPECT_EQ(counter.arrays_per_se, 1u);
    EXPECT_EQ(counter.se_count, 2u);
    DerivedCounter::CounterContext context;
    context.setCounter("TEST_COUNTER", std::make_shared<DerivedCounter::Tensor>(tensor(counter, data.sample_count)));
    DerivedCounter::Parser parser;
    const auto selected =
        parser.parseExpression("select[select[TEST_COUNTER, 1, axis=XCC], 1, axis=SE]")->evaluate(context);
    EXPECT_FLOAT_EQ(selected.at(0, 0, 1, 0), 8);
    EXPECT_FLOAT_EQ(selected.at(0, 0, 1, 1), 10);
    EXPECT_FALSE(selected.isValid(selected.linearIndex(0, 0, 1, 2)));
    EXPECT_EQ(selected.xccIndices(), (std::vector<size_t>{1}));
}

TEST(SpmJson, ExpandsAccumulatedMissingWindows)
{
    SpmData data = loadSpmJson(SPM_GAP_TEST_DATA);

    ASSERT_EQ(data.sample_count, 5u);
    EXPECT_EQ(data.timestamps, (std::vector<uint64_t>{100, 110, 120, 130, 140}));

    const auto& sq_cycles = findCounter(data, "SQ_CYCLES");
    const auto& counter = findCounter(data, "TEST_COUNTER");
    EXPECT_TRUE(Validity::test(counter.valid, index(counter, data.sample_count, 0, 0, 0, 0)));
    EXPECT_FLOAT_EQ(counter.values.at(index(counter, data.sample_count, 0, 0, 0, 0)), 0);
    for (size_t sample = 2; sample < 5; ++sample)
    {
        EXPECT_FLOAT_EQ(sq_cycles.values.at(index(sq_cycles, data.sample_count, 0, 0, 0, sample)), 10);
        EXPECT_FLOAT_EQ(counter.values.at(index(counter, data.sample_count, 0, 0, 0, sample)), 9);
    }
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

    // Timestamp 200 is before the anchor, so the partial interval uses the
    // following sample's 500 cycles/tick rate.
    EXPECT_NEAR(data.clock.at(1), -23'500.0, 0.01);
    EXPECT_NEAR(data.clock.at(2), 26'500.0, 0.01);
}

TEST(SpmJson, UsesPivotSamplesRateWhenPivotIsAfterAnchor)
{
    SpmData data;
    data.sample_count = 3;
    data.timestamps = {100, 200, 300};
    data.clock = {0, 0, 0};

    SpmCounterData sq_cycles;
    sq_cycles.name = "SQ_CYCLES";
    sq_cycles.values = {0, 10'000, 50'000};
    data.counters.push_back(std::move(sq_cycles));

    ASSERT_TRUE(alignSpmClock(
        data,
        {
            {0, 1000, 151}
    }
    ));

    EXPECT_NEAR(data.clock.at(1), 5'900.0, 0.01);
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

TEST(SpmJson, Navi4KeepsBothShaderArraysWithoutRequiringSqCycles)
{
    const SpmData data = loadSpmJson(SPM_NAVI4_TEST_DATA);
    ASSERT_EQ(data.sample_count, 2u);
    EXPECT_EQ(data.xccCount(), 1u);
    EXPECT_FALSE(data.clock_aligned);
    EXPECT_EQ(data.timestamps, (std::vector<uint64_t>{1000, 1100}));
    EXPECT_EQ(data.clock, (std::vector<float>{0, 100}));

    const auto& waves = findCounter(data, "SQ_WAVES");
    EXPECT_EQ(waves.arrays_per_se, 2u);
    EXPECT_EQ(waves.se_count, 4u);
    EXPECT_EQ(waves.instance_count, 2u);
    EXPECT_TRUE(waves.has_wgp);
    EXPECT_TRUE(waves.has_sa);
    EXPECT_EQ(
        waves.values, (std::vector<float>{10, 20, 20, 40, 110, 220, 120, 240, 30, 60, 40, 80, 130, 260, 140, 280})
    );
    for (size_t i = 0; i < waves.values.size(); ++i) EXPECT_TRUE(Validity::test(waves.valid, i));
    const auto& ta = findCounter(data, "TA_TA_BUSY");
    EXPECT_EQ(ta.instance_count, 4u);
    EXPECT_EQ(ta.values, (std::vector<float>{1, 2,  2, 4,  3, 6,  4, 8,  101, 202, 102, 204, 103, 206, 104, 208,
                                             5, 10, 6, 12, 7, 14, 8, 16, 105, 210, 106, 212, 107, 214, 108, 216}));
    const auto& global = findCounter(data, "GL2C_HIT");
    EXPECT_FALSE(global.has_sa);
    EXPECT_FALSE(global.has_wgp);
    EXPECT_EQ(global.instance_count, 3u);
    EXPECT_EQ(global.values, (std::vector<float>{5, 10, 6, 12, 7, 14}));

    DerivedCounter::Tensor clock({1, 1, 1, data.sample_count}, data.clock, data.sample_valid);
    const auto total = SpmSeries::makePlotSeries(tensor(ta, data.sample_count), clock);
    ASSERT_EQ(total.size(), 3u);
    EXPECT_FLOAT_EQ(total[0].value, 872);
    EXPECT_FLOAT_EQ(total[1].value, 1744);
    DerivedCounter::CounterContext context;
    context.setCounter("TA_TA_BUSY", std::make_shared<DerivedCounter::Tensor>(tensor(ta, data.sample_count)));
    DerivedCounter::Parser parser;
    const auto selected =
        parser.parseExpression("select[select[TA_TA_BUSY, 0, axis=SA], 2:4, axis=INSTANCE]")->evaluate(context);
    EXPECT_EQ(selected.select(0, DerivedCounter::Axis::CU).data(), (std::vector<float>{3, 6}));
    EXPECT_EQ(selected.select(1, DerivedCounter::Axis::CU).data(), (std::vector<float>{4, 8}));
    const auto isolated = SpmSeries::makePlotSeries(selected, clock);
    ASSERT_EQ(isolated.size(), 3u);
    EXPECT_FLOAT_EQ(isolated[0].value, 7);
    EXPECT_FLOAT_EQ(isolated[1].value, 14);
    // Hardware SE=1/local SA=0 maps to combined array 2, not array 1.
    const auto se1 =
        parser.parseExpression("select[select[TA_TA_BUSY, 2, axis=SA], 2:4, axis=INSTANCE]")->evaluate(context);
    EXPECT_EQ(se1.data(), (std::vector<float>{7, 14, 8, 16}));
    const auto sa1 =
        parser.parseExpression("select[select[TA_TA_BUSY, 1, axis=SA], 2:4, axis=INSTANCE]")->evaluate(context);
    EXPECT_EQ(sa1.data(), (std::vector<float>{103, 206, 104, 208}));
    const auto se1sa1 =
        parser.parseExpression("select[select[TA_TA_BUSY, 3, axis=SA], 2:4, axis=INSTANCE]")->evaluate(context);
    EXPECT_EQ(se1sa1.data(), (std::vector<float>{107, 214, 108, 216}));
}

TEST(SpmJson, Navi4PackedIdsMatchMetadataAndBatchBoundariesDoNotChangeResults)
{
    EditableSpmCapture capture;
    const auto reference = capture.load();
    for (auto& counter : capture.tool()["counters"]) counter.erase("instances");
    auto batch = capture.batches().front();
    auto& records = capture.batches().front()["records"];
    batch["records"] = nlohmann::json::array();
    while (records.size() > 10)
    {
        batch["records"].push_back(records.back());
        records.erase(records.size() - 1);
    }
    capture.batches().push_back(batch);
    const auto data = capture.load();
    EXPECT_EQ(data.timestamps, reference.timestamps);
    ASSERT_EQ(data.counters.size(), reference.counters.size());
    for (size_t i = 0; i < data.counters.size(); ++i)
    {
        EXPECT_EQ(data.counters[i].values, reference.counters[i].values);
        EXPECT_EQ(data.counters[i].valid, reference.counters[i].valid);
    }
}

TEST(SpmJson, SeOnlyAndSeSaInstanceCountersKeepTheirOwnIndexing)
{
    EditableSpmCapture capture;
    const auto source_counter = capture.tool()["counters"][0];
    const auto source_records = capture.batches().front()["records"];
    for (bool has_sa : {false, true})
    {
        const uint64_t id = has_sa ? 5 : 4;
        auto counter = source_counter;
        counter["name"] = has_sa ? "SA_INSTANCE" : "SE_INSTANCE";
        counter["id"]["handle"] = id;
        counter.erase("instances");
        auto& dimensions = counter["dimensions"];
        for (auto it = dimensions.begin(); it != dimensions.end();)
        {
            if (!has_sa && (*it)["name"] == "DIMENSION_SHADER_ARRAY")
                it = dimensions.erase(it);
            else
            {
                if ((*it)["name"] == "DIMENSION_WGP") (*it)["name"] = "DIMENSION_INSTANCE";
                ++it;
            }
        }
        capture.tool()["counters"].push_back(counter);
        for (auto record : source_records)
        {
            if (record["counter_id"]["handle"] != 1) continue;
            const uint64_t source_id = record["instance_id"];
            const uint64_t se = (source_id >> 12) & 0x3f;
            const uint64_t sa = (source_id >> 24) & 0x3f;
            const uint64_t instance = (source_id >> 30) & 0x3f;
            if (!has_sa && sa != 0) continue;
            record["counter_id"]["handle"] = id;
            record["instance_id"] = (id << 48) | (se << 12) | (sa << 24) | (instance << 36);
            capture.batches().front()["records"].push_back(record);
        }
    }
    const auto data = capture.load();
    const auto& se = findCounter(data, "SE_INSTANCE");
    const auto& sa = findCounter(data, "SA_INSTANCE");
    EXPECT_FALSE(se.has_sa);
    EXPECT_FALSE(se.has_wgp);
    EXPECT_EQ(se.arrays_per_se, 1u);
    EXPECT_EQ(se.se_count, 2u);
    EXPECT_TRUE(sa.has_sa);
    EXPECT_FALSE(sa.has_wgp);
    EXPECT_EQ(sa.se_count, 4u);
    DerivedCounter::CounterContext context;
    context.setCounter(se.name, std::make_shared<DerivedCounter::Tensor>(tensor(se, data.sample_count)));
    context.setCounter(sa.name, std::make_shared<DerivedCounter::Tensor>(tensor(sa, data.sample_count)));
    DerivedCounter::Parser parser;
    EXPECT_EQ(
        parser.parseExpression("select[select[SE_INSTANCE, 1, axis=SE], 1, axis=INSTANCE]")->evaluate(context).data(),
        (std::vector<float>{40, 80})
    );
    EXPECT_EQ(
        parser.parseExpression("select[select[SA_INSTANCE, 3, axis=SA], 1, axis=INSTANCE]")->evaluate(context).data(),
        (std::vector<float>{140, 280})
    );
}

TEST(SpmJson, NineWgpsPerArrayWorkWithMetadataAndPackedIds)
{
    EditableSpmCapture capture;
    auto& records = capture.batches().front()["records"];
    for (auto it = records.begin(); it != records.end();)
        if ((*it)["counter_id"]["handle"] != 3)
            it = records.erase(it);
        else
            ++it;

    for (auto& counter : capture.tool()["counters"])
    {
        const uint64_t id = counter["id"]["handle"];
        if (id == 3) continue;
        for (auto& dimension : counter["dimensions"])
            if (dimension["name"] == "DIMENSION_WGP") dimension["instance_size"] = 9;
        counter["instances"] = nlohmann::json::array();
        for (uint64_t se = 0; se < 2; ++se)
            for (uint64_t sa = 0; sa < 2; ++sa)
                for (uint64_t wgp = 0; wgp < 9; ++wgp)
                    for (uint64_t instance = 0; instance < (id == 2 ? 2u : 1u); ++instance)
                    {
                        const uint64_t iid = (id << 48) | (se << 12) | (sa << 24) | (wgp << 30) | (instance << 36);
                        auto dimensions = nlohmann::json::array({
                            {{"dimension_name", "DIMENSION_SHADER_ENGINE"}, {"index", se} },
                            {{"dimension_name", "DIMENSION_SHADER_ARRAY"},  {"index", sa} },
                            {{"dimension_name", "DIMENSION_WGP"},           {"index", wgp}}
                        });
                        if (id == 2)
                            dimensions.push_back({
                                {"dimension_name", "DIMENSION_INSTANCE"},
                                {"index",          instance            }
                            });
                        counter["instances"].push_back({
                            {"instance_id", iid       },
                            {"dimensions",  dimensions}
                        });
                        for (uint64_t sample = 0; sample < 2; ++sample)
                            records.push_back({
                                {"counter_id",  {{"handle", id}}                                               },
                                {"instance_id", iid                                                            },
                                {"timestamp",   1000 + 100 * sample                                            },
                                {"value",       (1000 * se + 100 * sa + 10 * wgp + instance + 1) * (sample + 1)}
                            });
                    }
    }
    const auto data = capture.load();
    const auto& ta = findCounter(data, "TA_TA_BUSY");
    EXPECT_EQ(ta.instance_count, 18u);
    const auto values = tensor(ta, data.sample_count);
    EXPECT_EQ(
        values.select(3, DerivedCounter::Axis::SA).selectRange(16, 18, 1, DerivedCounter::Axis::CU).data(),
        (std::vector<float>{1181, 2362, 1182, 2364})
    );
    EXPECT_EQ(
        values.sum({DerivedCounter::Axis::SA, DerivedCounter::Axis::CU}).data(), (std::vector<float>{42588, 85176})
    );
    for (auto& counter : capture.tool()["counters"]) counter.erase("instances");
    const auto packed = capture.load();
    for (size_t i = 0; i < data.counters.size(); ++i)
    {
        EXPECT_EQ(packed.counters[i].values, data.counters[i].values);
        EXPECT_EQ(packed.counters[i].valid, data.counters[i].valid);
    }
}

TEST(SpmJson, Navi4SupportsExplicitXccWithIndependentSampleCounts)
{
    EditableSpmCapture capture;
    for (auto& counter : capture.tool()["counters"])
    {
        counter.erase("instances");
        counter["dimensions"].push_back({
            {"name",          "DIMENSION_XCC"},
            {"instance_size", 2              }
        });
    }
    auto batch = capture.batches().front();
    batch["records"] = nlohmann::json::array();
    for (auto record : capture.batches().front()["records"])
    {
        if (record["timestamp"] != 1000) continue;
        record["instance_id"] = record["instance_id"].get<uint64_t>() | 1;
        record["timestamp"] = 1050;
        record["value"] = record["value"].get<float>() * 10;
        batch["records"].push_back(record);
    }
    capture.batches().push_back(batch);
    const auto data = capture.load();
    EXPECT_EQ(data.xccCount(), 2u);
    EXPECT_EQ(data.validSamples(0), 2u);
    EXPECT_EQ(data.validSamples(1), 1u);
    EXPECT_EQ(data.timestamps, (std::vector<uint64_t>{1000, 1100, 1050, 1050}));
    const auto waves = tensor(findCounter(data, "SQ_WAVES"), data.sample_count);
    EXPECT_FLOAT_EQ(waves.at(1, 0, 1, 0), 200);
    EXPECT_FALSE(waves.isValid(waves.linearIndex(1, 0, 1, 1)));
    const auto ta = tensor(findCounter(data, "TA_TA_BUSY"), data.sample_count);
    EXPECT_FLOAT_EQ(ta.at(1, 0, 2, 0), 30);
    EXPECT_FLOAT_EQ(ta.at(1, 0, 3, 0), 40);
    EXPECT_FALSE(ta.isValid(ta.linearIndex(1, 0, 3, 1)));
    const DerivedCounter::Tensor clock({2, 1, 1, data.sample_count}, data.clock, data.sample_valid);
    const auto selected = SpmSeries::makePlotSeries(waves.select(1, DerivedCounter::Axis::XCC), clock);
    ASSERT_FALSE(selected.empty());
    EXPECT_FLOAT_EQ(selected.front().time, 50);
    EXPECT_FLOAT_EQ(selected.front().value, 6000);
}

TEST(SpmJson, Navi4InvalidLocalInstanceLeavesOtherInstancesAndZeroValid)
{
    EditableSpmCapture capture;
    auto& records = capture.batches().front()["records"];
    for (auto& record : records)
        if (record["counter_id"]["handle"] == 2 && record["timestamp"] == 1000 &&
            record["instance_id"].get<uint64_t>() == ((uint64_t{2} << 48) | (uint64_t{1} << 30)))
            record["value"] = static_cast<double>(UINT64_MAX);
    records.front()["value"] = 0;
    const auto data = capture.load();
    const auto ta = tensor(findCounter(data, "TA_TA_BUSY"), data.sample_count);
    EXPECT_FALSE(ta.isValid(ta.linearIndex(0, 0, 2, 0)));
    EXPECT_TRUE(ta.isValid(ta.linearIndex(0, 0, 2, 1)));
    EXPECT_FLOAT_EQ(ta.at(0, 0, 2, 1), 6);
    EXPECT_TRUE(ta.isValid(ta.linearIndex(0, 0, 3, 0)));
    EXPECT_FLOAT_EQ(ta.at(0, 0, 3, 0), 4);
    const auto waves = tensor(findCounter(data, "SQ_WAVES"), data.sample_count);
    EXPECT_TRUE(waves.isValid(0));
    EXPECT_FLOAT_EQ(waves[0], 0);
}

TEST(SpmJson, RejectsAmbiguousCapturesDuplicateRecordsAndInvalidDimensions)
{
    EditableSpmCapture capture;
    auto duplicate = capture.batches().front();
    duplicate["dispatch_data"]["dispatch_info"]["dispatch_id"] = 3;
    capture.batches().push_back(duplicate);
    EXPECT_THROW(capture.load(), std::runtime_error);
    capture.batches().back()["dispatch_data"]["dispatch_info"]["dispatch_id"] = 2;
    capture.batches().back()["stream_id"]["handle"] = 1;
    EXPECT_THROW(capture.load(), std::runtime_error);
    capture.batches().back()["stream_id"]["handle"] = 0;
    EXPECT_THROW(capture.load(), std::runtime_error);
    capture.batches().erase(1);
    capture.tool()["counters"][0]["dimensions"].push_back({
        {"name",          "DIMENSION_AID"},
        {"instance_size", 2              }
    });
    EXPECT_THROW(capture.load(), std::runtime_error);
    capture.tool()["counters"][0]["dimensions"].erase(3);
    capture.tool()["counters"][0]["dimensions"][2]["instance_size"] = 1;
    EXPECT_THROW(capture.load(), std::runtime_error);
}

TEST(SpmJson, Navi4ClockNeedsTwoAnchorsWithoutSqCycles)
{
    SpmData data = loadSpmJson(SPM_NAVI4_TEST_DATA);
    const auto original_clock = data.clock;
    EXPECT_FALSE(alignSpmClock(
        data,
        {
            {0, 2000, 1000}
    }
    ));
    EXPECT_FALSE(alignSpmClock(
        data,
        {
            {0, 2000, 1000},
            {1, 2400, 1100}
    }
    ));
    EXPECT_EQ(data.clock, original_clock);
    EXPECT_FALSE(data.clock_aligned);
    ASSERT_TRUE(alignSpmClock(
        data,
        {
            {0, 2000, 1000},
            {0, 2400, 1100}
    }
    ));
    EXPECT_EQ(data.clock, (std::vector<float>{2000, 2400}));
    EXPECT_TRUE(data.clock_aligned);
}

TEST(SpmJson, WithoutSqCyclesUnrelatedRealtimeKeepsRelativePlotTimes)
{
    EditableSpmCapture capture;
    // Timestamp endpoints from spm2, whose SQTT REALTIME range is unrelated
    // to the SPM timestamp range despite belonging to the same dispatch.
    for (auto& batch : capture.batches())
        for (auto& record : batch["records"])
            record["timestamp"] = record["timestamp"] == 1000 ? uint64_t{4234154057} : uint64_t{4251141354};
    auto data = capture.load();
    const auto relative_clock = data.clock;
    const std::vector<SpmClockAnchor> anchors{
        {0, 26628,    707605054601},
        {0, 16936092, 707606105409}
    };

    EXPECT_FALSE(spmClockRangesOverlap(data, anchors));
    EXPECT_FALSE(alignSpmClock(data, anchors));
    EXPECT_FALSE(data.clock_aligned);
    EXPECT_EQ(data.clock, relative_clock);

    const DerivedCounter::Tensor clock({1, 1, 1, data.sample_count}, data.clock, data.sample_valid);
    for (const auto& counter : data.counters)
    {
        const auto points = SpmSeries::makePlotSeries(tensor(counter, data.sample_count), clock);
        ASSERT_EQ(points.size(), 3u);
        EXPECT_FLOAT_EQ(points.back().time, relative_clock.back());
        EXPECT_GT(points.back().time, 0);
    }
}

TEST(SpmJson, Navi4DoesNotInventWindowsFromChangingSqCycles)
{
    EditableSpmCapture capture;
    capture.tool()["counters"][0]["name"] = "SQ_CYCLES";
    auto third = capture.batches().front();
    third["records"] = nlohmann::json::array();
    for (auto& record : capture.batches().front()["records"])
    {
        if (record["counter_id"]["handle"] == 1) record["value"] = 10;
        if (record["timestamp"] != 1100) continue;
        auto last = record;
        last["timestamp"] = 1200;
        if (last["counter_id"]["handle"] == 1) last["value"] = 30;
        third["records"].push_back(last);
    }
    capture.batches().push_back(third);
    auto data = capture.load();
    EXPECT_EQ(data.timestamps, (std::vector<uint64_t>{1000, 1100, 1200}));
    ASSERT_TRUE(alignSpmClock(
        data,
        {
            {0, 2000, 1000}
    }
    ));
    // SQ_CYCLES is averaged over WGPs in the anchor SE, not spatially summed.
    EXPECT_EQ(data.clock, (std::vector<float>{2000, 2010, 2040}));
}

TEST(SpmJson, ClockAnchorHardwareSeMapsToCombinedArrayIndex)
{
    EditableSpmCapture capture;
    capture.tool()["counters"][0]["name"] = "SQ_CYCLES";
    for (auto& record : capture.batches().front()["records"])
        if (record["counter_id"]["handle"] == 1)
            record["value"] = ((record["instance_id"].get<uint64_t>() >> 12) & 0x3f) ? 70 : 10;
    auto data = capture.load();
    ASSERT_TRUE(alignSpmClock(
        data,
        {
            {1, 2000, 1000}
    }
    ));
    EXPECT_EQ(data.clock, (std::vector<float>{2000, 2070}));
}
