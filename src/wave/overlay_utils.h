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

#include <QPainter>
#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>
#include "config/config.hpp"
#include "data/marker_colors.h"
#include "data/records.h"

namespace WaveOverlay
{
inline constexpr int DecoderDispatchWidth = 5;
inline constexpr int DecoderEventWidth = 4;

inline constexpr uint32_t DecoderEventGroupDispatch = 1u << 0;
inline constexpr uint32_t DecoderEventGroupFlush = 1u << 1;
inline constexpr uint32_t DecoderEventGroupCodeObject = 1u << 2;
inline constexpr uint32_t DecoderEventGroupSQTT = 1u << 3;
inline constexpr uint32_t DecoderEventGroupOther = 1u << 4;
inline constexpr uint32_t DecoderEventAllGroups = DecoderEventGroupDispatch | DecoderEventGroupFlush |
                                                  DecoderEventGroupCodeObject | DecoderEventGroupSQTT |
                                                  DecoderEventGroupOther;

enum class DecoderEventSurface
{
    ComputeUnit,
    Global,
};

inline uint32_t traceEventGroup(int type)
{
    switch (type)
    {
        case ROCPROF_TRACE_DECODER_EVENT_GC_RINSE:
        case ROCPROF_TRACE_DECODER_EVENT_CS_PARTIAL_FLUSH:
        case ROCPROF_TRACE_DECODER_EVENT_CACHE_FLUSH:
        case ROCPROF_TRACE_DECODER_EVENT_BOTTOM_OF_PIPE_TS: return DecoderEventGroupFlush;
        case ROCPROF_TRACE_DECODER_EVENT_CODE_OBJECT_LOAD:
        case ROCPROF_TRACE_DECODER_EVENT_CODE_OBJECT_UNLOAD: return DecoderEventGroupCodeObject;
        case ROCPROF_TRACE_DECODER_EVENT_TT_STALL_BEGIN:
        case ROCPROF_TRACE_DECODER_EVENT_TT_STALL_END:
        case ROCPROF_TRACE_DECODER_EVENT_TT_FLUSH: return DecoderEventGroupSQTT;
        default: return DecoderEventGroupOther;
    }
}

inline bool showTraceEvent(
    const trace_event_record_t& event, DecoderEventSurface surface, uint32_t groups = DecoderEventAllGroups
)
{
    if ((traceEventGroup(event.type) & groups) == 0) return false;
    switch (surface)
    {
        case DecoderEventSurface::ComputeUnit:
            return event.type != ROCPROF_TRACE_DECODER_EVENT_SPM_SAMPLE &&
                   event.type != ROCPROF_TRACE_DECODER_EVENT_CODE_OBJECT_LOAD &&
                   event.type != ROCPROF_TRACE_DECODER_EVENT_CODE_OBJECT_UNLOAD &&
                   event.type != ROCPROF_TRACE_DECODER_EVENT_CS_PARTIAL_FLUSH;
        case DecoderEventSurface::Global: return true;
    }
    return true;
}

template <typename Record> auto eventLowerBound(const std::vector<Record>& records, int64_t clock)
{
    return std::lower_bound(
        records.begin(), records.end(), clock, [](const Record& record, int64_t value) { return record.time < value; }
    );
}

template <typename Record, typename Predicate> int findRecordIndexAtIf(
    const std::vector<Record>* records,
    int64_t clock,
    int64_t tolerance,
    Predicate predicate,
    bool prefer_later_on_tie = false
)
{
    if (!records || records->empty()) return -1;

    int best = -1;
    int64_t best_dist = tolerance + 1;
    for (auto it = eventLowerBound(*records, clock - tolerance); it != records->end() && it->time <= clock + tolerance;
         ++it)
    {
        if (!predicate(*it)) continue;
        const int64_t dist = it->time > clock ? it->time - clock : clock - it->time;
        if (dist < best_dist || (prefer_later_on_tie && dist == best_dist))
        {
            best_dist = dist;
            best = static_cast<int>(it - records->begin());
        }
    }
    return best;
}

template <typename Record> int findRecordIndexAt(
    const std::vector<Record>* records, int64_t clock, int64_t tolerance, bool prefer_later_on_tie = false
)
{
    return findRecordIndexAtIf(records, clock, tolerance, [](const Record&) { return true; }, prefer_later_on_tie);
}

template <typename Record> const Record* findRecordAt(
    const std::vector<Record>* records, int64_t clock, int64_t tolerance, bool prefer_later_on_tie = false
)
{
    const int idx = findRecordIndexAt(records, clock, tolerance, prefer_later_on_tie);
    return idx >= 0 ? &records->at(idx) : nullptr;
}

inline std::string formatDispatchTooltip(const dispatch_record_t& dispatch, int se)
{
    std::stringstream ss;
    ss << "Kernel Dispatch\nTime: " << dispatch.time << " cycles\n";
    if (se >= 0) ss << "SE:" << se << "  ";
    ss << "ME:" << static_cast<int>(dispatch.me_id) << "  Pipe:" << static_cast<int>(dispatch.pipe_id) << "\nEntry: 0x"
       << std::hex << dispatch.entry_point.address << "  Code Object: 0x" << dispatch.entry_point.code_object_id
       << std::dec << "\nThreads: " << dispatch.thread_dim_x << " x " << dispatch.thread_dim_y << " x "
       << dispatch.thread_dim_z << "\nVGPRs: " << dispatch.vgprs << "  SGPRs: " << dispatch.sgprs
       << "  LDS: " << dispatch.lds_size;
    return ss.str();
}

inline std::string formatTraceEventTooltip(const trace_event_record_t& event, int se)
{
    std::stringstream ss;
    ss << TraceDecoderEventName(event.type);
    if (event.flags & 0x2) ss << " (Bottom of Pipe)";
    ss << "\nTime: " << event.time << " clk\n";

    if (se >= 0) ss << "SE:" << se << "  ";
    ss << "ME:" << static_cast<int>(event.me_id) << "  Pipe:" << static_cast<int>(event.pipe_id);
    if (event.payload.raw != 0) ss << "\nPayload: 0x" << std::hex << event.payload.raw << std::dec;

    return ss.str();
}

template <typename ClockToPixel> void drawDecoderEvents(
    QPainter& painter,
    const std::vector<trace_event_record_t>* trace_events,
    const std::vector<dispatch_record_t>* dispatch_records,
    DecoderEventSurface surface,
    int64_t clock_start,
    int64_t clock_end,
    int height,
    ClockToPixel clock_to_pixel,
    uint32_t event_groups = DecoderEventAllGroups
)
{
    auto draw_line = [&](int64_t time, const QColor& color, int width)
    {
        if (time < clock_start || time > clock_end) return;
        QPen pen(color);
        pen.setWidth(width);
        painter.setPen(pen);
        const int x = clock_to_pixel(time);
        painter.drawLine(x, 0, x, height);
    };

    painter.save();
    if (dispatch_records && (event_groups & DecoderEventGroupDispatch))
        for (auto it = eventLowerBound(*dispatch_records, clock_start); it != dispatch_records->end(); ++it)
        {
            if (it->time > clock_end) break;
            draw_line(it->time, WindowColors::DecoderDispatchEventColor(), DecoderDispatchWidth);
        }

    if (trace_events)
        for (auto it = eventLowerBound(*trace_events, clock_start); it != trace_events->end(); ++it)
        {
            if (it->time > clock_end) break;
            const auto& event = *it;
            if (!showTraceEvent(event, surface, event_groups)) continue;
            draw_line(event.time, WindowColors::DecoderEventColor(event.type), DecoderEventWidth);
        }
    painter.restore();
}
} // namespace WaveOverlay
