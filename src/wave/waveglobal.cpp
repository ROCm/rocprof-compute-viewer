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

#include "waveglobal.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolTip>
#include "code/qcodelist.h"
#include "config/config.hpp"
#include "data/dispatch_resolver.h"
#include "data/marker_colors.h"
#include "data/shaderdata.h"
#include "data/wavedata.h"
#include "data/wavemanager.h"
#include "mainwindow.h"
#include "util/jsonrequest.hpp"
#include "wave/overlay_utils.h"

int64_t QGlobalView::begintime = 0;
int64_t QGlobalView::maxtime = 0;
int QGlobalView::mipmap_level = 10;
int QGlobalView::height_scale = 4;

// Column positions for SE|SA|CU|SM labels
static constexpr int COL_SE = 3;
static constexpr int COL_SA = 23;
static constexpr int COL_CU = 35;
static constexpr int COL_SM = 55;

// QTickHeader implementation
QTickHeader::QTickHeader(QWidget* parent) : QWidget(parent) { setFixedHeight(HEADER_HEIGHT); }

int QTickHeader::getHorizontalOffset() const
{
    if (m_scrollArea && m_scrollArea->horizontalScrollBar()) return m_scrollArea->horizontalScrollBar()->value();
    return 0;
}

void QTickHeader::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);

    // Fill background
    QPainterPath path;
    path.addRect(rect());
    painter.fillPath(path, WindowColors::Background());

    painter.setPen(WindowColors::textColor());
    QFontMetrics fm(painter.font());
    int headerY = (height() + fm.ascent()) / 2;

    // Draw column headers on the left (over label panel area)
    painter.drawText(COL_SE, headerY, "SE");
    painter.drawText(COL_SA, headerY, "A");
    painter.drawText(COL_CU, headerY, "CU");
    painter.drawText(COL_SM, headerY, "SM");

    // Draw tick marks (offset by LEFT_MARGIN since label panel is on the left)
    int64_t clock_spacing = QGlobalView::Delta() * 100;
    int hOffset = getHorizontalOffset(); // Get current scroll position

    if (clock_spacing > 0)
    {
        QPen pen = painter.pen();
        pen.setWidth(1);
        pen.setStyle(Qt::DashLine);
        pen.setColor(WindowColors::textColor());
        painter.setPen(pen);

        QFontMetrics fm(painter.font());
        int fmheight = fm.ascent();

        // Use the same calculation as QGlobalView::paintEvent
        int64_t bt = QGlobalView::PosToClock(0); // This equals static begintime
        int64_t clock_iter = bt + clock_spacing - (bt % clock_spacing);

        while (true)
        {
            // Calculate position the same way as in QGlobalView, then add offset
            int64_t content_pos = QGlobalView::ClockToPos(clock_iter);
            int barpos = LEFT_MARGIN + content_pos - hOffset;
            if (barpos >= width()) break;

            if (barpos >= LEFT_MARGIN)
            {
                painter.drawLine(barpos, 0, barpos, height());
                std::string cycle = std::to_string(clock_iter);
                painter.drawText(barpos + 3, fmheight + 2, cycle.c_str());
            }
            clock_iter += clock_spacing;
        }
    }

    // Draw vertical separator lines between columns and on the right of label area
    QPen sepPen = painter.pen();
    sepPen.setWidth(1);
    sepPen.setStyle(Qt::SolidLine);
    sepPen.setColor(QColor(80, 80, 80));
    painter.setPen(sepPen);
    painter.drawLine(COL_SA - 3, 0, COL_SA - 3, height()); // Between SE and SA
    painter.drawLine(COL_CU - 3, 0, COL_CU - 3, height()); // Between SA and CU
    painter.drawLine(COL_SM - 3, 0, COL_SM - 3, height()); // Between CU and SM
    painter.drawLine(LEFT_MARGIN - 1, 0, LEFT_MARGIN - 1, height());

    QWidget::paintEvent(event);
}

// QLabelPanel implementation
QLabelPanel::QLabelPanel(QWidget* parent) : QWidget(parent) { setFixedWidth(QTickHeader::LEFT_MARGIN); }

void QLabelPanel::addRow(int se, int sa, int cu, int simd, int slot) { m_rows.push_back({se, sa, cu, simd, slot, 0}); }

void QLabelPanel::setRowExtraHeight(int row_idx, int extra_px)
{
    if (row_idx < 0 || row_idx >= static_cast<int>(m_rows.size())) return;
    m_rows[row_idx].extra_px = extra_px;
}

void QLabelPanel::recalculatePositions()
{
    // Recalculate groups based on current mipmap level
    m_simdGroups.clear();
    m_simdBoundaries.clear();
    if (m_rows.empty()) return;

    const int trackheight = QGlobalView::HEIGHT() + 1;
    int totalSimdGroups = 0;
    int cur_se = -1, cur_sa = -1, cur_cu = -1, cur_simd = -1;
    for (const auto& row : m_rows)
    {
        if (row.se != cur_se || row.sa != cur_sa || row.cu != cur_cu || row.simd != cur_simd)
        {
            totalSimdGroups++;
            cur_se = row.se;
            cur_sa = row.sa;
            cur_cu = row.cu;
            cur_simd = row.simd;
        }
    }

    int totalHeight = 0;
    for (const auto& row : m_rows) totalHeight += trackheight + row.extra_px;
    int avgSimdHeight = totalSimdGroups > 0 ? totalHeight / totalSimdGroups : totalHeight;

    // Decide grouping: if average SIMD group is too small, group by CU instead
    bool groupByCU = avgSimdHeight < QFontMetrics(font()).height();

    cur_se = -1;
    cur_sa = -1;
    cur_cu = -1;
    cur_simd = -1;
    int groupStart = 0;
    int y = 0;

    for (size_t i = 0; i < m_rows.size(); i++)
    {
        const auto& row = m_rows[i];
        bool newGroup;
        if (groupByCU)
            newGroup = (row.se != cur_se || row.sa != cur_sa || row.cu != cur_cu);
        else
            newGroup = (row.se != cur_se || row.sa != cur_sa || row.cu != cur_cu || row.simd != cur_simd);

        if (newGroup)
        {
            // Close previous group
            if (cur_se >= 0)
            {
                m_simdGroups.push_back({cur_se, cur_sa, cur_cu, groupByCU ? -1 : cur_simd, groupStart, y});
                m_simdBoundaries.push_back(y);
            }
            // Start new group
            cur_se = row.se;
            cur_sa = row.sa;
            cur_cu = row.cu;
            cur_simd = row.simd;
            groupStart = y;
        }
        y += trackheight + row.extra_px;
    }
    // Close last group
    if (cur_se >= 0) m_simdGroups.push_back({cur_se, cur_sa, cur_cu, groupByCU ? -1 : cur_simd, groupStart, y});

    update();
}

void QLabelPanel::finalize() { recalculatePositions(); }

void QLabelPanel::setVerticalOffset(int offset)
{
    m_vOffset = offset;
    update();
}

void QLabelPanel::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);

    // Fill background
    painter.fillRect(rect(), WindowColors::Background());

    painter.setPen(WindowColors::textColor());
    QFontMetrics fm(painter.font());

    // Separator pen
    QPen sepPen;
    sepPen.setWidth(1);
    sepPen.setStyle(Qt::SolidLine);
    sepPen.setColor(QColor(80, 80, 80));

    // Draw SIMD group labels (centered vertically in each group)
    for (size_t i = 0; i < m_simdGroups.size(); i++)
    {
        const auto& grp = m_simdGroups[i];
        int yStart = grp.yStart - m_vOffset;
        int yEnd = grp.yEnd - m_vOffset;

        if (yEnd < 0) continue;       // Above visible area
        if (yStart > height()) break; // Below visible area

        // Draw horizontal separator line at the top of each SIMD group (except first)
        if (i > 0 && yStart >= 0 && yStart < height())
        {
            painter.setPen(sepPen);
            painter.drawLine(0, yStart, width(), yStart);
        }

        // Center the text vertically in the group
        int yCenter = (yStart + yEnd) / 2;
        int textY = yCenter + fm.ascent() / 2 - 1;

        // Only draw if text would be visible
        painter.setPen(WindowColors::textColor());
        if (textY >= 0 && textY - fm.ascent() < height())
        {
            QString seStr = QString("%1").arg(grp.se, 2, 10, QChar('0'));
            QString saStr = QString::number(grp.sa);
            QString cuStr = QString("%1").arg(grp.cu, 2, 10, QChar('0'));
            QString smStr = (grp.simd >= 0) ? QString::number(grp.simd) : QString("-");

            painter.drawText(COL_SE, textY, seStr);
            painter.drawText(COL_SA, textY, saStr);
            painter.drawText(COL_CU, textY, cuStr);
            painter.drawText(COL_SM, textY, smStr);
        }
    }

    // Draw vertical separator lines between columns
    painter.setPen(sepPen);
    painter.drawLine(COL_SA - 3, 0, COL_SA - 3, height()); // Between SE and SA
    painter.drawLine(COL_CU - 3, 0, COL_CU - 3, height()); // Between SA and CU
    painter.drawLine(COL_SM - 3, 0, COL_SM - 3, height()); // Between CU and SM

    // Draw vertical separator line on the right
    painter.drawLine(width() - 1, 0, width() - 1, height());

    QWidget::paintEvent(event);
}

void QGlobalView::setScrollArea(QScrollArea* sa)
{
    m_scrollArea = sa;
    if (!sa) return;
    if (tickHeader)
    {
        tickHeader->setScrollArea(sa);
        connect(sa->horizontalScrollBar(), &QScrollBar::valueChanged, tickHeader, &QTickHeader::onScrollChanged);
    }
    if (labelPanel)
        connect(sa->verticalScrollBar(), &QScrollBar::valueChanged, labelPanel, &QLabelPanel::setVerticalOffset);
}

int QGlobalView::calcZoomScroll(int old_mip, int new_mip, int old_scroll, int anchor_viewport_x)
{
    // Calculate the content position of the anchor point
    int anchor_content_x = old_scroll + anchor_viewport_x;

    // Calculate where that content position will be after zoom
    int64_t new_anchor_content_x;
    if (new_mip > old_mip)
        new_anchor_content_x = anchor_content_x >> (new_mip - old_mip);
    else
        new_anchor_content_x = anchor_content_x << (old_mip - new_mip);

    // New scroll keeps anchor at same viewport position
    int new_scroll = new_anchor_content_x - anchor_viewport_x;
    return std::max(0, new_scroll);
}

std::unordered_map<int, std::string> QGlobalView::kernel_names{};

namespace
{
constexpr int64_t SQTT_POINT_MARKER_MIN_CYCLES = 8;

/// Pack a (se, cu, simd, slot) location into the 24-bit key used by both
/// constructors' trace maps. Layout (LSB → MSB):
///   slot:5 | simd:2 | cu:7 | sa:1 | se:N
/// Note: the SA bit is encoded as 0 here because both producer paths only
/// have (se, cu, simd, slot) at this point — sa is inferred from cu by the
/// QOutsideWaveView constructor downstream of decodeTraceID().
inline int packTraceID(int se, int cu, int simd, int slot) { return slot + 32 * (simd + 4 * (cu + 256 * se)); }

struct DecodedTraceID
{
    int se, sa, cu, simd, slot;
};

inline DecodedTraceID decodeTraceID(int traceID)
{
    DecodedTraceID d;
    d.slot = traceID & 0x1F;
    traceID /= 32;
    d.simd = traceID & 0x3;
    traceID /= 4;
    d.cu = traceID & 0x7F;
    traceID /= 128;
    d.sa = traceID & 0x1;
    traceID /= 2;
    d.se = traceID;
    return d;
}

std::string clippedTooltipText(const std::string& text, size_t max_chars = 256)
{
    if (text.size() <= max_chars) return text;
    return text.substr(0, max_chars) + "...";
}

/// Append one occupancy sample to the per-traceID trace under construction,
/// updating begin/end time bookkeeping. Shared by both constructor paths.
void accumulateOccupancySample(
    const occupancy_data& data,
    int se,
    std::map<int, std::vector<WaveTraceData>>& traces,
    int64_t& begintime,
    int64_t& maxtime,
    const WaveTraceData* extra = nullptr
)
{
    int traceID = packTraceID(se, data.cu, data.simd, data.slot);
    auto& trace = traces[traceID];

    if (data.enable == 1)
    {
        WaveTraceData wtd;
        wtd.begin = data.time;
        wtd.end = 0;
        wtd.kid = data.kernel_id;
        if (extra)
        {
            wtd.has_dispatcher_info = extra->has_dispatcher_info;
            wtd.me = extra->me;
            wtd.pipe = extra->pipe;
            wtd.has_workgroup_id = extra->has_workgroup_id;
            wtd.workgroup_id = extra->workgroup_id;
            wtd.has_occupancy_flags = extra->has_occupancy_flags;
            wtd.occupancy_flags = extra->occupancy_flags;
            wtd.has_register_usage = extra->has_register_usage;
            wtd.sgprs = extra->sgprs;
            wtd.vgprs = extra->vgprs;
        }
        trace.push_back(wtd);
    }
    else if (trace.size()) { trace.back().end = data.time; }
    begintime = std::min<int64_t>(begintime, data.time);
    maxtime = std::max<int64_t>(maxtime, data.time);
}

/// Walk a per-SE traces map, materializing one QOutsideWaveView per non-empty
/// trace and pushing each into `layout` and `views`. Updates the rolling
/// CU-group tracking state shared across SEs.
void materializeTraces(
    std::map<int, std::vector<WaveTraceData>>& traces,
    std::vector<QOutsideWaveView*>& views,
    QVBox* layout,
    std::shared_ptr<MeasureTool>& tool,
    int64_t maxtime,
    int& current_se,
    int& current_sa,
    int& current_cu,
    int& cu_slot_count
)
{
    for (auto& [_traceid, trace] : traces)
    {
        if (!trace.size()) continue;
        if (trace.back().end == 0) trace.back().end = maxtime;

        DecodedTraceID d = decodeTraceID(_traceid);

        if (d.se != current_se || d.sa != current_sa || d.cu != current_cu)
        {
            current_se = d.se;
            current_sa = d.sa;
            current_cu = d.cu;
            cu_slot_count = 0;
        }
        cu_slot_count++;

        views.push_back(new QOutsideWaveView(d.se, d.sa, d.cu, d.simd, d.slot, trace, tool));
        layout->addWidget(views.back());
    }
}

} // anonymous namespace

QGlobalView::QGlobalView(const std::string& filename)
{
    JsonRequest file(filename);
    QWARNING(!file.fail() && !file.bad(), "Error opening file " << filename, return );

    try
    {
        std::string(file.data["version"]).at(0) == '3';
    }
    catch (...)
    {
        std::cerr << "QGlobalView: unsupported wave-occupancy schema in " << filename
                  << " (expected version starting with '3')" << std::endl;
        QWARNING(false, "Legacy version not supported!", return );
    }

    for (auto& [id, name] : file.data["dispatches"].items()) kernel_names[stoi(id)] = name;

    QVBox* layout = new QVBox();
    maxtime = 0;
    begintime = INT64_MAX;

    int current_se = -1, current_sa = -1, current_cu = -1;
    int cu_slot_count = 0;

    for (auto& [SE, array] : file.data.items())
    {
        if (array.size() == 0) continue;
        int se = 0;
        try
        {
            se = std::stoi(SE);
            if (std::to_string(se) != SE) continue;
        }
        catch (...)
        {
            RCV_LOG();
            continue;
        }

        std::map<int, std::vector<WaveTraceData>> traces{};
        for (auto& v : array)
        {
            occupancy_data data = occupancy_data::build(v);
            accumulateOccupancySample(data, se, traces, begintime, maxtime);
        }
        materializeTraces(traces, views, layout, tool, maxtime, current_se, current_sa, current_cu, cu_slot_count);
        layout->addStretch();
    }
    if (begintime == INT64_MAX) begintime = 0;
    this->setLayout(layout);
    setMouseTracking(true);
    this->setAttribute(Qt::WA_AlwaysShowToolTips, true);

    if (tool) tool->update_list.insert(this);
}

QGlobalView::QGlobalView(DataStore& store)
{
    QVBox* layout = new QVBox();
    maxtime = 0;
    // Events/dispatches can precede the first occupancy sample, so origin is the
    // min over all three record sets.
    begintime = INT64_MAX;

    int current_se = -1, current_sa = -1, current_cu = -1;
    int cu_slot_count = 0;

    std::map<int, DispatchRecordVec> dispatches_by_se;
    for (const auto& [se, records] : store.dispatch_records_by_se)
    {
        dispatches_by_se[se] = std::make_shared<std::vector<dispatch_record_t>>(records);
        for (const auto& rec : records)
        {
            begintime = std::min(begintime, rec.time);
            maxtime = std::max(maxtime, rec.time);
        }
    }

    // Build trace-event spans before sizing bars so trailing events extend maxtime.
    std::map<int, TraceEventRecordVec> trace_events_by_se;
    for (const auto& [se, records] : store.trace_events_by_se)
    {
        auto sorted = std::make_shared<std::vector<trace_event_record_t>>(records);
        std::sort(
            sorted->begin(),
            sorted->end(),
            [](const trace_event_record_t& a, const trace_event_record_t& b) { return a.time < b.time; }
        );
        trace_events_by_se[se] = sorted;
        if (!sorted->empty())
        {
            begintime = std::min(begintime, sorted->front().time);
            maxtime = std::max(maxtime, sorted->back().time);
        }
    }

    for (auto& [se, records] : store.occupancy_by_se)
    {
        if (records.empty()) continue;

        std::map<int, std::vector<WaveTraceData>> traces{};
        const bool has_decoder_extras =
            !store.wave_records.empty() || !store.trace_events_by_se.empty() || !store.dispatch_records_by_se.empty();

        for (const auto& rec : records)
        {
            occupancy_data data;
            data.time = rec.time;
            data.cu = rec.cu;
            data.simd = rec.simd;
            data.slot = rec.wave_id;
            data.enable = rec.start;
            data.kernel_id = store.dispatch_resolver.Resolve(rec.pc);

            WaveTraceData extra{};
            extra.me = rec.me_id;
            extra.pipe = rec.pipe_id;
            extra.workgroup_id = rec.workgroup_id;
            extra.has_dispatcher_info = has_decoder_extras && rec.start;
            extra.has_workgroup_id = has_decoder_extras && rec.start && rec.is_ext;
            auto dispatch_it = dispatches_by_se.find(se);
            if (rec.start && dispatch_it != dispatches_by_se.end())
                for (auto it = dispatch_it->second->rbegin(); it != dispatch_it->second->rend(); ++it)
                {
                    if (it->time >= rec.time) continue;
                    if (it->me_id != rec.me_id || it->pipe_id != rec.pipe_id) continue;
                    extra.has_register_usage = true;
                    extra.sgprs = it->sgprs;
                    extra.vgprs = it->vgprs;
                    break;
                }

            accumulateOccupancySample(data, se, traces, begintime, maxtime, &extra);
        }
        materializeTraces(traces, views, layout, tool, maxtime, current_se, current_sa, current_cu, cu_slot_count);
        layout->addStretch();
    }

    if (begintime == INT64_MAX) begintime = 0;

    for (auto* view : views)
    {
        auto event_it = trace_events_by_se.find(view->se);
        if (event_it != trace_events_by_se.end()) view->SetTraceEvents(event_it->second);

        auto dispatch_it = dispatches_by_se.find(view->se);
        if (dispatch_it != dispatches_by_se.end()) view->SetDispatchRecords(dispatch_it->second);
    }

    // Mirror resolver names into the legacy static so tooltips (line ~1000) and
    // any other static-lookup users don't fall back to "kernel_<kid>".
    for (auto& [kid, name] : store.dispatch_resolver.Names()) kernel_names[kid] = name;

    this->setLayout(layout);
    setMouseTracking(true);
    this->setAttribute(Qt::WA_AlwaysShowToolTips, true);

    if (tool) tool->update_list.insert(this);
}

void QGlobalView::SetShaderData(const ShaderDataManager& manager)
{
    if (!manager.HasData()) return;

    for (auto* view : views)
    {
        auto records = manager.GetRecords(view->se, view->cu, view->simd, view->slot);
        if (records) view->SetShaderData(std::move(records));
    }
}

void QGlobalView::SetMarkers(const ShaderDataManager& manager)
{
    if (!manager.HasMarkers()) return;

    for (auto* view : views)
    {
        auto spans = manager.GetMarkers(view->se, view->cu, view->simd, view->slot);
        if (spans) view->SetMarkers(std::move(spans));
    }

    // Rows that grew to host a marker track must be reflected in the sticky
    // label panel and SIMD-group separator positions, otherwise labels drift
    // away from the wave rows they describe. Note: populateLabelPanel may not
    // have been called yet (MainWindow currently calls SetMarkers first); the
    // sync there is idempotent and handles either ordering.
    syncLabelPanelExtraHeights();
    this->updateGeometry();
    this->update();
}

void QGlobalView::syncLabelPanelExtraHeights()
{
    if (!labelPanel) return;
    for (size_t i = 0; i < views.size(); ++i)
        labelPanel->setRowExtraHeight(
            static_cast<int>(i), views[i]->markerTrackHeightPx() + views[i]->markerBottomPadPx()
        );
    labelPanel->recalculatePositions();
    labelPanel->update();
}

void QGlobalView::populateLabelPanel()
{
    if (!labelPanel) return;

    for (auto* view : views) labelPanel->addRow(view->se, view->sa, view->cu, view->simd, view->slot);

    labelPanel->finalize();
    // If SetMarkers ran before populateLabelPanel (current ordering in
    // MainWindow), the extra-height info on each view is already known but
    // wasn't applied because rows didn't exist yet. Re-sync now.
    syncLabelPanelExtraHeights();
}

void QGlobalView::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);

    QPainterPath path;
    path.addRect(QRect(0, 0, width(), height()));
    painter.fillPath(path, WindowColors::Background());

    // Draw horizontal separator lines at SIMD group boundaries
    if (labelPanel)
    {
        QPen sepPen;
        sepPen.setWidth(1);
        sepPen.setStyle(Qt::SolidLine);
        sepPen.setColor(QColor(80, 80, 80));
        painter.setPen(sepPen);

        for (int y : labelPanel->m_simdBoundaries) painter.drawLine(0, y, width(), y);
    }

    // Draw vertical tick lines (labels are in the sticky header)
    int64_t clock_spacing = Delta() * 100;

    if (clock_spacing > 0)
    {
        QPen pen = painter.pen();
        pen.setWidth(1);
        pen.setStyle(Qt::DashLine);
        pen.setColor(WindowColors::textColor());
        painter.setPen(pen);

        int64_t clock_iter = begintime + clock_spacing - (begintime % clock_spacing);

        while (true)
        {
            int barpos = ClockToPos(clock_iter);
            if (barpos >= width()) break;

            painter.drawLine(barpos, 0, barpos, height());
            clock_iter += clock_spacing;
        }
    }

    this->Super::paintEvent(event);
}

QOutsideWaveView::QOutsideWaveView(
    int _se,
    int _sa,
    int _cu,
    int _simd,
    int _slot,
    std::vector<WaveTraceData>& _waves,
    std::shared_ptr<MeasureTool>& _tool
) :
se(_se), sa(_sa), cu(_cu), simd(_simd), slot(_slot), waves(_waves), tool(_tool)
{
    setMouseTracking(true);
    this->setAttribute(Qt::WA_AlwaysShowToolTips, true);

    height_multiplier = 0; // No extra space for separators

    if (tool) tool->update_list.insert(this);
}

void QOutsideWaveView::SetShaderData(ShaderDataRecordVec records) { shaderdata_records = std::move(records); }

void QOutsideWaveView::SetTraceEvents(TraceEventRecordVec records)
{
    trace_events = std::move(records);
    update();
}

void QOutsideWaveView::SetDispatchRecords(DispatchRecordVec records)
{
    dispatch_records = std::move(records);
    update();
}

void QOutsideWaveView::SetMarkers(MarkerSpanVec spans)
{
    markers.Reset(std::move(spans));
    updateGeometry();
    update();
}

int QOutsideWaveView::markerRowPx() const
{
    // Scale row height with vertical zoom so labels become legible as the user
    // grows the wave. HEIGHT()*3 picks the default band height (12px at zoom=4)
    // and grows linearly from there.
    return std::max(MARKER_ROW_PX_MIN, static_cast<int>(QGlobalView::HEIGHT()) * 3);
}

int QOutsideWaveView::markerTrackHeightPx() const
{
    if (markers.empty()) return 0;
    const int n_rows = markers.max_depth + 1;
    const int desired = markerRowPx() * n_rows;
    // Cap also scales with zoom — at low zoom we keep the track compact, but
    // when the user explicitly zooms in we let the marker track grow with it.
    const int cap = std::max(MARKER_TRACK_MAX_PX_MIN, static_cast<int>(QGlobalView::HEIGHT()) * 24);
    return std::min(desired, cap);
}

int QOutsideWaveView::markerBottomPadPx() const
{
    // ~1 wave-bar height of empty space below the wave when the row has
    // markers, so neighbouring slots don't visually run together.
    if (markers.empty()) return 0;
    return std::max<int>(4, static_cast<int>(QGlobalView::HEIGHT()));
}

void QOutsideWaveView::DrawTypedMarkers(QPainter& painter, const QRect& area)
{
    if (markers.empty()) return;
    const int track_h = markerTrackHeightPx();
    if (track_h <= 0) return;
    const auto& spans = *markers.spans;

    const int64_t visible_clock_start = QGlobalView::PosToClock(area.left());
    const int64_t visible_clock_end = QGlobalView::PosToClock(area.right());

    // Subdivide the *dedicated marker track* (above the wave) by stack depth so
    // nested scopes are visible (Perfetto-style). The wave is left untouched.
    const int n_rows = std::max(1, markers.max_depth + 1);
    const int row_h = std::max(1, track_h / n_rows);

    // Inline labels are only legible when the row is taller than the font
    // ascent and the span is wide enough to fit a few characters.
    const QFontMetrics fm = painter.fontMetrics();
    const int fa = fm.ascent();
    const bool labels_fit_vertically = row_h >= fa + 2;
    const int min_label_w = fm.averageCharWidth() * 3 + 4;

    // Walk forward from FirstCandidate to catch closed spans that started before
    // the viewport. Open spans straddle arbitrarily and are tracked separately.
    const int64_t search_from = visible_clock_start - markers.max_closed_dur;
    auto it_begin = markers.FirstCandidate(visible_clock_start);

    painter.save();

    // Per-depth coalescing — sub-pixel spans on the same row collapse, but
    // different rows must paint independently so nested scopes don't drop out.
    std::vector<int> last_drawn_pixel(n_rows, INT_MIN);

    auto draw_span = [&](const MarkerSpan& s, size_t idx)
    {
        const int x0 = QGlobalView::ClockToPos(s.enter_time);
        const int64_t exit_clk = s.is_open ? QGlobalView::PosToClock(area.right() + 1) : s.exit_time;
        if (exit_clk < visible_clock_start) return;
        const int x1 = QGlobalView::ClockToPos(exit_clk);

        const int depth = std::clamp(s.depth, 0, n_rows - 1);
        // Invert the depth-to-Y mapping: depth 0 (outermost — e.g. Kernel) sits
        // at the bottom of the marker track, adjacent to the wave. Deeper scopes
        // grow upward, forming a natural pyramid that fans out away from the wave.
        const int y0 = (n_rows - 1 - depth) * row_h;

        QColor col = markers.colors[idx];
        col.setAlphaF(0.92);

        if (s.is_point)
        {
            if (x0 == last_drawn_pixel[depth]) return;
            last_drawn_pixel[depth] = x0;
            const int64_t point_width_px = QGlobalView::ClockToPos(s.enter_time + SQTT_POINT_MARKER_MIN_CYCLES) - x0;
            const int point_w = static_cast<int>(std::max<int64_t>(2, point_width_px));
            painter.setPen(Qt::NoPen);
            painter.setBrush(col);
            painter.drawRect(x0, y0, point_w, row_h);
            return;
        }

        int w = std::max(1, x1 - x0);
        if (x0 == last_drawn_pixel[depth] && w == 1) return;
        last_drawn_pixel[depth] = x0 + w - 1;
        painter.setPen(Qt::NoPen);
        painter.setBrush(col);
        painter.drawRect(x0, y0, w, row_h);

        // Inline label — only when the span has the room. Skip for unresolved
        // (empty name) spans to avoid rendering clutter.
        if (labels_fit_vertically && w >= min_label_w && !s.name.empty())
        {
            // Clip the label rect to the visible area so spans wider than the
            // viewport still get a legible label anchored to their visible left.
            int label_x = std::max(x0 + 2, area.left() + 2);
            int label_w = (x0 + w - 2) - label_x;
            if (label_w >= min_label_w)
            {
                QString name = QString::fromStdString(s.name);
                QString text = fm.elidedText(name, Qt::ElideRight, label_w);
                // Pick a contrasting text color from the fill.
                int luma = (col.red() * 299 + col.green() * 587 + col.blue() * 114) / 1000;
                painter.setPen(luma > 140 ? Qt::black : Qt::white);
                painter.drawText(QRect(label_x, y0, label_w, row_h), Qt::AlignVCenter | Qt::AlignLeft, text);
            }
        }
    };

    for (auto it = it_begin; it != spans.end(); ++it)
    {
        if (it->enter_time > visible_clock_end) break;
        draw_span(*it, static_cast<size_t>(it - spans.begin()));
    }
    for (int idx : markers.open_indices)
    {
        if (spans[idx].enter_time >= search_from) break;
        draw_span(spans[idx], static_cast<size_t>(idx));
    }

    painter.restore();
}

void QOutsideWaveView::DrawShaderDataMarkers(QPainter& painter, const QRect& area)
{
    // Marker mode: use typed colored spans instead of red rectangles.
    if (!markers.empty())
    {
        DrawTypedMarkers(painter, area);
        return;
    }

    if (!shaderdata_records || shaderdata_records->empty()) return;

    const auto& recs = *shaderdata_records;
    const int waveheight = QGlobalView::HEIGHT();
    const int markerWidth = std::max<int64_t>(24 / QGlobalView::Delta(), 10);

    // Binary search: find the first record whose pixel position could be visible
    const int64_t visible_clock_start = QGlobalView::PosToClock(area.left() - markerWidth);
    const int64_t visible_clock_end = QGlobalView::PosToClock(area.right() + markerWidth);

    auto it_begin = std::lower_bound(
        recs.begin(), recs.end(), visible_clock_start, [](const ShaderDataRecord& r, int64_t c) { return r.time < c; }
    );

    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(WindowColors::ShaderDataColor());

    int last_pixel = INT_MIN; // For pixel deduplication

    for (auto it = it_begin; it != recs.end(); ++it)
    {
        if (it->time > visible_clock_end) break;

        int pos = QGlobalView::ClockToPos(it->time);

        // Skip if this record maps to the same pixel as the previous one
        if (pos == last_pixel) continue;
        last_pixel = pos;

        painter.drawRect(pos, 0, markerWidth, waveheight);
    }

    painter.restore();
}

int QOutsideWaveView::FindShaderDataAt(int64_t clock_pos) const
{
    const int markerWidth = std::max<int64_t>(24 / QGlobalView::Delta(), 10);
    return FindShaderDataRecord(shaderdata_records, clock_pos, QGlobalView::Delta() * markerWidth);
}

void QOutsideWaveView::DrawDecoderEvents(QPainter& painter, const QRect& area)
{
    const int row_h = sizeHint().height();
    const int64_t visible_clock_start = QGlobalView::PosToClock(area.left() - 2);
    const int64_t visible_clock_end = QGlobalView::PosToClock(area.right() + 2);

    WaveOverlay::drawDecoderEvents(
        painter,
        trace_events.get(),
        dispatch_records.get(),
        visible_clock_start,
        visible_clock_end,
        row_h,
        [](int64_t time) { return QGlobalView::ClockToPos(time); }
    );
}

int QOutsideWaveView::FindTraceEventAt(int64_t clock_pos) const
{
    const int64_t tol = std::max<int64_t>(QGlobalView::Delta() * 4, 4);
    return WaveOverlay::findRecordIndexAt(trace_events.get(), clock_pos, tol);
}

int QOutsideWaveView::FindDispatchAt(int64_t clock_pos) const
{
    const int64_t tol = std::max<int64_t>(QGlobalView::Delta() * 4, 4);
    return WaveOverlay::findRecordIndexAt(dispatch_records.get(), clock_pos, tol);
}

int QOutsideWaveView::FindMarkerAt(int64_t clock_pos, int y) const
{
    if (markers.empty()) return -1;
    const int track_h = markerTrackHeightPx();
    if (track_h <= 0) return -1;
    // Hover only resolves to a marker when the cursor is inside the dedicated
    // marker track at the top — never when the user is over the wave itself.
    if (y >= 0 && y >= track_h) return -1;
    const auto& spans = *markers.spans;

    // Mirror DrawTypedMarkers row layout (track-relative, NOT wave-relative).
    // y < 0 means "ignore Y; pick deepest match".
    const int n_rows = std::max(1, markers.max_depth + 1);
    const int row_h = std::max(1, track_h / n_rows);
    // Match DrawTypedMarkers' inverted layout: y=0 is the deepest row at the
    // top, y=marker_track_height_px-1 is depth 0 adjacent to the wave.
    const int target_depth = (y >= 0) ? std::clamp((n_rows - 1) - (y / row_h), 0, n_rows - 1) : -1;

    // Tolerance in clocks for hovering near a point or short span.
    const int64_t tol = std::max<int64_t>(QGlobalView::Delta() * 4, SQTT_POINT_MARKER_MIN_CYCLES);

    // FirstCandidate uses max_closed_dur as the backstep; widen the cursor by
    // the hover tolerance so a hover just before/after a span's edge still hits.
    const int64_t search_from = clock_pos - markers.max_closed_dur - tol;
    auto it = markers.FirstCandidate(clock_pos - tol);

    int best_idx = -1;
    int best_depth = -1;
    auto consider = [&](const MarkerSpan& s, int idx)
    {
        const int64_t end = s.is_open ? std::numeric_limits<int64_t>::max() : s.exit_time;
        const int64_t lo = s.is_point ? s.enter_time - tol : s.enter_time;
        const int64_t hi = s.is_point ? s.enter_time + tol : end;
        if (clock_pos < lo || clock_pos > hi) return;
        // Mirror DrawTypedMarkers: depths past n_rows-1 collapse onto the topmost row.
        const int s_depth = std::clamp(s.depth, 0, n_rows - 1);
        if (target_depth >= 0)
        {
            if (s_depth != target_depth) return;
            best_idx = idx; // exact row match — accept the first (or last) we see
            best_depth = s_depth;
        }
        else if (s_depth > best_depth)
        {
            best_depth = s_depth;
            best_idx = idx;
        }
    };
    for (; it != spans.end(); ++it)
    {
        if (it->enter_time > clock_pos + tol) break;
        consider(*it, static_cast<int>(it - spans.begin()));
    }
    for (int idx : markers.open_indices)
    {
        if (spans[idx].enter_time >= search_from) break;
        consider(spans[idx], idx);
    }
    return best_idx;
}

static QColor whiter(const QColor& a)
{
    return QColor(2 * a.red() / 3 + 85, 2 * a.green() / 3 + 85, 2 * a.blue() / 3 + 85);
}

void QOutsideWaveView::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int64_t leftover = 0;
    int64_t end_clock = 0;
    for (auto& wave : waves)
    {
        if (end_clock + QGlobalView::Delta() < wave.begin) leftover = 0;

        leftover = this->DrawWave(painter, wave.begin - leftover, wave, event->rect());

        if (leftover == 0) end_clock = wave.end;
    }

    if (tool && tool->bClicking)
    {
        QPainterPath path;
        path.addRect(QRect(tool->measure_start_x, 0, tool->measure_size_x, height()));
        painter.fillPath(path, WindowColors::MeasureTool());
    }

    // Draw shaderdata triangle markers on top of waves
    DrawShaderDataMarkers(painter, event->rect());
    DrawDecoderEvents(painter, event->rect());

    // Subtle separator between the marker track (top) and the wave (below) so
    // the two layers read as distinct.
    const int track_h = markerTrackHeightPx();
    if (track_h > 0)
    {
        QPen sep;
        sep.setWidth(1);
        sep.setColor(QColor(60, 60, 60));
        painter.setPen(sep);
        painter.drawLine(0, track_h - 1, sizeHint().width(), track_h - 1);
    }

    // Draw 1-pixel separator line at the bottom of the wave bar. With marker
    // bottom-padding active this leaves a gap below the separator before the
    // next row begins, visually decoupling adjacent slots.
    QPen pen = painter.pen();
    pen.setWidth(1);
    pen.setColor(QColor(60, 60, 60));
    painter.setPen(pen);
    const int sep_y = markerTrackHeightPx() + static_cast<int>(QGlobalView::HEIGHT());
    painter.drawLine(0, sep_y, sizeHint().width(), sep_y);

    this->Super::paintEvent(event);
}

int64_t QOutsideWaveView::DrawWave(QPainter& painter, int64_t start, const WaveTraceData& wave, const QRect& area)
{
    int pos = QGlobalView::ClockToPos(start);
    int width = QGlobalView::ClockToPos(wave.end) - QGlobalView::ClockToPos(start);

    if (pos > area.right()) return 0;
    if (pos + width < area.left()) return 0;

    QColor& color = MainWindow::dispatchcolors[wave.kid % MainWindow::dispatchcolors.size()];

    if (width < 1) return wave.end - start;
    const int waveheight = QGlobalView::HEIGHT();

    QPainterPath path;
    // Push wave below the dedicated marker track (zero pixels when no markers).
    QRectF rect(pos, qreal(markerTrackHeightPx()), width, waveheight);
    path.addRect(rect);

    QLinearGradient grad(0, 0, 0, waveheight);
    grad.setColorAt(0.5, color);
    grad.setColorAt(0, whiter(color));
    QBrush brush(grad);
    painter.fillPath(path, brush);
    return 0;
}

QSize QOutsideWaveView::sizeHint() const
{
    int baseheight = QGlobalView::HEIGHT() + markerTrackHeightPx() + markerBottomPadPx();
    return QSize(waves.size() ? QGlobalView::ClockToPos(QGlobalView::MaxTime()) : 256, baseheight + 1);
};

void QOutsideWaveView::mouseMoveEvent(QMouseEvent* event)
{
    this->Super::mouseMoveEvent(event);

    IMPLEMENT_FPS_LIMITER();

    if (tool && tool->bClicking)
    {
        tool->mouseMoveEvent(event->pos().x(), event->pos().y());
        return;
    }

    const int64_t clock_pos = QGlobalView::PosToClock(event->pos().x());

    if (markers.empty())
    {
        int sd_idx = FindShaderDataAt(clock_pos);
        if (sd_idx >= 0)
        {
            QToolTip::showText(event->globalPos(), (*shaderdata_records)[sd_idx].ToolTip().c_str());
            return;
        }
    }

    int dispatch_idx = FindDispatchAt(clock_pos);
    if (dispatch_idx >= 0)
    {
        const auto& dispatch = (*dispatch_records)[dispatch_idx];
        QToolTip::showText(
            event->globalPos(), QString::fromStdString(WaveOverlay::formatDispatchTooltip(dispatch, se))
        );
        return;
    }

    int trace_event_idx = FindTraceEventAt(clock_pos);
    if (trace_event_idx >= 0)
    {
        const auto& trace_event = (*trace_events)[trace_event_idx];
        QToolTip::showText(
            event->globalPos(), QString::fromStdString(WaveOverlay::formatTraceEventTooltip(trace_event, se))
        );
        return;
    }

    int m_idx = FindMarkerAt(clock_pos, event->pos().y());
    if (m_idx >= 0)
    {
        const MarkerCoord coord{se, cu, simd, slot};
        QToolTip::showText(
            event->globalPos(), QString::fromStdString(FormatMarkerTooltip((*markers.spans)[m_idx], coord))
        );
        return;
    }

    int index = 0;
    while (index < waves.size() && waves[index].end < clock_pos) index++;

    if (index >= waves.size() || waves[index].begin > clock_pos) return;

    const auto& wave = waves[index];
    std::stringstream tooltip;
    tooltip << "SE:" << se << "  SA:" << sa << "  CU:" << cu << "  SIMD:" << simd << "  SLOT:" << slot
            << "  ID:" << index << "\nBegin: " << wave.begin << "  End: " << wave.end
            << "  Dur: " << (wave.end - wave.begin) << " cycles";
    if (wave.has_dispatcher_info)
    {
        tooltip << "\n";
        if (wave.me >= 0) tooltip << "ME: " << wave.me << "  ";
        if (wave.pipe >= 0) tooltip << "Pipe: " << wave.pipe;
    }
    if (wave.has_workgroup_id) tooltip << "\nWorkgroup ID: " << wave.workgroup_id;
    if (wave.has_register_usage) tooltip << "\nSGPRs: " << wave.sgprs << "  VGPRs: " << wave.vgprs;
    if (wave.has_occupancy_flags)
        tooltip << "\nFlags: 0x" << std::hex << std::uppercase << wave.occupancy_flags << std::dec << std::nouppercase;
    try
    {
        auto& name = QGlobalView::kernel_names.at(wave.kid);
        tooltip << "\nKernel: " << clippedTooltipText(name);
    }
    catch (...)
    {
        RCV_LOG();
        tooltip << "\nKernel ID: " << wave.kid;
    }
    QToolTip::showText(event->globalPos(), tooltip.str().c_str());
}

void QOutsideWaveView::mousePressEvent(QMouseEvent* event)
{
    this->Super::mousePressEvent(event);
    if (tool && event->button() & Qt::RightButton) tool->mousePressEvent(true, event->pos().x(), event->pos().y());
}

void QOutsideWaveView::mouseReleaseEvent(QMouseEvent* event)
{
    this->Super::mouseReleaseEvent(event);
    if (tool && event->button() & Qt::RightButton) tool->mousePressEvent(false, event->pos().x(), event->pos().y());
}

void QGlobalView::mousePressEvent(QMouseEvent* event)
{
    this->Super::mousePressEvent(event);
    if (tool && event->button() & Qt::RightButton) tool->mousePressEvent(true, event->pos().x(), event->pos().y());
}

void QGlobalView::mouseReleaseEvent(QMouseEvent* event)
{
    this->Super::mouseReleaseEvent(event);
    if (tool && event->button() & Qt::RightButton) tool->mousePressEvent(false, event->pos().x(), event->pos().y());
}

void QGlobalView::wheelEvent(QWheelEvent* event)
{
    bool ctrl = QApplication::keyboardModifiers() & Qt::ControlModifier;
    bool shift = QApplication::keyboardModifiers() & Qt::ShiftModifier;

    if (shift && !ctrl)
    {
        // Shift+Wheel: vertical track height
        int inc = event->angleDelta().y() > 0 ? 1 : -1;
        int new_scale = std::clamp(height_scale + inc, 1, 20);
        if (new_scale != height_scale && m_scrollArea && m_scrollArea->verticalScrollBar())
        {
            int old_scroll = m_scrollArea->verticalScrollBar()->value();
            int anchor_content_y = static_cast<int>(event->position().y());
            int mouse_viewport_y = anchor_content_y - old_scroll;

            // Scale the anchor position proportionally
            // Actual track height is HEIGHT()+1 due to separator, so use that ratio
            int old_track_h = height_scale + 1;
            int new_track_h = new_scale + 1;
            int new_anchor_y = anchor_content_y * new_track_h / old_track_h;
            int new_scroll = std::max(0, new_anchor_y - mouse_viewport_y);

            height_scale = new_scale;
            for (auto* view : views) view->updateGeometry();
            // Marker track height scales with HEIGHT(), so each row's contribution
            // to the label panel must be re-pushed before recalculatePositions().
            syncLabelPanelExtraHeights();
            this->updateGeometry();
            this->repaint();

            m_scrollArea->verticalScrollBar()->setValue(new_scroll);
        }
        else if (new_scale != height_scale)
        {
            height_scale = new_scale;
            for (auto* view : views) view->updateGeometry();
            syncLabelPanelExtraHeights();
            this->updateGeometry();
            this->repaint();
        }
        return;
    }

    if (ctrl)
    {
        // Ctrl+Wheel: horizontal zoom
        int mouse_x = event->position().x();
        MainWindow::incrementGlobalViewMipmap(event->angleDelta().y() > 0 ? 1 : -1, mouse_x);
        return;
    }

    this->Super::wheelEvent(event);
}

void QGlobalView::mouseMoveEvent(QMouseEvent* event)
{
    this->Super::mouseMoveEvent(event);

    if (!tool || !tool->bClicking) return;

    IMPLEMENT_FPS_LIMITER();

    tool->mouseMoveEvent(event->pos().x(), event->pos().y());

    std::stringstream ss;
    ss << "Start: " << PosToClock(tool->measure_start_x) << "\n Cycles: " << tool->measure_size_x * Delta();

    QToolTip::showText(event->globalPos(), ss.str().c_str());
}
