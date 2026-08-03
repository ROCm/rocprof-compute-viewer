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

#include "spm_json.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>

#include "json/include/nlohmann/json.hpp"

namespace
{
using json = nlohmann::json;

struct Coordinate
{
    size_t xcc = 0;
    size_t se = 0;
    size_t instance = 0;
};

struct CounterMetadata
{
    std::string name;
    size_t xcc_count = 1;
    size_t se_count = 1;
    size_t instance_count = 1;
    std::unordered_map<uint64_t, Coordinate> instances;
};

struct Sample
{
    uint64_t counter = 0;
    uint64_t timestamp = 0;
    Coordinate coordinate;
    float value = 0;
};

Coordinate parseCoordinate(const json& dimensions)
{
    Coordinate result;
    for (const auto& dimension : dimensions)
    {
        const std::string name = dimension.at("dimension_name");
        const size_t index = dimension.at("index");
        if (name == "DIMENSION_XCC")
            result.xcc = index;
        else if (name == "DIMENSION_SHADER_ENGINE")
            result.se = index;
        else if (name == "DIMENSION_INSTANCE")
            result.instance = index;
    }
    return result;
}

Coordinate decodeCoordinate(uint64_t instance_id)
{
    return {
        static_cast<size_t>(instance_id & 0x3f),
        static_cast<size_t>((instance_id >> 12) & 0x3f),
        static_cast<size_t>((instance_id >> 36) & 0x3ff),
    };
}

size_t flattenedIndex(
    size_t xcc, size_t se, size_t instance, size_t sample, const SpmCounterData& counter, size_t sample_count
)
{
    return ((xcc * counter.se_count + se) * counter.instance_count + instance) * sample_count + sample;
}

long double timestampDelta(uint64_t lhs, uint64_t rhs)
{
    return lhs >= rhs ? static_cast<long double>(lhs - rhs) : -static_cast<long double>(rhs - lhs);
}

double counterValue(const SpmCounterData& counter, size_t xcc, size_t se, size_t sample, size_t sample_count)
{
    auto averageAtSe = [&](size_t selected_se)
    {
        double total = 0;
        size_t count = 0;
        for (size_t instance = 0; instance < counter.instance_count; ++instance)
        {
            const float value =
                counter.values.at(flattenedIndex(xcc, selected_se, instance, sample, counter, sample_count));
            if (value <= 0 || !std::isfinite(value)) continue;
            total += value;
            count++;
        }
        return count ? total / count : 0.0;
    };

    if (se < counter.se_count)
        if (double value = averageAtSe(se); value > 0) return value;

    double total = 0;
    size_t count = 0;
    for (size_t selected_se = 0; selected_se < counter.se_count; ++selected_se)
    {
        double value = averageAtSe(selected_se);
        if (value <= 0) continue;
        total += value;
        count++;
    }
    return count ? total / count : 0.0;
}
} // namespace

SpmData loadSpmJson(const std::string& path)
{
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Unable to open SPM JSON: " + path);

    json root;
    input >> root;
    const auto& tools = root.at("rocprofiler-sdk-tool");
    if (!tools.is_array() || tools.empty()) throw std::runtime_error("SPM JSON has no rocprofiler-sdk-tool data");

    const auto& tool = tools.front();
    const auto& collections = tool.at("callback_records").at("spm_counter_collection");
    if (!collections.is_array() || collections.empty()) throw std::runtime_error("SPM JSON has no SPM samples");

    const uint64_t agent = collections.front().at("dispatch_data").at("dispatch_info").at("agent_id").at("handle");

    std::map<uint64_t, CounterMetadata> metadata;
    size_t metadata_xcc_count = 1;
    for (const auto& counter : tool.at("counters"))
    {
        if (counter.at("agent_id").at("handle").get<uint64_t>() != agent) continue;

        CounterMetadata info;
        info.name = counter.at("name");
        for (const auto& dimension : counter.value("dimensions", json::array()))
        {
            const std::string name = dimension.at("name");
            const size_t size = dimension.at("instance_size");
            if (name == "DIMENSION_XCC")
                info.xcc_count = size;
            else if (name == "DIMENSION_SHADER_ENGINE")
                info.se_count = size;
            else if (name == "DIMENSION_INSTANCE")
                info.instance_count = size;
        }
        for (const auto& instance : counter.value("instances", json::array()))
            info.instances.emplace(
                instance.at("instance_id").get<uint64_t>(), parseCoordinate(instance.at("dimensions"))
            );

        metadata_xcc_count = std::max(metadata_xcc_count, info.xcc_count);
        metadata.emplace(counter.at("id").at("handle").get<uint64_t>(), std::move(info));
    }

    uint64_t minimum_timestamp = std::numeric_limits<uint64_t>::max();
    size_t xcc_count = metadata_xcc_count;
    std::map<size_t, std::set<uint64_t>> timestamps;
    std::set<uint64_t> active_counters;
    std::vector<Sample> samples;

    for (const auto& collection : collections)
    {
        const uint64_t collection_agent =
            collection.at("dispatch_data").at("dispatch_info").at("agent_id").at("handle");
        if (collection_agent != agent)
            throw std::runtime_error("SPM JSON contains samples from more than one GPU agent");

        for (const auto& record : collection.at("records"))
        {
            const uint64_t counter_id = record.at("counter_id").at("handle");
            auto metadata_it = metadata.find(counter_id);
            if (metadata_it == metadata.end()) throw std::runtime_error("SPM sample references an unknown counter");

            const uint64_t instance_id = record.at("instance_id").get<uint64_t>();
            auto instance_it = metadata_it->second.instances.find(instance_id);
            Coordinate coordinate = instance_it == metadata_it->second.instances.end() ? decodeCoordinate(instance_id)
                                                                                       : instance_it->second;
            const uint64_t timestamp = record.at("timestamp").get<uint64_t>();

            minimum_timestamp = std::min(minimum_timestamp, timestamp);
            xcc_count = std::max(xcc_count, coordinate.xcc + 1);
            metadata_it->second.se_count = std::max(metadata_it->second.se_count, coordinate.se + 1);
            metadata_it->second.instance_count = std::max(metadata_it->second.instance_count, coordinate.instance + 1);
            timestamps[coordinate.xcc].insert(timestamp);
            active_counters.insert(counter_id);
            samples.push_back({counter_id, timestamp, coordinate, record.at("value").get<float>()});
        }
    }
    if (samples.empty()) throw std::runtime_error("SPM JSON has no counter values");

    SpmData result;
    for (const auto& [xcc, values] : timestamps) result.sample_count = std::max(result.sample_count, values.size());
    result.sample_counts.resize(xcc_count, 0);
    result.timestamps.resize(xcc_count * result.sample_count, 0);
    result.clock.resize(xcc_count * result.sample_count, 0);

    std::vector<std::unordered_map<uint64_t, size_t>> sample_indices(xcc_count);
    for (size_t xcc = 0; xcc < xcc_count; ++xcc)
    {
        auto timestamp_it = timestamps.find(xcc);
        if (timestamp_it == timestamps.end()) continue;

        size_t sample = 0;
        float last_clock = 0;
        uint64_t last_timestamp = 0;
        for (uint64_t timestamp : timestamp_it->second)
        {
            sample_indices[xcc].emplace(timestamp, sample);
            last_clock = static_cast<float>(timestamp - minimum_timestamp);
            last_timestamp = timestamp;
            result.timestamps[xcc * result.sample_count + sample] = timestamp;
            result.clock[xcc * result.sample_count + sample++] = last_clock;
        }
        result.sample_counts[xcc] = sample;
        while (sample < result.sample_count)
        {
            result.timestamps[xcc * result.sample_count + sample] = last_timestamp;
            result.clock[xcc * result.sample_count + sample++] = last_clock;
        }
    }

    std::unordered_map<uint64_t, size_t> counter_indices;
    for (auto& [counter_id, info] : metadata)
    {
        if (!active_counters.count(counter_id)) continue;

        SpmCounterData counter;
        counter.name = info.name;
        counter.xcc_count = xcc_count;
        counter.se_count = info.se_count;
        counter.instance_count = info.instance_count;
        counter.values.resize(counter.xcc_count * counter.se_count * counter.instance_count * result.sample_count, 0);
        counter_indices.emplace(counter_id, result.counters.size());
        result.counters.push_back(std::move(counter));
    }

    for (const auto& sample : samples)
    {
        auto& counter = result.counters.at(counter_indices.at(sample.counter));
        const size_t sample_index = sample_indices.at(sample.coordinate.xcc).at(sample.timestamp);
        counter.values.at(flattenedIndex(
            sample.coordinate.xcc,
            sample.coordinate.se,
            sample.coordinate.instance,
            sample_index,
            counter,
            result.sample_count
        )) += sample.value;
    }

    return result;
}

bool alignSpmClock(SpmData& spm, const std::map<int, std::vector<realtime_record_t>>& realtime_by_se)
{
    if (spm.empty() || spm.sample_count == 0 || realtime_by_se.empty()) return false;

    int anchor_se = -1;
    const realtime_record_t* first = nullptr;
    for (const auto& [se, records] : realtime_by_se)
        for (const auto& record : records)
            if (!first || record.realtime_clock < first->realtime_clock)
            {
                first = &record;
                anchor_se = se;
            }
    if (!first) return false;

    const realtime_record_t* last = first;
    auto anchor_se_it = realtime_by_se.find(anchor_se);
    if (anchor_se_it != realtime_by_se.end())
        for (const auto& record : anchor_se_it->second)
            if (record.realtime_clock > last->realtime_clock) last = &record;
    if (last == first)
        for (const auto& [_, records] : realtime_by_se)
            for (const auto& record : records)
                if (record.realtime_clock > last->realtime_clock) last = &record;

    const bool has_linear_range =
        last->realtime_clock > first->realtime_clock && last->shader_clock > first->shader_clock;
    const long double linear_slope = has_linear_range
                                       ? static_cast<long double>(last->shader_clock - first->shader_clock) /
                                             static_cast<long double>(last->realtime_clock - first->realtime_clock)
                                       : 0.0L;

    const SpmCounterData* sq_cycles = nullptr;
    for (const auto& counter : spm.counters)
        if (counter.name == "SQ_CYCLES")
        {
            sq_cycles = &counter;
            break;
        }

    std::vector<float> aligned_clock = spm.clock;
    for (size_t xcc = 0; xcc < spm.sample_counts.size(); ++xcc)
    {
        const size_t count = spm.sample_counts[xcc];
        if (count == 0) continue;

        auto timestampAt = [&](size_t sample) { return spm.timestamps.at(xcc * spm.sample_count + sample); };
        std::vector<double> interval_cycles(count, std::numeric_limits<double>::quiet_NaN());
        double local_rate_sum = 0;
        size_t local_rate_count = 0;

        for (size_t sample = 1; sample < count; ++sample)
        {
            const uint64_t delta_timestamp = timestampAt(sample) - timestampAt(sample - 1);
            // SQ_CYCLES at a sample measures the interval ending at that sample.
            double cycles = sq_cycles ? counterValue(*sq_cycles, xcc, anchor_se, sample, spm.sample_count) : 0.0;
            if (cycles > 0)
            {
                interval_cycles[sample] = cycles;
                if (delta_timestamp > 0)
                {
                    local_rate_sum += cycles / delta_timestamp;
                    local_rate_count++;
                }
            }
            else if (has_linear_range)
                interval_cycles[sample] = static_cast<double>(linear_slope * delta_timestamp);
        }

        const double local_rate = local_rate_count ? local_rate_sum / local_rate_count : 0.0;
        for (size_t sample = 1; sample < count; ++sample)
        {
            if (std::isfinite(interval_cycles[sample])) continue;
            const uint64_t delta_timestamp = timestampAt(sample) - timestampAt(sample - 1);
            if (local_rate > 0)
                interval_cycles[sample] = local_rate * delta_timestamp;
            else
                return false;
        }

        const uint64_t* timestamps = spm.timestamps.data() + xcc * spm.sample_count;
        size_t upper = std::lower_bound(timestamps, timestamps + count, first->realtime_clock) - timestamps;
        size_t pivot = 0;
        double pivot_clock = 0;

        if (upper < count && timestamps[upper] == first->realtime_clock)
        {
            pivot = upper;
            pivot_clock = first->shader_clock;
        }
        else if (upper > 0 && upper < count)
        {
            const uint64_t lower_timestamp = timestamps[upper - 1];
            const uint64_t upper_timestamp = timestamps[upper];
            const long double fraction = static_cast<long double>(upper_timestamp - first->realtime_clock) /
                                         static_cast<long double>(upper_timestamp - lower_timestamp);
            pivot = upper;
            pivot_clock = first->shader_clock + static_cast<double>(fraction * interval_cycles[upper]);
        }
        else
        {
            pivot = upper == 0 ? 0 : count - 1;
            long double rate = has_linear_range ? linear_slope : local_rate;
            if (rate <= 0) return false;
            pivot_clock = first->shader_clock +
                          static_cast<double>(timestampDelta(timestamps[pivot], first->realtime_clock) * rate);
        }

        std::vector<double> xcc_clock(count);
        xcc_clock[pivot] = pivot_clock;
        for (size_t sample = pivot; sample > 0; --sample)
            xcc_clock[sample - 1] = xcc_clock[sample] - interval_cycles[sample];
        for (size_t sample = pivot + 1; sample < count; ++sample)
            xcc_clock[sample] = xcc_clock[sample - 1] + interval_cycles[sample];

        for (size_t sample = 0; sample < count; ++sample)
            aligned_clock[xcc * spm.sample_count + sample] = static_cast<float>(xcc_clock[sample]);
        for (size_t sample = count; sample < spm.sample_count; ++sample)
            aligned_clock[xcc * spm.sample_count + sample] = static_cast<float>(xcc_clock.back());
    }

    spm.clock = std::move(aligned_clock);
    return true;
}
