// MIT License
//
// Copyright (c) 2024-2025 Advanced Micro Devices, Inc. All rights reserved.
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
#include <QPainter>
#include <QPainterPath>
#include <chrono>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include "code/qcodelist.h"
#include "data/wavemanager.h"
#include "mainwindow.h"

#define ARROW_SPACING 5

std::vector<ArrowCanvas::arrow_t> ArrowCanvas::arrows;

struct PairHash
{
    size_t operator()(const std::pair<int, int>& x) const { return (size_t(x.first) << 32) | size_t(x.second); }
};

std::vector<QColor> ArrowCanvas::arrow_colors = {
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

QSize ArrowCanvas::sizeHint() const { return QSize(std::max(1, ARROW_SPACING * (2 + max_slot_alloc)), 1); };

void ArrowCanvas::paintEvent(QPaintEvent* event)
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

bool ArrowCanvas::checkConnectionsCache(const std::vector<WaitList>& waitcnt)
{
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

void ArrowCanvas::buildConnections(const std::vector<WaitList>& waitcnt)
{
    // First check if we already have the answer
    if (checkConnectionsCache(waitcnt)) return;

    arrows.clear();
    // list of connections jumping over a waitcnt
    std::unordered_set<std::pair<int, int>, PairHash> crossed_connections;
    // list of all connections per waitcnt, key = waitcnt linenumber
    std::map<int, std::set<int>> connections_map;
    // maximum slot used per waitcnt linenumber. Slot defines how far from the edge an arrow is
    // std::map<int, int> slots_map;
    std::map<int, std::unordered_set<int>> slots_map;

    // Make list of all connections
    for (const WaitList& w : waitcnt)
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

    max_slot_alloc = 0;

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
                arrows.push_back({wait_number, mem_line, prev_slot_n, true});
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
                arrows.push_back({wait_number, *mem_iter, next_slot_n, true});
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
        arrows.push_back({conn.first, conn.second, slot, false});

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

    updateGeometry();
    update();
};

bool ArrowCanvas::Connect(QPainter& painter, int l1, int l2, int xslot, QColor& color)
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
