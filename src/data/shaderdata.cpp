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

#include "shaderdata.h"
#include <algorithm>
#include <array>
#include <climits>
#include <future>
#include <iostream>
#include <sstream>
#include "data/marker_walker.h"
#include "util/jsonrequest.hpp"

#ifdef RCV_HAS_TRACE_DECODER
#    include <rocprof_trace_decoder/cxx/code_printing.hpp>
#    include <rocprof_trace_decoder/cxx/funcmap.hpp>
#endif

std::string ShaderDataRecord::ToolTip() const
{
    std::stringstream ss;
    ss << "Shaderdata Record"
       << "\nSE:" << se << "  CU:" << cu << "  SIMD:" << simd << "  WaveID:" << wave_id << "\nTime: " << time
       << "  Value: 0x" << std::hex << value << "  Flags: 0x" << flags << std::dec;
    return ss.str();
}

std::vector<ShaderDataRecord> ShaderDataManager::LoadFile(const std::string& filepath, int se)
{
    std::vector<ShaderDataRecord> local;
    try
    {
        JsonRequest request(filepath, false);
        if (!request.bValid) return local;

        auto& data = request.data;
        if (!data.contains("records")) return local;

        local.reserve(data.value("records_count", 0));

        for (auto& record : data["records"])
        {
            if (record.size() < 6) continue;

            ShaderDataRecord rec;
            rec.time = static_cast<int64_t>(record[0]);
            rec.value = static_cast<uint32_t>(record[1]);
            rec.cu = static_cast<int>(record[2]);
            rec.simd = static_cast<int>(record[3]);
            rec.wave_id = static_cast<int>(record[4]);
            rec.flags = static_cast<int>(record[5]);
            rec.se = se;
            local.push_back(rec);
        }
    }
    catch (std::exception& e)
    {
        std::cout << "Warning: Failed to load shaderdata file " << filepath << ": " << e.what() << std::endl;
    }
    return local;
}

void ShaderDataManager::Load(const nlohmann::json& shaderdata_filenames, const std::string& base_dir)
{
    m_records_by_location.clear();
    m_has_data = false;

    // Collect all (filepath, se) pairs to load
    std::vector<std::pair<std::string, int>> file_tasks;

    for (auto& [se_str, file_list] : shaderdata_filenames.items())
    {
        int se = 0;
        try
        {
            se = std::stoi(se_str);
        }
        catch (...)
        {
            RCV_LOG();
            continue;
        }

        for (auto& file_entry : file_list)
        {
            if (file_entry.size() < 3) continue;
            std::string filename = file_entry[0];
            file_tasks.push_back({base_dir + filename, se});
        }
    }

    if (file_tasks.empty()) return;

    // Load files in parallel (max 8 at a time)
    constexpr size_t MAX_THREADS = 8;
    std::map<HWID, std::vector<ShaderDataRecord>> tmp;
    std::array<std::future<std::vector<ShaderDataRecord>>, MAX_THREADS> futures;
    std::array<int, MAX_THREADS> future_se{};

    auto collect = [&](size_t idx)
    {
        for (auto& rec : futures.at(idx).get()) tmp[{rec.se, rec.cu, rec.simd, rec.wave_id}].push_back(std::move(rec));
    };

    for (size_t i = 0; i < file_tasks.size(); i++)
    {
        size_t slot = i % MAX_THREADS;
        if (i >= MAX_THREADS) collect(slot);
        futures.at(slot) = std::async(std::launch::async, LoadFile, file_tasks[i].first, file_tasks[i].second);
        future_se.at(slot) = file_tasks[i].second;
    }

    for (size_t i = 0; i < std::min(file_tasks.size(), MAX_THREADS); i++) collect(i);

    // Sort each bucket by time and wrap in shared_ptr
    auto cmp = [](const ShaderDataRecord& a, const ShaderDataRecord& b) { return a.time < b.time; };

    for (auto& [key, records] : tmp)
    {
        std::sort(records.begin(), records.end(), cmp);
        m_records_by_location[key] = std::make_shared<const std::vector<ShaderDataRecord>>(std::move(records));
        m_has_data = true;
    }
}

ShaderDataRecordVec ShaderDataManager::GetRecords(HWID hwid) const
{
    auto it = m_records_by_location.find(hwid);
    if (it != m_records_by_location.end()) return it->second;
    return nullptr;
}

void ShaderDataManager::AddRecord(int se, const shaderdata_record_t& rec)
{
    ShaderDataRecord r;
    r.time = rec.time;
    r.value = static_cast<uint32_t>(rec.value);
    r.cu = rec.cu;
    r.simd = rec.simd;
    r.wave_id = rec.wave_id;
    r.flags = rec.flags;
    r.se = se;
    m_pending[{r.se, r.cu, r.simd, r.wave_id}].push_back(std::move(r));
}

void ShaderDataManager::Finalize()
{
    if (m_pending.empty()) return;

    auto cmp = [](const ShaderDataRecord& a, const ShaderDataRecord& b) { return a.time < b.time; };

    for (auto& [key, records] : m_pending)
    {
        std::sort(records.begin(), records.end(), cmp);
        m_records_by_location[key] = std::make_shared<const std::vector<ShaderDataRecord>>(std::move(records));
        m_has_data = true;
    }
    m_pending.clear();
}

void ShaderDataManager::ApplyTimeOffsets(const std::map<int, int64_t>& offsets)
{
    if (offsets.empty()) return;
    for (auto& [hwid, records] : m_pending)
    {
        auto it = offsets.find(hwid.se);
        if (it == offsets.end() || it->second == 0) continue;
        int64_t off = it->second;
        for (auto& r : records) r.time += off;
    }

    for (auto& [key, records_ptr] : m_records_by_location)
    {
        auto it = offsets.find(key.se);
        if (it == offsets.end() || it->second == 0 || !records_ptr) continue;

        auto shifted = std::make_shared<std::vector<ShaderDataRecord>>(*records_ptr);
        for (auto& r : *shifted) r.time += it->second;
        records_ptr = shifted;
    }
    ClearMarkers();
}

std::set<int> ShaderDataManager::SEs() const
{
    std::set<int> ses;
    for (const auto& [key, records] : m_pending)
        if (!records.empty()) ses.insert(key.se);
    for (const auto& [key, records] : m_records_by_location)
        if (records && !records->empty()) ses.insert(key.se);
    return ses;
}

MarkerSpanVec ShaderDataManager::GetMarkers(HWID hwid) const
{
    auto it = m_markers_by_location.find(hwid);
    if (it != m_markers_by_location.end()) return it->second;
    return nullptr;
}

void ShaderDataManager::ClearMarkers()
{
    m_markers_by_location.clear();
    m_marker_diags.clear();
    m_has_markers = false;
}

void ShaderDataManager::ResolveMarkers(const MarkerResolveAtFn& resolver)
{
    // Idempotent: caller may invoke this on every load. Clear stale state first.
    ClearMarkers();

    if (m_records_by_location.empty()) return;

    for (const auto& [hwid, records_ptr] : m_records_by_location)
    {
        if (!records_ptr || records_ptr->empty()) continue;
        const auto& records = *records_ptr;

        // Adapt ShaderDataRecord stream into the pure walker's input format.
        std::vector<MarkerInputRecord> in;
        in.reserve(records.size());
        for (const auto& rec : records) in.push_back({rec.time, rec.value});

        MarkerResolveFn bucket_resolver;
        if (resolver)
        {
            bucket_resolver = [&](uint32_t id, int64_t time) -> ResolvedMarker { return resolver(hwid, id, time); };
        }

        std::vector<MarkerSpan> spans;
        walkMarkerStream(in, hwid, bucket_resolver, &spans, &m_marker_diags);

        if (spans.empty()) continue;

        // Sort by enter_time so paint can binary-search the visible range.
        std::sort(
            spans.begin(),
            spans.end(),
            [](const MarkerSpan& a, const MarkerSpan& b) { return a.enter_time < b.enter_time; }
        );

        m_markers_by_location[hwid] = std::make_shared<const std::vector<MarkerSpan>>(std::move(spans));
        m_has_markers = true;
    }
}

#ifdef RCV_HAS_TRACE_DECODER
namespace
{
using FuncmapEntryKind = rocprof_trace_decoder::codeobj::FuncmapEntryKind;

MarkerKind toMarkerKind(FuncmapEntryKind k)
{
    switch (k)
    {
        case FuncmapEntryKind::Function: return MarkerKind::Function;
        case FuncmapEntryKind::Kernel: return MarkerKind::Kernel;
        case FuncmapEntryKind::UserScope: return MarkerKind::UserScope;
        case FuncmapEntryKind::Point: return MarkerKind::Point;
    }
    return MarkerKind::Unknown;
}
} // namespace

void ShaderDataManager::ResolveMarkers(
    rocprof_trace_decoder::codeobj::CodeobjAddressTranslate& codeobj_map, const ActiveCodeobjFn& active_codeobj_fn
)
{
    namespace cobj = rocprof_trace_decoder::codeobj;

    ResolveMarkers(
        [&](HWID hwid, uint32_t id, int64_t time) -> ResolvedMarker
        {
            const uint64_t coid = active_codeobj_fn ? active_codeobj_fn(hwid, time) : 0ull;
            if (coid == 0) return {};

            cobj::Funcmap::EntryPtr entry = codeobj_map.getMarker(coid, id);

            ResolvedMarker out;
            if (entry)
            {
                out.found = true;
                out.kind = toMarkerKind(entry->kind);
                out.name = entry->name;
                out.source_loc = entry->source_loc;
            }
            return out;
        }
    );
}
#endif // RCV_HAS_TRACE_DECODER

int FindShaderDataRecord(const ShaderDataRecordVec& records, int64_t clock_pos, int64_t hit_width)
{
    if (!records || records->empty()) return -1;

    const auto& recs = *records;

    auto it = std::upper_bound(
        recs.begin(), recs.end(), clock_pos, [](int64_t c, const ShaderDataRecord& r) { return c < r.time; }
    );

    if (it != recs.begin())
    {
        --it;
        if (clock_pos <= it->time + hit_width) return static_cast<int>(it - recs.begin());
    }

    return -1;
}
