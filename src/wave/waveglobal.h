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

#include <QWidget>
#include <cstdint>
#include <iostream>
#include <memory>
#include <unordered_set>
#include "data/datastore.h"
#include "data/marker_colors.h"
#include "data/marker_types.h"
#include "data/shaderdata.h"
#include "data/wavemanager.h"
#include "measure.h"

class QScrollArea;

struct WaveTraceData
{
    int64_t begin;
    int64_t end;
    int kid;
    bool has_dispatcher_info = false;
    int me = -1;
    int pipe = -1;
    int workgroup_id = -1;
    int cluster_id = 0;
    uint32_t occupancy_flags = 0;
    int sgprs = 0;
    int vgprs = 0;
};

using TraceEventRecordVec = std::shared_ptr<const std::vector<trace_event_record_t>>;
using DispatchRecordVec = std::shared_ptr<const std::vector<dispatch_record_t>>;

//! A Block of tokens with pre-defined size.
class QOutsideWaveView : public QWidget
{
    Q_OBJECT
    set_tracked();
    using Super = QWidget;

public:
    QOutsideWaveView(
        int se,
        int sa,
        int cu,
        int simd,
        int slot,
        std::vector<WaveTraceData>& waves,
        std::shared_ptr<MeasureTool>& tool
    );
    virtual ~QOutsideWaveView()
    {
        if (tool) tool->update_list.erase(this);
    };

    int64_t DrawWave(QPainter& painter, int64_t start, const WaveTraceData& wave, const QRect& rect);

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override { return sizeHint(); };

    virtual void paintEvent(QPaintEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;

    const int se;
    const int sa;
    const int cu;
    const int simd;
    const int slot;

    bool bIsVisible = true;

    /// Set shaderdata records that match this view's SE/CU/SIMD (shared, zero-copy).
    void SetShaderData(ShaderDataRecordVec records);

    /// Set decoder events/dispatches emitted for this Shader Engine.
    void SetTraceEvents(TraceEventRecordVec records);
    void SetDispatchRecords(DispatchRecordVec records);

    /// Set decoded marker spans for this bucket. When non-null+non-empty the
    /// view switches into typed marker rendering for the markers track;
    /// otherwise the legacy red-rect raw-record path is used. Pre-computes
    /// per-span color once — paint and mouse never recompute.
    void SetMarkers(MarkerSpanVec spans);

    /// Pixels reserved at the top of this row for the marker track (zero when
    /// no markers). Read by QGlobalView to keep the sticky label panel and
    /// SIMD-group separators in sync with per-row growth. Computed on demand
    /// so vertical zoom (Shift+Wheel changing HEIGHT()) flows through.
    int markerTrackHeightPx() const;
    /// Per-depth band height in the current zoom state (pixels).
    int markerRowPx() const;
    /// Empty-pixel pad below the wave bar when this row carries markers — visually
    /// separates one slot's marker stack from the next. Zero when no markers.
    int markerBottomPadPx() const;

protected:
    float height_multiplier;
    const std::vector<WaveTraceData> waves;
    std::shared_ptr<MeasureTool> tool;

    /// Shaderdata records overlapping this view's location (shared across slots)
    ShaderDataRecordVec shaderdata_records;

    /// Decoded marker spans + derived render data; empty when no marker funcmap
    /// is loaded for this bucket (legacy red-rect path is used in that case).
    MarkerRenderCache markers;
    TraceEventRecordVec trace_events;
    DispatchRecordVec dispatch_records;

    /// Minimum per-depth band height (pixels) — keeps markers visible even at
    /// the smallest vertical zoom. Above this floor, band height scales with
    /// HEIGHT() so Shift+Wheel zoom enlarges markers alongside the wave.
    static constexpr int MARKER_ROW_PX_MIN = 8;
    /// Floor for the soft cap that limits marker-track growth at low zoom.
    /// At higher zoom the cap scales with HEIGHT() so the user can grow
    /// the marker track to read labels on deep stacks.
    static constexpr int MARKER_TRACK_MAX_PX_MIN = 96;

    /// Draw shaderdata triangle markers (only visible range, with pixel dedup)
    void DrawShaderDataMarkers(QPainter& painter, const QRect& area);

    /// Draw decoder events/dispatches as vertical markers.
    void DrawDecoderEvents(QPainter& painter, const QRect& area);

    /// Draw typed marker spans (only when markers are set)
    void DrawTypedMarkers(QPainter& painter, const QRect& area);

    /// Find shaderdata record near a clock position (for tooltip). Returns index or -1.
    int FindShaderDataAt(int64_t clock_pos) const;

    int FindTraceEventAt(int64_t clock_pos) const;
    int FindDispatchAt(int64_t clock_pos) const;

    /// Find typed marker span at clock position. Returns index or -1.
    /// When y >= 0, restrict the hit to the depth row covering that y in the
    /// view's vertical extent — matches the Perfetto-style row layout used by
    /// DrawTypedMarkers. Pass y = -1 to fall back to deepest-span selection.
    int FindMarkerAt(int64_t clock_pos, int y = -1) const;

public:
signals:
};

// Sticky header widget for tick marks - stays at top during vertical scroll
class QTickHeader : public QWidget
{
    Q_OBJECT
public:
    QTickHeader(QWidget* parent = nullptr);
    virtual QSize sizeHint() const override { return QSize(100, HEADER_HEIGHT); }
    virtual QSize minimumSizeHint() const override { return sizeHint(); }
    void setScrollArea(QScrollArea* sa) { m_scrollArea = sa; }
    void onScrollChanged() { update(); } // Trigger repaint on scroll
    virtual void paintEvent(QPaintEvent* event) override;
    static constexpr int HEADER_HEIGHT = 20;
    static constexpr int LEFT_MARGIN = 84; // Width of label panel on the left
private:
    int getHorizontalOffset() const; // Gets current offset from scrollbar
    QScrollArea* m_scrollArea = nullptr;
};

// Sticky label panel - stays on the left during horizontal scroll
class QLabelPanel : public QWidget
{
    Q_OBJECT
public:
    QLabelPanel(QWidget* parent = nullptr);
    void addRow(int se, int sa, int cu, int simd, int slot); // Just store raw data
    /// Update the per-row extra pixel height (e.g. marker track) for an existing
    /// row. Index matches the order in which addRow was called. Caller must
    /// invoke recalculatePositions() after applying all updates.
    void setRowExtraHeight(int row_idx, int extra_px);
    void finalize(); // Call after all rows added to compute SIMD groups
    void setVerticalOffset(int offset);
    void recalculatePositions(); // Recalculate when mipmap level changes
    virtual void paintEvent(QPaintEvent* event) override;
    virtual QSize sizeHint() const override { return QSize(QTickHeader::LEFT_MARGIN, 100); }
    virtual QSize minimumSizeHint() const override { return sizeHint(); }

private:
    struct RowInfo
    {
        int se, sa, cu, simd, slot;
        int extra_px = 0; // Marker track or other top-of-row decoration height
    };
    struct SimdGroup
    {
        int se, sa, cu, simd, yStart, yEnd;
    }; // Computed positions
    std::vector<RowInfo> m_rows;
    std::vector<SimdGroup> m_simdGroups;
    std::vector<int> m_simdBoundaries; // Y positions where SIMD groups change
    int m_vOffset = 0;

    friend class QGlobalView; // Allow QGlobalView to access boundaries
};

class QGlobalView : public QWidget
{
    Q_OBJECT
    set_tracked();
    using Super = QWidget;

public:
    QGlobalView(const std::string& filename);
    QGlobalView(DataStore& store);
    virtual ~QGlobalView()
    {
        if (tool) tool->update_list.erase(this);
    }

    static int64_t PosToClock(int64_t value) { return value * Delta() + begintime; }
    static int64_t ClockToPos(int64_t value) { return (value - begintime) / Delta(); }
    static int64_t Delta() { return 1 << mipmap_level; }
    static int64_t HEIGHT() { return height_scale; }
    static int64_t MaxTime() { return maxtime; }

    static int GetMip() { return mipmap_level; }
    static int SpinToMip(int spinValue) { return std::max(0, std::min(15 - spinValue, 15)); }
    static uint32_t DecoderEventGroups() { return decoder_event_groups; }

    // Calculate new scroll position when zooming, keeping anchor_x at the same viewport position
    static int calcZoomScroll(int old_mip, int new_mip, int old_scroll, int anchor_viewport_x);

    void SetMip(int new_mipmap_level)
    {
        mipmap_level = new_mipmap_level;
        for (auto* view : views)
        {
            view->setFixedHeight(view->sizeHint().height());
            view->updateGeometry();
        }
        if (labelPanel) labelPanel->recalculatePositions();
        if (tickHeader) tickHeader->update();
        this->updateGeometry();
        this->update();
    }

    virtual void paintEvent(QPaintEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void wheelEvent(QWheelEvent* event) override;

    static std::unordered_map<int, std::string> kernel_names;
    std::shared_ptr<MeasureTool> tool = std::make_shared<MeasureTool>();

private:
    static int64_t maxtime;
    static int64_t begintime;
    static int mipmap_level;
    static int height_scale;
    static uint32_t decoder_event_groups;
    std::vector<QOutsideWaveView*> views;
    QScrollArea* m_scrollArea = nullptr;

public:
    QTickHeader* tickHeader = nullptr;   // External header widget for sticky ticks
    QLabelPanel* labelPanel = nullptr;   // External label panel for sticky labels
    void setScrollArea(QScrollArea* sa); // Connect to scroll area for sync
    void populateLabelPanel();           // Fill labelPanel with label data
    void SetDecoderEventGroups(uint32_t groups);

    /// Distribute pre-loaded shaderdata records to matching wave views.
    void SetShaderData(const ShaderDataManager& manager);

    /// Distribute decoded marker spans to matching wave views (no-op if absent).
    void SetMarkers(const ShaderDataManager& manager);

private:
    /// Push each view's markerTrackHeightPx() into the label panel's per-row
    /// extra-height field and trigger a re-layout. Idempotent — safe to call
    /// after SetMarkers, after populateLabelPanel, or both.
    void syncLabelPanelExtraHeights();
};
