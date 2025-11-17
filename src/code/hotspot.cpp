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

#include "sourcefile.h"
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QScrollBar>
#include <fstream>
#include <string>
#include "../config/config.hpp"
#include "asmcode.h"
#include "mainwindow.h"
#include "qcodelist.h"
#include "util/custom_layouts.h"

bool HorizontalHotspot::is_pcs_enabled = false;
int  HorizontalHotspot::HISTOGRAM_WIDTH = 100;

void HorizontalHotspot::add_latency(int type, Latency _sqtt, Latency _pcs)
{
    QWARNING(type >= 0 && type < typed_latency.size(), "Invalid latency type" << type, type = 0);

    sqtt += _sqtt;
    pcs += _pcs;
    typed_latency[type] += _sqtt.latency;
}

void HorizontalHotspot::paint(
    QPainter& painter, const int posx, const int posy, const int sizey, const float sqtt_maxvalue, const float pcs_maxvalue, DrawFormat format, bool rightToLeft
) const
{
    const int padding = 2;
    const int reducedHeight = sizey - 2 * padding;
    const float dir = rightToLeft ? -1 : 1;

    QPen savedPen = painter.pen();
    QBrush savedBrush = painter.brush();

    painter.setPen(Qt::NoPen);

    int xstart = posx;
    float pos = posx;
    int totalWidth = 0;

    const float NORM = HISTOGRAM_WIDTH / (is_pcs_enabled ? pcs_maxvalue : sqtt_maxvalue);

    auto Draw = [&](float width, const QColor& color, int _posy, int _height)
    {
        pos += dir * width;
        if (std::abs(xstart - pos) >= 1.0f)
        {
            int start = std::min(xstart, static_cast<int>(pos));
            int end = std::max(xstart, static_cast<int>(pos));
            painter.setBrush(QBrush(color));

            painter.drawRect(start, _posy, end - start, _height);

            xstart = pos;
            totalWidth = std::max(totalWidth, std::abs(static_cast<int>(pos) - posx));
        }
    };

    auto drawStall = [&](const Latency& latency, int _posy, int _height)
    {
        float stallwidth = latency.stalled * NORM;
        float issuewidth = latency.latency * NORM - stallwidth;

        Draw(stallwidth, Config::StallColor(), _posy, _height);
        Draw(issuewidth, Config::IssueColor(), _posy, _height);
    };

    auto drawType = [&] (const std::array<int64_t, 16>& array, int _posy, int _height)
    {
        float barWidth = 0;
        for (int c = 0; c < array.size(); c++)
        {
            barWidth += array[c] * NORM;
            if (barWidth < 0.1f) continue;
            Draw(barWidth, Config::TokenColors().at(c).qcolor, _posy, _height);
            barWidth = 0;
        }
    };

    {
        int _height = reducedHeight;
        int _posy = posy + padding;
        if (format == DrawFormat::DRAWBOTH) _height /= 2;

        if (format & DrawFormat::DRAWSTALL) drawStall(is_pcs_enabled ? pcs : sqtt, _posy, _height);

        _posy += reducedHeight - _height;
        xstart = pos = posx;

        if (format & DrawFormat::DRAWTYPE) drawType(is_pcs_enabled ? stall_reason : typed_latency, _posy, _height);
    }

    // Draw single border around entire hotspot
    if (totalWidth > 0)
    {
        QPen pen;
        pen.setColor(WindowColors::HotspotOutline());
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);

        int x = rightToLeft ? posx - totalWidth : posx;
        painter.drawRect(x, posy + padding, totalWidth, reducedHeight);
    }

    painter.setPen(savedPen);
    painter.setBrush(savedBrush);
}
