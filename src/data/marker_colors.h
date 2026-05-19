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
/// then run in O(visible) without further allocation or color resolution.
struct MarkerRenderCache
{
    MarkerSpanVec spans;
    /// Parallel to *spans; resolved once via MarkerColor() at Reset time.
    std::vector<QColor> colors;
    /// Indices of all is_open spans, in enter_time order. They straddle the
    /// viewport arbitrarily and are always considered on top of the lower_bound
    /// result.
    std::vector<int> open_indices;
    int max_depth = 0;
    /// Largest closed-span duration; used to extend the lower_bound starting
    /// cursor backward so a long outer span whose enter_time precedes the
    /// visible window is not missed.
    int64_t max_closed_dur = 0;

    /// Replace the cache contents with the given spans. Recomputes colors,
    /// max_depth, max_closed_dur, and open_indices. Safe to call with a null
    /// or empty `spans` (clears the cache).
    void Reset(MarkerSpanVec spans);

    /// True when no spans are bound. Cheaper than spans->empty() against a null.
    bool empty() const { return !spans || spans->empty(); }

    /// Iterator into *spans of the first closed span whose enter_time may
    /// overlap `cursor_clock` (i.e. enter_time >= cursor_clock - max_closed_dur).
    /// Use this as the start of a forward iteration in paint and hit-test.
    /// Precondition: !empty().
    std::vector<MarkerSpan>::const_iterator FirstCandidate(int64_t cursor_clock) const;
};
