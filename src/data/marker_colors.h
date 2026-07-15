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

#include <QColor>
#include <QString>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "data/marker_types.h"

/// Qt-aware helpers for rendering MarkerSpans. Kept separate from
/// marker_types.h so the decoding side stays Qt-free.

/// Human-readable kind name for tooltips.
const char* MarkerKindName(MarkerKind k);

/// Theme base color associated with a marker kind.
const QColor& MarkerBaseColor(MarkerKind k);

/// Final per-span color: base color of the kind shifted by a stable hue
/// derived from the marker name, so distinct names within the same kind are
/// visually distinguishable while still belonging to the same family.
QColor MarkerColor(MarkerKind k, const std::string& name);

/// Optional location stamp appended to tooltips that need it (the global
/// view labels each tooltip with its bucket; the per-wave view does not).
struct MarkerCoord
{
    int se;
    int cu;
    int simd;
    int slot;
};

/// Multi-line tooltip text for a marker span. Includes name, kind, source
/// location (if any), depth (if `include_depth`), bucket coords (if `coord`),
/// and a time/range line.
std::string FormatMarkerTooltip(
    const MarkerSpan& s, std::optional<MarkerCoord> coord = std::nullopt, bool include_depth = false
);

/// Per-bucket precomputed cache shared by both the per-wave and global wave
/// views. All derived fields are filled once in Reset() — paint and hit-test
/// then prune non-intersecting spans without allocation or color resolution.
struct MarkerRenderCache
{
    MarkerSpanVec spans;
    /// Parallel to *spans; resolved once via MarkerColor() at Reset time.
    std::vector<QColor> colors;
    int max_depth = 0;

    /// Replace the cache contents with the given spans. Recomputes colors,
    /// max_depth, and the overlap index. Safe to call with a null or empty
    /// `spans` (clears the cache).
    void Reset(MarkerSpanVec spans);

    /// True when no spans are bound. Cheaper than spans->empty() against a null.
    bool empty() const { return !spans || spans->empty(); }

    /// Invoke `f(index)` for every span intersecting [begin_clock, end_clock],
    /// in enter-time order. Open spans are included naturally.
    template <typename F> void ForEachOverlapping(int64_t begin_clock, int64_t end_clock, F&& f) const
    {
        if (empty() || begin_clock > end_clock) return;

        const size_t limit = static_cast<size_t>(
            std::upper_bound(
                spans->begin(),
                spans->end(),
                end_clock,
                [](int64_t clock, const MarkerSpan& span) { return clock < span.enter_time; }
            ) -
            spans->begin()
        );
        if (limit == 0) return;

        forEachOverlapping(1, 0, overlap_tree_base, limit, begin_clock, f);
    }

private:
    template <typename F>
    void forEachOverlapping(size_t node, size_t first, size_t last, size_t limit, int64_t begin_clock, F& f) const
    {
        if (first >= limit || overlap_max_exit[node] < begin_clock) return;
        if (last - first == 1)
        {
            f(first);
            return;
        }

        const size_t middle = first + (last - first) / 2;
        forEachOverlapping(node * 2, first, middle, limit, begin_clock, f);
        forEachOverlapping(node * 2 + 1, middle, last, limit, begin_clock, f);
    }

    /// Segment tree over `spans`, storing the latest exit in each subtree.
    /// This avoids a longest-duration backscan when one marker spans a large
    /// fraction of the trace.
    size_t overlap_tree_base = 0;
    std::vector<int64_t> overlap_max_exit;
};
