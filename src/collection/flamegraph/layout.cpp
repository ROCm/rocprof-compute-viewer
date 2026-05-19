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

#include "layout.h"

#include <QColor>
#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <utility>

#include "config/config.hpp"

namespace flamegraph
{

namespace
{

constexpr std::array<QColor, 10> kFileColors = {
    {
     QColor(0x40, 0x60, 0xD0), // Muted blue
        QColor(0x90, 0x40, 0xC0), // Purple
        QColor(0xC0, 0x30, 0x60), // Rose/magenta
        QColor(0x30, 0x80, 0xB0), // Teal-blue
        QColor(0xA0, 0x30, 0xA0), // Violet
        QColor(0x20, 0x60, 0x90), // Steel blue
        QColor(0xB0, 0x40, 0x80), // Plum
        QColor(0x50, 0x50, 0xC0), // Indigo
        QColor(0x80, 0x30, 0x30), // Dark red/maroon
        QColor(0x30, 0x70, 0x70)  // Dark cyan
    }
};

QColor fileColor(int index) { return kFileColors[index % kFileColors.size()]; }

QColor sourceColor(double latencyRatio)
{
    // Cool blue-to-magenta gradient; avoids green/orange/yellow/gray
    int r = 100 + static_cast<int>(120 * latencyRatio);
    int g = 60 + static_cast<int>(20 * latencyRatio);
    int b = 160 + static_cast<int>(60 * latencyRatio);
    return QColor(std::min(r, 255), std::max(g, 0), std::min(b, 255));
}

QColor asmColorForType(int tokenType)
{
    auto& colors = Config::TokenColors();
    if (tokenType >= 0 && tokenType < static_cast<int>(colors.size())) return colors[tokenType].qcolor;
    return QColor(0x70, 0x70, 0x70);
}

/// Copy a `string → StackNode` map into a vector sorted by node latency
/// descending. Used everywhere we lay out children/roots so the largest
/// frames anchor the left edge.
std::vector<std::pair<std::string, std::shared_ptr<StackNode>>> sortedByLatency(
    const std::map<std::string, std::shared_ptr<StackNode>>& m
)
{
    std::vector<std::pair<std::string, std::shared_ptr<StackNode>>> out(m.begin(), m.end());
    std::sort(
        out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second->latency > b.second->latency; }
    );
    return out;
}

/// Compute the layout denominator for `node`'s row content (children + asm)
/// and the breakdown that drives where ASM starts after the child subtree.
///
/// Stack_builder is supposed to maintain
///   node.latency == childChildrenLatSum + asmLatSum
/// when both are populated (integrated path). The global-marker path uses
/// node.latency = own duration with only nested-marker children, so undershoot
/// (sum < node.latency) is normal there and just leaves a self-time gap on the
/// right.
///
/// Overshoot (sum > node.latency) is the dangerous case: if we used
/// node.latency as the denominator, child + asm widths would exceed the
/// parent's slot and spill into the next sibling's column. Returning
/// max(node.latency, sum) caps the denominator so every emitted frame stays
/// inside [parentX, parentX + parentW] by construction. The same denominator
/// must be used both for recursing into children and for placing ASM, so the
/// two share the parent's slot without overlap.
///
/// `warnLabel` is the human-readable label of the parent node, only used in
/// the overshoot warning. Pass an empty string to suppress the warning (e.g.
/// while inspecting a node from a context where warning was already done).
struct LayoutBreakdown
{
    int64_t childChildrenLatSum;
    int64_t asmLatSum;
    int64_t layoutLat;
};

LayoutBreakdown contentLayoutLatency(const StackNode& node, const std::string& warnLabel)
{
    LayoutBreakdown b{0, 0, 0};
    for (auto& [_, gc] : node.children) b.childChildrenLatSum += gc->latency;
    for (auto& asm_e : node.asmEntries) b.asmLatSum += asm_e.latency;
    b.layoutLat = std::max(node.latency, b.childChildrenLatSum + b.asmLatSum);

    if (!warnLabel.empty() && b.childChildrenLatSum + b.asmLatSum > node.latency)
    {
        std::cerr << "flamegraph: row content overshoots parent for node '" << warnLabel << "' (key='" << node.key
                  << "'): children_sum=" << b.childChildrenLatSum << " + asm_sum=" << b.asmLatSum
                  << " > node.latency=" << node.latency << " (capped denominator at layoutLat=" << b.layoutLat << ")\n";
    }
    return b;
}

/// Recursively emit frames for `node`'s children starting at `depth`. Returns
/// the highest row used by this subtree (or `depth - 1` if nothing was emitted),
/// so the caller can size `numRows` to the actual layout instead of relying on
/// a pre-walk depth count.
///
/// In the integrated path a source-line node can end up with both `children`
/// (deeper inline lines from a longer call chain) AND `asmEntries` (asm from
/// a shorter chain that ended right here). The two are NOT alternatives at
/// different depths — they are different instructions executed inside the
/// same source-line scope, and `child->latency == sum(children.latency) +
/// sum(asm.latency)` holds by construction in stack_builder. So they share
/// the same row (`depth + 1`) and split the parent's x range: inline children
/// on the left, this node's own asm on the right, never overlapping another
/// asm bar in x.
///
/// Stacking asm above asm without a source frame in between would imply two
/// distinct call-stack depths for the same x, which contradicts the stack
/// the layout is meant to depict.
int flattenNode(
    LayoutResult& out, const StackNode& node, int depth, double parentX, double parentW, int64_t parentLatency
)
{
    if (parentLatency <= 0) return depth - 1;

    int maxRowUsed = depth - 1;
    double xPos = parentX;

    for (auto& [key, child] : sortedByLatency(node.children))
    {
        double w = parentW * static_cast<double>(child->latency) / parentLatency;
        double ratio = static_cast<double>(child->latency) / out.totalLatency;

        Frame f;
        f.label = child->label;
        f.location = child->fullLocation;
        f.content = child->content;
        f.latency = child->latency;
        f.x = xPos;
        f.w = w;
        f.row = depth;
        f.color = child->isMarker ? child->markerColor : sourceColor(std::min(ratio * 5.0, 1.0));
        f.filename = child->filename;
        f.lineNumber = child->lineNumber;
        out.frames.push_back(f);
        maxRowUsed = std::max(maxRowUsed, depth);

        // Cap the denominator at max(child->latency, child_children_sum +
        // asm_sum) so the row content beneath this child stays inside its
        // [xPos, xPos+w] slot even when stack_builder hands us an overshoot.
        // See contentLayoutLatency() for the full rationale.
        LayoutBreakdown b = contentLayoutLatency(*child, child->label);

        // Recurse into deeper inline levels using `layoutLat` as the
        // denominator so grandchild widths sum to
        //   (childChildrenLatSum / layoutLat) * w
        // and the leftover slot for asm is exactly
        //   (asmLatSum / layoutLat) * w.
        int childMax = flattenNode(out, *child, depth + 1, xPos, w, b.layoutLat);
        maxRowUsed = std::max(maxRowUsed, childMax);

        if (!child->asmEntries.empty() && b.layoutLat > 0)
        {
            double childrenW = w * static_cast<double>(b.childChildrenLatSum) / b.layoutLat;
            double asmX = xPos + childrenW;

            for (auto& asm_e : child->asmEntries)
            {
                double asmW = w * static_cast<double>(asm_e.latency) / b.layoutLat;

                Frame af;
                af.label = asm_e.label;
                af.content = asm_e.label;
                af.latency = asm_e.latency;
                af.x = asmX;
                af.w = asmW;
                af.row = depth + 1;
                af.color = asmColorForType(asm_e.tokenType);
                af.asmIndex = asm_e.asmIndex;
                out.frames.push_back(af);

                asmX += asmW;
            }
            maxRowUsed = std::max(maxRowUsed, depth + 1);
        }

        xPos += w;
    }

    return maxRowUsed;
}

} // anonymous namespace

LayoutResult layoutFromRoots(Roots& roots)
{
    LayoutResult out;

    for (auto& [name, node] : roots) out.totalLatency += node->latency;
    if (out.totalLatency <= 0) return out;

    int maxRowUsed = 0;
    double xPos = 0.0;
    int fileIdx = 0;
    for (auto& [name, node] : sortedByLatency(roots))
    {
        double w = static_cast<double>(node->latency) / out.totalLatency;

        Frame f;
        f.label = node->label;
        f.location = name;
        f.latency = node->latency;
        f.x = xPos;
        f.w = w;
        f.row = 0;
        f.color = node->isMarker ? node->markerColor : fileColor(fileIdx);
        f.filename = name;
        out.frames.push_back(f);

        // Apply the same overshoot-cap at root level so row 1 cannot extend
        // beyond this root's `w` and overlap the next root's column. Without
        // this, root-level invariant violations would bypass the protection
        // flattenNode applies to non-root parents.
        LayoutBreakdown rb = contentLayoutLatency(*node, node->label);

        // numRows is derived from the actual highest row emitted by the
        // recursive walk; the old pre-walk maxDepth helper is no longer needed.
        int subtreeMax = flattenNode(out, *node, 1, xPos, w, rb.layoutLat);
        maxRowUsed = std::max(maxRowUsed, subtreeMax);

        xPos += w;
        fileIdx++;
    }

    out.numRows = maxRowUsed + 1;

    // Per-row x-sorted index for sub-linear visibility culling in paintEvent
    // and hit-testing in frameAt. Without this, every paint / mouse-move
    // iterates all frames (50k+ for multi-kernel traces) and computes a QRect
    // per frame.
    out.framesByRow.assign(out.numRows, {});
    for (int i = 0; i < static_cast<int>(out.frames.size()); ++i)
    {
        const Frame& f = out.frames[i];
        if (f.row >= 0 && f.row < out.numRows) out.framesByRow[f.row].push_back(i);
    }
    for (auto& row : out.framesByRow)
        std::sort(row.begin(), row.end(), [&](int a, int b) { return out.frames[a].x < out.frames[b].x; });

    return out;
}

} // namespace flamegraph
