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

#ifdef RCV_HAS_TRACE_DECODER

#    include "trace_decoder_emitter.h"

#    include <algorithm>
#    include <atomic>
#    include <cstring>
#    include <filesystem>
#    include <fstream>
#    include <iostream>
#    include <limits>
#    include <semaphore>
#    include <sstream>
#    include <thread>
#    include <unordered_set>
#    include "code/codeload.hpp"
#    include "data/datastore.h"
#    include "data/record_dispatcher.h"
#    include "data/shaderdata.h"
#    include "data/sqtt_funcmap_json.h"
#    include "json/include/nlohmann/json.hpp"
#    include "util/diagnostic_log.h"
#    include "util/memtracker.h"

#    define C_API_BEGIN                                                                                                \
        try                                                                                                            \
        {
#    define C_API_END                                                                                                  \
        }                                                                                                              \
        catch (std::exception & e) { QWARNING(false, e.what(), ); }

namespace fs = std::filesystem;

namespace
{
std::atomic<uint64_t> arch_offset{};

std::string findAttSidecar(const InputInfo& info, const char* filename)
{
    std::vector<fs::path> dirs;
    if (!info.att_files.empty())
    {
        fs::path dir = fs::path(info.att_files.front()).parent_path();
        dirs.push_back(dir.empty() ? fs::path(".") : dir);
    }
    if (!info.base_path.empty()) dirs.push_back(fs::path(info.base_path));

    for (const auto& dir : dirs)
    {
        if (dir.empty()) continue;
        std::error_code ec;
        fs::path candidate = dir / filename;
        if (fs::is_regular_file(candidate, ec) && !ec) return candidate.string();
    }

    return {};
}

MarkerKind toMarkerKind(rocprof_trace_decoder::codeobj::FuncmapEntryKind kind)
{
    using FuncmapEntryKind = rocprof_trace_decoder::codeobj::FuncmapEntryKind;
    switch (kind)
    {
        case FuncmapEntryKind::Function: return MarkerKind::Function;
        case FuncmapEntryKind::Kernel: return MarkerKind::Kernel;
        case FuncmapEntryKind::UserScope: return MarkerKind::UserScope;
        case FuncmapEntryKind::Point: return MarkerKind::Point;
    }
    return MarkerKind::Unknown;
}

bool hasRuntimeMarkerEntries(rocprof_trace_decoder::codeobj::CodeobjAddressTranslate& codeobj_map, uint64_t codeobj_id)
{
    if (codeobj_id == 0) return false;
    try
    {
        return !codeobj_map.getFuncmap(codeobj_id).by_id.empty();
    }
    catch (...)
    {
        return false;
    }
}

template <typename WaveRecord> int waveClusterId(const WaveRecord& wave)
{
    if constexpr (requires { wave.cluster_id; })
        return static_cast<int>(wave.cluster_id);
    else
        return 0;
}
} // namespace

TraceDecoderEmitter::TraceDecoderEmitter(const InputInfo& in, RecordDispatcher& disp, DataStore& st) :
info(in),
dispatcher(disp),
store(st),
active_codeobj_index(st, [](const occupancy_record_t& rec) { return rec.pc.code_object_id; })
{
    auto status = rocprof_trace_decoder_create_handle(&decoder_handle);
    if (status != ROCPROFILER_THREAD_TRACE_DECODER_STATUS_SUCCESS)
    {
        std::cerr << "Failed to create trace decoder handle: " << rocprof_trace_decoder_get_status_string(status)
                  << std::endl;
        decoder_handle.handle = 0;
    }
}

TraceDecoderEmitter::~TraceDecoderEmitter()
{
    if (decoder_handle.handle != 0) rocprof_trace_decoder_destroy_handle(decoder_handle);
}

void TraceDecoderEmitter::addParseError(const std::string& message)
{
    std::lock_guard<std::mutex> lock(parse_errors_mutex);
    parse_errors.push_back(message);
}

void TraceDecoderEmitter::run()
{
    if (decoder_handle.handle == 0) return;

    const std::string code_json_path =
        info.code_json_override.empty() ? findAttSidecar(info, "code.json") : info.code_json_override;
    const std::string snapshots_json_path =
        info.snapshots_json_override.empty() ? findAttSidecar(info, "snapshots.json") : info.snapshots_json_override;

    // If a code.json override or .att sibling was found, load it into a temporary cache.
    // We can't put it into store.code directly: rocprofv3 code.json is an
    // optional metadata sidecar and is not guaranteed to be a complete listing
    // of every traced instruction. Instead we always build store.code from the
    // ISA cache and layer code.json's richer fields (cppline, stallreasons,
    // ...) onto matching rows in buildCodeFromISACache.
    std::vector<CodeData> code_json_data;
    SqttFuncmapJson code_json_funcmap;
    if (!code_json_path.empty())
    {
        code_json_funcmap = SqttFuncmapJson::LoadFromCodeJson(code_json_path);
        for (const auto& diag : code_json_funcmap.diagnostics()) std::cerr << "Warning: " << diag << std::endl;

        try
        {
            code_json_data = CodeData::LoadCode(code_json_path);
        }
        catch (const std::exception& e)
        {
            std::cerr << "Warning: Failed to load code.json: " << code_json_path << ": " << e.what() << std::endl;
        }
        catch (...)
        {
            std::cerr << "Warning: Failed to load code.json: " << code_json_path << std::endl;
        }
    }

    // If a snapshots.json override or .att sibling was found, load source snapshots.
    if (!snapshots_json_path.empty())
    {
        try
        {
            std::ifstream snap_file(snapshots_json_path);
            if (snap_file.is_open())
            {
                auto snap_json = nlohmann::json::parse(snap_file);
                store.loadSourceSnapshots(snap_json, fs::path(snapshots_json_path).parent_path().string() + "/");
            }
        }
        catch (...)
        {
            std::cerr << "Warning: Failed to load snapshots.json: " << snapshots_json_path << std::endl;
        }
    }

    loadCodeObjects();

    // Set the ISA callback (Mode 2): we provide ISA text to the decoder and cache it
    // for our own display (ISA view, hotspot, etc). The callback resolves PC addresses
    // by disassembling loaded .out code objects via COMGR, or from code.json if provided.
    auto status = rocprof_trace_decoder_set_isa_callback(decoder_handle, isaCallback, this);
    if (status != ROCPROFILER_THREAD_TRACE_DECODER_STATUS_SUCCESS)
    {
        std::cerr << "Failed to set ISA callback: " << rocprof_trace_decoder_get_status_string(status) << std::endl;
        return;
    }

    // Seed isa_cache from code.json before parseATTFiles so the callback can serve PCs
    // when no disasm backend is available; otherwise the stitcher abandons every wave.
    preSeedIsaCacheFromCodeJson(code_json_data);

    parseATTFiles();

    // Cross-SE cycle alignment via REALTIME records. Must happen before any
    // downstream pass that consumes wave/occupancy/shaderdata times.
    alignSEClocks();

    // After parsing, isa_cache contains entries for instructions seen in the trace.
    // Now disassemble the full kernels for only those code objects/kernels that were hit.
    preDisassembleKernels();

    // Always build store.code from the ISA cache (so every PC the trace touches
    // gets a row); layer code.json metadata onto matching addresses if provided.
    buildCodeFromISACache(code_json_data);

    // Populate dispatches from occupancy PCs + symbol names
    buildDispatches();

    dispatcher.signalComplete();

    // Resolve marker spans now that all shaderdata records are in. Marker ids
    // are scoped to the active code object; neither code.json nor the loaded
    // code objects are guaranteed to contain every funcmap row, so use them as
    // complementary sources.
    if (store.shaderdata && store.shaderdata->HasData())
    {
        store.shaderdata->ResolveMarkers(
            [this, &code_json_funcmap](HWID hwid, uint32_t id, int64_t time)
            {
                namespace cobj = rocprof_trace_decoder::codeobj;

                const uint64_t codeobj_id = this->activeCodeobjAt(hwid, time);
                ResolvedMarker json_resolved = code_json_funcmap.Resolve(id, codeobj_id);

                const bool has_decoder_markers = hasRuntimeMarkerEntries(codeobj_map, codeobj_id);
                if (!json_resolved.found && !has_decoder_markers) return ResolvedMarker{};

                cobj::Funcmap::EntryPtr entry = has_decoder_markers ? codeobj_map.getMarker(codeobj_id, id) : nullptr;
                if (!entry)
                {
                    if (has_decoder_markers) json_resolved.metadata_available = true;
                    return json_resolved;
                }

                ResolvedMarker resolved;
                resolved.metadata_available = true;
                resolved.found = true;
                resolved.kind = toMarkerKind(entry->kind);
                resolved.name = entry->name;
                resolved.source_loc = entry->source_loc;
                if (resolved.name.empty()) resolved.name = json_resolved.name;
                if (resolved.source_loc.empty()) resolved.source_loc = json_resolved.source_loc;
                return resolved;
            }
        );
    }
}

void TraceDecoderEmitter::alignSEClocks()
{
    if (store.applyRealtimeAlignment()) active_codeobj_index.clear();
}

uint64_t TraceDecoderEmitter::activeCodeobjAt(HWID hwid, int64_t time) const
{
    return active_codeobj_index.resolve(hwid, time);
}

namespace
{
/// Parse "codeobj_<id>.out" → id. Returns 0 if the filename doesn't match that
/// shape, signalling "no usable id" to the caller (which can then skip the file
/// for the lazy-lookup map; the eager path falls back to a hash so behavior is
/// preserved for non-standard names).
uint64_t parseCodeobjIdFromPath(const std::string& path)
{
    auto stem = fs::path(path).stem().string();
    auto pos = stem.find_last_of('_');
    if (pos == std::string::npos) return 0;
    try
    {
        return std::stoull(stem.substr(pos + 1));
    }
    catch (...)
    {
        RCV_LOG();
        return 0;
    }
}

} // namespace

bool TraceDecoderEmitter::loadCodeObjectFile(const std::string& path, uint64_t code_object_id)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        std::cerr << "Warning: Cannot open code object: " << path << std::endl;
        return false;
    }

    auto file_size = file.tellg();
    file.seekg(0);

    std::vector<char> buffer(file_size);
    file.read(buffer.data(), file_size);

    try
    {
        // TODO: We dont know the load address so we use an address not available
        constexpr uint64_t dummy_addr = uint64_t{1} << 48;
        codeobj_map.addDecoder(buffer.data(), buffer.size(), code_object_id, dummy_addr * code_object_id, file_size);
        return true;
    }
    catch (...)
    {
        std::cerr << "Warning: Failed to disassemble code object: " << path << std::endl;
        return false;
    }
}

void TraceDecoderEmitter::loadCodeObjects()
{
    // Discover every .out file we have on disk: the explicit info.out_files list
    // (from auto-detection) plus any siblings under info.base_path. Eager-load
    // each one — addDecoder() just registers the file with codeobj_map; the
    // expensive per-PC COMGR disassembly stays lazy via codeobj_map.get() inside
    // isaCallback.
    std::unordered_set<std::string> seen;
    std::map<uint64_t, std::string> loaded_ids;
    auto load = [&](const std::string& out_path)
    {
        if (!seen.insert(out_path).second) return;
        uint64_t id = parseCodeobjIdFromPath(out_path);
        auto id_it = loaded_ids.find(id);
        if (id_it != loaded_ids.end())
        {
            std::ostringstream oss;
            oss << "Code object ID conflict: " << id_it->second << " and " << out_path << " both use ID " << id
                << ". Skipping " << out_path << ".";
            addParseError(oss.str());
            return;
        }
        if (loadCodeObjectFile(out_path, id)) loaded_ids.emplace(id, out_path);
    };

    for (const auto& out_path : info.out_files) load(out_path);

    if (!info.base_path.empty())
    {
        std::error_code ec;
        if (fs::is_directory(info.base_path, ec))
        {
            // Wrap the whole walk in try/catch — even with the ec overload,
            // individual entry queries (is_regular_file, extension) can throw
            // on permission errors or transient filesystem issues, and one bad
            // sibling shouldn't abort code-object loading entirely.
            try
            {
                for (const auto& entry : fs::directory_iterator(info.base_path, ec))
                {
                    if (ec) break;
                    std::error_code entry_ec;
                    if (!entry.is_regular_file(entry_ec) || entry_ec) continue;
                    if (entry.path().extension() != ".out" && entry.path().extension() != ".hsaco") continue;
                    load(entry.path().string());
                }
            }
            catch (const std::exception& e)
            {
                std::cerr << "Warning: directory walk failed under " << info.base_path << ": " << e.what() << std::endl;
            }
        }
    }
}

void TraceDecoderEmitter::preDisassembleKernels()
{
    // Called after parseATTFiles(): isa_cache already contains entries for instructions
    // seen in the trace. For each code object that has cache entries, find which kernel(s)
    // those addresses belong to and disassemble the full kernel.

    for (auto& [cobj_id, addr_map] : isa_cache)
    {
        auto symbols = codeobj_map.getSymbolMap(cobj_id);
        if (symbols.empty()) continue;

        // Find which kernels were hit by checking if any cached address falls within them
        for (auto& [sym_vaddr, sym_info] : symbols)
        {
            if (sym_info.mem_size == 0) continue;

            uint64_t end_addr = sym_vaddr + sym_info.mem_size;

            // Check if any cached instruction falls in [sym_vaddr, end_addr)
            auto it = addr_map.lower_bound(sym_vaddr);
            if (it == addr_map.end() || it->first >= end_addr) continue;

            // This kernel was hit — disassemble it fully
            uint64_t addr = sym_vaddr;
            while (addr < end_addr)
            {
                auto cache_it = addr_map.find(addr);
                if (cache_it != addr_map.end())
                {
                    addr += cache_it->second.memory_size;
                    if (cache_it->second.memory_size == 0) break;
                    continue;
                }

                std::unique_ptr<rocprof_trace_decoder::codeobj::Instruction> inst;
                try
                {
                    inst = codeobj_map.get(cobj_id, addr);
                }
                catch (...)
                {
                    RCV_LOG();
                    break;
                }
                if (!inst || inst->size == 0) break;

                ISALine line;
                line.text = inst->inst;
                line.memory_size = inst->size;
                line.addr = addr;
                line.codeobj_id = cobj_id;

                addr_map[addr] = line;
                addr += inst->size;
            }
        }
    }
}

rocprofiler_thread_trace_decoder_status_t TraceDecoderEmitter::isaCallback(
    char* instruction,
    uint64_t* memory_size,
    uint64_t* size,
    rocprofiler_thread_trace_decoder_pc_t address,
    void* userdata
)
{
    C_API_BEGIN

    auto* self = static_cast<TraceDecoderEmitter*>(userdata);
    std::lock_guard<std::mutex> lock(self->isa_mutex);

    if (address.code_object_id == 0)
    {
        if (address.address == 0 && arch_offset == 0)
        {
            auto symbols = self->codeobj_map.getSymbolMap(0);
            for (auto& [_, symbol] : symbols) arch_offset = symbol.vaddr;

            auto it = self->isa_cache.find(0);
            if (arch_offset == 0 && it != self->isa_cache.end() && it->second.size() != 0)
            {
                arch_offset = INT64_MAX;
                for (auto& [addr, _] : it->second) arch_offset = std::min(arch_offset.load(), addr);
            }
        }
        if (!arch_offset) return ROCPROFILER_THREAD_TRACE_DECODER_STATUS_ERROR;

        address.address += arch_offset;
    }

    // Check the ISA cache first. Reject any cached entry with memory_size == 0:
    // the decoder uses memory_size to step to the next PC (see CodeService::
    // GetInstruction in rocprof_trace_decoder.cpp), and a 0-byte step would
    // make the stitcher loop on the same address.
    auto cobj_it = self->isa_cache.find(address.code_object_id);
    if (cobj_it != self->isa_cache.end())
    {
        auto line_it = cobj_it->second.find(address.address);
        if (line_it != cobj_it->second.end() && line_it->second.memory_size > 0)
        {
            auto& cached = line_it->second;
            if (cached.text.size() > *size)
            {
                *size = cached.text.size();
                return ROCPROFILER_THREAD_TRACE_DECODER_STATUS_ERROR_OUT_OF_RESOURCES;
            }
            std::memcpy(instruction, cached.text.c_str(), cached.text.size());
            *size = cached.text.size();
            *memory_size = cached.memory_size;
            return ROCPROFILER_THREAD_TRACE_DECODER_STATUS_SUCCESS;
        }
    }

    // Not cached — try to disassemble via CodeobjMap (COMGR).
    auto inst = self->codeobj_map.get(address.code_object_id, address.address);
    if (!inst || inst->inst.empty() || inst->size == 0)
    {
        // Three failure modes, all handled the same way:
        //   1. No decoder registered for this code_object_id.
        //   2. PC falls outside any known symbol in this codeobj.
        //   3. Decoder returned a zero-size instruction (degenerate symbol).
        // We must NOT fabricate placeholder text with a guessed size: the
        // Stitcher uses memory_size to walk to the next PC, so a wrong/guessed
        // size makes it loop until it falls off the trace (or runs us OOM via
        // the isa_cache). Signal invalid argument so the decoder stops walking
        // this wave instead. Stitcher::stitchWave catches the resulting throw
        // (stitch.cpp:87,123,149,161) and skips this wave only — the rest of
        // the parse continues.
        return ROCPROFILER_THREAD_TRACE_DECODER_STATUS_ERROR_INVALID_ARGUMENT;
    }

    const std::string& inst_text = inst->inst;
    uint64_t inst_mem_size = inst->size;

    ISALine line;
    line.text = inst_text;
    line.memory_size = inst_mem_size;
    line.addr = address.address;
    line.codeobj_id = address.code_object_id;

    self->isa_cache[address.code_object_id][address.address] = line;

    if (inst_text.size() > *size)
    {
        *size = inst_text.size();
        return ROCPROFILER_THREAD_TRACE_DECODER_STATUS_ERROR_OUT_OF_RESOURCES;
    }

    std::memcpy(instruction, inst_text.c_str(), inst_text.size());
    *size = inst_text.size();
    *memory_size = inst_mem_size;
    return ROCPROFILER_THREAD_TRACE_DECODER_STATUS_SUCCESS;

    C_API_END
    return ROCPROFILER_THREAD_TRACE_DECODER_STATUS_ERROR;
}

void TraceDecoderEmitter::preSeedIsaCacheFromCodeJson(const std::vector<CodeData>& code_json_data)
{
    // Seed isa_cache so the callback can answer PCs the decoder's disassembler can't reach
    // (no LLVM/COMGR backend, missing .out). memory_size is derived from address deltas;
    // the last entry per codeobj falls back to 4B (min GFX instr width).
    std::lock_guard<std::mutex> lock(isa_mutex);
    for (const auto& cd : code_json_data)
    {
        if (!cd.line || cd.line->addr == 0) continue;
        auto& slot = isa_cache[uint64_t(cd.line->codeobj_id)][uint64_t(cd.line->addr)];
        slot.text = cd.line->inst;
        slot.addr = uint64_t(cd.line->addr);
        slot.codeobj_id = uint64_t(cd.line->codeobj_id);
    }
    for (auto& [cobj_id, addr_map] : isa_cache)
    {
        for (auto it = addr_map.begin(); it != addr_map.end(); ++it)
        {
            if (it->second.memory_size != 0) continue;
            auto next = std::next(it);
            it->second.memory_size = (next != addr_map.end()) ? std::max<uint64_t>(next->first - it->first, 4) : 4;
        }
    }
}

void TraceDecoderEmitter::buildCodeFromISACache(const std::vector<CodeData>& code_json_data)
{
    // The ISA cache is map<cobj_id, map<addr, ISALine>>.
    // Both levels are std::map so iterating gives (cobj_id, addr) order.
    // Assign line_numbers sequentially in this address order.

    // Build (cobj_id, addr) -> code.json Line lookup so that, when we emit
    // each store.code row below, we can layer the richer metadata onto
    // addresses code.json knows about (typically a single kernel).
    std::map<std::pair<uint64_t, uint64_t>, std::shared_ptr<CodeData::Line>> json_overlay;
    for (const auto& cd : code_json_data)
    {
        if (cd.line && cd.line->addr != 0)
            json_overlay.emplace(std::make_pair(uint64_t(cd.line->codeobj_id), uint64_t(cd.line->addr)), cd.line);
    }

    // 1. Assign address-ordered line numbers and build PC → line_number map
    struct PCKey
    {
        uint64_t cobj_id;
        uint64_t addr;
        bool operator<(const PCKey& o) const { return std::tie(cobj_id, addr) < std::tie(o.cobj_id, o.addr); }
    };
    std::map<PCKey, int> pc_to_line;
    int line_num = 1;

    for (auto& [cobj_id, addr_map] : isa_cache)
        for (auto& [addr, isa_line] : addr_map)
        {
            pc_to_line[{cobj_id, addr}] = line_num;
            line_num++;
        }

    // 2. Resolve PC → line_number in all wave records
    // No locking needed: parsing threads have joined and UI hasn't started reading yet.
    for (auto& [id, rec] : store.wave_records)
        for (auto& inst : rec.instructions)
        {
            auto it = pc_to_line.find({inst.pc.code_object_id, inst.pc.address});
            if (it != pc_to_line.end()) inst.line_number = it->second;
        }

    // 3. Build symbol labels keyed by line number (only at kernel start addresses)
    std::map<int, std::string> label_at_line;
    for (auto& [cobj_id, addr_map] : isa_cache)
    {
        auto symbols = codeobj_map.getSymbolMap(cobj_id);
        for (auto& [sym_vaddr, sym_info] : symbols)
        {
            auto it = pc_to_line.find({cobj_id, sym_vaddr});
            if (it != pc_to_line.end()) label_at_line[it->second] = sym_info.name;
        }
    }

    // 4. Accumulate per-line stats from wave records
    struct LineStats
    {
        int hitcount = 0;
        int64_t latency_sum = 0;
        int64_t idle_sum = 0;
        int64_t stall_sum = 0;
    };
    std::map<int, LineStats> stats;

    for (auto& [id, rec] : store.wave_records)
    {
        int64_t prev_end = rec.begin;
        for (auto& inst : rec.instructions)
        {
            int latency = std::max(inst.stall, inst.duration);
            if (inst.line_number < 0)
            {
                prev_end = inst.time + latency;
                continue;
            }
            auto& s = stats[inst.line_number];
            s.hitcount++;
            s.latency_sum += latency;
            s.stall_sum += inst.stall;
            if (inst.time > prev_end) s.idle_sum += inst.time - prev_end;
            prev_end = inst.time + latency;
        }
    }

    // 5. Build CodeData in address order
    int label_index = line_num; // labels get indices after all instructions
    int code_line = 1;

    for (auto& [cobj_id, addr_map] : isa_cache)
    {
        for (auto& [addr, isa_line] : addr_map)
        {
            auto label_it = label_at_line.find(code_line);
            if (label_it != label_at_line.end())
            {
                // LabelMinimap::IsLabel only matches text starting with
                // "label" or "; _". rocprofv3's code.json convention is
                // "; _Z..." (mangled C++ name). Mirror that so the same
                // minimap code path lights up for trace_decoder inputs.
                // Force a leading underscore for unmangled (C / non-_Z)
                // symbols so the "; _" prefix rule still matches.
                const std::string& nm = label_it->second;
                std::string label_text = (!nm.empty() && nm[0] == '_') ? ("; " + nm) : ("; _" + nm);
                store.code.push_back(CodeData(
                    label_index++,
                    0,
                    int64_t(addr),
                    int64_t(cobj_id),
                    0,
                    0,
                    0,
                    0,
                    0,
                    label_text,
                    "",
                    std::vector<int64_t>()
                ));
            }

            auto json_it = json_overlay.find({cobj_id, addr});
            auto& s = stats[code_line];
            if (json_it != json_overlay.end())
            {
                // hitcount / latency / idle / stall reflect what *this* trace
                // measured — always take them from the wave-trace stats (`s`).
                // Only the richer code.json-derived metadata (cppline, stall
                // reasons, pc samples/stalls) layers on top.
                const auto& jl = *json_it->second;
                store.code.push_back(CodeData(
                    code_line,
                    s.hitcount,
                    int64_t(addr),
                    int64_t(cobj_id),
                    s.latency_sum,
                    s.idle_sum,
                    s.stall_sum,
                    jl.pcsamples,
                    jl.pcstalls,
                    isa_line.text,
                    jl.cppline,
                    jl.stallreasons
                ));
            }
            else
            {
                store.code.push_back(CodeData(
                    code_line,
                    s.hitcount,
                    int64_t(addr),
                    int64_t(cobj_id),
                    s.latency_sum,
                    s.idle_sum,
                    s.stall_sum,
                    0,
                    0,
                    isa_line.text,
                    "",
                    std::vector<int64_t>()
                ));
            }
            code_line++;

            CodeData::ApplyCustomType(store.code.back().line);
        }
    }
}

void TraceDecoderEmitter::buildDispatches()
{
    struct PcHash
    {
        size_t operator()(const rocprofiler_thread_trace_decoder_pc_t& p) const noexcept
        {
            return (p.address << 6) ^ p.code_object_id;
        }
    };
    struct PcEq
    {
        bool operator()(const rocprofiler_thread_trace_decoder_pc_t& a, const rocprofiler_thread_trace_decoder_pc_t& b)
            const noexcept
        {
            return a.address == b.address && a.code_object_id == b.code_object_id;
        }
    };
    std::unordered_set<rocprofiler_thread_trace_decoder_pc_t, PcHash, PcEq> seen;

    auto register_pc = [&](rocprofiler_thread_trace_decoder_pc_t& pc)
    {
        if (pc.code_object_id == 0) pc.address += arch_offset;
        if (!seen.insert(pc).second) return;

        const char* name = static_cast<rocprof_trace_decoder::codeobj::CodeobjMap&>(codeobj_map)
                               .getSymbolName(pc.code_object_id, pc.address);

        std::string fallback_name;
        if (name == nullptr)
        {
            std::stringstream ss;
            ss << "0 / 0x" << std::hex << pc.address;
            fallback_name = ss.str();
            name = fallback_name.c_str();
        }
        store.dispatch_resolver.Register(pc.code_object_id, pc.address, name);
    };

    for (auto& [se, records] : store.occupancy_by_se)
        for (auto& rec : records) register_pc(rec.pc);

    for (auto& [se, records] : store.dispatch_records_by_se)
        for (auto& rec : records) register_pc(rec.entry_point);
}

void TraceDecoderEmitter::parseATTFiles()
{
    // Sort .att files — typically named seN.att
    auto att_files = info.att_files;
    std::sort(att_files.begin(), att_files.end());

    // Memory budget: ~100MB concurrent .att data
    constexpr size_t MEMORY_BUDGET = 100 * 1024 * 1024;
    std::atomic<size_t> inflight_bytes{0};

    // Group .att files by SE number (extract from filename "seN.att" or "N.att")
    struct SEFile
    {
        int se;
        std::string path;
    };
    std::vector<SEFile> se_files;

    for (const auto& att_path : att_files)
    {
        auto stem = fs::path(att_path).stem().string();
        int se = -1;
        // Try "seN" pattern (e.g. "se0.att")
        if (stem.size() > 2 && stem.substr(0, 2) == "se")
        {
            try
            {
                se = std::stoi(stem.substr(2));
            }
            catch (...)
            {
                RCV_LOG();
            }
        }

        // Try "*_shader_engine_N_*" pattern (rocprofv3: pid_tid_shader_engine_SE_SA.att)
        if (se < 0)
        {
            auto pos = stem.find("shader_engine_");
            if (pos != std::string::npos)
            {
                auto after = stem.substr(pos + 14);
                auto underscore = after.find('_');
                try
                {
                    se = std::stoi(after.substr(0, underscore));
                }
                catch (...)
                {
                    RCV_LOG();
                }
            }
        }

        // Fallback: just use the number
        if (se < 0)
        {
            try
            {
                se = std::stoi(stem);
            }
            catch (...)
            {
                RCV_LOG();
                se = static_cast<int>(se_files.size());
            }
        }

        se_files.push_back({se, att_path});
    }

    // Parse all SEs with bounded parallelism
    std::vector<std::thread> threads;
    std::counting_semaphore<8> semaphore(8);

    for (auto& se_file : se_files)
    {
        std::error_code ec;
        const auto raw_file_size = fs::file_size(se_file.path, ec);
        if (ec)
        {
            addParseError("SE" + std::to_string(se_file.se) + " (" + se_file.path + "): " + ec.message());
            continue;
        }
        const auto file_size =
            static_cast<size_t>(std::min<uintmax_t>(raw_file_size, std::numeric_limits<size_t>::max()));

        // Wait if adding this file would exceed the memory budget
        while (inflight_bytes.load() + file_size > MEMORY_BUDGET && inflight_bytes.load() > 0)
            std::this_thread::yield();

        inflight_bytes += file_size;
        semaphore.acquire();

        threads.emplace_back(
            [this, &semaphore, &inflight_bytes, se = se_file.se, path = se_file.path, file_size]()
            {
                // Read the .att file
                std::ifstream file(path, std::ios::binary);
                if (!file.is_open())
                {
                    std::string msg = "SE" + std::to_string(se) + " (" + path + "): cannot open ATT file";
                    std::cerr << "Warning: " << msg << std::endl;
                    addParseError(msg);
                    inflight_bytes -= file_size;
                    semaphore.release();
                    return;
                }

                std::vector<uint8_t> buffer(file_size);
                file.read(reinterpret_cast<char*>(buffer.data()), file_size);
                file.close();

                // Set up per-SE parse context
                ParseContext ctx;
                ctx.emitter = this;
                ctx.dispatcher = &dispatcher;
                ctx.store = &store;
                ctx.se = se;
                ctx.next_dispatch_id = 0;

                auto status =
                    rocprof_trace_decoder_parse(decoder_handle, buffer.data(), buffer.size(), traceCallback, &ctx);

                if (status != ROCPROFILER_THREAD_TRACE_DECODER_STATUS_SUCCESS)
                {
                    std::ostringstream oss;
                    oss << "SE" << se << " (" << path << "): " << rocprof_trace_decoder_get_status_string(status);
                    addParseError(oss.str());
                }

                // Free the buffer immediately
                buffer.clear();
                buffer.shrink_to_fit();

                inflight_bytes -= file_size;
                semaphore.release();
            }
        );
    }

    for (auto& thread : threads)
        if (thread.joinable()) thread.join();
}

rocprofiler_thread_trace_decoder_status_t TraceDecoderEmitter::traceCallback(
    rocprofiler_thread_trace_decoder_record_type_t record_type_id,
    void* trace_events,
    uint64_t trace_size,
    void* userdata
)
{
    C_API_BEGIN
    auto* ctx = static_cast<ParseContext*>(userdata);

    switch (record_type_id)
    {
        case ROCPROFILER_THREAD_TRACE_DECODER_RECORD_GFXIP:
        {
            // The decoder passes gfxip as the pointer value itself (reinterpret_cast<void*>(gfxip)),
            // not as a pointer to data.
            int gfxip = static_cast<int>(reinterpret_cast<uintptr_t>(trace_events));
            ctx->dispatcher->dispatchMetadata(record_type_t::GFXIP, &gfxip);
            break;
        }

        case ROCPROFILER_THREAD_TRACE_DECODER_RECORD_WAVE:
        {
            auto* wave = static_cast<rocprofiler_thread_trace_decoder_wave_t*>(trace_events);
            if (trace_size != 1)
                std::cerr << "[trace_decoder][se=" << ctx->se << "] warning: expected one WAVE record, got "
                          << trace_size << "\n";
            if (trace_size == 0) break;
            handleWave(*ctx, wave);
            break;
        }

        case ROCPROFILER_THREAD_TRACE_DECODER_RECORD_OCCUPANCY:
        {
            auto* occ = static_cast<rocprofiler_thread_trace_decoder_occupancy_t*>(trace_events);
            handleOccupancy(*ctx, occ, trace_size);
            break;
        }

        case ROCPROFILER_THREAD_TRACE_DECODER_RECORD_PERFEVENT:
        {
            auto* perf = static_cast<rocprofiler_thread_trace_decoder_perfevent_t*>(trace_events);
            handlePerfEvent(*ctx, perf, trace_size);
            break;
        }

        case ROCPROFILER_THREAD_TRACE_DECODER_RECORD_EVENT:
        {
            auto* event = static_cast<rocprofiler_thread_trace_decoder_event_t*>(trace_events);
            for (size_t i = 0; i < trace_size; i++) handleTraceEvent(*ctx, event + i);
            break;
        }

        case ROCPROFILER_THREAD_TRACE_DECODER_RECORD_DISPATCH:
        {
            auto* dispatch = static_cast<rocprofiler_thread_trace_decoder_dispatch_t*>(trace_events);
            handleDispatch(*ctx, dispatch, trace_size);
            break;
        }

        case ROCPROFILER_THREAD_TRACE_DECODER_RECORD_SHADERDATA:
        {
            auto* sd = static_cast<rocprofiler_thread_trace_decoder_shaderdata_t*>(trace_events);
            handleShaderData(*ctx, sd, trace_size);
            break;
        }

        case ROCPROFILER_THREAD_TRACE_DECODER_RECORD_REALTIME:
        {
            auto* rt = static_cast<rocprofiler_thread_trace_decoder_realtime_t*>(trace_events);
            handleRealtime(*ctx, rt, trace_size);
            break;
        }

        case ROCPROFILER_THREAD_TRACE_DECODER_RECORD_RT_FREQUENCY:
        {
            int64_t freq = static_cast<int64_t>(*static_cast<uint64_t*>(trace_events));
            ctx->dispatcher->dispatchMetadata(record_type_t::RT_FREQUENCY, &freq);
            break;
        }

        case ROCPROFILER_THREAD_TRACE_DECODER_RECORD_INST_OTHER_SIMD:
        {
            auto* inst = static_cast<rocprofiler_thread_trace_decoder_inst_other_simd_t*>(trace_events);
            handleOtherSimd(*ctx, inst, trace_size);
            break;
        }

        case ROCPROFILER_THREAD_TRACE_DECODER_RECORD_INFO:
        {
            auto* infos = static_cast<rocprofiler_thread_trace_decoder_info_t*>(trace_events);
            for (uint64_t i = 0; i < trace_size; ++i)
            {
                const char* desc = rocprof_trace_decoder_get_info_string(infos[i]);
                std::cerr << "[trace_decoder][se=" << ctx->se << "] INFO " << static_cast<int>(infos[i]) << ": "
                          << (desc ? desc : "(null)") << '\n';
            }
            break;
        }
        default: break;
    }
    return ROCPROFILER_THREAD_TRACE_DECODER_STATUS_SUCCESS;

    C_API_END

    return ROCPROFILER_THREAD_TRACE_DECODER_STATUS_ERROR;
}

void TraceDecoderEmitter::handleWave(ParseContext& ctx, const rocprofiler_thread_trace_decoder_wave_t* wave)
{
    if (!wave) return;
    if (wave->size != sizeof(rocprofiler_thread_trace_decoder_wave_t))
    {
        std::cerr << "[trace_decoder][se=" << ctx.se << "] ignoring WAVE record with size " << wave->size
                  << " (expected " << sizeof(rocprofiler_thread_trace_decoder_wave_t) << ")\n";
        return;
    }
    if ((wave->instructions_size != 0 && !wave->instructions_array) ||
        (wave->timeline_size != 0 && !wave->timeline_array))
    {
        std::cerr << "[trace_decoder][se=" << ctx.se << "] ignoring WAVE record with null payload arrays\n";
        return;
    }

    wave_record_t rec{};
    rec.cu = wave->cu;
    rec.simd = wave->simd;
    rec.wave_id = wave->wave_id;
    rec.begin = wave->begin_time;
    rec.end = wave->end_time;
    rec.me = (wave->dispatcher >> 4) & 0x7;
    rec.pipe = wave->dispatcher & 0xF;
    rec.workgroup_id = wave->workgroup_id;
    rec.cluster_id = waveClusterId(*wave);
    rec.has_dispatcher_info = true;

    // Synthetic ID for cache key: "se<N>_simd<S>_cu<C>_w<W>_<begin>"
    rec.id = "se" + std::to_string(ctx.se) + "_simd" + std::to_string(wave->simd) + "_cu" + std::to_string(wave->cu) +
             "_w" + std::to_string(wave->wave_id) + "_" + std::to_string(wave->begin_time);

    // Copy instructions — the decoder's arrays are only valid during the callback.
    // Store PCs directly; line_number is resolved later in buildCodeFromISACache()
    // once final address-ordered line numbers are assigned.
    rec.instructions.reserve(wave->instructions_size);
    for (uint64_t i = 0; i < wave->instructions_size; i++)
    {
        auto& inst = wave->instructions_array[i];
        wave_instruction_t wi{};
        wi.time = inst.time;
        wi.category = inst.category;
        wi.stall = inst.stall;
        wi.duration = inst.duration;
        wi.pc.address = inst.pc.address;
        wi.pc.code_object_id = inst.pc.code_object_id;
        if (wi.pc.code_object_id == 0) wi.pc.address += arch_offset;

        rec.instructions.push_back(wi);
    }

    // Copy timeline
    rec.timeline.reserve(wave->timeline_size);
    for (uint64_t i = 0; i < wave->timeline_size; i++) rec.timeline.push_back(wave->timeline_array[i]);

    ctx.dispatcher->dispatchWave(ctx.se, rec);
}

void TraceDecoderEmitter::handleOccupancy(
    ParseContext& ctx, const rocprofiler_thread_trace_decoder_occupancy_t* occ, uint64_t count
)
{
    for (uint64_t i = 0; i < count; i++)
    {
        // The occupancy_record_t is the decoder type directly (via using alias)
        ctx.dispatcher->dispatchOccupancy(ctx.se, occ[i]);
    }
}

void TraceDecoderEmitter::handlePerfEvent(
    ParseContext& ctx, const rocprofiler_thread_trace_decoder_perfevent_t* perf, uint64_t count
)
{
    for (uint64_t i = 0; i < count; i++)
    {
        counter_record_t rec{};
        rec.time = perf[i].time;
        rec.cu = static_cast<int8_t>(perf[i].CU);
        rec.bank = static_cast<int8_t>(perf[i].bank);
        rec.values[0] = static_cast<float>(perf[i].events0);
        rec.values[1] = static_cast<float>(perf[i].events1);
        rec.values[2] = static_cast<float>(perf[i].events2);
        rec.values[3] = static_cast<float>(perf[i].events3);
        ctx.dispatcher->dispatchCounter(ctx.se, rec);
    }
}

void TraceDecoderEmitter::handleTraceEvent(ParseContext& ctx, const rocprofiler_thread_trace_decoder_event_t* event)
{
    if (!event) return;
    if (event->size != sizeof(rocprofiler_thread_trace_decoder_event_t))
    {
        std::cerr << "[trace_decoder][se=" << ctx.se << "] ignoring EVENT record with size " << event->size
                  << " (expected " << sizeof(rocprofiler_thread_trace_decoder_event_t) << ")\n";
        return;
    }
    ctx.dispatcher->dispatchTraceEvent(ctx.se, *event);
}

void TraceDecoderEmitter::handleDispatch(
    ParseContext& ctx, const rocprofiler_thread_trace_decoder_dispatch_t* dispatch, uint64_t count
)
{
    if (!dispatch) return;
    for (uint64_t i = 0; i < count; i++)
    {
        if (dispatch[i].size != sizeof(rocprofiler_thread_trace_decoder_dispatch_t))
        {
            std::cerr << "[trace_decoder][se=" << ctx.se << "] ignoring DISPATCH record with size " << dispatch[i].size
                      << " (expected " << sizeof(rocprofiler_thread_trace_decoder_dispatch_t) << ")\n";
            continue;
        }
        ctx.dispatcher->dispatchDispatch(ctx.se, dispatch[i]);
    }
}

void TraceDecoderEmitter::handleShaderData(
    ParseContext& ctx, const rocprofiler_thread_trace_decoder_shaderdata_t* sd, uint64_t count
)
{
    for (uint64_t i = 0; i < count; i++) ctx.dispatcher->dispatchShaderData(ctx.se, sd[i]);
}

void TraceDecoderEmitter::handleRealtime(
    ParseContext& ctx, const rocprofiler_thread_trace_decoder_realtime_t* rt, uint64_t count
)
{
    for (uint64_t i = 0; i < count; i++) ctx.dispatcher->dispatchRealtime(ctx.se, rt[i]);
}

void TraceDecoderEmitter::handleOtherSimd(
    ParseContext& ctx, const rocprofiler_thread_trace_decoder_inst_other_simd_t* inst, uint64_t count
)
{
    if (!inst) return;
    for (uint64_t i = 0; i < count; i++)
    {
        if (inst[i].size != sizeof(rocprofiler_thread_trace_decoder_inst_other_simd_t))
        {
            std::cerr << "[trace_decoder][se=" << ctx.se << "] ignoring INST_OTHER_SIMD record with size "
                      << inst[i].size << " (expected " << sizeof(rocprofiler_thread_trace_decoder_inst_other_simd_t)
                      << ")\n";
            continue;
        }
        ctx.dispatcher->dispatchOtherSimd(ctx.se, inst[i]);
    }
}

#endif // RCV_HAS_TRACE_DECODER
