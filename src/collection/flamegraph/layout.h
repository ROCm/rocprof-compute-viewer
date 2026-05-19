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
#include <vector>

#include "stack_node.h"

namespace flamegraph
{

/// Output of layoutFromRoots — flat frames plus the per-row x-sorted index
/// used by the widget for sub-linear visibility culling.
struct LayoutResult
{
    std::vector<Frame> frames;
    std::vector<std::vector<int>> framesByRow; ///< frame indices sorted by Frame::x per row
    int numRows = 0;
    int64_t totalLatency = 0;
};

/// Convert a forest of StackNodes into flat Frames laid out in normalized
/// [0,1] coordinates. Pure function: no Qt or MainWindow access. Children
/// are sorted by latency descending; root order follows the same rule.
LayoutResult layoutFromRoots(Roots& roots);

} // namespace flamegraph
