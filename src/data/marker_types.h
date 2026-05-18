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
#include <memory>
#include <string>
#include <vector>

/// Mirror of rocprof_trace_decoder::codeobj::FuncmapEntryKind so UI/wave/global
/// view code can compile without pulling in the decoder header. Same integer
/// ordering as the decoder enum, with an extra "Unknown" tail used when an ID
/// failed to resolve through the funcmap.
enum class MarkerKind : uint8_t
{
    Function = 0,
    Kernel = 1,
    UserScope = 2,
    Point = 3,
    Unknown = 4
};

/// One resolved marker span: either an enter/exit pair, a point marker, or an
/// open scope (no exit observed before trace end).
struct MarkerSpan
{
    int64_t enter_time{0};
    int64_t exit_time{0}; ///< INT64_MAX if `is_open` is true
    uint32_t marker_id{0};
    MarkerKind kind{MarkerKind::Unknown};
    int depth{0}; ///< 0-based; for points this is the parent stack depth
    bool is_point{false};
    bool is_open{false};
    std::string name;       ///< empty if unresolved
    std::string source_loc; ///< empty if unresolved or absent
};

/// Shared, sorted (by `enter_time`) vector of marker spans for one
/// (se,cu,simd,slot) bucket — avoids copying across views.
using MarkerSpanVec = std::shared_ptr<const std::vector<MarkerSpan>>;

/// A diagnostic emitted while decoding marker streams. Surfaced to the user;
/// never silently dropped.
struct MarkerDiagnostic
{
    enum class Severity : uint8_t
    {
        Info,
        Warning,
        Error
    };
    Severity severity{Severity::Info};
    std::string message; ///< should include (se,cu,simd,slot) and time when relevant
};
