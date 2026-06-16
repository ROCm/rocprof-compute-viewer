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

#include "datastore.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include "data/shaderdata.h"
#include "util/memtracker.h"

namespace
{
std::string joinPath(const std::string& a, const std::string& b)
{
    if (a.empty()) return b;
    if (a.back() == '/') return a + b;
    return a + "/" + b;
}

void collectSourceSnapshots(
    const nlohmann::json& node, const std::string& prefix, const std::string& snapshot_base_dir, DataStore& store
)
{
    for (const auto& [key, value] : node.items())
    {
        const std::string original_path = joinPath(prefix, key);
        if (value.is_string())
        {
            store.source_snapshots.push_back({original_path, joinPath(snapshot_base_dir, std::string(value))});
        }
        else if (value.is_object()) { collectSourceSnapshots(value, original_path, snapshot_base_dir, store); }
    }
}

template <typename RecordMap, typename Apply>
void applyOffsetsBySE(const std::map<int, int64_t>& offsets, RecordMap& records_by_se, Apply apply)
{
    for (auto& [se, records] : records_by_se)
    {
        auto it = offsets.find(se);
        if (it == offsets.end() || it->second == 0) continue;
        apply(records, it->second);
    }
}
} // namespace

void DataStore::clear()
{
    gfxip = 0;
    gfxv.clear();
    has_thread_trace = true;
    has_pc_sampling = false;
    counter_names.clear();
    wave_hierarchy.clear();
    code.clear();
    source_snapshots.clear();
    source_tree_json.clear();
    occupancy_by_se.clear();
    trace_events_by_se.clear();
    dispatch_records_by_se.clear();
    dispatch_resolver.Clear();
    counters_by_se.clear();
    realtime_frequency = 0;
    realtime_by_se.clear();
    realtime_alignment_applied = false;
    shaderdata.reset();
    other_simd_files.clear();
    other_simd_by_se.clear();
    ui_dir.clear();
    {
        std::unique_lock<std::shared_mutex> lock(wave_records_mutex);
        wave_records.clear();
    }
}

void DataStore::loadSourceSnapshots(const nlohmann::json& snapshots_json, const std::string& snapshot_base_dir)
{
    source_tree_json = snapshots_json.dump();
    collectSourceSnapshots(snapshots_json, "", snapshot_base_dir, *this);
}

namespace
{
std::map<int, int64_t> realtimeAlignmentOffsets(const DataStore& store)
{
    std::map<int, int64_t> empty;
    if (store.realtime_by_se.size() < 2) return empty;

    struct Anchor
    {
        int64_t sc;
        uint64_t rc;
    };

    std::map<int, Anchor> first;
    std::map<int, Anchor> last;
    for (const auto& [se, recs] : store.realtime_by_se)
    {
        if (recs.empty()) continue;
        Anchor lo{recs.front().shader_clock, recs.front().realtime_clock};
        Anchor hi = lo;
        for (const auto& r : recs)
        {
            if (r.shader_clock < lo.sc) lo = {r.shader_clock, r.realtime_clock};
            if (r.shader_clock > hi.sc) hi = {r.shader_clock, r.realtime_clock};
        }
        first.emplace(se, lo);
        last.emplace(se, hi);
    }
    if (first.size() < 2) return empty;

    int64_t delta_shader = 25;
    uint64_t delta_realtime = 1;
    for (const auto& [se, lo] : first)
    {
        const auto& hi = last.at(se);
        if (hi.sc > lo.sc && lo.sc > 0 && hi.rc > lo.rc)
        {
            delta_shader += hi.sc - lo.sc;
            delta_realtime += hi.rc - lo.rc;
        }
    }

    const double k = static_cast<double>(delta_realtime) / static_cast<double>(delta_shader);
    if (k <= 0.0) return empty;

    std::map<int, double> origin_rt;
    int anchor_se = first.begin()->first;
    double anchor_origin = std::numeric_limits<double>::infinity();
    for (const auto& [se, a] : first)
    {
        double origin = static_cast<double>(a.rc) - static_cast<double>(a.sc) * k;
        origin_rt[se] = origin;
        if (origin < anchor_origin)
        {
            anchor_origin = origin;
            anchor_se = se;
        }
    }

    std::map<int, int64_t> offset;
    for (const auto& [se, origin] : origin_rt)
    {
        double off = (origin - anchor_origin) / k;
        offset[se] = static_cast<int64_t>(off);
    }

    const int64_t rt_freq = store.realtime_frequency > 0 ? store.realtime_frequency : 100'000'000;
    std::cout << "REALTIME alignment: anchor SE=" << anchor_se << ", k=" << k << " (rt_freq=" << rt_freq << " Hz)\n";
    for (const auto& [se, off] : offset) std::cout << "  SE" << se << " offset=" << off << " cycles\n";

    return offset;
}
} // namespace

bool DataStore::applyRealtimeAlignment()
{
    QWARNING(!realtime_alignment_applied, "REALTIME alignment already applied", return false)
    const auto offsets = realtimeAlignmentOffsets(*this);
    if (offsets.empty()) return false;
    for (const auto& [se, _] : wave_hierarchy)
        QWARNING(offsets.count(se), "REALTIME alignment skipped; missing realtime samples for SE" << se, return false)
    applyTimeOffsets(offsets);
    realtime_alignment_applied = true;
    return true;
}

void DataStore::applyTimeOffsets(const std::map<int, int64_t>& offsets)
{
    {
        std::unique_lock<std::shared_mutex> lock(wave_records_mutex);
        for (auto& [se, simd_map] : wave_hierarchy)
        {
            auto it = offsets.find(se);
            if (it == offsets.end() || it->second == 0) continue;
            const int64_t off = it->second;
            for (auto& [simd, slot_map] : simd_map)
                for (auto& [slot, instance_map] : slot_map)
                    for (auto& [inst_id, entry] : instance_map)
                    {
                        entry.begin += off;
                        entry.end += off;
                        entry.time_offset += off;
                        auto rec_it = wave_records.find(entry.id);
                        if (rec_it == wave_records.end()) continue;
                        auto& rec = rec_it->second;
                        rec.begin += off;
                        rec.end += off;
                        for (auto& wi : rec.instructions) wi.time += off;
                    }
        }
    }

    applyOffsetsBySE(
        offsets,
        occupancy_by_se,
        [](auto& recs, int64_t off)
        {
            for (auto& r : recs) r.time += off;
        }
    );
    applyOffsetsBySE(
        offsets,
        counters_by_se,
        [](auto& banks, int64_t off)
        {
            for (auto& bank : banks)
                for (auto& r : bank) r.time += off;
        }
    );
    applyOffsetsBySE(
        offsets,
        trace_events_by_se,
        [](auto& recs, int64_t off)
        {
            for (auto& r : recs) r.time += off;
        }
    );
    applyOffsetsBySE(
        offsets,
        dispatch_records_by_se,
        [](auto& recs, int64_t off)
        {
            for (auto& r : recs) r.time += off;
        }
    );
    applyOffsetsBySE(
        offsets,
        other_simd_by_se,
        [](auto& recs, int64_t off)
        {
            for (auto& r : recs) r.time += off;
        }
    );

    for (auto& [se, entries] : other_simd_files)
    {
        auto it = offsets.find(se);
        if (it == offsets.end() || it->second == 0) continue;
        const int64_t off = it->second;
        for (auto& entry : entries)
        {
            entry.time_offset += off;
            if (entry.range)
            {
                entry.range->first += off;
                entry.range->second += off;
            }
        }
    }

    if (shaderdata) shaderdata->ApplyTimeOffsets(offsets);
}

ActiveCodeobjIndex::ActiveCodeobjIndex(const DataStore& st, ResolveCodeobj resolve) :
store(st), resolve_codeobj(std::move(resolve))
{}

uint64_t ActiveCodeobjIndex::keyFor(int se, int cu, int simd, int slot)
{
    return (static_cast<uint64_t>(static_cast<uint16_t>(se))) |
           (static_cast<uint64_t>(static_cast<uint16_t>(cu)) << 16) |
           (static_cast<uint64_t>(static_cast<uint16_t>(simd)) << 32) |
           (static_cast<uint64_t>(static_cast<uint16_t>(slot)) << 48);
}

std::vector<ActiveCodeobjIndex::Interval> ActiveCodeobjIndex::buildBucket(int se, int cu, int simd, int slot) const
{
    std::vector<Interval> built;
    auto se_it = store.occupancy_by_se.find(se);
    if (se_it == store.occupancy_by_se.end()) return built;

    std::vector<const occupancy_record_t*> records;
    records.reserve(se_it->second.size());
    for (const auto& rec : se_it->second)
    {
        if (rec.cu != cu || rec.simd != simd || rec.wave_id != slot) continue;
        records.push_back(&rec);
    }

    std::sort(
        records.begin(),
        records.end(),
        [](const occupancy_record_t* a, const occupancy_record_t* b)
        {
            if (a->time != b->time) return a->time < b->time;
            return a->start < b->start;
        }
    );

    bool active = false;
    int64_t active_begin = 0;
    uint64_t active_codeobj = 0;

    auto close_active_at = [&](int64_t end)
    {
        if (active && active_codeobj != 0 && end >= active_begin) built.push_back({active_begin, end, active_codeobj});
    };

    for (const auto* rec : records)
    {
        if (rec->start)
        {
            close_active_at(static_cast<int64_t>(rec->time));
            active = true;
            active_begin = static_cast<int64_t>(rec->time);
            active_codeobj = resolve_codeobj(*rec);
            continue;
        }

        close_active_at(static_cast<int64_t>(rec->time));
        active = false;
        active_codeobj = 0;
    }

    close_active_at(std::numeric_limits<int64_t>::max());
    return built;
}

const std::vector<ActiveCodeobjIndex::Interval>& ActiveCodeobjIndex::bucketFor(int se, int cu, int simd, int slot)
{
    const uint64_t key = keyFor(se, cu, simd, slot);
    std::lock_guard<std::mutex> lock(cache_mutex);
    auto it = cache.find(key);
    if (it == cache.end()) it = cache.emplace(key, buildBucket(se, cu, simd, slot)).first;
    return it->second;
}

uint64_t ActiveCodeobjIndex::resolve(int se, int cu, int simd, int slot, int64_t time)
{
    const auto& bucket = bucketFor(se, cu, simd, slot);
    if (bucket.empty()) return 0;

    auto first_after =
        std::upper_bound(bucket.begin(), bucket.end(), time, [](int64_t t, const Interval& e) { return t < e.begin; });

    for (auto it = std::make_reverse_iterator(first_after); it != bucket.rend(); ++it)
    {
        if (time < it->begin) continue;
        if (time <= it->end) return it->codeobj_id;
        break;
    }
    return 0;
}

void ActiveCodeobjIndex::clear()
{
    std::lock_guard<std::mutex> lock(cache_mutex);
    cache.clear();
}
