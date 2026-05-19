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

#include <mutex>
#include <string>
#include <vector>
#include "records.h"

class RecordHandler
{
public:
    virtual ~RecordHandler() = default;

    virtual void onWave(int /* se */, const wave_record_t& /* rec */) {}
    virtual void onOccupancy(int /* se */, const occupancy_record_t& /* rec */) {}
    virtual void onTraceEvent(int /* se */, const trace_event_record_t& /* rec */) {}
    virtual void onDispatch(int /* se */, const dispatch_record_t& /* rec */) {}
    virtual void onCounter(int /* se */, const counter_record_t& /* rec */) {}
    virtual void onShaderData(int /* se */, const shaderdata_record_t& /* rec */) {}
    virtual void onOtherSimd(int /* se */, const other_simd_record_t& /* rec */) {}
    virtual void onRealtime(int /* se */, const realtime_record_t& /* rec */) {}
    virtual void onMetadata(record_type_t /* type */, const void* /* data */) {}
    virtual void onParsingComplete() {}
};

class RecordDispatcher
{
public:
    void addHandler(RecordHandler* handler);

    void dispatchWave(int se, const wave_record_t& rec);
    void dispatchOccupancy(int se, const occupancy_record_t& rec);
    void dispatchTraceEvent(int se, const trace_event_record_t& rec);
    void dispatchDispatch(int se, const dispatch_record_t& rec);
    void dispatchCounter(int se, const counter_record_t& rec);
    void dispatchShaderData(int se, const shaderdata_record_t& rec);
    void dispatchOtherSimd(int se, const other_simd_record_t& rec);
    void dispatchRealtime(int se, const realtime_record_t& rec);
    void dispatchMetadata(record_type_t type, const void* data);
    void signalComplete();

private:
    std::mutex dispatch_mutex;
    std::vector<RecordHandler*> handlers;
};
