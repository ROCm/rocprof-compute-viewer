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

#pragma once

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "code/codeload.hpp"
#include "data/dispatch_resolver.h"
#include "data/records.h"
#include "json/include/nlohmann/json.hpp"
#include "wave/othersimd_types.h"

class ShaderDataManager;
struct WaveInstance;

struct WaveEntry
{
    std::string id;
    int64_t begin = 0;
    int64_t end = 0;
};

using WaveSlotMap = std::map<int, WaveEntry>;
using SlotMap = std::map<int, WaveSlotMap>;
using SimdMap = std::map<int, SlotMap>;
using SEWaveMap = std::map<int, SimdMap>;

class DataStore
{
public:
    DataStore() = default;

    void clear();
    void loadSourceSnapshots(const nlohmann::json& snapshots_json, const std::string& snapshot_base_dir);
    void applyTimeOffsets(const std::map<int, int64_t>& offsets);

    int gfxip = 0;
    std::string gfxv;
    bool has_thread_trace = true;
    bool has_pc_sampling = false;
    std::vector<std::string> counter_names;

    SEWaveMap wave_hierarchy;

    std::shared_ptr<WaveInstance> getWave(const WaveEntry& entry);

    std::vector<CodeData> code;

    struct SourceSnapshot
    {
        std::string original_path;
        std::string snapshot_path;
    };
    std::vector<SourceSnapshot> source_snapshots;
    std::string source_tree_json;

    std::map<int, std::vector<occupancy_record_t>> occupancy_by_se;
    std::map<int, std::vector<trace_event_record_t>> trace_events_by_se;
    std::map<int, std::vector<dispatch_record_t>> dispatch_records_by_se;
    DispatchResolver dispatch_resolver;

    std::map<int, std::array<std::vector<counter_record_t>, 2>> counters_by_se;

    int64_t realtime_frequency = 0;
    std::map<int, std::vector<realtime_record_t>> realtime_by_se;

    std::unique_ptr<ShaderDataManager> shaderdata;

    OtherSimdFiles other_simd_files;
    std::map<int, std::vector<other_simd_record_t>> other_simd_by_se;

    std::string ui_dir;

    // In-memory wave records for the decoder path (keyed by synthetic wave ID).
    // Empty for the JSON path.
    std::unordered_map<std::string, wave_record_t> wave_records;
    mutable std::shared_mutex wave_records_mutex;
};

class ActiveCodeobjIndex
{
public:
    using ResolveCodeobj = std::function<uint64_t(const occupancy_record_t&)>;

    ActiveCodeobjIndex(const DataStore& store, ResolveCodeobj resolve_codeobj);

    uint64_t resolve(int se, int cu, int simd, int slot, int64_t time);
    void clear();

private:
    struct Interval
    {
        int64_t begin = 0;
        int64_t end = 0;
        uint64_t codeobj_id = 0;
    };

    static uint64_t keyFor(int se, int cu, int simd, int slot);
    std::vector<Interval> buildBucket(int se, int cu, int simd, int slot) const;
    const std::vector<Interval>& bucketFor(int se, int cu, int simd, int slot);

    const DataStore& store;
    ResolveCodeobj resolve_codeobj;
    std::map<uint64_t, std::vector<Interval>> cache;
    std::mutex cache_mutex;
};
