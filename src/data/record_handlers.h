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

#include "data/datastore.h"
#include "data/record_dispatcher.h"

class WaveHandler : public RecordHandler
{
public:
    explicit WaveHandler(DataStore& store) : store(store) {}
    void onWave(int se, const wave_record_t& rec) override;

private:
    DataStore& store;
};

class OccupancyHandler : public RecordHandler
{
public:
    explicit OccupancyHandler(DataStore& store) : store(store) {}
    void onOccupancy(int se, const occupancy_record_t& rec) override;
    void onTraceEvent(int se, const trace_event_record_t& rec) override;
    void onDispatch(int se, const dispatch_record_t& rec) override;
    void onParsingComplete() override;

private:
    DataStore& store;
};

class CounterHandler : public RecordHandler
{
public:
    explicit CounterHandler(DataStore& store) : store(store) {}
    void onCounter(int se, const counter_record_t& rec) override;

private:
    DataStore& store;
};

class ShaderDataHandler : public RecordHandler
{
public:
    explicit ShaderDataHandler(DataStore& store) : store(store) {}
    void onShaderData(int se, const shaderdata_record_t& rec) override;
    void onParsingComplete() override;

private:
    DataStore& store;
};

class OtherSimdHandler : public RecordHandler
{
public:
    explicit OtherSimdHandler(DataStore& store) : store(store) {}
    void onOtherSimd(int se, const other_simd_record_t& rec) override;

private:
    DataStore& store;
};

class RealtimeHandler : public RecordHandler
{
public:
    explicit RealtimeHandler(DataStore& store) : store(store) {}
    void onRealtime(int se, const realtime_record_t& rec) override;

private:
    DataStore& store;
};

class MetadataHandler : public RecordHandler
{
public:
    explicit MetadataHandler(DataStore& store) : store(store) {}
    void onMetadata(record_type_t type, const void* data) override;

private:
    DataStore& store;
};
