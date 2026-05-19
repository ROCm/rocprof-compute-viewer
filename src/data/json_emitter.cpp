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

#include "json_emitter.h"

#include <algorithm>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <utility>
#include "code/codeload.hpp"
#include "data/dispatch_resolver.h"
#include "data/shaderdata.h"
#include "data/sqtt_funcmap_json.h"
#include "data/wavemanager.h"
#include "util/jsonrequest.hpp"
#include "wave/othersimd.h"

namespace
{
using MarkerBucketKey = std::tuple<int, int, int, int>;

struct JsonMarkerWaveEntry
{
    int64_t begin = 0;
    int64_t end = 0;
    std::shared_ptr<WaveInstance> wave;
};

std::unordered_map<int, uint64_t> buildCodeobjLineMap(const std::vector<CodeData>& code)
{
    std::unordered_map<int, uint64_t> out;
    for (const auto& line : code)
    {
        if (!line.line || line.line->codeobj_id <= 0) continue;
        out[static_cast<int>(line.line->index)] = static_cast<uint64_t>(line.line->codeobj_id);
    }
    return out;
}

uint64_t codeobjAtTime(
    const JsonMarkerWaveEntry& entry, const std::unordered_map<int, uint64_t>& codeobj_by_line, int64_t time
)
{
    if (!entry.wave || entry.wave->tokens.empty()) return 0;

    auto token_it = entry.wave->tokens.upper_bound(time);
    if (token_it == entry.wave->tokens.begin()) return 0;
    --token_it;

    auto co_it = codeobj_by_line.find(token_it->code_line);
    if (co_it == codeobj_by_line.end()) return 0;
    return co_it->second;
}

template <typename EntryT, typename ResolveFn>
uint64_t activeIntervalCodeobj(const std::vector<EntryT>& bucket, int64_t time, ResolveFn resolve)
{
    if (bucket.empty()) return 0;

    auto first_after =
        std::upper_bound(bucket.begin(), bucket.end(), time, [](int64_t t, const EntryT& e) { return t < e.begin; });

    for (auto it = std::make_reverse_iterator(first_after); it != bucket.rend(); ++it)
    {
        if (time < it->begin) continue;
        if (time <= it->end) return resolve(*it, time);
        break;
    }
    return 0;
}

class JsonMarkerCodeobjResolver
{
public:
    JsonMarkerCodeobjResolver(DataStore& store, const SqttFuncmapJson& funcmap) :
    store(store),
    funcmap(funcmap),
    codeobj_by_line(buildCodeobjLineMap(store.code)),
    occupancy_index(store, [this](const occupancy_record_t& rec) { return codeobjForOccupancyRecord(rec); })
    {}

    uint64_t operator()(int se, int cu, int simd, int slot, int64_t time)
    {
        uint64_t codeobj_id = codeobjFromWaveCode(se, cu, simd, slot, time);
        if (codeobj_id != 0) return codeobj_id;
        return codeobjFromOccupancy(se, cu, simd, slot, time);
    }

private:
    uint64_t codeobjFromWaveCode(int se, int cu, int simd, int slot, int64_t time)
    {
        const auto& bucket = waveBucketFor(se, cu, simd, slot);
        return activeIntervalCodeobj(
            bucket,
            time,
            [this](const JsonMarkerWaveEntry& entry, int64_t t) { return codeobjAtTime(entry, codeobj_by_line, t); }
        );
    }

    uint64_t codeobjFromOccupancy(int se, int cu, int simd, int slot, int64_t time)
    {
        return occupancy_index.resolve(se, cu, simd, slot, time);
    }

    const std::vector<JsonMarkerWaveEntry>& waveBucketFor(int se, int cu, int simd, int slot)
    {
        const MarkerBucketKey key = std::make_tuple(se, cu, simd, slot);
        auto [it, inserted] = wave_cache.emplace(key, std::vector<JsonMarkerWaveEntry>{});
        if (!inserted) return it->second;
        if (codeobj_by_line.empty()) return it->second;

        auto se_it = store.wave_hierarchy.find(se);
        if (se_it == store.wave_hierarchy.end()) return it->second;
        auto simd_it = se_it->second.find(simd);
        if (simd_it == se_it->second.end()) return it->second;
        auto slot_it = simd_it->second.find(slot);
        if (slot_it == simd_it->second.end()) return it->second;

        for (const auto& [wid, wave_entry] : slot_it->second)
        {
            (void) wid;
            try
            {
                auto wave = store.getWave(wave_entry);
                if (!wave) continue;
                if (wave->cu >= 0 && wave->cu != cu) continue;

                JsonMarkerWaveEntry entry;
                entry.begin = wave_entry.begin;
                entry.end = wave_entry.end;
                entry.wave = std::move(wave);
                it->second.push_back(std::move(entry));
            }
            catch (const std::exception& e)
            {
                std::cerr << "Warning: Failed to load wave for marker resolution: " << wave_entry.id << ": " << e.what()
                          << std::endl;
            }
            catch (...)
            {
                std::cerr << "Warning: Failed to load wave for marker resolution: " << wave_entry.id << std::endl;
            }
        }

        std::sort(
            it->second.begin(),
            it->second.end(),
            [](const JsonMarkerWaveEntry& a, const JsonMarkerWaveEntry& b) { return a.begin < b.begin; }
        );
        return it->second;
    }

    uint64_t codeobjForOccupancyRecord(const occupancy_record_t& rec)
    {
        int kid = store.dispatch_resolver.Resolve(rec.pc);
        return funcmap.CodeobjForKernelName(store.dispatch_resolver.Name(kid));
    }

    DataStore& store;
    const SqttFuncmapJson& funcmap;
    std::unordered_map<int, uint64_t> codeobj_by_line;
    std::map<MarkerBucketKey, std::vector<JsonMarkerWaveEntry>> wave_cache;
    ActiveCodeobjIndex occupancy_index;
};
} // namespace

JsonRecordEmitter::JsonRecordEmitter(const std::string& dir, RecordDispatcher& disp, DataStore& st) :
ui_dir(dir), dispatcher(disp), store(st)
{
    if (!ui_dir.empty() && ui_dir.back() != '/') ui_dir.push_back('/');
}

void JsonRecordEmitter::run()
{
    store.ui_dir = ui_dir;
    emitMetadata();
    emitCode();
    emitSourceSnapshots();
    emitWaveHierarchy();
    emitOccupancy();
    emitCounters();
    emitRealtime();
    emitShaderData();
    emitOtherSimd();
    resolveMarkersFromCodeJson();
    dispatcher.signalComplete();
}

void JsonRecordEmitter::runOccupancyOnlyForTests() { emitOccupancy(); }

void JsonRecordEmitter::emitMetadata()
{
    try
    {
        JsonRequest file(ui_dir + "filenames.json", false);
        if (!file.bValid) return;

        auto& data = file.data;

        // gfxip
        if (data.contains("gfxip"))
        {
            int gfxip = data["gfxip"];
            dispatcher.dispatchMetadata(record_type_t::GFXIP, &gfxip);
        }

        // gfxv
        if (data.contains("gfxv")) { store.gfxv = std::string(data["gfxv"]); }

        // thread_trace / pc_sampling flags
        try
        {
            store.has_thread_trace = data["thread_trace"];
            store.has_pc_sampling = data["pc_sampling"];
        }
        catch (...)
        {}

        // counter names
        if (data.contains("counter_names"))
        {
            store.counter_names = data["counter_names"].get<std::vector<std::string>>();
        }
    }
    catch (std::exception& e)
    {
        std::cout << "Warning: Failed to load metadata: " << e.what() << std::endl;
    }
}

void JsonRecordEmitter::emitWaveHierarchy()
{
    try
    {
        JsonRequest file(ui_dir + "filenames.json", false);
        if (!file.bValid || !file.data.contains("wave_filenames")) return;

        auto& wave_filenames = file.data["wave_filenames"];

        for (auto& [se_name, se_data] : wave_filenames.items())
        {
            int se = std::stoi(se_name);
            for (auto& [simd_name, simd_data] : se_data.items())
            {
                int simd = std::stoi(simd_name);
                for (auto& [slot_name, slot_data] : simd_data.items())
                {
                    int slot = std::stoi(slot_name);
                    for (auto& [wid_name, wid_data] : slot_data.items())
                    {
                        int wid = std::stoi(wid_name);
                        WaveEntry entry;
                        entry.id = std::string(wid_data[0]);
                        entry.begin = int64_t(wid_data[1]);
                        entry.end = int64_t(wid_data[2]);
                        store.wave_hierarchy[se][simd][slot][wid] = entry;
                    }
                }
            }
        }
    }
    catch (std::exception& e)
    {
        std::cout << "Warning: Failed to load wave hierarchy: " << e.what() << std::endl;
    }
}

void JsonRecordEmitter::emitOccupancy()
{
    try
    {
        JsonRequest file(ui_dir + "occupancy.json", false);
        if (!file.bValid) return;

        // Dispatch names
        if (file.data.contains("dispatches"))
        {
            for (auto& [id, name] : file.data["dispatches"].items())
                store.dispatch_resolver.RegisterJsonKernel(std::stoi(id), std::string(name));
        }

        for (auto& [se_name, array] : file.data.items())
        {
            if (array.size() == 0) continue;

            int se = -1;
            try
            {
                se = std::stoi(se_name);
            }
            catch (...)
            {
                continue;
            }

            for (auto& v : array)
            {
                occupancy_record_t rec{};
                rec.time = int64_t(v[0]);
                rec.cu = uint8_t(v[1]);
                rec.simd = uint8_t(v[2]);
                rec.wave_id = uint8_t(v[3]);
                rec.start = int8_t(v[4]);
                int kid = int(v[5]);
                store.dispatch_resolver.RegisterJsonKernel(kid, "");
                rec.pc.address = uint64_t(kid);
                rec.pc.code_object_id = DispatchResolver::JsonKernelCodeObjectId;
                dispatcher.dispatchOccupancy(se, rec);
            }
        }
    }
    catch (std::exception& e)
    {
        std::cout << "Warning: Failed to load occupancy: " << e.what() << std::endl;
    }
}

void JsonRecordEmitter::emitCounters()
{
    // Counter data is per-SE in seN_perfcounter.json files.
    // We need to know how many SEs exist — get from occupancy dispatches or wave hierarchy.
    int max_se = 0;
    for (auto& [se, _] : store.wave_hierarchy) max_se = std::max(max_se, se + 1);
    for (auto& [se, _] : store.occupancy_by_se) max_se = std::max(max_se, se + 1);

    for (int se = 0; se < max_se; se++)
    {
        try
        {
            JsonRequest file(ui_dir + "se" + std::to_string(se) + "_perfcounter.json", false);
            if (!file.bValid) continue;

            for (auto& event : file.data["data"])
            {
                counter_record_t rec{};
                rec.time = int64_t(event[0]);
                rec.values[0] = float(event[1]);
                rec.values[1] = float(event[2]);
                rec.values[2] = float(event[3]);
                rec.values[3] = float(event[4]);
                rec.cu = int8_t(event[5]);
                rec.bank = int8_t(event[6]);
                dispatcher.dispatchCounter(se, rec);
            }
        }
        catch (std::exception& e)
        {
            RCV_LOG();
            // Counter file may not exist for all SEs
        }
    }
}

void JsonRecordEmitter::emitRealtime()
{
    try
    {
        JsonRequest file(ui_dir + "realtime.json", false);
        if (!file.bValid) return;

        // Frequency metadata
        if (file.data.contains("metadata") && file.data["metadata"].contains("frequency"))
        {
            int64_t freq = int64_t(file.data["metadata"]["frequency"]);
            dispatcher.dispatchMetadata(record_type_t::RT_FREQUENCY, &freq);
        }

        for (auto& [se_name, points] : file.data.items())
        {
            if (std::string(se_name).find("SE") != 0) continue;

            int se = std::stoi(std::string(se_name).substr(2));
            for (auto& p : points)
            {
                realtime_record_t rec{};
                rec.shader_clock = int64_t(p[0]);
                rec.realtime_clock = uint64_t(p[1]);
                rec.reserved = 0;
                dispatcher.dispatchRealtime(se, rec);
            }
        }
    }
    catch (std::exception& e)
    {
        RCV_LOG();
        // Realtime data is optional
    }
}

void JsonRecordEmitter::emitShaderData()
{
    // ShaderData uses its own manager with parallel loading.
    // We populate it directly from JSON the same way the old code did.
    try
    {
        JsonRequest file(ui_dir + "filenames.json", false);
        if (!file.bValid || !file.data.contains("shaderdata_filenames")) return;

        store.shaderdata = std::make_unique<ShaderDataManager>();
        store.shaderdata->Load(file.data["shaderdata_filenames"], ui_dir);
    }
    catch (std::exception& e)
    {
        std::cout << "Warning: Failed to load shaderdata: " << e.what() << std::endl;
    }
}

void JsonRecordEmitter::emitOtherSimd()
{
    // Other-SIMD uses file references for the JSON path.
    try
    {
        JsonRequest file(ui_dir + "filenames.json", false);
        if (!file.bValid || !file.data.contains("other_simd_filenames")) return;

        store.other_simd_files = ParseOtherSimdFilenames(file.data["other_simd_filenames"], ui_dir);
    }
    catch (std::exception& e)
    {
        RCV_LOG();
        // Other-SIMD data is optional
    }
}

void JsonRecordEmitter::resolveMarkersFromCodeJson()
{
    if (!store.shaderdata || !store.shaderdata->HasData()) return;

    SqttFuncmapJson funcmap = SqttFuncmapJson::LoadFromCodeJson(ui_dir + "code.json");
    for (const auto& diag : funcmap.diagnostics()) std::cerr << "Warning: " << diag << std::endl;

    if (funcmap.empty())
    {
        store.shaderdata->ClearMarkers();
        return;
    }

    JsonMarkerCodeobjResolver active_codeobj(store, funcmap);
    store.shaderdata->ResolveMarkers(
        [&](int se, int cu, int simd, int slot, uint32_t id, int64_t time) -> ResolvedMarker
        {
            const uint64_t codeobj_id = active_codeobj(se, cu, simd, slot, time);
            return funcmap.Resolve(id, codeobj_id);
        }
    );
}

void JsonRecordEmitter::emitCode()
{
    // code.json is optional (e.g. pc_sampling mode has none initially); a missing file
    // raises in JsonRequest and we silently swallow that. A parse failure is logged.
    const std::string path = ui_dir + "code.json";
    try
    {
        store.code = CodeData::LoadCode(path);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Warning: code.json: " << path << ": " << e.what() << std::endl;
    }
    catch (...)
    {}
}

void JsonRecordEmitter::emitSourceSnapshots()
{
    try
    {
        JsonRequest file(ui_dir + "snapshots.json", false);
        if (!file.bValid) return;
        store.loadSourceSnapshots(file.data, ui_dir);
    }
    catch (...)
    {
        RCV_LOG();
        // Source snapshots are optional
    }
}
