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

#    include "rocpd_emitter.h"

#    include <stdexcept>

RocpdEmitter::RocpdEmitter(const InputInfo& in, RecordDispatcher& disp, DataStore& st) :
info(in), dispatcher(disp), store(st)
{}

void RocpdEmitter::run()
{
    // TODO: Implement rocpd support.
    // The rocpd format is a SQLite database that can contain:
    //   1. Embedded mode: .att and .out binary blobs stored directly in tables.
    //   2. External mode: relative string paths to .att/.out files on disk.
    //
    // Once binary data is extracted (from either mode), it should be handed
    // to a TraceDecoderEmitter for actual parsing and dispatch.
    //
    // Future: DWARF/source mapping data will also come from rocpd tables.
    throw std::runtime_error("rocpd support not yet implemented");
}

#endif // RCV_HAS_TRACE_DECODER
