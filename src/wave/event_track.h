// MIT License
//
// Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.
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
#include <map>
#include <memory>
#include <vector>
#include "waveglobal.h"

class QScrollArea;

/// One item on the event track: either a point (end == begin) or a time range.
struct EventSpan
{
    enum class Kind
    {
        Dispatch,
        TraceEvent,
    };

    int64_t begin = 0;
    int64_t end = 0;
    Kind kind = Kind::TraceEvent;
    int index = -1;           ///< Into the owning sub-track's dispatch or trace-event vector
    int64_t launch_end = -1;  ///< Dispatch only: time of the paired dispatch-end event
    int kid = -1;             ///< Dispatch only: kernel id of its waves (color and name)
    int lane = 0;             ///< Assigned by EventTrackModel::Layout()

    bool IsRange() const { return end > begin; }
};

/// Events of one Shader Engine, packed into non-overlapping lanes.
struct EventSubTrack
{
    int se = 0;
    DispatchRecordVec dispatches;
    TraceEventRecordVec events;
    std::vector<EventSpan> spans; ///< All spans, sorted by begin
    std::vector<int> visible;     ///< Indices into spans passing the group filter, sorted by begin
    int lanes = 0;                ///< Lane count of the visible spans; zero when everything is filtered out
    int64_t max_len = 0;          ///< Longest visible span, bounds the backwards search for overlaps
};

/// Builds per-SE event spans and lays them out into lanes.
class EventTrackModel
{
public:
    /// Add one Shader Engine. Dispatches become ranges when a dispatch-end event
    /// follows them on the same {ME & 1, pipe}: the range ends when the last wave
    /// launched between the dispatch and its dispatch-end finishes.
    /// Expects `traces` with wave end times filled in.
    void AddShaderEngine(
        int se,
        std::vector<dispatch_record_t> dispatches,
        std::vector<trace_event_record_t> events,
        const std::map<int, std::vector<WaveTraceData>>& traces
    );

    /// Filter by decoder event groups and pack the remaining spans into lanes.
    void Layout(uint32_t groups);

    bool empty() const { return tracks.empty(); }
    /// True when no sub-track has a visible span after the last Layout().
    bool AllHidden() const;

    std::vector<EventSubTrack> tracks;
};

/// Sticky event viewer drawn above the Global View waves, time-synced with its
/// horizontal scrollbar. One band per Shader Engine.
class QEventTrack : public QWidget
{
    Q_OBJECT
    set_tracked();
    using Super = QWidget;

public:
    QEventTrack(std::shared_ptr<EventTrackModel> model, QWidget* parent = nullptr);

    void setScrollArea(QScrollArea* sa) { m_scrollArea = sa; }
    /// Re-filter and re-pack lanes; collapses the track when nothing is shown.
    void SetDecoderEventGroups(uint32_t groups);

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    /// Left click on an SE label collapses/expands that band.
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void wheelEvent(QWheelEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;

    int laneHeight() const;
    /// Half-size of point event markers.
    int markerRadius() const;
    /// A collapsed band draws all of its lanes overlaid in a single row.
    bool isBandCollapsed(size_t band) const;
    void setBandCollapsed(size_t band, bool collapsed);
    /// Rows drawn for a band: zero when all of its events are filtered out, one when collapsed.
    int bandLanes(size_t band) const;
    /// Height in pixels of a band: a few pixels when all of its events are filtered out.
    int bandHeight(size_t band) const;
    int contentHeight() const;
    /// Visible span under a widget position, following the paint stacking order.
    /// Returns {band, span} indices or {-1, -1}.
    std::pair<int, int> spanAt(const QPoint& pos) const;

    static constexpr int THIN_BAND_PX = 3;
    static constexpr int BAND_PAD_PX = 2;
    static constexpr int DEFAULT_MAX_LANES = 6;

private:
    int getHorizontalOffset() const;
    void clampVerticalOffset();
    /// Band index under widget y (and its top in `band_y`), or -1.
    int bandAt(int y, int* band_y = nullptr) const;

    std::shared_ptr<EventTrackModel> model;
    QScrollArea* m_scrollArea = nullptr;
    int m_vOffset = 0;
    int m_expandedHeight = 0;          ///< Track height to restore when events are re-enabled
    bool m_allHidden = false;          ///< Whole track shrunk because every event is filtered out
    std::vector<bool> m_bandCollapsed; ///< Per band, indexed like model->tracks
};
