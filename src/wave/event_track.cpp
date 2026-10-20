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

#include "event_track.h"
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QToolTip>
#include <algorithm>
#include <climits>
#include <queue>
#include <sstream>
#include "config/config.hpp"
#include "mainwindow.h"
#include "wave/overlay_utils.h"

namespace
{
using DispatchKey = std::pair<int, int>;

/// Dispatches, dispatch-ends and waves are matched on {ME & 1, pipe}.
DispatchKey dispatchKey(int me, int pipe) { return {me & 1, pipe}; }

bool spanVisible(const EventSubTrack& track, const EventSpan& span, uint32_t groups)
{
    if (span.kind == EventSpan::Kind::Dispatch) return groups & WaveOverlay::DecoderEventGroupDispatch;
    return WaveOverlay::showTraceEvent(
        (*track.events)[span.index], WaveOverlay::DecoderEventSurface::Global, groups
    );
}

QColor opaque(QColor color)
{
    color.setAlpha(255);
    return color;
}
} // namespace

void EventTrackModel::AddShaderEngine(
    int se,
    std::vector<dispatch_record_t> dispatches,
    std::vector<trace_event_record_t> events,
    const std::map<int, std::vector<WaveTraceData>>& traces
)
{
    if (dispatches.empty() && events.empty()) return;

    auto by_time = [](const auto& a, const auto& b) { return a.time < b.time; };
    std::stable_sort(dispatches.begin(), dispatches.end(), by_time);
    std::stable_sort(events.begin(), events.end(), by_time);

    EventSubTrack track;
    track.se = se;
    track.spans.reserve(dispatches.size() + events.size());

    // Dispatch spans occupy [0, dispatches.size()) until the final sort, so the
    // per-key lists below can index them directly.
    std::map<DispatchKey, std::vector<int>> dispatches_by_key;
    for (size_t i = 0; i < dispatches.size(); ++i)
    {
        const auto& dispatch = dispatches[i];
        EventSpan span;
        span.kind = EventSpan::Kind::Dispatch;
        span.begin = span.end = dispatch.time;
        span.index = static_cast<int>(i);
        track.spans.push_back(span);
        dispatches_by_key[dispatchKey(dispatch.me_id, dispatch.pipe_id)].push_back(static_cast<int>(i));
    }

    std::map<DispatchKey, std::vector<int>> ends_by_key;
    for (size_t i = 0; i < events.size(); ++i)
        if (events[i].type == ROCPROF_TRACE_DECODER_EVENT_DISPATCH_END)
            ends_by_key[dispatchKey(events[i].me_id, events[i].pipe_id)].push_back(static_cast<int>(i));

    // Pair each dispatch with the first dispatch-end that follows it before the
    // next dispatch on the same queue.
    std::vector<bool> consumed(events.size(), false);
    for (const auto& [key, list] : dispatches_by_key)
    {
        auto ends_it = ends_by_key.find(key);
        if (ends_it == ends_by_key.end()) continue;
        const auto& ends = ends_it->second;
        size_t j = 0;
        for (size_t i = 0; i < list.size(); ++i)
        {
            EventSpan& span = track.spans[list[i]];
            const int64_t next = i + 1 < list.size() ? track.spans[list[i + 1]].begin : INT64_MAX;
            while (j < ends.size() && events[ends[j]].time < span.begin) ++j;
            if (j == ends.size()) break;
            if (events[ends[j]].time >= next) continue;
            span.launch_end = span.end = events[ends[j]].time;
            consumed[ends[j]] = true;
            ++j;
        }
    }

    // A wave belongs to the latest dispatch on its queue that precedes it, as
    // long as it started before that dispatch's waves finished launching.
    for (const auto& [_id, waves] : traces)
        for (const auto& wave : waves)
        {
            if (wave.me < 0 || wave.pipe < 0) continue;
            auto key_it = dispatches_by_key.find(dispatchKey(wave.me, wave.pipe));
            if (key_it == dispatches_by_key.end()) continue;
            const auto& list = key_it->second;
            auto it = std::upper_bound(
                list.begin(),
                list.end(),
                wave.begin,
                [&](int64_t time, int idx) { return time < track.spans[idx].begin; }
            );
            if (it == list.begin()) continue;
            EventSpan& span = track.spans[*std::prev(it)];
            if (span.launch_end < 0 || wave.begin > span.launch_end) continue;
            span.end = std::max(span.end, wave.end);
            if (span.kid < 0) span.kid = wave.kid;
        }

    for (size_t i = 0; i < events.size(); ++i)
    {
        if (consumed[i]) continue;
        EventSpan span;
        span.begin = span.end = events[i].time;
        span.index = static_cast<int>(i);
        track.spans.push_back(span);
    }
    std::stable_sort(
        track.spans.begin(),
        track.spans.end(),
        [](const EventSpan& a, const EventSpan& b) { return a.begin < b.begin; }
    );

    track.dispatches = std::make_shared<const std::vector<dispatch_record_t>>(std::move(dispatches));
    track.events = std::make_shared<const std::vector<trace_event_record_t>>(std::move(events));
    tracks.push_back(std::move(track));
}

void EventTrackModel::Layout(uint32_t groups)
{
    using LaneEnd = std::pair<int64_t, int>; // {end time, lane}
    for (auto& track : tracks)
    {
        track.visible.clear();
        track.lanes = 0;
        track.max_len = 0;

        // Greedy interval packing: reuse the lane that frees up first.
        std::priority_queue<LaneEnd, std::vector<LaneEnd>, std::greater<>> lane_ends;
        for (size_t i = 0; i < track.spans.size(); ++i)
        {
            EventSpan& span = track.spans[i];
            if (!spanVisible(track, span, groups)) continue;
            if (!lane_ends.empty() && lane_ends.top().first < span.begin)
            {
                span.lane = lane_ends.top().second;
                lane_ends.pop();
            }
            else
                span.lane = track.lanes++;
            lane_ends.push({span.end, span.lane});
            track.visible.push_back(static_cast<int>(i));
            track.max_len = std::max(track.max_len, span.end - span.begin);
        }
    }
}

bool EventTrackModel::AllHidden() const
{
    return std::all_of(tracks.begin(), tracks.end(), [](const EventSubTrack& t) { return t.visible.empty(); });
}

QEventTrack::QEventTrack(std::shared_ptr<EventTrackModel> _model, QWidget* parent) :
QWidget(parent), model(std::move(_model))
{
    setMouseTracking(true);
    setAttribute(Qt::WA_AlwaysShowToolTips, true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    SetDecoderEventGroups(QGlobalView::DecoderEventGroups());
}

int QEventTrack::getHorizontalOffset() const
{
    if (m_scrollArea && m_scrollArea->horizontalScrollBar()) return m_scrollArea->horizontalScrollBar()->value();
    return 0;
}

int QEventTrack::laneHeight() const { return fontMetrics().ascent() + 2; }

int QEventTrack::markerRadius() const { return std::max(2, (laneHeight() - 6) / 2); }

bool QEventTrack::isBandCollapsed(size_t band) const
{
    return band < m_bandCollapsed.size() && m_bandCollapsed[band];
}

void QEventTrack::setBandCollapsed(size_t band, bool collapsed)
{
    if (band >= model->tracks.size()) return;
    m_bandCollapsed.resize(model->tracks.size(), false);
    m_bandCollapsed[band] = collapsed;
    clampVerticalOffset();
    updateGeometry();
    update();
}

int QEventTrack::bandLanes(size_t band) const
{
    const auto& track = model->tracks[band];
    if (track.lanes == 0) return 0;
    return isBandCollapsed(band) ? 1 : track.lanes;
}

int QEventTrack::bandHeight(size_t band) const
{
    const int lanes = bandLanes(band);
    if (lanes == 0) return THIN_BAND_PX;
    return lanes * laneHeight() + 2 * BAND_PAD_PX;
}

int QEventTrack::bandAt(int y, int* band_y) const
{
    int top = -m_vOffset;
    for (size_t i = 0; i < model->tracks.size(); ++i)
    {
        const int band_h = bandHeight(i);
        if (y >= top && y < top + band_h)
        {
            if (band_y) *band_y = top;
            return static_cast<int>(i);
        }
        top += band_h;
    }
    return -1;
}

int QEventTrack::contentHeight() const
{
    if (model->AllHidden()) return THIN_BAND_PX;
    int h = 0;
    for (size_t i = 0; i < model->tracks.size(); ++i) h += bandHeight(i);
    return h;
}

QSize QEventTrack::sizeHint() const
{
    return QSize(100, std::min(contentHeight(), DEFAULT_MAX_LANES * laneHeight() + 2 * BAND_PAD_PX));
}

QSize QEventTrack::minimumSizeHint() const { return QSize(100, THIN_BAND_PX); }

void QEventTrack::SetDecoderEventGroups(uint32_t groups)
{
    model->Layout(groups);
    const bool hidden = model->AllHidden();

    // Collapse to a thin strip when every event is filtered out, and give the
    // user's chosen height back once something is visible again.
    if (hidden && !m_allHidden)
    {
        if (isVisible()) m_expandedHeight = height();
        setFixedHeight(THIN_BAND_PX);
    }
    else if (!hidden && m_allHidden)
    {
        setMinimumHeight(THIN_BAND_PX);
        setMaximumHeight(QWIDGETSIZE_MAX);
        const int restore = m_expandedHeight > THIN_BAND_PX ? m_expandedHeight : sizeHint().height();
        if (auto* splitter = qobject_cast<QSplitter*>(parentWidget()))
        {
            QList<int> sizes = splitter->sizes();
            const int idx = splitter->indexOf(this);
            if (idx >= 0 && sizes.size() == 2)
            {
                const int total = sizes[0] + sizes[1];
                sizes[idx] = std::min(restore, total);
                sizes[1 - idx] = total - sizes[idx];
                splitter->setSizes(sizes);
            }
        }
    }
    m_allHidden = hidden;

    clampVerticalOffset();
    updateGeometry();
    update();
}

void QEventTrack::clampVerticalOffset()
{
    m_vOffset = std::clamp(m_vOffset, 0, std::max(0, contentHeight() - height()));
}

void QEventTrack::resizeEvent(QResizeEvent* event)
{
    Super::resizeEvent(event);
    clampVerticalOffset();
}

void QEventTrack::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    const QRect area = event->rect();
    painter.fillRect(area, WindowColors::Background());

    const int left = QTickHeader::LEFT_MARGIN;
    const int hOffset = getHorizontalOffset();
    auto toX = [&](int64_t time) { return static_cast<int>(left + QGlobalView::ClockToPos(time) - hOffset); };
    const int64_t clock_start = QGlobalView::PosToClock(hOffset + std::max(0, area.left() - left));
    const int64_t clock_end = QGlobalView::PosToClock(hOffset + std::max(0, area.right() - left));

    QPen sepPen(QColor(80, 80, 80));
    sepPen.setWidth(1);

    painter.save();
    painter.setClipRect(QRect(left, 0, width() - left, height()) & area);

    // Faint copy of the wave grid so events line up visually with the waves below.
    const int64_t clock_spacing = QGlobalView::Delta() * 100;
    QColor grid = WindowColors::textColor();
    grid.setAlpha(60);
    painter.setPen(QPen(grid, 1, Qt::DashLine));
    for (int64_t tick = QGlobalView::FirstVisibleTick(QGlobalView::PosToClock(0), clock_spacing, clock_start);
         tick <= clock_end;
         tick += clock_spacing)
        painter.drawLine(toX(tick), area.top(), toX(tick), area.bottom());

    // Lanes are shorter than a text line, so range labels use a smaller font.
    QFont label_font = painter.font();
    label_font.setPointSizeF(label_font.pointSizeF() * 0.85);
    const QFontMetrics fm(label_font);
    const int lane_h = laneHeight();
    const int min_label_w = fm.averageCharWidth() * 3 + 4;
    const int radius = markerRadius();
    const int64_t point_tol = QGlobalView::Delta() * radius;

    int y = -m_vOffset;
    for (size_t band = 0; band < model->tracks.size(); ++band)
    {
        const auto& track = model->tracks[band];
        const int band_h = bandHeight(band);
        const int band_y = y;
        y += band_h;
        if (band_y + band_h < area.top() || track.visible.empty()) continue;
        if (band_y > area.bottom()) break;

        // A collapsed band overlays all of its lanes in one row.
        const bool collapsed = isBandCollapsed(band);
        std::vector<int> last_px(bandLanes(band), INT_MIN);
        const auto& spans = track.spans;
        auto it = std::lower_bound(
            track.visible.begin(),
            track.visible.end(),
            clock_start - track.max_len - point_tol,
            [&](int idx, int64_t time) { return spans[idx].begin < time; }
        );
        for (; it != track.visible.end() && spans[*it].begin <= clock_end + point_tol; ++it)
        {
            const EventSpan& span = spans[*it];
            const int lane = collapsed ? 0 : span.lane;
            const int lane_y = band_y + BAND_PAD_PX + lane * lane_h;
            const int x0 = toX(span.begin);
            const int x1 = toX(span.end);

            if (span.IsRange() && x1 - x0 >= 2)
            {
                if (x1 < area.left()) continue;
                last_px[lane] = x1;
                // Clamp to the exposed area so very long spans keep sane coordinates.
                const int cx0 = std::max(x0, area.left() - 2);
                const int cx1 = std::min(x1, area.right() + 2);
                const QRect rect(cx0, lane_y + 1, cx1 - cx0, lane_h - 2);
                const QColor fill = span.kid >= 0
                                        ? MainWindow::dispatchcolors[span.kid % MainWindow::dispatchcolors.size()]
                                        : WindowColors::DecoderDispatchEventColor();
                painter.setRenderHint(QPainter::Antialiasing, true);
                painter.setPen(QPen(fill.darker(160), 1));
                painter.setBrush(fill);
                painter.drawRoundedRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);
                painter.setRenderHint(QPainter::Antialiasing, false);

                // Lighter shade over the launch phase, up to where the dispatch finished launching waves.
                const int xl = std::min(toX(span.launch_end), cx1);
                if (span.launch_end >= 0 && xl > cx0 + 1 && xl < x1 - 1)
                {
                    auto towardWhite = [](int c) { return (c * 3 + 255 * 2) / 5; };
                    const QColor launch(towardWhite(fill.red()), towardWhite(fill.green()), towardWhite(fill.blue()));
                    painter.fillRect(QRect(cx0 + 1, lane_y + 2, xl - cx0 - 1, lane_h - 4), launch);
                }

                const int label_x = std::max(x0, left) + 3;
                const int label_w = std::min(x1, width()) - 3 - label_x;
                if (label_w >= min_label_w)
                {
                    auto name_it = QGlobalView::kernel_names.find(span.kid);
                    const QString name = name_it != QGlobalView::kernel_names.end()
                                             ? QString::fromStdString(name_it->second)
                                             : QString("Dispatch");
                    const int luma = (fill.red() * 299 + fill.green() * 587 + fill.blue() * 114) / 1000;
                    painter.setPen(luma > 140 ? Qt::black : Qt::white);
                    painter.setFont(label_font);
                    painter.drawText(
                        QRect(label_x, lane_y, label_w, lane_h),
                        Qt::AlignVCenter | Qt::AlignLeft,
                        fm.elidedText(name, Qt::ElideRight, label_w)
                    );
                }
                continue;
            }

            if (x0 == last_px[lane]) continue;
            last_px[lane] = x0;
            const QColor fill = span.kind == EventSpan::Kind::Dispatch
                                    ? WindowColors::DecoderDispatchEventColor()
                                    : opaque(WindowColors::DecoderEventColor((*track.events)[span.index].type));
            const int cy = lane_y + lane_h / 2;
            const QPointF diamond[] = {
                QPointF(x0, cy - radius), QPointF(x0 + radius, cy), QPointF(x0, cy + radius), QPointF(x0 - radius, cy)};
            QColor outline = WindowColors::textColor();
            outline.setAlpha(160);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(QPen(outline, 1));
            painter.setBrush(fill);
            painter.drawPolygon(diamond, 4);
            painter.setRenderHint(QPainter::Antialiasing, false);
        }
    }
    painter.restore();

    // Left gutter: one label per SE band, plus band separators across the full width.
    painter.fillRect(QRect(0, 0, left, height()) & area, WindowColors::Background());
    y = -m_vOffset;
    for (size_t band = 0; band < model->tracks.size(); ++band)
    {
        const auto& track = model->tracks[band];
        const int band_h = bandHeight(band);
        if (y > area.bottom()) break;
        if (y + band_h >= area.top())
        {
            if (!track.visible.empty())
            {
                // Only bands with stacked lanes can be collapsed; show the toggle state for those.
                QString label = QString("SE %1").arg(track.se);
                if (track.lanes > 1) label.prepend(isBandCollapsed(band) ? "\u25B8 " : "\u25BE ");
                painter.setPen(WindowColors::textColor());
                painter.drawText(
                    QRect(3, y, left - 6, std::min(band_h, lane_h + 2 * BAND_PAD_PX)),
                    Qt::AlignVCenter | Qt::AlignLeft,
                    label
                );
            }
            painter.setPen(sepPen);
            painter.drawLine(0, y + band_h - 1, width(), y + band_h - 1);
        }
        y += band_h;
    }
    painter.setPen(sepPen);
    painter.drawLine(left - 1, 0, left - 1, height());
}

std::pair<int, int> QEventTrack::spanAt(const QPoint& pos) const
{
    const int left = QTickHeader::LEFT_MARGIN;
    if (pos.x() < left) return {-1, -1};

    const int lane_h = laneHeight();
    const int radius = markerRadius();
    const int pick_px = std::max(4, radius);
    const int64_t tol = QGlobalView::Delta() * pick_px;
    const int hOffset = getHorizontalOffset();
    auto toX = [&](int64_t time) { return static_cast<int>(left + QGlobalView::ClockToPos(time) - hOffset); };
    const int64_t clock = QGlobalView::PosToClock(pos.x() - left + hOffset);

    int band_y = 0;
    const int band = bandAt(pos.y(), &band_y);
    if (band < 0) return {-1, -1};
    const auto& track = model->tracks[band];
    if (track.visible.empty()) return {-1, -1};

    // In a collapsed band every lane shares the row, so any lane matches.
    const bool collapsed = isBandCollapsed(band);
    const int lane = std::clamp((pos.y() - band_y - BAND_PAD_PX) / lane_h, 0, track.lanes - 1);
    const int dy = std::abs(pos.y() - (band_y + BAND_PAD_PX + (collapsed ? 0 : lane) * lane_h + lane_h / 2));
    const auto& spans = track.spans;
    auto it = std::lower_bound(
        track.visible.begin(),
        track.visible.end(),
        clock - track.max_len - tol,
        [&](int idx, int64_t time) { return spans[idx].begin < time; }
    );

    // Replay paintEvent's order and pixel dedup for this row: the last shape drawn
    // under the cursor is the one on top. Otherwise fall back to the nearest shape.
    int top = -1;
    int nearest = -1;
    int nearest_px = pick_px + 1;
    int last_px = INT_MIN;
    for (; it != track.visible.end() && spans[*it].begin <= clock + tol; ++it)
    {
        const EventSpan& span = spans[*it];
        if (!collapsed && span.lane != lane) continue;
        const int x0 = toX(span.begin);
        const int x1 = toX(span.end);

        int dist;
        if (span.IsRange() && x1 - x0 >= 2)
        {
            last_px = x1;
            dist = pos.x() < x0 ? x0 - pos.x() : std::max(0, pos.x() - x1);
        }
        else
        {
            if (x0 == last_px) continue;
            last_px = x0;
            // Direct hit only inside the drawn diamond, so a bar showing past its corners keeps the hover.
            const int dx = std::abs(pos.x() - x0);
            dist = dx + dy <= radius ? 0 : std::max(1, dx);
        }

        if (dist == 0)
            top = *it;
        else if (dist < nearest_px)
        {
            nearest_px = dist;
            nearest = *it;
        }
    }
    const int best = top >= 0 ? top : nearest;
    return {best >= 0 ? band : -1, best};
}

void QEventTrack::mouseMoveEvent(QMouseEvent* event)
{
    Super::mouseMoveEvent(event);

    IMPLEMENT_FPS_LIMITER();

    const int hovered = event->pos().x() < QTickHeader::LEFT_MARGIN ? bandAt(event->pos().y()) : -1;
    setCursor(hovered >= 0 && model->tracks[hovered].lanes > 1 ? Qt::PointingHandCursor : Qt::ArrowCursor);

    const auto [t, s] = spanAt(event->pos());
    if (t < 0)
    {
        QToolTip::hideText();
        return;
    }

    const auto& track = model->tracks[t];
    const EventSpan& span = track.spans[s];
    std::string text;
    if (span.kind == EventSpan::Kind::TraceEvent)
        text = WaveOverlay::formatTraceEventTooltip((*track.events)[span.index], track.se);
    else
    {
        std::stringstream ss;
        ss << WaveOverlay::formatDispatchTooltip((*track.dispatches)[span.index], track.se);
        auto name_it = QGlobalView::kernel_names.find(span.kid);
        if (name_it != QGlobalView::kernel_names.end()) ss << "\nKernel: " << name_it->second.substr(0, 256);
        if (span.launch_end >= 0)
        {
            ss << "\nWaves launched until: " << span.launch_end;
            if (span.end > span.launch_end) ss << "\nLast wave end: " << span.end;
            ss << "\nDuration: " << (span.end - span.begin) << " cycles";
        }
        text = ss.str();
    }
    QToolTip::showText(
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        event->globalPos(),
#else
        event->globalPosition().toPoint(),
#endif
        QString::fromStdString(text),
        this
    );
}

void QEventTrack::mousePressEvent(QMouseEvent* event)
{
    Super::mousePressEvent(event);
    if (event->button() != Qt::LeftButton || event->pos().x() >= QTickHeader::LEFT_MARGIN) return;
    const int band = bandAt(event->pos().y());
    if (band >= 0 && model->tracks[band].lanes > 1) setBandCollapsed(band, !isBandCollapsed(band));
}

void QEventTrack::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier)
    {
        const int content_x =
            static_cast<int>(event->position().x()) - QTickHeader::LEFT_MARGIN + getHorizontalOffset();
        MainWindow::incrementGlobalViewMipmap(event->angleDelta().y() > 0 ? 1 : -1, std::max(0, content_x));
        return;
    }

    if (contentHeight() <= height())
    {
        event->ignore();
        return;
    }
    m_vOffset -= event->angleDelta().y() * laneHeight() / 120;
    clampVerticalOffset();
    update();
}
