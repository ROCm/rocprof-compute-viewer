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

#include "record_handlers.h"

#include <algorithm>
#include <iostream>
#include <shared_mutex>
#include "shaderdata.h"

void WaveHandler::onWave(int se, const wave_record_t& rec)
{
    WaveEntry entry;
    entry.id = rec.id;
    entry.begin = rec.begin;
    entry.end = rec.end;

    // Hierarchy: se → simd → wave_slot → instance_counter
    // For the decoder path: wave_id is the wave slot within the SIMD.
    // Multiple waves can occupy the same slot over time, so use a running counter
    // as the 4th key to distinguish them.
    auto& slot_map = store.wave_hierarchy[se][rec.simd][rec.wave_id];
    int next_id = slot_map.empty() ? 0 : slot_map.rbegin()->first + 1;
    slot_map[next_id] = entry;

    std::unique_lock<std::shared_mutex> lock(store.wave_records_mutex);
    store.wave_records.emplace(rec.id, rec);
}

void OccupancyHandler::onOccupancy(int se, const occupancy_record_t& rec) { store.occupancy_by_se[se].push_back(rec); }

void OccupancyHandler::onParsingComplete()
{
    auto by_time = [](const auto& a, const auto& b) { return a.time < b.time; };
    for (auto& [se, records] : store.occupancy_by_se)
        std::sort(
            records.begin(),
            records.end(),
            [](const occupancy_record_t& a, const occupancy_record_t& b)
            {
                if (a.time != b.time) return a.time < b.time;
                return a.start < b.start;
            }
        );
    for (auto& [se, records] : store.trace_events_by_se) std::stable_sort(records.begin(), records.end(), by_time);
    for (auto& [se, records] : store.dispatch_records_by_se) std::stable_sort(records.begin(), records.end(), by_time);
}

void OccupancyHandler::onTraceEvent(int se, const trace_event_record_t& rec)
{
    store.trace_events_by_se[se].push_back(rec);
}

void OccupancyHandler::onDispatch(int se, const dispatch_record_t& rec)
{
    store.dispatch_records_by_se[se].push_back(rec);
}

void CounterHandler::onCounter(int se, const counter_record_t& rec)
{
    int bank = rec.bank & 1;
    store.counters_by_se[se][bank].push_back(rec);
}

void ShaderDataHandler::onShaderData(int se, const shaderdata_record_t& rec)
{
    if (!store.shaderdata) store.shaderdata = std::make_unique<ShaderDataManager>();
    store.shaderdata->AddRecord(se, rec);
}

void ShaderDataHandler::onParsingComplete()
{
    if (store.shaderdata) store.shaderdata->Finalize();
}

void OtherSimdHandler::onOtherSimd(int se, const other_simd_record_t& rec)
{
    store.other_simd_by_se[se].push_back(rec);
}

void RealtimeHandler::onRealtime(int se, const realtime_record_t& rec) { store.realtime_by_se[se].push_back(rec); }

void MetadataHandler::onMetadata(record_type_t type, const void* data)
{
    switch (type)
    {
        case record_type_t::GFXIP:
            store.gfxip = *static_cast<const int*>(data);
            // Derive gfxv if not already set (JSON path sets it explicitly from filenames.json)
            if (store.gfxv.empty()) store.gfxv = (store.gfxip >= 10) ? "navi" : "vega";
            break;
        case record_type_t::RT_FREQUENCY: store.realtime_frequency = *static_cast<const int64_t*>(data); break;
        default: break;
    }
}
