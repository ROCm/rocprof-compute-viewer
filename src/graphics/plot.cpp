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

#include "plot.h"
#include <QWheelEvent>
#include <cmath>
#include <iostream>
#include "config/config.hpp"
#include "mainwindow.h"
#include "util/custom_layouts.h"
#include "wave/scroll.h"

const int ticksize = 5;
const int xticks = 7;
const int yticks = 6;
const int max_legend_chars = 20;

const int MAX_LODS = 20;
const int LOD_THRESHOLD = 16;

// Reduce graphical workload
#ifdef RCV_DISABLE_OPENGL
const float MIN_PIXELS_PER_DOT = 3.0f;
#else
const float MIN_PIXELS_PER_DOT = 1.5f;
#endif

size_t binarySearch(const std::vector<PlotPoint>& vec, float target)
{
    if (vec.back().time < target) return vec.size();

    auto it =
        std::lower_bound(vec.begin(), vec.end(), target, [](const PlotPoint& a, double tg) { return a.time < tg; });

    if (it == vec.begin() || it == vec.end()) return 0;
    return std::distance(vec.begin(), it) - 1;
}

float LODCurve::search(float time) const
{
    if (!data.size()) return 0;

    size_t index = binarySearch(data, time);
    if (index >= data.size()) return 0;

    if (index == 0 && data.at(index).time > time) return 0;

    return data.at(index).value;
}

void PlotCurve::SetData(std::vector<WeightedPoint>&& data)
{
    int n_lods = 0;

    if (data.size() < 2) return;

    for (size_t i = 0; i < data.size() - 1; i++) data.at(i).weight = data.at(i + 1).time - data.at(i).time;
    data.back().weight = data.at(data.size() - 2).weight;

    while (n_lods < MAX_LODS && CreateLODs(n_lods, data)) n_lods++;
}

bool PlotCurve::CreateLODs(int mip, std::vector<WeightedPoint>& points)
{
    if (mip == 0)
    {
        lods.emplace_back(LODCurve{}).set(points);
        return true;
    }

    size_t og_size = points.size();
    if (og_size < LOD_THRESHOLD) return false;

    float min_interval = std::numeric_limits<float>::max();

    float cur_time = points.at(0).time;
    for (auto& point : points)
        if (point.time - cur_time >= 1.0f)
        {
            min_interval = std::min(min_interval, point.time - cur_time);
            cur_time = point.time;
        }

    while (points.size() > og_size / 2 + 2)
    {
        min_interval *= 1.42f; // above to sqrt2

        std::vector<WeightedPoint> newdata;
        newdata.reserve(points.size());
        newdata.emplace_back(points.at(0));

        for (size_t i = 1; i < points.size() - 1; i++)
        {
            auto& dplus = points.at(i + 1);
            auto& d0 = points.at(i);

            if (dplus.time - d0.time < min_interval)
            {
                float weight = dplus.weight + d0.weight;
                if (weight < 0.001f) continue;

                float newtime = (dplus.time * dplus.weight + d0.time * d0.weight) / weight;

                newdata.emplace_back(WeightedPoint{
                    newtime, (dplus.value * dplus.weight + d0.value * d0.weight) / weight, weight});
                i += 1;
            }
            else
                newdata.emplace_back(d0);
        }

        newdata.emplace_back(points.back());
        points = std::move(newdata);
    }

    if (points.size() < LOD_THRESHOLD) return false;

    auto& lod = lods.emplace_back(LODCurve{});
    lod.min_interval = min_interval;
    lod.set(points);

    return true;
}

void PlotCurve::UpdateLOD(float range, int width, bool bAuto)
{
    if (!bAuto || lods.size() < 2)
    {
        lod = 0;
        return;
    }

    const float pixel_per_cycle = width / range;
    const float point_density_ratio = MIN_PIXELS_PER_DOT / pixel_per_cycle;
    lod = 1;

    while (lod < lods.size() && lods.at(lod).min_interval < point_density_ratio) lod++;

    lod--;
}

PlotGraph::PlotGraph(int _penwidth, QWidget* parent) : BasePlotWidget(parent), penwidth(_penwidth)
{
    setMouseTracking(true);
    setAutoFillBackground(true);
    QPalette pal = QPalette();
    pal.setColor(QPalette::Window, WindowColors::GraphBkg());
    setPalette(pal);
}

void PlotGraph::wheelEvent(QWheelEvent* ev)
{
    double scale = pow(1.001, ev->angleDelta().y());

    if (ev->modifiers() & Qt::ControlModifier) { yscale *= scale; }
    else
    {
        double midpoint = pixelToPos(mousepos.x());
        xscale *= scale;
        xoffset += pixelToPos(mousepos.x()) - midpoint;
    }

    update();
}

void PlotGraph::mousePressEvent(QMouseEvent* ev)
{
    mousepos = ev->pos();

    if (ev->button() == Qt::LeftButton)
    {
        if (ev->modifiers() & Qt::ControlModifier)
        {
            xscale = 1;
            yscale = 1;
            xoffset = 1;
        }
        else
        {
            bLClick = true;
            lclickpos = ev->pos();
        }
    }
    else if (ev->button() == Qt::RightButton)
        setCursor(Qt::ClosedHandCursor);

    update();
}

void PlotGraph::mouseReleaseEvent(QMouseEvent* ev)
{
    if (bLClick)
    {
        bLClick = false;
        int minpos = std::max<int>(std::min<int>(lclickpos.x(), ev->pos().x()), left_space);
        int maxpos = std::min<int>(std::max<int>(lclickpos.x(), ev->pos().x()), width() - margin);

        if (maxpos > minpos)
        {
            xoffset = -pixelToPos(minpos);
            xscale *= smallwidth() / double(maxpos - minpos);
        }
    }
    setCursor(Qt::ArrowCursor);
    update();
}

void PlotGraph::paintEvent(QPaintEvent* ev)
{
    QWidget::paintEvent(ev);
    const QColor bkgcolor = WindowColors::GraphBkg();

    QPainter painter(this);
    painter.setRenderHint(QPainter::TextAntialiasing);
#ifndef RCV_DISABLE_OPENGL
    painter.setRenderHint(QPainter::Antialiasing);
#endif

    painter.fillRect(QRect(left_space, 0, width() - left_space, height()), bkgcolor);

    if (auto view = MainWindow::getCUScroll())
    {
        Color viewcolor = bkgcolor;
        viewcolor += Color(24, 32, 40);

        int pos_start = posToPixel(QCustomScroll::clock_cutoff_start + view->start);
        int pos_end = posToPixel(QCustomScroll::clock_cutoff_start + view->start + view->range);

        if (pos_start < width() && pos_end > 0)
            painter.fillRect(QRect(pos_start, 0, pos_end - pos_start, height()), viewcolor);
    }

    QFont font = MainWindow::default_font.isEmpty() ? painter.font() : QFont(MainWindow::default_font);
    font.setPointSize(MainWindow::font() - 1);
    painter.setFont(font);

    QPen pen = painter.pen();
    pen.setWidth(penwidth);

    int start_y = height() - bottom_space;
    double cvty = (start_y - top_space) * yscale / ymax;

    for (auto& series : curves)
    {
        if (!series.lods.size()) continue;

        series.UpdateLOD((xmax - xmin) / xscale, width(), bAutoLod);
        auto& data = series.get().data;
        if (data.size() < 2) continue;

        float min_interval = series.get().min_interval;

        pen.setColor(series.color);
        painter.setPen(pen);

        QVector<QPoint> points;

        size_t i = binarySearch(data, -xoffset);
        float last_time = data.at(0).time;

        while (i < data.size())
        {
            auto& point = data.at(i);
            if (point.time - last_time > 2.01f * min_interval && !points.empty())
                points.push_back(QPoint(posToPixel(point.time - 0.1f), points.back().y()));

            last_time = point.time;
            points.push_back(QPoint(posToPixel(point.time), start_y - (point.value + yoffset) * cvty));
            i++;
            if (posToPixel(point.time) > width()) break;
        }

        if (i == data.size())
            points.push_back(QPoint(posToPixel(data.back().time) + 1, start_y - (data.back().value + yoffset) * cvty));

        painter.drawPolyline(points.data(), points.size());
    }

    pen.setColor(WindowColors::textColor());
    painter.setPen(pen);

    painter.fillRect(QRect(0, 0, left_space, height()), bkgcolor);
    painter.fillRect(QRect(0, 0, width(), top_space), bkgcolor);
    painter.fillRect(QRect(width() - margin, 0, margin, height()), bkgcolor);
    NOT_WINDOWS(
        painter.fillRect(QRect(left_space, height() - bottom_space, width() - left_space, bottom_space), bkgcolor)
    );

    QPen whitepen = painter.pen();
    whitepen.setColor(WindowColors::reverseTextColor());
    whitepen.setWidth(0);
    painter.setPen(whitepen);

    painter.drawLine(left_space, top_space, left_space, height() - bottom_space);
    painter.drawLine(left_space, height() - bottom_space, width() - margin, height() - bottom_space);
    painter.drawLine(width() - margin, top_space, width() - margin, height() - bottom_space);
    painter.drawLine(left_space, top_space, width() - margin, top_space);

    double xview = smallwidth() / double(xticks + 1);

    QFontMetrics fm(font);

    for (int i = 0; i <= xticks; i++)
    {
        double xpos = left_space + i * xview;

        std::string line = std::to_string(int64_t(pixelToPos(xpos)));

        painter.setPen(pen);
        painter.drawText(xpos + ticksize, start_y + bottom_space - 5, line.c_str());

        painter.setPen(whitepen);
        painter.drawLine(xpos, start_y - ticksize, xpos, start_y + ticksize);
    }

    double yview = double(height() - bottom_space - top_space) / yticks;
    int yalign = fm.horizontalAdvance("AAAAA");

    std::array<char, 5> exp_char = {' ', 'K', 'M', 'G', 'T'};
    std::array<double, 5> exp_value = {1.0, 1E3, 1E6, 1E9, 1E12};

    for (int i = 0; i <= yticks; i++)
    {
        double d_value = i * ymax / yscale / yticks;

        int exp_idx = 0;
        for (int i = 1; i < exp_value.size(); i++) exp_idx += d_value >= exp_value.at(i);

        int precision = (d_value / exp_value.at(exp_idx) < 10.0) ? 1 : 0;
        auto value = QString("%1").arg(d_value / exp_value.at(exp_idx), 0, 'f', precision) + exp_char.at(exp_idx);

        double ypos = height() - bottom_space - i * yview;

        painter.setPen(pen);
        painter.drawText(2 + yalign - fm.horizontalAdvance(value), ypos - 3, value);

        painter.setPen(whitepen);
        painter.drawLine(left_space - ticksize, ypos, left_space + ticksize, ypos);
    }

    painter.setPen(pen);
    int font_height = fm.height();

    // Draw token colors
    int total_text_width = curves.size() * font_height * 3 / 2;
    for (auto& data : curves) total_text_width += fm.horizontalAdvance(data.shortname.c_str());

    int left_padding = std::max((width() - total_text_width) / 2 + font_height, 0);

    for (auto& data : curves)
    {
        painter.fillRect(
            QRectF(left_padding + font_height / 4, font_height / 2, font_height / 2, font_height / 2), data.color
        );
        painter.drawText(left_padding + font_height, font_height, data.shortname.c_str());
        left_padding += font_height * 3 / 2 + fm.horizontalAdvance(data.shortname.c_str());
    }

    pen.setStyle(Qt::DotLine);

    if (mousepos.x() >= left_space && mousepos.x() < width() - margin)
    {
        pen.setColor(Qt::lightGray);
        painter.setPen(pen);
        painter.drawLine(mousepos.x(), top_space - margin, mousepos.x(), height() - bottom_space / 2);
    }

    int barpixelpos = posToPixel(barpos);

    if (barpos >= 0 && barpixelpos >= left_space && barpixelpos < width() - margin)
    {
        pen.setColor(Qt::cyan);
        painter.setPen(pen);
        painter.drawLine(barpixelpos, top_space - margin, barpixelpos, height() - bottom_space / 2);
    }

    if (bLClick)
    {
        pen.setColor(Qt::cyan);
        painter.setPen(pen);
        int minpos = std::max<int>(std::min<int>(lclickpos.x(), mousepos.x()), left_space);
        int maxpos = std::min<int>(std::max<int>(lclickpos.x(), mousepos.x()), width() - margin);

        if (minpos < maxpos) painter.drawRect(QRect(minpos, top_space, maxpos - minpos, start_y - top_space));
    }
}

void PlotGraph::AddData(std::string name, QColor color, std::vector<WeightedPoint>&& data)
{
    for (const auto& point : data)
    {
        xmax = std::max(xmax, (double) point.time);
        ymax = std::max(ymax, 1.05 * point.value);
    }

    PlotCurve& curve = curves.emplace_back(PlotCurve{});
    curve.fullname = name;
    curve.shortname = name.substr(0, max_legend_chars);
    curve.color = color;
    curve.SetData(std::move(data));

    update();
};

void PlotGraph::mouseMoveEvent(QMouseEvent* event)
{
    BasePlotWidget::mouseMoveEvent(event);

    if (event->buttons() & Qt::RightButton)
    {
        double cvt = xmax / scaledwidth();
        xoffset += cvt * (event->pos().x() - mousepos.x());
    }
    mousepos = event->pos();

    IMPLEMENT_FPS_LIMITER();

    if (!bLClick)
        if (mousepos.x() < left_space || mousepos.x() > width() - margin) return;

    UpdateGraphTable(pixelToPos(mousepos.x()));
    update();
}
