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
#include <cstdint>
#include <string>
#include <vector>

#ifdef RCV_HAS_TRACE_DECODER
#    include <rocprof_trace_decoder/trace_decoder_types.h>
using shaderdata_record_t = rocprofiler_thread_trace_decoder_shaderdata_t;
using realtime_record_t = rocprofiler_thread_trace_decoder_realtime_t;
using other_simd_record_t = rocprofiler_thread_trace_decoder_inst_other_simd_t;
using occupancy_record_t = rocprofiler_thread_trace_decoder_occupancy_t;
using wave_state_record_t = rocprofiler_thread_trace_decoder_wave_state_t;
using inst_category_t = rocprofiler_thread_trace_decoder_inst_category_t;
using trace_event_record_t = rocprofiler_thread_trace_decoder_event_t;
using dispatch_record_t = rocprofiler_thread_trace_decoder_dispatch_t;
#else
struct shaderdata_record_t
{
    int64_t time;
    uint64_t value;
    uint8_t cu;
    uint8_t simd;
    uint8_t wave_id;
    uint8_t flags;
    uint32_t reserved;
};

struct realtime_record_t
{
    int64_t shader_clock;
    uint64_t realtime_clock;
    uint64_t reserved;
};

struct other_simd_record_t
{
    uint64_t size;
    int64_t time;
    uint16_t cycles;
    uint8_t wgp;
    uint8_t category;
};

struct occupancy_record_t
{
    struct
    {
        uint64_t address;
        uint64_t code_object_id;
    } pc;
    uint64_t time;
    uint8_t reserved;
    uint8_t cu;
    uint8_t simd;
    uint8_t wave_id;
    uint32_t start        : 1;
    uint32_t me_id        : 3;
    uint32_t pipe_id      : 4;
    uint32_t is_ext       : 1;
    uint32_t workgroup_id : 7;
    uint32_t _rsvd        : 16;
};

struct wave_state_record_t
{
    int32_t type;
    int32_t duration;
};

enum inst_category_t
{
    ROCPROFILER_THREAD_TRACE_DECODER_INST_NONE = 0,
    ROCPROFILER_THREAD_TRACE_DECODER_INST_SMEM,
    ROCPROFILER_THREAD_TRACE_DECODER_INST_SALU,
    ROCPROFILER_THREAD_TRACE_DECODER_INST_VMEM,
    ROCPROFILER_THREAD_TRACE_DECODER_INST_FLAT,
    ROCPROFILER_THREAD_TRACE_DECODER_INST_LDS,
    ROCPROFILER_THREAD_TRACE_DECODER_INST_VALU,
    ROCPROFILER_THREAD_TRACE_DECODER_INST_JUMP,
    ROCPROFILER_THREAD_TRACE_DECODER_INST_NEXT,
    ROCPROFILER_THREAD_TRACE_DECODER_INST_IMMED,
    ROCPROFILER_THREAD_TRACE_DECODER_INST_CONTEXT,
    ROCPROFILER_THREAD_TRACE_DECODER_INST_MESSAGE,
    ROCPROFILER_THREAD_TRACE_DECODER_INST_BVH,
    ROCPROFILER_THREAD_TRACE_DECODER_INST_LAST
};
#endif

struct counter_record_t
{
    int64_t time;
    int8_t cu;
    int8_t bank;
    std::array<float, 4> values;
};

#ifndef RCV_HAS_TRACE_DECODER
struct trace_event_record_t
{
    uint64_t size = sizeof(trace_event_record_t);
    int64_t time = 0;
    int type = 0;
    uint8_t me_id = 0;
    uint8_t pipe_id = 0;
    uint16_t reserved = 0;
    uint64_t payload = 0;
};

enum
{
    ROCPROF_TRACE_DECODER_EVENT_NONE = 0,
    ROCPROF_TRACE_DECODER_EVENT_CS_PARTIAL_FLUSH,
    ROCPROF_TRACE_DECODER_EVENT_BOTTOM_OF_PIPE_TS,
    ROCPROF_TRACE_DECODER_EVENT_SAVE_CONTEXT,
    ROCPROF_TRACE_DECODER_EVENT_DISPATCH_END,
    ROCPROF_TRACE_DECODER_EVENT_CACHE_FLUSH,
    ROCPROF_TRACE_DECODER_EVENT_PACKET_LOSS,
    ROCPROF_TRACE_DECODER_EVENT_CODE_OBJECT_LOAD,
    ROCPROF_TRACE_DECODER_EVENT_CODE_OBJECT_UNLOAD,
    ROCPROF_TRACE_DECODER_EVENT_TT_STALL_BEGIN,
    ROCPROF_TRACE_DECODER_EVENT_TT_STALL_END,
    ROCPROF_TRACE_DECODER_EVENT_TT_FLUSH,
    ROCPROF_TRACE_DECODER_EVENT_DIDT_STALL_BEGIN,
    ROCPROF_TRACE_DECODER_EVENT_DIDT_STALL_END,
    ROCPROF_TRACE_DECODER_EVENT_CLUSTER_BEGIN, ///< Payload is the ID
    ROCPROF_TRACE_DECODER_EVENT_CLUSTER_END,   ///< Payload is the ID
    ROCPROF_TRACE_DECODER_EVENT_GC_RINSE,
    ROCPROF_TRACE_DECODER_EVENT_SPM_SAMPLE,
    ROCPROF_TRACE_DECODER_EVENT_LAST
};

struct dispatch_record_t
{
    uint64_t size = sizeof(dispatch_record_t);
    int64_t time = 0;
    struct
    {
        uint64_t address;
        uint64_t code_object_id;
    } entry_point = {0, 0};
    uint8_t me_id = 0;
    uint8_t pipe_id = 0;
    uint16_t user_sgprs = 0;
    int flags = 0;
    uint32_t vgprs = 0;
    uint32_t sgprs = 0;
    uint32_t lds_size = 0;
    uint32_t thread_dim_x = 0;
    uint32_t thread_dim_y = 0;
    uint32_t thread_dim_z = 0;
    uint64_t dispatch_pkt_addr = 0;
};
#endif

inline std::string TraceDecoderEventName(int type)
{
    switch (type)
    {
        case ROCPROF_TRACE_DECODER_EVENT_CS_PARTIAL_FLUSH: return "CS Partial Flush";
        case ROCPROF_TRACE_DECODER_EVENT_BOTTOM_OF_PIPE_TS: return "Bottom Of Pipe Timestamp";
        case ROCPROF_TRACE_DECODER_EVENT_SAVE_CONTEXT: return "Save Context";
        case ROCPROF_TRACE_DECODER_EVENT_DISPATCH_END: return "Dispatch End";
        case ROCPROF_TRACE_DECODER_EVENT_CACHE_FLUSH: return "Cache Flush";
        case ROCPROF_TRACE_DECODER_EVENT_PACKET_LOSS: return "Packet Loss";
        case ROCPROF_TRACE_DECODER_EVENT_CODE_OBJECT_LOAD: return "Code Object Load";
        case ROCPROF_TRACE_DECODER_EVENT_CODE_OBJECT_UNLOAD: return "Code Object Unload";
        case ROCPROF_TRACE_DECODER_EVENT_TT_STALL_BEGIN: return "Thread Trace Stall Begin";
        case ROCPROF_TRACE_DECODER_EVENT_TT_STALL_END: return "Thread Trace Stall End";
        case ROCPROF_TRACE_DECODER_EVENT_TT_FLUSH: return "Thread Trace Flush";
        case ROCPROF_TRACE_DECODER_EVENT_DIDT_STALL_BEGIN: return "DIDT Stall Begin";
        case ROCPROF_TRACE_DECODER_EVENT_DIDT_STALL_END: return "DIDT Stall End";
        case ROCPROF_TRACE_DECODER_EVENT_CLUSTER_BEGIN: return "Cluster Begin";
        case ROCPROF_TRACE_DECODER_EVENT_CLUSTER_END: return "Cluster End";
        case ROCPROF_TRACE_DECODER_EVENT_GC_RINSE: return "GC Rinse";
        case ROCPROF_TRACE_DECODER_EVENT_SPM_SAMPLE: return "SPM sample taken";
        default: return "Trace Event " + std::to_string(type);
    }
}

struct wave_instruction_t
{
    int64_t time;
    int category;
    int stall;
    int duration;
    int line_number = -1;
    struct
    {
        uint64_t address;
        uint64_t code_object_id;
    } pc = {0, 0}; // Populated by decoder path; zero for JSON path
};

struct wave_record_t
{
    int cu;
    int simd;
    int wave_id;
    int64_t begin;
    int64_t end;
    std::string id; // JSON path: relative filename; decoder path: synthetic key
    std::vector<wave_instruction_t> instructions;
    std::vector<wave_state_record_t> timeline;
    bool has_dispatcher_info = false;
    int me = -1;
    int pipe = -1;
    bool has_workgroup_id = false;
    int workgroup_id = -1;
    bool has_occupancy_flags = false;
    uint32_t occupancy_flags = 0;
    struct waitcnt_entry_t
    {
        int code_line;
        std::vector<std::pair<int, int>> sources;
    };
    std::vector<waitcnt_entry_t> waitcnt;
};

enum class record_type_t
{
    WAVE,
    OCCUPANCY,
    COUNTER,
    SHADERDATA,
    OTHER_SIMD,
    REALTIME,
    RT_FREQUENCY,
    GFXIP,
    CODE,
    METADATA
};
