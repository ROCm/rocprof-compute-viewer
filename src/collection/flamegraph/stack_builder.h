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

#include "stack_node.h"

namespace flamegraph
{

/// Legacy file-rooted builder: aggregates ASMCodeline::hotspot.combined()
/// per asm line into a file → inlined-source-line → asm tree. Used for
/// JSON traces and any ATT trace without markers.
Roots buildSourceRoots();

/// Integrated builder: walks every wave_instruction on (target_se, target_cu,
/// target_simd), attributes each instruction's `duration` cycles to the marker
/// stack active at that instruction's time, and grows file/line/asm under the
/// resulting marker (or "[no scope]") leaf. Returns an empty map if anything
/// required is missing (no wave hierarchy, no asm lookup, etc.).
Roots buildIntegratedRoots(int t_se, int t_cu, int t_simd);

/// Global marker-only builder for the marker tab: walks every non-empty
/// marker bucket and collapses identical scopes (by kind, name) across all
/// waves into one tree.
Roots buildGlobalMarkerRoots();

} // namespace flamegraph
