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

#ifdef RCV_HAS_TRACE_DECODER

#    include <rocprof_trace_decoder/rocprof_trace_decoder.h>
#    include <cstdint>
#    include <map>
#    include <mutex>
#    include <rocprof_trace_decoder/cxx/code_printing.hpp>
#    include <string>
#    include <vector>
#    include "data/datastore.h"
#    include "data/input_detector.h"

class RecordDispatcher;

class TraceDecoderEmitter
{
public:
    TraceDecoderEmitter(const InputInfo& info, RecordDispatcher& dispatcher, DataStore& store);
    ~TraceDecoderEmitter();

    void run();

    /// Errors collected during parseATTFiles() — one entry per .att file whose
    /// rocprof_trace_decoder_parse() returned non-SUCCESS. Read after run()
    /// returns; UI surfaces these in a popup so the user knows the trace is
    /// incomplete (until now they only appeared on stderr).
    const std::vector<std::string>& parseErrors() const { return parse_errors; }

private:
    struct ISALine
    {
        std::string text;
        uint64_t memory_size = 0;
        uint64_t addr = 0;
        uint64_t codeobj_id = 0;
    };

    struct ParseContext
    {
        TraceDecoderEmitter* emitter = nullptr;
        RecordDispatcher* dispatcher = nullptr;
        DataStore* store = nullptr;
        int se = -1;
        std::map<uint64_t, int> dispatch_id_map;
        int next_dispatch_id = 0;
    };

    void loadCodeObjects();

    /// Load a single .out file at `path` into codeobj_map under `code_object_id`.
    /// Returns true on success. Logs and returns false on any failure.
    bool loadCodeObjectFile(const std::string& path, uint64_t code_object_id);

    void preDisassembleKernels();
    void parseATTFiles();
    /// Build store.code in (cobj_id, addr) order from the ISA cache. If
    /// `code_json_data` is non-empty, layer its richer per-instruction metadata
    /// (cppline, stallreasons, hitcount, latency/idle/stall sums) onto matching
    /// rows. code.json typically only describes one kernel; the rest of the
    /// trace's PCs still get plain disassembly rows.
    void buildCodeFromISACache(const std::vector<class CodeData>& code_json_data);

    /// Seed isa_cache from code.json before the decoder runs, so the ISA callback can
    /// answer PCs when no disasm backend is available. memory_size from address deltas.
    void preSeedIsaCacheFromCodeJson(const std::vector<class CodeData>& code_json_data);

    void buildDispatches();

    void alignSEClocks();

    /// Find the active code object at a given (se,cu,simd,slot,time) by
    /// locating the wave instance occupying that slot and binary-searching its
    /// instruction list. Returns 0 if no instruction bracket can be found.
    /// Used by ShaderDataManager::ResolveMarkers to scope per-record funcmap lookups.
    uint64_t activeCodeobjAt(int se, int cu, int simd, int slot, int64_t time) const;

    static rocprofiler_thread_trace_decoder_status_t isaCallback(
        char* instruction,
        uint64_t* memory_size,
        uint64_t* size,
        rocprofiler_thread_trace_decoder_pc_t address,
        void* userdata
    );

    static rocprofiler_thread_trace_decoder_status_t traceCallback(
        rocprofiler_thread_trace_decoder_record_type_t record_type_id,
        void* trace_events,
        uint64_t trace_size,
        void* userdata
    );

    static void handleWave(ParseContext& ctx, const rocprofiler_thread_trace_decoder_wave_t* wave);
    static void handleOccupancy(
        ParseContext& ctx, const rocprofiler_thread_trace_decoder_occupancy_t* occ, uint64_t count
    );
    static void handlePerfEvent(
        ParseContext& ctx, const rocprofiler_thread_trace_decoder_perfevent_t* perf, uint64_t count
    );
    static void handleTraceEvent(ParseContext& ctx, const rocprofiler_thread_trace_decoder_event_t* event);
    static void handleDispatch(
        ParseContext& ctx, const rocprofiler_thread_trace_decoder_dispatch_t* dispatch, uint64_t count
    );
    static void handleShaderData(
        ParseContext& ctx, const rocprofiler_thread_trace_decoder_shaderdata_t* sd, uint64_t count
    );
    static void handleRealtime(
        ParseContext& ctx, const rocprofiler_thread_trace_decoder_realtime_t* rt, uint64_t count
    );
    static void handleOtherSimd(
        ParseContext& ctx, const rocprofiler_thread_trace_decoder_inst_other_simd_t* inst, uint64_t count
    );

    InputInfo info;
    RecordDispatcher& dispatcher;
    DataStore& store;

    rocprof_trace_decoder_handle_t decoder_handle{};

    std::mutex isa_mutex;
    rocprof_trace_decoder::codeobj::CodeobjAddressTranslate codeobj_map;
    std::map<uint64_t, std::map<uint64_t, ISALine>> isa_cache;

    mutable ActiveCodeobjIndex active_codeobj_index;

    std::mutex parse_errors_mutex;
    std::vector<std::string> parse_errors;
};

#endif // RCV_HAS_TRACE_DECODER
