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

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "data/marker_types.h"

/// Pure data-driven SQTT instrumentation marker stack walker. Decoupled from
/// the trace-decoder so it can be unit-tested without any decoder dependency.
/// Production uses a thin wrapper (`ShaderDataManager::ResolveMarkers`) that
/// supplies a decoder-backed resolver; tests supply a synthetic resolver.

struct ResolvedMarker
{
    bool found = false;
    MarkerKind kind = MarkerKind::Unknown;
    std::string name;
    std::string source_loc;
};

/// Resolver callback: map a raw marker id (at the given record time) to a
/// funcmap entry. Return `found=false` when the id is unknown — the walker
/// will then synthesize an Unknown-kind entry and emit a Warning diagnostic.
/// `time` lets production resolvers pick the active codeobj at that moment;
/// tests can ignore it.
using MarkerResolveFn = std::function<ResolvedMarker(uint32_t id, int64_t time)>;

/// One raw shaderdata token: just (time, value). `value` is the packed marker
/// payload `(id << 2) | (is_enter << 1) | exit_prev`.
struct MarkerInputRecord
{
    int64_t time;
    uint32_t value;
};

/// Walk a time-ordered stream of marker tokens for one (se,cu,simd,slot) bucket
/// and emit span + diagnostic vectors. `out_spans` is appended (NOT cleared).
void walkMarkerStream(
    const std::vector<MarkerInputRecord>& records,
    int se,
    int cu,
    int simd,
    int slot,
    const MarkerResolveFn& resolver,
    std::vector<MarkerSpan>* out_spans,
    std::vector<MarkerDiagnostic>* out_diags
);
