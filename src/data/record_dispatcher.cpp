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

#include "record_dispatcher.h"

namespace
{
template <typename Fn> void dispatchToHandlers(std::mutex& mutex, const std::vector<RecordHandler*>& handlers, Fn&& fn)
{
    std::lock_guard<std::mutex> lock(mutex);
    for (auto* h : handlers) fn(*h);
}
} // namespace

void RecordDispatcher::addHandler(RecordHandler* handler) { handlers.push_back(handler); }

void RecordDispatcher::dispatchWave(int se, const wave_record_t& rec)
{
    dispatchToHandlers(dispatch_mutex, handlers, [&](RecordHandler& h) { h.onWave(se, rec); });
}

void RecordDispatcher::dispatchOccupancy(int se, const occupancy_record_t& rec)
{
    dispatchToHandlers(dispatch_mutex, handlers, [&](RecordHandler& h) { h.onOccupancy(se, rec); });
}

void RecordDispatcher::dispatchTraceEvent(int se, const trace_event_record_t& rec)
{
    dispatchToHandlers(dispatch_mutex, handlers, [&](RecordHandler& h) { h.onTraceEvent(se, rec); });
}

void RecordDispatcher::dispatchDispatch(int se, const dispatch_record_t& rec)
{
    dispatchToHandlers(dispatch_mutex, handlers, [&](RecordHandler& h) { h.onDispatch(se, rec); });
}

void RecordDispatcher::dispatchCounter(int se, const counter_record_t& rec)
{
    dispatchToHandlers(dispatch_mutex, handlers, [&](RecordHandler& h) { h.onCounter(se, rec); });
}

void RecordDispatcher::dispatchShaderData(int se, const shaderdata_record_t& rec)
{
    dispatchToHandlers(dispatch_mutex, handlers, [&](RecordHandler& h) { h.onShaderData(se, rec); });
}

void RecordDispatcher::dispatchOtherSimd(int se, const other_simd_record_t& rec)
{
    dispatchToHandlers(dispatch_mutex, handlers, [&](RecordHandler& h) { h.onOtherSimd(se, rec); });
}

void RecordDispatcher::dispatchRealtime(int se, const realtime_record_t& rec)
{
    dispatchToHandlers(dispatch_mutex, handlers, [&](RecordHandler& h) { h.onRealtime(se, rec); });
}

void RecordDispatcher::dispatchMetadata(record_type_t type, const void* data)
{
    dispatchToHandlers(dispatch_mutex, handlers, [&](RecordHandler& h) { h.onMetadata(type, data); });
}

void RecordDispatcher::signalComplete()
{
    for (auto* h : handlers) h->onParsingComplete();
}
