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
bool parseNonNegativeIntKey(const std::string& key, int& value)
{
    try
    {
        size_t parsed = 0;
        int parsed_value = std::stoi(key, &parsed);
        if (parsed != key.size() || parsed_value < 0) return false;
        value = parsed_value;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

template <typename OccupancyRecord> void setOccupancyClusterId(OccupancyRecord& rec, uint32_t cluster_id)
{
    if constexpr (requires { rec.cluster_id = cluster_id; }) rec.cluster_id = cluster_id;
}

trace_event_record_t traceEventFromJson(const nlohmann::json& event_json)
{
    trace_event_record_t rec{};
    rec.size = sizeof(trace_event_record_t);
    rec.time = event_json.value("time", int64_t{0});
    rec.type = static_cast<decltype(rec.type)>(event_json.value("type", 0));
    rec.me_id = static_cast<uint8_t>(event_json.value("me_id", 0));
    rec.pipe_id = static_cast<uint8_t>(event_json.value("pipe_id", 0));
    rec.flags = static_cast<uint16_t>(event_json.value("flags", 0));
    rec.payload.raw = event_json.value("payload", uint64_t{0});
    rec.byte_offset = event_json.value("byte_offset", uint64_t{0});
    return rec;
}

dispatch_record_t dispatchFromJson(const nlohmann::json& dispatch_json)
{
    dispatch_record_t rec{};
    rec.size = sizeof(dispatch_record_t);
    rec.time = dispatch_json.value("time", int64_t{0});
    rec.me_id = static_cast<uint8_t>(dispatch_json.value("me_id", 0));
    rec.pipe_id = static_cast<uint8_t>(dispatch_json.value("pipe_id", 0));
    rec.user_sgprs = static_cast<uint16_t>(dispatch_json.value("user_sgprs", 0));
    rec.flags = dispatch_json.value("flags", 0);
    rec.vgprs = dispatch_json.value("vgprs", uint32_t{0});
    rec.sgprs = dispatch_json.value("sgprs", uint32_t{0});
    rec.lds_size = dispatch_json.value("lds_size", uint32_t{0});
    rec.thread_dim_x = dispatch_json.value("thread_dim_x", uint32_t{0});
    rec.thread_dim_y = dispatch_json.value("thread_dim_y", uint32_t{0});
    rec.thread_dim_z = dispatch_json.value("thread_dim_z", uint32_t{0});
    rec.dispatch_pkt_addr = dispatch_json.value("dispatch_pkt_addr", uint64_t{0});
    rec.byte_offset = dispatch_json.value("byte_offset", uint64_t{0});

    auto entry_it = dispatch_json.find("entry_point");
    if (entry_it != dispatch_json.end() && entry_it->is_object())
    {
        rec.entry_point.address = entry_it->value("address", uint64_t{0});
        rec.entry_point.code_object_id = entry_it->value("code_object_id", uint64_t{0});
    }

    return rec;
}

void emitTimelineEvents(const nlohmann::json& root, RecordDispatcher& dispatcher, DataStore& store)
{
    auto events_it = root.find("events");
    if (events_it == root.end() || !events_it->is_object()) return;

    for (const auto& [se_name, timeline] : events_it->items())
    {
        int se = -1;
        if (!parseNonNegativeIntKey(se_name, se) || !timeline.is_array()) continue;

        for (const auto& record_json : timeline)
        {
            if (!record_json.is_object()) continue;
            const std::string kind = record_json.value("kind", std::string{});
            if (kind == "event") { dispatcher.dispatchTraceEvent(se, traceEventFromJson(record_json)); }
            else if (kind == "dispatch")
            {
                const int kernel_id = record_json.value("kernel_id", -1);
                if (kernel_id >= 0)
                {
                    store.dispatch_resolver.RegisterJsonKernel(
                        kernel_id, record_json.value("kernel_name", std::string{})
                    );
                }
                dispatcher.dispatchDispatch(se, dispatchFromJson(record_json));
            }
        }
    }
}

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

    uint64_t operator()(HWID hwid, int64_t time)
    {
        uint64_t codeobj_id = codeobjFromWaveCode(hwid, time);
        if (codeobj_id != 0) return codeobj_id;
        return codeobjFromOccupancy(hwid, time);
    }

private:
    uint64_t codeobjFromWaveCode(HWID hwid, int64_t time)
    {
        const auto& bucket = waveBucketFor(hwid);
        return activeIntervalCodeobj(
            bucket,
            time,
            [this](const JsonMarkerWaveEntry& entry, int64_t t) { return codeobjAtTime(entry, codeobj_by_line, t); }
        );
    }

    uint64_t codeobjFromOccupancy(HWID hwid, int64_t time) { return occupancy_index.resolve(hwid, time); }

    const std::vector<JsonMarkerWaveEntry>& waveBucketFor(HWID hwid)
    {
        auto [it, inserted] = wave_cache.emplace(hwid, std::vector<JsonMarkerWaveEntry>{});
        if (!inserted) return it->second;
        if (codeobj_by_line.empty()) return it->second;

        auto se_it = store.wave_hierarchy.find(hwid.se);
        if (se_it == store.wave_hierarchy.end()) return it->second;
        auto simd_it = se_it->second.find(hwid.simd);
        if (simd_it == se_it->second.end()) return it->second;
        auto slot_it = simd_it->second.find(hwid.slot);
        if (slot_it == simd_it->second.end()) return it->second;

        for (const auto& [wid, wave_entry] : slot_it->second)
        {
            (void) wid;
            try
            {
                auto wave = store.getWave(wave_entry);
                if (!wave) continue;
                if (wave->cu >= 0 && wave->cu != hwid.cu) continue;

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
    std::map<HWID, std::vector<JsonMarkerWaveEntry>> wave_cache;
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
    store.applyRealtimeAlignment();
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
        if (file.data.contains("dispatches") && file.data["dispatches"].is_object())
        {
            for (auto& [id, name] : file.data["dispatches"].items())
            {
                int kid = -1;
                if (!parseNonNegativeIntKey(id, kid)) continue;
                store.dispatch_resolver.RegisterJsonKernel(kid, name.is_string() ? name.get<std::string>() : "");
            }
        }

        for (auto& [se_name, array] : file.data.items())
        {
            if (!array.is_array() || array.empty()) continue;

            int se = -1;
            if (!parseNonNegativeIntKey(se_name, se)) continue;

            for (auto& v : array)
            {
                if (!v.is_array() || v.size() < 6) continue;

                occupancy_record_t rec{};
                rec.time = v[0].get<int64_t>();
                rec.cu = static_cast<uint8_t>(v[1].get<uint32_t>());
                rec.simd = static_cast<uint8_t>(v[2].get<uint32_t>());
                rec.wave_id = static_cast<uint8_t>(v[3].get<uint32_t>());
                rec.start = v[4].get<uint32_t>() ? 1 : 0;
                int kid = v[5].get<int>();

                if (v.size() >= 11)
                {
                    rec.me_id = v[6].get<uint32_t>();
                    rec.pipe_id = v[7].get<uint32_t>();
                    rec.is_ext = v[8].get<uint32_t>() ? 1 : 0;
                    rec.workgroup_id = v[9].get<uint32_t>();
                    setOccupancyClusterId(rec, v[10].get<uint32_t>());
                    store.occupancy_has_dispatcher_info = true;
                }

                store.dispatch_resolver.RegisterJsonKernel(kid, "");
                rec.pc.address = uint64_t(kid);
                rec.pc.code_object_id = DispatchResolver::JsonKernelCodeObjectId;
                dispatcher.dispatchOccupancy(se, rec);
            }
        }

        emitTimelineEvents(file.data, dispatcher, store);
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
        [&](HWID hwid, uint32_t id, int64_t time) -> ResolvedMarker
        {
            const uint64_t codeobj_id = active_codeobj(hwid, time);
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
