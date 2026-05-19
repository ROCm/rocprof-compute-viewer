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
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "data/marker_types.h"

namespace flamegraph
{

/// A call-stack node used during tree construction. Builders produce a forest
/// of these (keyed in a top-level Roots map); layout flattens it into Frames.
struct StackNode
{
    std::string key;          ///< Unique key for this node (e.g. "file.cpp:42")
    std::string label;        ///< Display label (location)
    std::string content;      ///< Source text content
    std::string fullLocation; ///< Full path location for tooltip
    std::string filename;
    int lineNumber = -1;
    int64_t latency = 0;
    std::map<std::string, std::shared_ptr<StackNode>> children;

    /// Assembly instructions directly at this call-stack leaf
    struct AsmEntry
    {
        std::string label;
        int64_t latency = 0;
        int asmIndex = -1;
        int tokenType = 0; ///< Index into Config::TokenColors()
    };
    std::vector<AsmEntry> asmEntries;
    /// Optional: when populated, asmEntries are merged-by-label via this index.
    /// Used for fallback ([Unassigned] / file:?) nodes to collapse identical
    /// instructions into one frame each, instead of one per ASM line.
    std::unordered_map<std::string, size_t> asmIndexByLabel;

    /// Marker-mode metadata. When isMarker is true the renderer uses
    /// markerColor for fill and markerSourceLoc for the tooltip; the rest of
    /// the file/source layout machinery is reused unchanged.
    bool isMarker = false;
    QColor markerColor;
    MarkerKind markerKind = MarkerKind::Unknown;
    std::string markerSourceLoc;
};

/// A single rectangular frame in the flamegraph. Produced by layout, consumed
/// by the widget for painting and hit-testing.
struct Frame
{
    std::string label;    ///< Display label shown inside the frame bar
    std::string location; ///< Full path location (e.g. "/path/file.cpp:42")
    std::string content;  ///< Source/ASM content text
    int64_t latency = 0;  ///< Total latency (SQTT + PCS) of this frame
    double x = 0;         ///< Left position in [0,1] normalized coordinates (relative to total)
    double w = 0;         ///< Width in [0,1] normalized coordinates
    int row = 0;          ///< Row index (0 = bottom = files)
    QColor color;         ///< Fill color

    // For click navigation
    std::string filename; ///< Source filename (for file and source frames)
    int lineNumber = -1;  ///< Source line number (for source frames)
    int asmIndex = -1;    ///< ASM line index (for asm frames)
};

/// Top-level container: outermost stack nodes keyed by their unique scope key.
using Roots = std::map<std::string, std::shared_ptr<StackNode>>;

} // namespace flamegraph
