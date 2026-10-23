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
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

#include "json/include/nlohmann/json.hpp"

namespace
{
using json = nlohmann::json;

struct Coordinate
{
    size_t xcc = 0;
    size_t se = 0;
    size_t sa = 0;
    size_t wgp = 0;
    size_t instance = 0;
    // Nonzero index in a dimension without a tensor axis, such as AID or AGENT.
    bool folded = false;
};

struct Dimension
{
    const char* name;
    size_t Coordinate::*field;
    unsigned shift;
    size_t mask;
};

// SDK counters/id_decode.hpp. The high 16 bits hold the counter identity.
constexpr Dimension dimensions[] = {
    {"DIMENSION_XCC",           &Coordinate::xcc,      0,  0x3f },
    {"DIMENSION_SHADER_ENGINE", &Coordinate::se,       12, 0x3f },
    {"DIMENSION_SHADER_ARRAY",  &Coordinate::sa,       24, 0x3f },
    {"DIMENSION_WGP",           &Coordinate::wgp,      30, 0x3f },
    {"DIMENSION_INSTANCE",      &Coordinate::instance, 36, 0x3ff},
};

const Dimension* findDimension(const std::string& name)
{
    for (const auto& dimension : dimensions)
        if (name == dimension.name) return &dimension;
    return nullptr;
}

struct ParsedCounter
{
    SpmCounterData output;
    Coordinate extents{1, 1, 1, 1, 1};
    // Without dimension metadata, extents come from the records alone.
    bool declared_dimensions = false;
    std::unordered_map<uint64_t, Coordinate> instances;
    size_t output_index = std::numeric_limits<size_t>::max();
    bool active = false;
};

struct ParsedRecord
{
    uint64_t counter_id = 0;
    ParsedCounter* counter = nullptr;
    Coordinate coordinate;
    // Distinguishes instances that differ only in folded dimensions.
    uint64_t fold_key = 0;
    float value = 0;
};

struct SampleGroup
{
    std::vector<size_t> indices;
    double cycles = 0;
    size_t cycle_count = 0;
};

struct TimelineSample
{
    uint64_t timestamp = 0;
    const SampleGroup* group = nullptr;
    size_t windows = 1;
};

using GroupedSamples = std::map<size_t, std::map<uint64_t, SampleGroup>>;
using Timelines = std::vector<std::vector<TimelineSample>>;

Coordinate parseCoordinate(const json& coordinates)
{
    Coordinate result;
    for (const auto& dimension : coordinates)
    {
        const std::string name = dimension.at("dimension_name");
        const size_t index = dimension.at("index");
        if (const auto* definition = findDimension(name))
        {
            // The packed ID limit also bounds the tensor allocation.
            if (index > definition->mask) throw std::runtime_error("SPM coordinate out of range: " + name);
            result.*(definition->field) = index;
        }
        else if (index != 0)
            result.folded = true;
    }
    return result;
}

Coordinate decodeCoordinate(uint64_t instance_id)
{
    Coordinate result;
    for (const auto& dimension : dimensions)
        result.*(dimension.field) = (instance_id >> dimension.shift) & dimension.mask;
    // AID and AGENT have no corresponding tensor axes.
    result.folded = ((instance_id >> 6) & 0x3f) || ((instance_id >> 18) & 0x3f);
    return result;
}

// The SDK derives dimension metadata and SPM instance IDs separately, so
// records may exceed the declared extents. Returns whether a spatial extent grew.
bool growExtents(const Coordinate& coordinate, ParsedCounter& counter)
{
    bool grown = false;
    for (const auto& dimension : dimensions)
    {
        auto& extent = counter.extents.*(dimension.field);
        if (coordinate.*(dimension.field) < extent) continue;
        extent = coordinate.*(dimension.field) + 1;
        // XCC is a global axis sized from all records.
        grown |= dimension.field != &Coordinate::xcc;
    }
    return grown;
}

size_t flattenedIndex(
    size_t xcc, size_t array, size_t instance, size_t sample, const SpmCounterData& counter, size_t sample_count
)
{
    return ((xcc * counter.se_count + array) * counter.instance_count + instance) * sample_count + sample;
}

size_t nearestTimestamp(const uint64_t* timestamps, size_t count, uint64_t target)
{
    const size_t upper = std::lower_bound(timestamps, timestamps + count, target) - timestamps;
    if (upper == 0) return 0;
    if (upper == count) return count - 1;

    const uint64_t before = target - timestamps[upper - 1];
    const uint64_t after = timestamps[upper] - target;
    return before <= after ? upper - 1 : upper;
}

long double signedTimestampDelta(uint64_t lhs, uint64_t rhs)
{
    return lhs >= rhs ? static_cast<long double>(lhs - rhs) : -static_cast<long double>(rhs - lhs);
}

// Sorts anchors by shader clock, keeping the first anchor for each clock value.
void sortUniqueShaderClocks(std::vector<SpmClockAnchor>& anchors)
{
    std::stable_sort(
        anchors.begin(), anchors.end(), [](const auto& a, const auto& b) { return a.shader_clock < b.shader_clock; }
    );
    anchors.erase(
        std::unique(
            anchors.begin(),
            anchors.end(),
            [](const auto& a, const auto& b) { return a.shader_clock == b.shader_clock; }
        ),
        anchors.end()
    );
}

// Keeps the longest subsequence of anchors, sorted by unique shader clock, whose
// realtime clock also increases. A single inconsistent REALTIME record is dropped
// instead of reordering every sample match that depends on it.
void keepMonotonicAnchors(std::vector<SpmClockAnchor>& anchors)
{
    constexpr size_t none = std::numeric_limits<size_t>::max();
    std::vector<size_t> tails;
    std::vector<size_t> previous(anchors.size(), none);
    for (size_t i = 0; i < anchors.size(); ++i)
    {
        auto it = std::lower_bound(
            tails.begin(),
            tails.end(),
            anchors[i].realtime_clock,
            [&](size_t tail, uint64_t clock) { return anchors[tail].realtime_clock < clock; }
        );
        if (it != tails.begin()) previous[i] = *std::prev(it);
        if (it == tails.end())
            tails.push_back(i);
        else
            *it = i;
    }
    std::vector<SpmClockAnchor> kept(tails.size());
    for (size_t i = tails.empty() ? none : tails.back(), k = tails.size(); k > 0; i = previous[i])
        kept[--k] = anchors[i];
    anchors = std::move(kept);
}

double counterValue(const SpmCounterData& counter, size_t xcc, size_t se, size_t sample, size_t sample_count)
{
    auto averageAtSe = [&](size_t selected_se)
    {
        double total = 0;
        size_t count = 0;
        for (size_t sa = 0; sa < counter.arrays_per_se; ++sa)
            for (size_t instance = 0; instance < counter.instance_count; ++instance)
            {
                const size_t array = selected_se * counter.arrays_per_se + sa;
                const size_t index = flattenedIndex(xcc, array, instance, sample, counter, sample_count);
                const float value = counter.values.at(index);
                if (!Validity::test(counter.valid, index) || value <= 0 || !std::isfinite(value)) continue;
                total += value;
                count++;
            }
        return count ? total / count : 0.0;
    };

    const size_t hardware_se_count = counter.se_count / counter.arrays_per_se;
    if (se < hardware_se_count)
        if (double value = averageAtSe(se); value > 0) return value;

    double total = 0;
    size_t count = 0;
    for (size_t selected_se = 0; selected_se < hardware_se_count; ++selected_se)
    {
        double value = averageAtSe(selected_se);
        if (value <= 0) continue;
        total += value;
        count++;
    }
    return count ? total / count : 0.0;
}

Timelines buildTimelines(const GroupedSamples& groups, size_t xcc_count, bool reconstruct_windows)
{
    Timelines timelines(xcc_count);
    for (const auto& [xcc, xcc_groups] : groups)
    {
        std::vector<double> cycle_values;
        for (const auto& [timestamp, group] : xcc_groups)
        {
            (void) timestamp;
            if (group.cycle_count) cycle_values.push_back(group.cycles / group.cycle_count);
        }
        double nominal_cycles = 0;
        if (!cycle_values.empty())
        {
            const auto middle = cycle_values.begin() + cycle_values.size() / 2;
            std::nth_element(cycle_values.begin(), middle, cycle_values.end());
            nominal_cycles = *middle;
        }

        uint64_t previous_timestamp = 0;
        bool has_previous = false;
        for (const auto& [timestamp, group] : xcc_groups)
        {
            size_t windows = 1;
            if (reconstruct_windows && has_previous && nominal_cycles > 0 && group.cycle_count)
            {
                const double sample_cycles = group.cycles / group.cycle_count;
                const double ratio = sample_cycles / nominal_cycles;
                const size_t rounded = static_cast<size_t>(std::llround(ratio));
                if (rounded > 1 && std::abs(ratio - rounded) < 0.2) windows = rounded;
            }

            for (size_t window = 1; window <= windows; ++window)
            {
                const uint64_t expanded_timestamp =
                    windows == 1
                        ? timestamp
                        : previous_timestamp +
                              static_cast<uint64_t>(
                                  std::llround(static_cast<double>(timestamp - previous_timestamp) * window / windows)
                              );
                timelines[xcc].push_back({expanded_timestamp, &group, windows});
            }
            previous_timestamp = timestamp;
            has_previous = true;
        }
    }
    return timelines;
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

    std::vector<std::string> warnings;
    auto warn = [&](std::string message)
    {
        if (std::find(warnings.begin(), warnings.end(), message) == warnings.end())
            warnings.push_back(std::move(message));
    };
    if (tools.size() > 1)
        warn("SPM JSON contains " + std::to_string(tools.size()) + " tool captures; only the first was loaded.");

    const auto& tool = tools.front();
    const auto& collections = tool.at("callback_records").at("spm_counter_collection");
    if (!collections.is_array() || collections.empty()) throw std::runtime_error("SPM JSON has no SPM samples");
    const auto first = std::find_if(
        collections.begin(), collections.end(), [](const json& collection) { return !collection.at("records").empty(); }
    );
    if (first == collections.end()) throw std::runtime_error("SPM JSON has no counter values");
    const auto& dispatch_info = first->at("dispatch_data").at("dispatch_info");
    const uint64_t agent = dispatch_info.at("agent_id").at("handle");
    const uint64_t dispatch = dispatch_info.value("dispatch_id", uint64_t{0});
    const uint64_t stream = first->value("stream_id", json::object()).value("handle", uint64_t{0});
    std::string gfx_arch;
    for (const auto& gpu : tool.value("agents", json::array()))
        if (gpu.at("id").at("handle").get<uint64_t>() == agent) gfx_arch = gpu.value("name", "");
    // The current JSON may omit the sampling unit. On Navi4, SQ_CYCLES alone
    // cannot distinguish refclk frequency changes from lost sampling windows.
    bool reconstruct_windows = !gfx_arch.starts_with("gfx12");
    const auto config = tool.value("metadata", json::object()).value("config", json::object());
    if (config.value("spm_sample_interval_unit", "") == "refclk_cycles") reconstruct_windows = false;

    std::map<uint64_t, ParsedCounter> metadata;
    size_t metadata_xcc_count = 1;
    for (const auto& counter : tool.at("counters"))
    {
        if (counter.at("agent_id").at("handle").get<uint64_t>() != agent) continue;

        ParsedCounter info;
        info.output.name = counter.at("name");
        info.declared_dimensions = counter.contains("dimensions");
        for (const auto& dimension : counter.value("dimensions", json::array()))
        {
            const std::string name = dimension.at("name");
            const size_t size = dimension.at("instance_size");
            if (const auto* definition = findDimension(name))
            {
                if (size > definition->mask + 1) throw std::runtime_error("Invalid SPM dimension size: " + name);
                // An empty extent is left to the records.
                if (size > 0) info.extents.*(definition->field) = size;
                if (definition->field == &Coordinate::wgp) reconstruct_windows = false;
            }
            else if (size > 1)
                warn("SPM counter " + info.output.name + " has unsupported dimension " + name + "; it was summed.");
        }
        for (const auto& instance : counter.value("instances", json::array()))
        {
            const Coordinate coordinate = parseCoordinate(instance.at("dimensions"));
            if (growExtents(coordinate, info) && info.declared_dimensions)
                warn("SPM counter " + info.output.name + " has instances outside its declared dimensions.");
            info.instances.emplace(instance.at("instance_id").get<uint64_t>(), coordinate);
        }

        metadata_xcc_count = std::max(metadata_xcc_count, info.extents.xcc);
        metadata.emplace(counter.at("id").at("handle").get<uint64_t>(), std::move(info));
    }

    uint64_t minimum_timestamp = std::numeric_limits<uint64_t>::max();
    size_t xcc_count = metadata_xcc_count;
    GroupedSamples groups;
    std::vector<ParsedRecord> records;
    size_t total_records = 0;
    for (const auto& collection : collections) total_records += collection.at("records").size();
    records.reserve(total_records);

    for (const auto& collection : collections)
    {
        if (collection.at("records").empty()) continue;
        const auto& info = collection.at("dispatch_data").at("dispatch_info");
        const uint64_t collection_agent = info.at("agent_id").at("handle");
        if (collection_agent != agent)
            throw std::runtime_error("SPM JSON contains samples from more than one GPU agent");
        // TODO(SPM): Partition or select SPM data by dispatch and stream.
        if (info.value("dispatch_id", uint64_t{0}) != dispatch ||
            collection.value("stream_id", json::object()).value("handle", uint64_t{0}) != stream)
            warn("SPM JSON contains multiple dispatches or streams; their samples were merged.");

        for (const auto& record : collection.at("records"))
        {
            const uint64_t counter_id = record.at("counter_id").at("handle");
            auto metadata_it = metadata.find(counter_id);
            if (metadata_it == metadata.end()) throw std::runtime_error("SPM sample references an unknown counter");

            const uint64_t instance_id = record.at("instance_id").get<uint64_t>();
            auto& counter = metadata_it->second;
            auto instance_it = counter.instances.find(instance_id);
            Coordinate coordinate =
                instance_it == counter.instances.end() ? decodeCoordinate(instance_id) : instance_it->second;
            if (growExtents(coordinate, counter) && counter.declared_dimensions)
                warn("SPM counter " + counter.output.name + " has records outside its declared dimensions.");
            if (coordinate.folded)
                warn("SPM counter " + counter.output.name + " was summed over dimensions without a viewer axis.");
            const uint64_t timestamp = record.at("timestamp").get<uint64_t>();
            const double raw_value = record.at("value").get<double>();
            // The decoder's invalid uint64_t(-1) sentinel rounds to 2^64 as a double.
            const float value =
                std::isfinite(raw_value) && raw_value >= 0 && raw_value < static_cast<double>(UINT64_MAX)
                    ? static_cast<float>(raw_value)
                    : std::numeric_limits<float>::quiet_NaN();

            minimum_timestamp = std::min(minimum_timestamp, timestamp);
            xcc_count = std::max(xcc_count, coordinate.xcc + 1);
            counter.active = true;

            groups[coordinate.xcc][timestamp].indices.push_back(records.size());
            records.push_back({counter_id, &counter, coordinate, coordinate.folded ? instance_id : 0, value});
        }
    }
    if (records.empty()) throw std::runtime_error("SPM JSON has no counter values");

    // Duplicates share a group, since the group key holds the XCC and timestamp.
    // Keep the first record, as overlapping dispatches or batches repeat samples.
    size_t duplicates = 0;
    for (auto& [xcc, xcc_groups] : groups)
        for (auto& [timestamp, group] : xcc_groups)
        {
            auto key = [&](size_t index)
            {
                const auto& record = records[index];
                const auto& coordinate = record.coordinate;
                return std::tie(
                    record.counter_id,
                    coordinate.se,
                    coordinate.sa,
                    coordinate.wgp,
                    coordinate.instance,
                    record.fold_key
                );
            };
            auto& indices = group.indices;
            std::stable_sort(indices.begin(), indices.end(), [&](size_t a, size_t b) { return key(a) < key(b); });
            const auto end =
                std::unique(indices.begin(), indices.end(), [&](size_t a, size_t b) { return key(a) == key(b); });
            duplicates += std::distance(end, indices.end());
            indices.erase(end, indices.end());

            for (size_t index : indices)
            {
                const auto& record = records[index];
                if (record.counter->output.name != "SQ_CYCLES" || !(record.value > 0) || !std::isfinite(record.value))
                    continue;
                group.cycles += record.value;
                ++group.cycle_count;
            }
        }
    if (duplicates) warn("Ignored " + std::to_string(duplicates) + " duplicate SPM counter records.");

    const Timelines timelines = buildTimelines(groups, xcc_count, reconstruct_windows);
    SpmData result;
    for (const auto& timeline : timelines) result.sample_count = std::max(result.sample_count, timeline.size());
    result.timestamps.resize(xcc_count * result.sample_count, 0);
    result.clock.resize(xcc_count * result.sample_count, 0);
    result.sample_valid.resize(Validity::words(result.timestamps.size()), 0);

    for (size_t xcc = 0; xcc < xcc_count; ++xcc)
    {
        size_t sample = 0;
        float last_clock = 0;
        uint64_t last_timestamp = 0;
        for (const auto& timeline_sample : timelines[xcc])
        {
            const uint64_t timestamp = timeline_sample.timestamp;
            last_clock = static_cast<float>(timestamp - minimum_timestamp);
            last_timestamp = timestamp;
            result.timestamps[xcc * result.sample_count + sample] = timestamp;
            result.clock[xcc * result.sample_count + sample++] = last_clock;
            Validity::set(result.sample_valid, xcc * result.sample_count + sample - 1);
        }
        while (sample < result.sample_count)
        {
            result.timestamps[xcc * result.sample_count + sample] = last_timestamp;
            result.clock[xcc * result.sample_count + sample++] = last_clock;
        }
    }

    for (auto& [counter_id, info] : metadata)
    {
        (void) counter_id;
        if (!info.active) continue;

        auto& counter = info.output;
        counter.xcc_count = xcc_count;
        counter.arrays_per_se = info.extents.sa;
        counter.se_count = info.extents.se * info.extents.sa;
        counter.instance_count = info.extents.wgp * info.extents.instance;
        counter.values.resize(counter.xcc_count * counter.se_count * counter.instance_count * result.sample_count, 0);
        counter.valid.resize(Validity::words(counter.values.size()), 0);
        info.output_index = result.counters.size();
        result.counters.push_back(std::move(counter));
    }

    for (size_t xcc = 0; xcc < timelines.size(); ++xcc)
    {
        size_t sample_index = 0;
        for (const auto& timeline_sample : timelines[xcc])
        {
            for (size_t record_index : timeline_sample.group->indices)
            {
                const auto& record = records[record_index];
                if (!std::isfinite(record.value)) continue;
                auto& counter = result.counters.at(record.counter->output_index);
                const size_t value_index = flattenedIndex(
                    xcc,
                    counter.arrays_per_se * record.coordinate.se + record.coordinate.sa,
                    record.counter->extents.instance * record.coordinate.wgp + record.coordinate.instance,
                    sample_index,
                    counter,
                    result.sample_count
                );
                // Only records differing in folded dimensions share a cell.
                counter.values.at(value_index) += record.value / timeline_sample.windows;
                Validity::set(counter.valid, value_index);
            }
            ++sample_index;
        }
    }

    result.warnings = std::move(warnings);
    return result;
}

bool spmClockRangesOverlap(const SpmData& spm, const std::vector<SpmClockAnchor>& anchors)
{
    if (spm.empty() || anchors.empty()) return false;

    uint64_t spm_min = std::numeric_limits<uint64_t>::max();
    uint64_t spm_max = 0;
    for (size_t xcc = 0; xcc < spm.xccCount(); ++xcc)
        for (size_t sample = 0; sample < spm.validSamples(xcc); ++sample)
        {
            const uint64_t timestamp = spm.timestamps.at(xcc * spm.sample_count + sample);
            spm_min = std::min(spm_min, timestamp);
            spm_max = std::max(spm_max, timestamp);
        }

    uint64_t realtime_min = std::numeric_limits<uint64_t>::max();
    uint64_t realtime_max = 0;
    for (const auto& anchor : anchors)
    {
        realtime_min = std::min(realtime_min, anchor.realtime_clock);
        realtime_max = std::max(realtime_max, anchor.realtime_clock);
    }

    return spm_min <= realtime_max && realtime_min <= spm_max;
}

bool alignSpmClock(SpmData& spm, const std::vector<SpmClockAnchor>& anchors)
{
    if (spm.empty() || spm.sample_count == 0 || anchors.empty()) return false;
    int anchor_se = -1;
    const SpmClockAnchor* first = nullptr;
    for (const auto& anchor : anchors)
        if (!first || anchor.realtime_clock < first->realtime_clock)
        {
            first = &anchor;
            anchor_se = anchor.se;
        }
    if (!first) return false;

    const SpmClockAnchor* last = first;
    for (const auto& anchor : anchors)
        if (anchor.se == anchor_se && anchor.realtime_clock > last->realtime_clock) last = &anchor;

    const bool has_fallback_slope =
        last->realtime_clock > first->realtime_clock && last->shader_clock > first->shader_clock;
    const double fallback_slope = has_fallback_slope
                                    ? static_cast<double>(last->shader_clock - first->shader_clock) /
                                          static_cast<double>(last->realtime_clock - first->realtime_clock)
                                    : 0.0;

    const SpmCounterData* sq_cycles = nullptr;
    for (const auto& counter : spm.counters)
        if (counter.name == "SQ_CYCLES")
        {
            sq_cycles = &counter;
            break;
        }
    // Unrelated timestamp ranges cannot provide the fallback clock mapping.
    if (!sq_cycles && (!has_fallback_slope || !spmClockRangesOverlap(spm, anchors))) return false;

    std::vector<float> aligned_clock = spm.clock;
    for (size_t xcc = 0; xcc < spm.xccCount(); ++xcc)
    {
        const size_t count = spm.validSamples(xcc);
        if (count == 0) continue;

        auto timestampAt = [&](size_t sample) { return spm.timestamps.at(xcc * spm.sample_count + sample); };
        std::vector<double> interval_cycles(count, 0.0);

        for (size_t sample = 1; sample < count; ++sample)
        {
            const uint64_t delta_timestamp = timestampAt(sample) - timestampAt(sample - 1);
            // SQ_CYCLES at a sample measures the interval ending at that sample.
            double cycles = sq_cycles ? counterValue(*sq_cycles, xcc, anchor_se, sample, spm.sample_count) : 0.0;
            if (cycles <= 0)
            {
                if (!has_fallback_slope) return false;
                cycles = fallback_slope * delta_timestamp;
            }
            interval_cycles[sample] = cycles;
        }

        const uint64_t* timestamps = spm.timestamps.data() + xcc * spm.sample_count;
        const size_t pivot = nearestTimestamp(timestamps, count, first->realtime_clock);
        double pivot_clock = first->shader_clock;
        if (timestamps[pivot] != first->realtime_clock)
        {
            double rate = fallback_slope;
            if (count > 1)
            {
                const size_t interval =
                    timestamps[pivot] < first->realtime_clock && pivot + 1 < count ? pivot + 1 : (pivot ? pivot : 1);
                const uint64_t duration = timestamps[interval] - timestamps[interval - 1];
                if (duration > 0) rate = interval_cycles[interval] / duration;
            }
            if (rate <= 0) return false;
            pivot_clock += static_cast<double>(signedTimestampDelta(timestamps[pivot], first->realtime_clock) * rate);
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
    spm.clock_aligned = true;
    return true;
}

bool alignSpmClockFromEvents(
    SpmData& spm, const std::vector<SpmClockAnchor>& anchors, const std::vector<SpmSampleEvent>& events
)
{
    if (spm.empty() || spm.sample_count == 0 || events.empty()) return false;

    std::map<int, std::vector<SpmClockAnchor>> realtime_by_se;
    for (const auto& anchor : anchors) realtime_by_se[anchor.se].push_back(anchor);

    // Use one SE's shader clock throughout, as in the SQ_CYCLES alignment path.
    for (auto& [se, realtime] : realtime_by_se)
    {
        sortUniqueShaderClocks(realtime);
        keepMonotonicAnchors(realtime);
        if (realtime.size() < 2) continue;

        std::vector<SpmClockAnchor> sample_anchors;
        for (const auto& event : events)
        {
            if (event.se != se) continue;
            auto upper = std::lower_bound(
                realtime.begin(),
                realtime.end(),
                event.shader_clock,
                [](const auto& anchor, int64_t clock) { return anchor.shader_clock < clock; }
            );
            // Outside the realtime records, extrapolate using the nearest pair.
            const size_t right = std::clamp<size_t>(upper - realtime.begin(), 1, realtime.size() - 1);
            const auto& a = realtime[right - 1];
            const auto& b = realtime[right];
            const long double fraction = (static_cast<long double>(event.shader_clock) - a.shader_clock) /
                                         (static_cast<long double>(b.shader_clock) - a.shader_clock);
            const long double offset = std::round(fraction * (b.realtime_clock - a.realtime_clock));
            if (offset < -static_cast<long double>(a.realtime_clock) ||
                offset >= static_cast<long double>(std::numeric_limits<uint64_t>::max() - a.realtime_clock))
                continue;
            const uint64_t estimate = offset < 0 ? a.realtime_clock - static_cast<uint64_t>(-offset)
                                                 : a.realtime_clock + static_cast<uint64_t>(offset);
            sample_anchors.push_back({se, event.shader_clock, estimate});
        }
        sortUniqueShaderClocks(sample_anchors);
        if (sample_anchors.size() < 2) continue;

        std::vector<float> aligned_clock = spm.clock;
        bool aligned = false;
        for (size_t xcc = 0; xcc < spm.xccCount(); ++xcc)
        {
            const size_t count = spm.validSamples(xcc);
            if (count == 0) continue;
            aligned = false;
            if (count < 2) break;
            const uint64_t* timestamps = spm.timestamps.data() + xcc * spm.sample_count;
            std::map<size_t, SpmClockAnchor> matched;
            std::set<size_t> ambiguous;
            for (const auto& anchor : sample_anchors)
            {
                const size_t sample = nearestTimestamp(timestamps, count, anchor.realtime_clock);
                uint64_t interval = std::numeric_limits<uint64_t>::max();
                if (sample > 0) interval = timestamps[sample] - timestamps[sample - 1];
                if (sample + 1 < count) interval = std::min(interval, timestamps[sample + 1] - timestamps[sample]);
                // Use the shorter neighboring interval to reject uncertain
                // matches around closely spaced samples and capture boundaries.
                if (std::abs(signedTimestampDelta(anchor.realtime_clock, timestamps[sample])) >
                    static_cast<long double>(interval) / 2)
                    continue;
                if (!matched.emplace(sample, anchor).second) ambiguous.insert(sample);
            }
            // Multiple events nearest the same sample do not identify a unique
            // match. Leave it to interpolation instead of shifting later events.
            for (const size_t sample : ambiguous) matched.erase(sample);
            // Monotonic anchors give nondecreasing estimates, so the matched
            // shader clocks increase with the sample index.
            if (matched.size() < 2) break;

            auto left = matched.begin();
            auto right = std::next(left);
            for (size_t sample = 0; sample < count; ++sample)
            {
                while (sample > right->first && std::next(right) != matched.end()) left = right++;
                const long double fraction = signedTimestampDelta(timestamps[sample], timestamps[left->first]) /
                                             (timestamps[right->first] - timestamps[left->first]);
                // Matched samples keep the event clock, up to float precision.
                // Missing events are interpolated; samples outside the matches
                // use the end pair.
                aligned_clock[xcc * spm.sample_count + sample] =
                    left->second.shader_clock +
                    fraction * (static_cast<long double>(right->second.shader_clock) - left->second.shader_clock);
            }
            for (size_t sample = count; sample < spm.sample_count; ++sample)
                aligned_clock[xcc * spm.sample_count + sample] = aligned_clock[xcc * spm.sample_count + count - 1];
            aligned = true;
        }
        if (!aligned) continue;
        spm.clock = std::move(aligned_clock);
        spm.clock_aligned = true;
        return true;
    }
    return false;
}
