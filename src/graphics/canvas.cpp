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

#include "canvas.h"
#include <QBrush>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QThread>
#include <QToolTip>
#include <chrono>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include "code/qcodelist.h"
#include "config/config.hpp"
#include "data/wavemanager.h"
#include "mainwindow.h"

#define ARROW_SPACING 5

Canvas::DrawType Canvas::drawtype = Canvas::DrawType::DrawArrows;
double Canvas::max_memory_latency = 0.0;

struct PairHash
{
    size_t operator()(const std::pair<int, int>& x) const { return (size_t(x.first) << 32) | size_t(x.second); }
};

std::vector<QColor> Canvas::arrow_colors = {
    {  0, 200,   0},
    {  0,   0, 255},
    {255,   0,   0},
    { 16,  16,  16},
    {128, 128, 128},

    {255, 160,   0},
    {  0, 160, 255},
    {160,   0, 255},
    {  0, 220, 128},
    {128, 220,   0},

    {255,  16, 255},
    {255, 220,  16},
    { 16, 220, 255},
};

void Canvas::paintArrows()
{
    QPainter painter(this);
    MainWindow::getScaling(painter);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);

    int colorstate = 0;
    std::unordered_map<int, int> waitcnt_colors_map;

    // Every waitcnt is one color
    for (auto& arrow : arrows)
    {
        auto it = waitcnt_colors_map.find(arrow.wait_number);
        if (it == waitcnt_colors_map.end()) it = waitcnt_colors_map.insert({arrow.wait_number, colorstate++}).first;

        QColor& color = arrow_colors.at(it->second % arrow_colors.size()); // pick color & increment
        Connect(painter, arrow.wait_number, arrow.mem_line, arrow.prev_slot_n, color);
    }
}

bool Canvas::checkConnectionsCache(const std::vector<WaitList>& waitcnt)
{
    auto _lk = std::unique_lock{mut};

    std::unordered_map<std::pair<int, int>, bool, PairHash> arrow_cache_check;
    for (auto& arrow : arrows) arrow_cache_check[{arrow.wait_number, arrow.mem_line}] = false;

    try
    {
        for (const WaitList& w : waitcnt)
        {
            for (auto& [source, _] : w.sources) { arrow_cache_check.at({w.code_line, source}) = true; }
        }
    }
    catch (...)
    {
        return false;
    }

    for (auto& [k, v] : arrow_cache_check)
    {
        if (v == false) return false;
    }
    return true;
}

std::pair<int, std::vector<Canvas::arrow_t>> buildConnections(const std::vector<Canvas::WaitList>& waitcnt)
{
    auto output = std::vector<Canvas::arrow_t>();
    // list of connections jumping over a waitcnt
    std::unordered_set<std::pair<int, int>, PairHash> crossed_connections;
    // list of all connections per waitcnt, key = waitcnt linenumber
    std::map<int, std::set<int>> connections_map;
    // maximum slot used per waitcnt linenumber. Slot defines how far from the edge an arrow is
    // std::map<int, int> slots_map;
    std::map<int, std::unordered_set<int>> slots_map;

    // Make list of all connections
    for (const auto& w : waitcnt)
    {
        if (connections_map.find(w.code_line) == connections_map.end())
        {
            connections_map[w.code_line] = std::set<int>({w.code_line});
            slots_map[w.code_line] = std::unordered_set<int>();
        }

        std::set<int>& map = connections_map[w.code_line];

        for (auto& [source, _] : w.sources)
        {
            map.insert(source);
            slots_map[source] = std::unordered_set<int>();
        }
    }

    int max_slot_alloc = 0;

    // Draw connections on which there is no waitcnt in between src (memory op) and dst (waitcnt)
    int prev_wait = -1;
    for (auto iter = connections_map.begin(); iter != connections_map.end(); ++iter)
    {
        int next_wait = std::next(iter) == connections_map.end() ? INT_MAX : (*std::next(iter)).first;

        const int wait_number = (*iter).first;
        std::set<int>& conn_set = (*iter).second;

        int prev_slot_n = 0;
        for (auto mem_iter = conn_set.find(wait_number); mem_iter != conn_set.begin(); --mem_iter)
        {
            int mem_line = *std::prev(mem_iter);

            if (mem_line <= prev_wait) { crossed_connections.insert({wait_number, mem_line}); }
            else
            {
                output.push_back({wait_number, mem_line, prev_slot_n, true});
                prev_slot_n++;
                max_slot_alloc = std::max(max_slot_alloc, prev_slot_n);
                for (int ml = 1; ml <= prev_slot_n; ml++) slots_map[mem_line].insert(ml);
            }
        }
        int next_slot_n = 0;
        for (auto mem_iter = std::next(conn_set.find(wait_number)); mem_iter != conn_set.end(); ++mem_iter)
        {
            if (*mem_iter >= next_wait) { crossed_connections.insert({wait_number, *mem_iter}); }
            else
            {
                output.push_back({wait_number, *mem_iter, next_slot_n, true});
                next_slot_n++;
                for (int ml = 1; ml <= next_slot_n; ml++) slots_map[*mem_iter].insert(ml);
            }
        }

        for (int ml = 1; ml <= std::max(prev_slot_n, next_slot_n); ml++) slots_map[wait_number].insert(ml);
        prev_wait = wait_number;
    }

    // Sort leftover connections by source-destination distance
    std::vector<std::pair<int, int>> crossed_vector(crossed_connections.begin(), crossed_connections.end());
    sort(
        crossed_vector.begin(),
        crossed_vector.end(),
        [](const std::pair<int, int>& a, const std::pair<int, int>& b)
        { return std::abs(a.first - a.second) < std::abs(b.first - b.second); }
    );

    // Draw sorted leftover connections in sequence and increment slot counters on the way
    for (auto& conn : crossed_vector)
    { // .first = waitcnt linenumber, .second = mem_op linenumber
        int src = std::min(conn.first, conn.second);
        int dst = std::max(conn.first, conn.second);

        auto map_iterator = slots_map.find(conn.first);

        std::unordered_set<int> used_slots;
        if (conn.first < conn.second)
        {
            for (auto it = map_iterator; it != slots_map.end() && (*it).first <= conn.second; ++it)
                for (int elem : (*it).second) used_slots.insert(elem);
        }
        else
        {
            auto rev_map = std::make_reverse_iterator(map_iterator);
            for (auto it = std::prev(rev_map); it != slots_map.rend() && (*it).first >= conn.second; ++it)
                for (int elem : (*it).second) used_slots.insert(elem);
        }

        int slot = 1;
        while (used_slots.find(slot) != used_slots.end()) slot++;

        max_slot_alloc = std::max(slot, max_slot_alloc);
        output.push_back({conn.first, conn.second, slot, false});

        if (conn.first < conn.second)
        {
            for (auto it = map_iterator; it != slots_map.end() && (*it).first <= conn.second; ++it)
                (*it).second.insert(slot); // update all slots in path above
        }
        else
        {
            (*map_iterator).second.insert(slot);
            auto rev_map = std::make_reverse_iterator(map_iterator);
            for (auto it = rev_map; it != slots_map.rend() && (*it).first >= conn.second; ++it)
                (*it).second.insert(slot); // update all slots in path below
        }
    }

    return {max_slot_alloc, std::move(output)};
};

QSize Canvas::sizeHint() const
{
    int _size = HorizontalHotspot::GetHistogramWidth() * 2 + 1;
    if (drawtype == DrawType::DrawArrows)
        _size = std::max(_size, ARROW_SPACING * (2 + max_wait_alloc));
    else if (drawtype == DrawType::DrawBranch)
        _size = std::max(_size, ARROW_SPACING * (2 + max_branch_alloc));

    return QSize(_size, 1);
};

void Canvas::paintEvent(QPaintEvent* event)
{
    if (drawtype == DrawType::DrawArrows)
        paintArrows();
    else if (drawtype == DrawType::DrawStall || drawtype == DrawType::DrawReasons || drawtype == DrawType::DrawStallAndReason)
        paintStalls();
    else if (drawtype == DrawType::MemoryLatency)
        paintMemoryLatency();
    else
        paintBranch();

    QWidget::paintEvent(event);
}

void Canvas::buildWaitConnections(const std::vector<WaitList>& waitcnt)
{
    // First check if we already have the answer
    if (checkConnectionsCache(waitcnt)) return;

    auto result = buildConnections(waitcnt);

    auto applyResult = [this, result = std::move(result)]() mutable
    {
        auto _lk = std::unique_lock{mut};
        max_wait_alloc = result.first;
        arrows = std::move(result.second);
        updateGeometry();
        update();
    };

    if (QThread::currentThread() == thread())
        applyResult();
    else
        QMetaObject::invokeMethod(this, std::move(applyResult), Qt::QueuedConnection);
};

void Canvas::buildBranchConnections(const std::vector<WaitList>& waitcnt)
{
    auto result = buildConnections(waitcnt);

    auto applyResult = [this, result = std::move(result)]() mutable
    {
        auto _lk = std::unique_lock{mut};
        max_branch_alloc = result.first;
        branches = std::move(result.second);
        updateGeometry();
        update();
    };

    if (QThread::currentThread() == thread())
        applyResult();
    else
        QMetaObject::invokeMethod(this, std::move(applyResult), Qt::QueuedConnection);
};

bool Canvas::Connect(QPainter& painter, int l1, int l2, int xslot, QColor& color)
{
    const double width = this->width() - 8;
    const double lineheight = QCodelist::lineheight();
    const int heightw = this->height();

    double posy1 = -1;
    double posy2 = -1;

    try
    {
        posy1 = lineheight * (1 + ASMCodeline::line_map.at(l1)->line_index);
        posy2 = lineheight * (1 + ASMCodeline::line_map.at(l2)->line_index);
    }
    catch (...)
    {
        return true;
    }

    if (posy1 <= 0 || posy2 <= 0) return true;

    // --- Begin arrow out of bounds wrapping
    int horz_length = ARROW_SPACING * (1 + xslot);
    if (horz_length >= width - 5) horz_length = width / 3 + horz_length % int(2 * width / 3);
    // --- End

    double xpos = width - horz_length;

    // ymin and ymax are the center y-positions of line1 and line2 widgets, sorted by min/max
    double ymin = std::min(posy1, posy2) - lineheight / 3;
    double ymax = std::max(posy1, posy2) + 1 - 2 * lineheight / 3;

    ymin -= scrollposy;
    ymax -= scrollposy;

    if (ymin < 0 && ymax < 0) return true;
    if (ymin > heightw && ymax > heightw) return true;

    const double invscale = 1.0 / MainWindow::getScaling();
    xpos *= invscale;
    ymin *= invscale;
    ymax *= invscale;

    painter.fillRect(QRectF(xpos, ymin, 3, ymax - ymin), color); // vertical line from line1 to line2

    const double line_xpos = xpos;
    xpos += invscale * horz_length;

    QPainterPath path;

    if (ymin >= -10 && ymin < heightw * std::max(invscale, 1.0) + 10)
    {
        painter.fillRect(QRectF(line_xpos, ymin - 1, invscale * horz_length, 3), color); // horizontal line at ymin
        path.moveTo(xpos + 8, ymin + 0.5);                                               // Draw triangle at ymin
        path.lineTo(xpos, ymin + 5);
        path.lineTo(xpos, ymin - 4);
        path.lineTo(xpos + 8, ymin + 0.5);
    }
    if (ymax >= -10 && ymax < heightw * std::max(invscale, 1.0) + 10)
    {
        painter.fillRect(QRectF(line_xpos, ymax - 1, invscale * horz_length, 3), color); // horizontal line at ymax
        path.moveTo(xpos + 8, ymax + 0.5);                                               // Draw triangle at ymax
        path.lineTo(xpos, ymax + 5);
        path.lineTo(xpos, ymax - 4);
        path.lineTo(xpos + 8, ymax + 0.5);
    }

    painter.fillPath(path, QBrush(color));

    return false;
};

void Canvas::paintStalls()
{
    QWARNING(MainWindow::window && MainWindow::window->code_contents, "no contents", return );
    auto* contents = MainWindow::window->code_contents;

    QPainter painter(this);
    MainWindow::getScaling(painter);
    QPen pen;
    pen.setColor(WindowColors::HotspotOutline());
    painter.setPen(Qt::NoPen); // Set to NoPen initially since HorizontalHotspot will manage pen state
    painter.setBrush(Qt::NoBrush);

    const int lineheight = QCodelist::lineheight();

    // Binary search for first visible line (ypos >= 0)
    int target_index = std::max(0, (scrollposy - padding) / lineheight);
    auto start_it = std::lower_bound(
        ASMCodeline::line_vec.begin(),
        ASMCodeline::line_vec.end(),
        target_index,
        [](const auto& line, int idx) { return line && line->line_index < idx; }
    );

    auto drawFormat = drawtype == DrawType::DrawStallAndReason ? HorizontalHotspot::DrawFormat::DRAWBOTH
                                                               : HorizontalHotspot::DrawFormat::DRAWSTALL;
    if (drawtype == DrawType::DrawReasons) drawFormat = HorizontalHotspot::DrawFormat::DRAWTYPE;

    const double invscale = 1.0 / MainWindow::getScaling();

    for (auto it = start_it; it != ASMCodeline::line_vec.end(); ++it)
    {
        auto line = *it;
        if (!line) continue;

        auto ypos = indexToYpos(line->line_index, lineheight);
        if (ypos > this->height()) break;

        bool highlighted = (hovered_line_index == line->line_index);
        line->hotspot.paint(
            painter,
            0,
            ypos * invscale,
            width() * invscale,
            (lineheight - 2 * padding) * invscale,
            contents->max_sqtt_latency,
            contents->max_pcs_latency,
            drawFormat,
            false,
            highlighted
        );
    }
}

// Helper method for handling hotspot hover interactions
void Canvas::setHoveredLine(int line_index)
{
    if (hovered_line_index != line_index)
    {
        hovered_line_index = line_index;
        update();
    }
}

void Canvas::handleHotspotHover(QMouseEvent* event)
{
    QWARNING(MainWindow::window && MainWindow::window->code_contents, "no contents", return );
    auto* contents = MainWindow::window->code_contents;

    const int lineheight = QCodelist::lineheight();
    const int mouse_y = event->pos().y();

    // Binary search for first visible line
    int target_index = std::max(0, (scrollposy - padding) / lineheight);
    auto start_it = std::lower_bound(
        ASMCodeline::line_vec.begin(),
        ASMCodeline::line_vec.end(),
        target_index,
        [](const auto& line, int idx) { return line && line->line_index < idx; }
    );

    for (auto it = start_it; it != ASMCodeline::line_vec.end(); ++it)
    {
        auto line = *it;
        if (!line) continue;

        auto ypos = indexToYpos(line->line_index, lineheight);
        if (ypos > this->height()) break;

        // Check if mouse is vertically within this line's bounds
        if (mouse_y < ypos || mouse_y > ypos + lineheight - 2 * padding) continue;

        std::string tooltip;
        if (drawtype == DrawType::DrawStall)
            tooltip = line->hotspot.getTooltip();
        else if (drawtype == DrawType::DrawReasons)
            tooltip = line->hotspot.getStallTip();
        else if (drawtype == DrawType::DrawStallAndReason)
            tooltip = line->hotspot.getTooltip() + line->hotspot.getStallTip();
        else if (drawtype == DrawType::MemoryLatency && line->memory_latency.count > 0)
        {
            std::ostringstream ss;
            ss << "<b>Average memory Latency:</b><br>"
               << "<table>"
               << "<tr><td>Exec to data:</td><td>" << std::fixed << std::setprecision(1) << line->memory_latency.mean
               << " cycles</td></tr>"
               << "<tr><td>Issue to Exec:</td><td>" << line->memory_latency.meanIssue << " cycles</td></tr>"
               << "<tr><td>Stall to Issue:</td><td>" << line->memory_latency.meanStall << " cycles</td></tr>"
               << "<tr><td>StdDev:</td><td>" << line->memory_latency.stdDev << "</td></tr>"
               << "<tr><td>Error:</td><td>" << line->memory_latency.error << "</td></tr>"
               << "</table>";
            tooltip = ss.str();
        }

        if (!tooltip.empty())
        {
            setHoveredLine(line->line_index);
            QToolTip::showText(event->globalPos(), QString::fromStdString(tooltip), this);
            return;
        }
    }

    setHoveredLine(-1);
}

void Canvas::mouseMoveEvent(QMouseEvent* event)
{
    if (drawtype == DrawType::DrawStall || drawtype == DrawType::DrawReasons ||
        drawtype == DrawType::DrawStallAndReason || drawtype == DrawType::MemoryLatency)
        handleHotspotHover(event);
    QWidget::mouseMoveEvent(event);
}

void Canvas::leaveEvent(QEvent* event)
{
    setHoveredLine(-1);
    QWidget::leaveEvent(event);
}

void Canvas::paintBranch()
{
    QPainter painter(this);
    MainWindow::getScaling(painter);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);

    int colorstate = 0;
    std::unordered_map<int, int> waitcnt_colors_map;

    // Every waitcnt is one color
    for (auto& arrow : branches)
    {
        auto it = waitcnt_colors_map.find(arrow.wait_number);
        if (it == waitcnt_colors_map.end()) it = waitcnt_colors_map.insert({arrow.wait_number, colorstate++}).first;

        QColor& color = arrow_colors.at(it->second % arrow_colors.size()); // pick color & increment
        Connect(painter, arrow.wait_number, arrow.mem_line, arrow.prev_slot_n, color);
    }
}

void Canvas::paintMemoryLatency()
{
    if (max_memory_latency <= 0.0) return; // No data - run Memory Latency analysis first

    QPainter painter(this);
    MainWindow::getScaling(painter);

    const double invscale = 1.0 / MainWindow::getScaling();
    const int lineheight = QCodelist::lineheight();
    const int barHeight = (lineheight - 2 * padding) * invscale;
    const int maxBarWidth = (width() - 3) * invscale;

    // Binary search for first visible line
    int target_index = std::max(0, (scrollposy - padding) / lineheight);
    auto start_it = std::lower_bound(
        ASMCodeline::line_vec.begin(),
        ASMCodeline::line_vec.end(),
        target_index,
        [](const auto& line, int idx) { return line && line->line_index < idx; }
    );

    for (auto it = start_it; it != ASMCodeline::line_vec.end(); ++it)
    {
        auto line = *it;
        if (!line) continue;

        auto ypos = indexToYpos(line->line_index, lineheight) * invscale;
        if (ypos > this->height() * invscale) break;

        if (line->memory_latency.count <= 0) continue;

        const auto& stats = line->memory_latency;
        double meanRatio = stats.mean / max_memory_latency;
        double stdRatio = stats.stdDev / max_memory_latency;

        int meanWidth = static_cast<int>(meanRatio * maxBarWidth);
        int stdWidth = static_cast<int>(stdRatio * maxBarWidth);

        bool highlighted = (hovered_line_index == line->line_index);

        // Calculate issue and stall widths based on their mean values
        double issueRatio = stats.meanIssue / max_memory_latency;
        double stallRatio = stats.meanStall / max_memory_latency;
        int issueWidth = static_cast<int>(issueRatio * maxBarWidth);
        int stallWidth = static_cast<int>(stallRatio * maxBarWidth);

        // Use token colors: VMEM = light orange (#ffc432), LDS = dark orange (#ff7000)
        QColor barColor;
        if (line->memory_latency_type == LatencyAnalysis::CounterType::LDS)
            barColor = QColor(0xff, 0x70, 0x00); // Dark orange for LDS
        else
            barColor = QColor(0xff, 0xc4, 0x32); // Light orange for VMEM

        // Get colors from config
        QColor issueColor = Config::IssueColor();
        QColor stallColor = Config::StallColor();

        if (highlighted)
        {
            barColor = barColor.lighter(130);
            issueColor = issueColor.lighter(130);
            stallColor = stallColor.lighter(130);
        }

        // Draw bars in order: Issue (green), Latency (orange), Stall (red)
        painter.setPen(Qt::NoPen);

        // Draw issue (green) rectangle first
        painter.setBrush(issueColor);
        painter.drawRect(2 * invscale, ypos, issueWidth, barHeight);

        // Draw mean latency bar (orange, based on counter type) after issue
        painter.setBrush(barColor);
        painter.drawRect(2 * invscale + issueWidth, ypos, meanWidth, barHeight);

        // Draw stall (red) rectangle after latency
        painter.setBrush(stallColor);
        painter.drawRect(2 * invscale + issueWidth + meanWidth, ypos, stallWidth, barHeight);

        // Total bar width for centering stddev and highlight
        int totalBarWidth = issueWidth + meanWidth + stallWidth;

        // Draw stdDev as horizontal "H" error bar centered at end of all bars
        if (stdWidth > 0)
        {
            int stdCenter = 2 * invscale + totalBarWidth;
            int stdStart = std::clamp(stdCenter - stdWidth, static_cast<int>(2 * invscale), maxBarWidth);
            int stdEnd = std::clamp(stdCenter + stdWidth, static_cast<int>(2 * invscale), maxBarWidth);
            int centerY = ypos + (barHeight + 1 * invscale) / 2; // Round up to center properly
            int capHeight = barHeight / 3;

            QPen stdPen(Qt::black, 2 * invscale);
            painter.setPen(stdPen);
            painter.setBrush(Qt::NoBrush);

            // Horizontal line
            painter.drawLine(stdStart, centerY, stdEnd, centerY);
            // Left vertical cap (only if visible)
            if (stdStart > 2 * invscale) painter.drawLine(stdStart, centerY - capHeight, stdStart, centerY + capHeight);
            // Right vertical cap (only if visible)
            if (stdEnd < (width() - 2) * invscale) painter.drawLine(stdEnd, centerY - capHeight, stdEnd, centerY + capHeight);
        }

        // Draw highlight outline encompassing all bars
        if (highlighted)
        {
            painter.setPen(QPen(Qt::white, 1 * invscale));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(2 * invscale, ypos, totalBarWidth, barHeight);
        }
    }
}
