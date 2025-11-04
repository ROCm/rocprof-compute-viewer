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

#include "waveglobal.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QToolTip>
#include "code/qcodelist.h"
#include "config/config.hpp"
#include "data/wavedata.h"
#include "data/wavemanager.h"
#include "mainwindow.h"

int64_t QGlobalView::begintime = 0;
int64_t QGlobalView::maxtime = 0;
int QGlobalView::mipmap_level = 5;

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

        layout->addWidget(new QLabel(("Shader engine " + SE).c_str()));
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

            views.push_back(new QOutsideWaveView(se, sa, cu, simd, slot, trace, tool));
            layout->addWidget(views.back());
        }
        layout->addStretch();
    }
    this->setLayout(layout);
    setMouseTracking(true);
    this->setAttribute(Qt::WA_AlwaysShowToolTips, true);

    if (tool) tool->update_list.insert(this);
};

void QGlobalView::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);

    QPainterPath path;
    path.addRect(QRect(0, 0, width(), height()));
    painter.fillPath(path, WindowColors::Background());

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

    height_multiplier = 1 + (simd == 0);
    height_multiplier *= slot == 0;

    if (tool) tool->update_list.insert(this);
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

    if (height_multiplier == 0) return;

    float height = QGlobalView::HEIGHT() * height_multiplier;
    if (simd == 0)
    {
        QPainterPath path;
        QRect rect(0, height / 4, this->sizeHint().width(), height / 2);
        QBrush brush(QColor(0, 0, 0));
        path.addRect(rect);
        painter.fillPath(path, brush);
    }
    else { painter.drawLine(0, height / 2, this->sizeHint().width(), height / 2); }

    this->Super::paintEvent(event);
}

int64_t QOutsideWaveView::DrawWave(QPainter& painter, int64_t start, const WaveTraceData& wave, const QRect& area)
{
    int pos = QGlobalView::ClockToPos(start);
    int width = QGlobalView::ClockToPos(wave.end) - pos;

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
