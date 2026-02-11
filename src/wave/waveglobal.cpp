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
#include "data/shaderdata.h"
#include "data/wavedata.h"
#include "data/wavemanager.h"
#include "mainwindow.h"
#include "util/jsonrequest.hpp"

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

void QLabelPanel::addRow(int se, int sa, int cu, int simd, int slot) { m_rows.push_back({se, sa, cu, simd, slot}); }

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

    int totalHeight = static_cast<int>(m_rows.size()) * trackheight;
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
        y += trackheight;
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

QGlobalView::QGlobalView(const std::string& filename)
{
    JsonRequest file(filename);
    QWARNING(!file.fail() && !file.bad(), "Error opening file " << filename, return );

    {
        bool bLegacy = true;
        try
        {
            if (std::string(file.data["version"]).at(0) == '3') bLegacy = false;
        }
        catch (...)
        {}
        QWARNING(!bLegacy, "Legacy version not supported!", return );
    }

    const int num_dispatch_ids = file.data["dispatches"].size();
    for (auto& [id, name] : file.data["dispatches"].items()) kernel_names[stoi(id)] = name;

    QVBox* layout = new QVBox();
    maxtime = 0;
    begintime = 0;

    // Tracking for CU groups
    int current_se = -1, current_sa = -1, current_cu = -1;
    int cu_slot_count = 0; // Count slots in current CU for label height

    for (auto& [SE, array] : file.data.items())
    {
        if (array.size() == 0) continue;
        try
        {
            [[maybe_unused]] bool b = std::to_string(std::stoi(SE)) == "SE";
        }
        catch (...)
        {
            continue;
        }

        std::map<int, std::vector<WaveTraceData>> traces{};

        for (auto& v : array)
        {
            occupancy_data data = occupancy_data::build(v);

            int traceID = data.slot + 32 * (data.simd + 4 * (data.cu + 256 * stoi(SE)));
            if (traces.find(traceID) == traces.end()) { traces[traceID] = std::vector<WaveTraceData>{}; }

            auto& trace = traces.at(traceID);

            if (data.enable == 1)
            {
                WaveTraceData wtd;
                wtd.begin = data.time;
                wtd.end = 0;
                wtd.kid = data.kernel_id;
                trace.push_back(wtd);
            }
            else if (trace.size()) { trace.back().end = data.time; }
            if (maxtime == 0) begintime = data.time;
            maxtime = std::max<int64_t>(maxtime, data.time);
        }

        for (auto& [_traceid, trace] : traces)
        {
            int traceID = _traceid;
            if (!trace.size()) continue;

            if (trace.back().end == 0) trace.back().end = maxtime;

            int slot = traceID & 0x1F;
            traceID /= 32;
            int simd = traceID & 0x3;
            traceID /= 4;
            int cu = traceID & 0x7F;
            traceID /= 128;
            int sa = traceID & 0x1;
            traceID /= 2;
            int se = traceID;

            // Check if this is a new CU group
            if (se != current_se || sa != current_sa || cu != current_cu)
            {
                current_se = se;
                current_sa = sa;
                current_cu = cu;
                cu_slot_count = 0;
            }
            cu_slot_count++;

            views.push_back(new QOutsideWaveView(se, sa, cu, simd, slot, trace, tool));

            // Just add the wave view directly - labels are handled by external labelPanel
            layout->addWidget(views.back());
        }
        layout->addStretch();
    }
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

void QGlobalView::populateLabelPanel()
{
    if (!labelPanel) return;

    for (auto* view : views) labelPanel->addRow(view->se, view->sa, view->cu, view->simd, view->slot);

    labelPanel->finalize();
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

void QOutsideWaveView::DrawShaderDataMarkers(QPainter& painter, const QRect& area)
{
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
    painter.setBrush(QColor(0, 0, 0)); // Black rectangles

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
    if (!shaderdata_records || shaderdata_records->empty()) return -1;

    const auto& recs = *shaderdata_records;

    // Match the drawn rectangle: starts at rec.time, extends markerWidth pixels right
    const int markerWidth = std::max<int64_t>(24 / QGlobalView::Delta(), 10);
    const int64_t hit_width = QGlobalView::Delta() * markerWidth;

    // Find the last record with time <= clock_pos
    auto it = std::upper_bound(
        recs.begin(), recs.end(), clock_pos, [](int64_t c, const ShaderDataRecord& r) { return c < r.time; }
    );

    // it points to first record with time > clock_pos; check the one before it
    if (it != recs.begin())
    {
        --it;
        // rec.time <= clock_pos; hit if clock_pos is within the rectangle's right edge
        if (clock_pos <= it->time + hit_width) return static_cast<int>(it - recs.begin());
    }

    return -1;
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

    // Draw 1-pixel separator line at the bottom of each slot
    QPen pen = painter.pen();
    pen.setWidth(1);
    pen.setColor(QColor(60, 60, 60));
    painter.setPen(pen);
    painter.drawLine(0, height() - 1, sizeHint().width(), height() - 1);

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
    QRectF rect(pos, height_multiplier * qreal(waveheight), width, waveheight);
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
    int baseheight = QGlobalView::HEIGHT() * (1 + height_multiplier);
    return QSize(waves.size() ? QGlobalView::ClockToPos(waves.back().end) : 256, baseheight + 1);
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

    // Check shaderdata records first (drawn on top, so higher priority)
    int sd_idx = FindShaderDataAt(clock_pos);
    if (sd_idx >= 0)
    {
        const auto& rec = (*shaderdata_records)[sd_idx];
        std::stringstream tooltip;
        tooltip << "Shaderdata Record"
                << "\nSE:" << rec.se << "  CU:" << rec.cu << "  SIMD:" << rec.simd << "  WaveID:" << rec.wave_id
                << "\nTime: " << rec.time << "  Value: 0x" << std::hex << rec.value << "  Flags: 0x" << rec.flags
                << std::dec;
        QToolTip::showText(event->globalPos(), tooltip.str().c_str());
        return;
    }

    int index = 0;
    while (index < waves.size() && waves[index].end < clock_pos) index++;

    if (index >= waves.size()) return;

    const auto& wave = waves[index];
    if (wave.begin > clock_pos) return;

    std::stringstream tooltip;
    tooltip << "SE:" << se << "  SA:" << sa << "  CU:" << cu << "  SIMD:" << simd << "  SLOT:" << slot
            << "  ID:" << index << "\nBegin: " << wave.begin << "  End: " << wave.end
            << "  Dur: " << (wave.end - wave.begin) << " cycles";
    try
    {
        auto& name = QGlobalView::kernel_names.at(wave.kid);
        tooltip << "\nKernel: " << name;
    }
    catch (...)
    {
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
            if (labelPanel) labelPanel->recalculatePositions();
            this->updateGeometry();
            this->repaint();

            m_scrollArea->verticalScrollBar()->setValue(new_scroll);
        }
        else if (new_scale != height_scale)
        {
            height_scale = new_scale;
            for (auto* view : views) view->updateGeometry();
            if (labelPanel) labelPanel->recalculatePositions();
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
