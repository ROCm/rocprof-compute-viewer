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

#include <QScrollArea>
#include <QScrollBar>
#include <QWidget>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "code/hotspot.hpp"

/**
 * A flamegraph-style widget that visualizes profiling data in a hierarchical,
 * stacked bar chart. From bottom to top:
 *   1. Source files — width proportional to total latency per file
 *   2. C++ source lines — grouped under their file, with inline call stacks expanded
 *   3. Assembly instructions — grouped under their source line
 *
 * Inline functions are handled by building a call-stack tree from the ASMLine::line_ref
 * chain. Each level of inlining becomes a separate row in the flamegraph.
 */
class FlameGraphWidget : public QWidget
{
    Q_OBJECT;

public:
    explicit FlameGraphWidget(QWidget* parent = nullptr);
    virtual ~FlameGraphWidget() = default;

    /// Rebuild the flamegraph data from the current SourceLine::all_lines and ASMCodeline::line_vec
    void rebuild();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    /// A single rectangular frame in the flamegraph
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

    /// A call-stack node used during tree construction
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
    };

    void buildFrames();
    void layoutFrames();

    /// Convert a StackNode tree into frames at a given depth, within [parentX, parentX+parentW]
    void flattenNode(
        const StackNode& node, int depth, int maxDepth, double parentX, double parentW, int64_t parentLatency
    );

    /// Get the frame under the mouse cursor, or nullptr
    const Frame* frameAt(const QPoint& pos) const;

    /// Pixel geometry for a frame
    QRect frameRect(const Frame& f) const;

    std::vector<Frame> m_frames;
    int m_numRows = 0;      ///< Total number of rows
    int m_rowHeight = 20;   ///< Pixel height per row
    int m_padding = 2;      ///< Pixel padding between frames
    int m_marginBottom = 4; ///< Bottom margin
    int m_marginTop = 4;    ///< Top margin
    int m_marginLeft = 4;
    int m_marginRight = 4;

    const Frame* m_hoveredFrame = nullptr;
    int64_t m_totalLatency = 0;

    // Zoom/pan state
    double m_viewLeft = 0.0;  ///< Left edge of visible region in [0,1] space
    double m_viewWidth = 1.0; ///< Width of visible region in [0,1] space
    QPoint m_lastMousePos;
    bool m_isPanning = false;

    // Horizontal scrollbar
    QScrollBar* m_hScrollBar = nullptr;
    void updateScrollBar();
    void onScrollBarChanged(int value);

    /// Color palette for files (avoids green, orange, yellow, gray)
    static QColor fileColor(int index);
    /// Color for source line frames (cool tones, avoids green/orange/yellow/gray)
    static QColor sourceColor(double latencyRatio);
    /// Color for assembly frames based on token type
    static QColor asmColorForType(int tokenType);
};
