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

#    include <string>
#    include "data/input_detector.h"

class RecordDispatcher;
class DataStore;

/// Skeleton emitter for .rocpd (SQLite-based) trace archives.
/// The .rocpd format can embed .att and .out binary data directly in tables,
/// or reference them as external file paths relative to the .rocpd location.
/// Once the binary data is obtained, parsing is delegated to TraceDecoderEmitter.
class RocpdEmitter
{
public:
    RocpdEmitter(const InputInfo& info, RecordDispatcher& dispatcher, DataStore& store);
    void run();

private:
    InputInfo info;
    RecordDispatcher& dispatcher;
    DataStore& store;
};

#endif // RCV_HAS_TRACE_DECODER
