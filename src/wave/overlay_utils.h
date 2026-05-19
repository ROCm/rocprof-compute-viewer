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
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>
#include "data/marker_colors.h"
#include "data/records.h"

namespace WaveOverlay
{
inline constexpr int DecoderDispatchWidth = 5;
inline constexpr int DecoderEventWidth = 4;

template <typename Record> int findRecordIndexAt(
    const std::vector<Record>* records, int64_t clock, int64_t tolerance, bool prefer_later_on_tie = false
)
{
    if (!records || records->empty()) return -1;

    int best = -1;
    int64_t best_dist = tolerance + 1;
    for (auto it = records->begin(); it != records->end(); ++it)
    {
        const int64_t dist = it->time > clock ? it->time - clock : clock - it->time;
        if (dist < best_dist || (prefer_later_on_tie && dist == best_dist))
        {
            best_dist = dist;
            best = static_cast<int>(it - records->begin());
        }
    }
    return best;
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
    ss << TraceDecoderEventName(event.type) << "\nTime: " << event.time << " cycles\n";
    if (se >= 0) ss << "SE:" << se << "  ";
    ss << "ME:" << static_cast<int>(event.me_id) << "  Pipe:" << static_cast<int>(event.pipe_id);
    if (event.payload != 0) ss << "\nPayload: 0x" << std::hex << event.payload << std::dec;
    return ss.str();
}

template <typename ClockToPixel> void drawDecoderEvents(
    QPainter& painter,
    const std::vector<trace_event_record_t>* trace_events,
    const std::vector<dispatch_record_t>* dispatch_records,
    int64_t clock_start,
    int64_t clock_end,
    int height,
    ClockToPixel clock_to_pixel
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
    if (dispatch_records)
        for (const auto& dispatch : *dispatch_records)
            draw_line(dispatch.time, WindowColors::DecoderDispatchEventColor(), DecoderDispatchWidth);

    if (trace_events)
        for (const auto& event : *trace_events)
            draw_line(event.time, WindowColors::DecoderEventColor(event.type), DecoderEventWidth);
    painter.restore();
}
} // namespace WaveOverlay
