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

#include <QScrollBar>
#include <QWidget>
#include <vector>

#include "flamegraph/layout.h"
#include "flamegraph/stack_node.h"

class QComboBox;

namespace flamegraph
{
enum class LatencyMetric;
}

/**
 * Flamegraph widget shell. Owns no tree-building or layout logic — those live
 * in `flamegraph/stack_builder.{h,cpp}` (builders) and `flamegraph/layout.{h,cpp}`
 * (tree → frames). This class is just the Qt surface: paint, mouse, wheel,
 * scrollbar, and the picker that decides which builder to run for a given dataset.
 *
 * Subclasses (e.g. MarkerFlameGraphWidget) override rebuild() to plug in a
 * different builder, and reuse the base painter/input handling unchanged.
 */
class FlameGraphWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FlameGraphWidget(QWidget* parent = nullptr);
    virtual ~FlameGraphWidget() = default;

    void setHiddenLatencyAvailable(bool available, bool selectNonhidden = false);

    /// Pick a builder, run layout, and refresh the widget.
    virtual void rebuild();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    /// Apply a layout result to the widget (frames, row index, totals) and
    /// refresh the scrollbar / minimum height. Used by both the base
    /// `rebuild()` and subclass overrides.
    void applyLayout(flamegraph::LayoutResult result);

    /// Reset frame state to defaults and refresh the scrollbar.
    void resetFrameState();

    /// Marker-only flamegraphs are duration based and do not use hidden latency.
    void setLatencyModeControlVisible(bool visible);

private:
    /// Pick a builder for the current dataset (markers + target wave →
    /// integrated; otherwise legacy file roots). Member function so the
    /// MainWindow `friend FlameGraphWidget` declaration covers its access
    /// to private MainWindow coordinates.
    flamegraph::Roots pickBuilder() const;
    flamegraph::LatencyMetric latencyMetric() const;
    void updateLatencyModeControlGeometry();

    /// Y-pixel of the top of `row`'s band. Centralizes the bottom-up row math
    /// shared by paint, hit-test, and frameRect.
    int rowTopY(int row) const;

    QRect frameRect(const flamegraph::Frame& f) const;
    const flamegraph::Frame* frameAt(const QPoint& pos) const;

    void updateScrollBar();
    void onScrollBarChanged(int value);

protected:
    std::vector<flamegraph::Frame> m_frames;
    /// Per-row index into m_frames, sorted by Frame::x ascending. Used by
    /// paintEvent and frameAt to binary-search by x and avoid scanning all
    /// 50k+ frames every repaint / mouse move.
    std::vector<std::vector<int>> m_framesByRow;
    /// Cached `m_hScrollBar->sizeHint().height()`; the original cost was paid
    /// once per Frame per paint. Recomputed in resizeEvent and applyLayout.
    int m_cachedScrollBarHeight = 0;
    int m_numRows = 0;
    int m_rowHeight = 20;
    int m_padding = 2;
    int m_marginBottom = 4;
    int m_marginTop = 4;
    int m_marginLeft = 4;
    int m_marginRight = 4;

    const flamegraph::Frame* m_hoveredFrame = nullptr;
    int64_t m_allTotalLatency = 0;
    int64_t m_allNonhiddenLatency = 0;

    // Zoom/pan state
    double m_viewLeft = 0.0;  ///< Left edge of visible region in [0,1] space
    double m_viewWidth = 1.0; ///< Width of visible region in [0,1] space
    QPoint m_lastMousePos;
    bool m_isPanning = false;

    QScrollBar* m_hScrollBar = nullptr;
    QComboBox* m_latencyModeBox = nullptr;
    bool m_hiddenLatencyAvailable = false;
};
