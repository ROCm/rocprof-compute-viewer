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

#include "flamegraphwidget.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QToolTip>
#include <algorithm>
#include <cmath>
#include <iostream>

#include "code/asmcode.h"
#include "code/sourcefile.h"
#include "config/config.hpp"
#include "data/shaderdata.h"
#include "data/wavemanager.h"
#include "flamegraph/layout.h"
#include "flamegraph/stack_builder.h"
#include "mainwindow.h"

namespace
{

QString formatLatency(int64_t cycles)
{
    if (cycles > 1000000) return QString("%1M cycles").arg(cycles / 1000000.0, 0, 'f', 1);
    if (cycles > 1000) return QString("%1K cycles").arg(cycles / 1000.0, 0, 'f', 1);
    return QString("%1 cycles").arg(cycles);
}

QString formatFrameTooltip(const flamegraph::Frame& f, int64_t totalLatency)
{
    QString prefix;
    if (f.asmIndex >= 0)
    {
        auto it = ASMCodeline::line_map.find(f.asmIndex);
        if (it != ASMCodeline::line_map.end())
            if (auto* a = dynamic_cast<ASMLine*>(it->second->elements.at(ASMCodeline::Element::EASM).get()))
                prefix = QString("%1 / 0x%2 - ").arg(a->codeobj).arg(a->addr, 0, 16);
    }

    double pct = totalLatency > 0 ? 100.0 * f.latency / totalLatency : 0;
    QString tip = prefix + formatLatency(f.latency) + QString(" (%1%)").arg(pct, 0, 'f', 1);
    if (!f.location.empty()) tip += "\n" + QString::fromStdString(f.location);
    if (!f.content.empty()) tip += "\n" + QString::fromStdString(f.content);
    return tip;
}

} // anonymous namespace

FlameGraphWidget::FlameGraphWidget(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(200);

    QPalette pal = QPalette();
    pal.setColor(QPalette::Window, WindowColors::Background());
    setAutoFillBackground(true);
    setPalette(pal);

    m_hScrollBar = new QScrollBar(Qt::Horizontal, this);
    m_hScrollBar->setRange(0, 0);
    connect(m_hScrollBar, &QScrollBar::valueChanged, this, &FlameGraphWidget::onScrollBarChanged);
}

void FlameGraphWidget::resetFrameState()
{
    m_frames.clear();
    m_framesByRow.clear();
    m_numRows = 0;
    m_totalLatency = 0;
    m_hoveredFrame = nullptr;
    m_viewLeft = 0.0;
    m_viewWidth = 1.0;
    updateScrollBar();
}

void FlameGraphWidget::applyLayout(flamegraph::LayoutResult result)
{
    m_frames = std::move(result.frames);
    m_framesByRow = std::move(result.framesByRow);
    m_numRows = result.numRows;
    m_totalLatency = result.totalLatency;

    if (m_hScrollBar) m_cachedScrollBarHeight = m_hScrollBar->sizeHint().height();

    int totalHeight = m_marginTop + m_numRows * (m_rowHeight + m_padding) + m_marginBottom;
    setMinimumHeight(totalHeight);
    updateGeometry();
    update();
}

flamegraph::Roots FlameGraphWidget::pickBuilder()
{
    auto* mw = MainWindow::window;
    if (!mw || !mw->source_filetab) return {};

    const bool has_markers = mw->shaderdata_manager && mw->shaderdata_manager->HasMarkers();
    const bool has_target =
        WaveInstance::main_wave && mw->current_wave_coord_se >= 0 && WaveInstance::main_wave->cu >= 0;

    if (has_markers && has_target)
    {
        auto roots = flamegraph::buildIntegratedRoots(
            mw->current_wave_coord_se, WaveInstance::main_wave->cu, mw->current_wave_coord_sm
        );
        if (roots.empty())
        {
            // Don't silently fall through to the legacy path — that hides a
            // malformed marker walk behind a different tree shape.
            std::cerr << "FlameGraphWidget: integrated builder returned empty for "
                      << "(SE=" << mw->current_wave_coord_se << ", CU=" << WaveInstance::main_wave->cu
                      << ", SIMD=" << mw->current_wave_coord_sm << ")\n";
        }
        return roots;
    }

    if (!mw->source_filetab->files.empty()) return flamegraph::buildSourceRoots();
    return {};
}

void FlameGraphWidget::rebuild()
{
    resetFrameState();

    flamegraph::Roots roots = pickBuilder();
    if (roots.empty()) return;

    applyLayout(flamegraph::layoutFromRoots(roots));
}

int FlameGraphWidget::rowTopY(int row) const
{
    const int bottomMargin = m_marginBottom + m_cachedScrollBarHeight;
    const int rowSpan = m_rowHeight + m_padding;
    return height() - bottomMargin - (row + 1) * rowSpan + m_padding;
}

QRect FlameGraphWidget::frameRect(const flamegraph::Frame& f) const
{
    int drawWidth = width() - m_marginLeft - m_marginRight;

    double normLeft = (f.x - m_viewLeft) / m_viewWidth;
    double normWidth = f.w / m_viewWidth;

    int px = m_marginLeft + static_cast<int>(normLeft * drawWidth);
    int pw = std::max(1, static_cast<int>(normWidth * drawWidth));

    return QRect(px, rowTopY(f.row), pw, m_rowHeight);
}

const flamegraph::Frame* FlameGraphWidget::frameAt(const QPoint& pos) const
{
    if (m_frames.empty() || m_framesByRow.empty()) return nullptr;

    int drawWidth = width() - m_marginLeft - m_marginRight;
    if (drawWidth <= 0) return nullptr;

    const int bottomMargin = m_marginBottom + m_cachedScrollBarHeight;
    const int rowSpan = m_rowHeight + m_padding;
    if (rowSpan <= 0) return nullptr;
    int distFromBottom = (height() - bottomMargin) - pos.y();
    if (distFromBottom <= 0) return nullptr;
    int row = (distFromBottom + m_padding - 1) / rowSpan;
    if (row < 0 || row >= static_cast<int>(m_framesByRow.size())) return nullptr;

    int rowTop = rowTopY(row);
    if (pos.y() < rowTop || pos.y() >= rowTop + m_rowHeight) return nullptr;

    double xNorm = m_viewLeft + (static_cast<double>(pos.x() - m_marginLeft) / drawWidth) * m_viewWidth;

    const auto& rowIdx = m_framesByRow[row];
    if (rowIdx.empty()) return nullptr;

    auto it =
        std::upper_bound(rowIdx.begin(), rowIdx.end(), xNorm, [&](double v, int idx) { return v < m_frames[idx].x; });
    if (it == rowIdx.begin()) return nullptr;
    --it;

    const flamegraph::Frame& f = m_frames[*it];
    if (xNorm >= f.x && xNorm < f.x + f.w) return &f;
    return nullptr;
}

QSize FlameGraphWidget::sizeHint() const
{
    int h = m_marginTop + std::max(m_numRows, 3) * (m_rowHeight + m_padding) + m_marginBottom;
    return QSize(800, h);
}

QSize FlameGraphWidget::minimumSizeHint() const { return QSize(200, 100); }

void FlameGraphWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    if (m_frames.empty())
    {
        QFont font = MainWindow::default_font.isEmpty() ? painter.font() : QFont(MainWindow::default_font);
        font.setPointSize(MainWindow::font() + 2);
        painter.setFont(font);
        painter.setPen(WindowColors::textColor());
        painter.drawText(rect(), Qt::AlignCenter, "No profiling data available for flamegraph");
        return;
    }

    QFont font = MainWindow::default_font.isEmpty() ? painter.font() : QFont(MainWindow::default_font);
    font.setPointSize(std::max(MainWindow::font() - 2, 7));
    painter.setFont(font);
    QFontMetrics fm(font);

    const int drawWidth = width() - m_marginLeft - m_marginRight;
    if (drawWidth <= 0) return;
    const int widgetH = height();
    const double invViewWidth = 1.0 / m_viewWidth;
    const double drawWidthD = static_cast<double>(drawWidth);

    // Visible normalized-x range. Per-row binary-search bounds the visible
    // count to ~drawWidth frames; no coalescing — that broke wide frames.
    const double vxMin = m_viewLeft;
    const double vxMax = m_viewLeft + m_viewWidth;

    for (int row = 0; row < static_cast<int>(m_framesByRow.size()); ++row)
    {
        const auto& rowIdx = m_framesByRow[row];
        if (rowIdx.empty()) continue;

        const int py = rowTopY(row);
        if (py + m_rowHeight < 0 || py > widgetH) continue;

        // First frame whose x + w > vxMin — anything before that ends to the
        // left of the visible region and can be skipped.
        auto firstIt = std::lower_bound(
            rowIdx.begin(),
            rowIdx.end(),
            vxMin,
            [&](int idx, double v) { return m_frames[idx].x + m_frames[idx].w <= v; }
        );

        for (auto it = firstIt; it != rowIdx.end(); ++it)
        {
            const flamegraph::Frame& f = m_frames[*it];
            if (f.x >= vxMax) break;

            const double normLeft = (f.x - m_viewLeft) * invViewWidth;
            const double normWidth = f.w * invViewWidth;

            const int px = m_marginLeft + static_cast<int>(normLeft * drawWidthD);
            // Clamp width to 1px so thin markers/instructions stay visible
            // when zoomed out; do NOT silently drop them.
            const int pw = std::max(1, static_cast<int>(normWidth * drawWidthD));

            QRect r(px, py, pw, m_rowHeight);

            QColor fillColor = f.color;
            if (&f == m_hoveredFrame) fillColor = fillColor.lighter(130);

            painter.fillRect(r, fillColor);
            painter.setPen(QPen(fillColor.darker(150), 1));
            painter.drawRect(r);

            if (pw > 20)
            {
                painter.setPen(WindowColors::reverseTextColor());

                QString text = QString::fromStdString(f.label);
                if ((f.lineNumber >= 0 && f.asmIndex < 0) && !f.content.empty())
                    text += "          " + QString::fromStdString(f.content);

                QString elided = fm.elidedText(text, Qt::ElideRight, pw - 4);
                painter.drawText(r.adjusted(2, 0, -2, 0), Qt::AlignVCenter | Qt::AlignLeft, elided);
            }
        }
    }
}

void FlameGraphWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isPanning)
    {
        int dx = event->pos().x() - m_lastMousePos.x();
        int drawWidth = width() - m_marginLeft - m_marginRight;
        if (drawWidth > 0)
        {
            double shift = -static_cast<double>(dx) / drawWidth * m_viewWidth;
            m_viewLeft = std::clamp(m_viewLeft + shift, 0.0, 1.0 - m_viewWidth);
        }
        m_lastMousePos = event->pos();
        updateScrollBar();
        update();
        return;
    }

    const flamegraph::Frame* hovered = frameAt(event->pos());
    if (hovered != m_hoveredFrame)
    {
        m_hoveredFrame = hovered;
        update();

        if (hovered)
            QToolTip::showText(event->globalPosition().toPoint(), formatFrameTooltip(*hovered, m_totalLatency), this);
        else
            QToolTip::hideText();
    }
}

void FlameGraphWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton)
    {
        m_isPanning = true;
        m_lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    if (event->button() != Qt::LeftButton) return;
    const flamegraph::Frame* clicked = frameAt(event->pos());
    if (!clicked) return;

    if (clicked->asmIndex >= 0)
    {
        auto it = ASMCodeline::line_map.find(clicked->asmIndex);
        if (it != ASMCodeline::line_map.end() && MainWindow::window && MainWindow::window->code_contents)
        {
            auto* asmElem = dynamic_cast<ASMLine*>(it->second->elements.at(ASMCodeline::Element::EASM).get());
            if (asmElem) asmElem->onMousePress();
        }
    }
    else if (clicked->lineNumber >= 0 && !clicked->filename.empty())
    {
        if (MainWindow::window && MainWindow::window->source_filetab)
            MainWindow::window->source_filetab->scrollTo(clicked->filename, clicked->lineNumber);
    }
    else if (!clicked->filename.empty())
    {
        if (MainWindow::window && MainWindow::window->source_filetab)
            MainWindow::window->source_filetab->scrollTo(clicked->filename, 0);
    }
}

void FlameGraphWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_isPanning && (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton))
    {
        m_isPanning = false;
        setCursor(Qt::ArrowCursor);
    }
}

void FlameGraphWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    int scrollBarHeight = m_hScrollBar->sizeHint().height();
    m_cachedScrollBarHeight = scrollBarHeight;
    m_hScrollBar->setGeometry(0, height() - scrollBarHeight, width(), scrollBarHeight);

    update();
}

void FlameGraphWidget::wheelEvent(QWheelEvent* event)
{
    double zoomFactor = 1.15;
    int drawWidth = width() - m_marginLeft - m_marginRight;
    if (drawWidth <= 0) return;

    double mouseNorm = m_viewLeft + m_viewWidth * (event->position().x() - m_marginLeft) / drawWidth;

    constexpr double kMinViewWidth = 0.001;

    if (event->angleDelta().y() > 0)
    {
        // At minimum zoom, don't shift the view sideways toward the cursor —
        // without this guard m_viewLeft kept drifting on every wheel tick even
        // though m_viewWidth was already clamped.
        if (m_viewWidth <= kMinViewWidth)
        {
            event->accept();
            return;
        }
        double newWidth = std::max(m_viewWidth / zoomFactor, kMinViewWidth);
        double newLeft = mouseNorm - (mouseNorm - m_viewLeft) / zoomFactor;
        m_viewLeft = std::max(newLeft, 0.0);
        m_viewWidth = std::min(newWidth, 1.0 - m_viewLeft);
    }
    else if (event->angleDelta().y() < 0)
    {
        if (m_viewWidth >= 1.0)
        {
            event->accept();
            return;
        }
        double newWidth = std::min(m_viewWidth * zoomFactor, 1.0);
        double newLeft = mouseNorm - (mouseNorm - m_viewLeft) * zoomFactor;
        m_viewLeft = std::max(newLeft, 0.0);
        if (m_viewLeft + newWidth > 1.0) m_viewLeft = 1.0 - newWidth;
        m_viewWidth = newWidth;
    }

    updateScrollBar();
    update();
    event->accept();
}

void FlameGraphWidget::updateScrollBar()
{
    const int scrollSteps = 10000;
    int maxVal = static_cast<int>((1.0 - m_viewWidth) * scrollSteps);
    int pageStep = static_cast<int>(m_viewWidth * scrollSteps);
    int currentVal = static_cast<int>(m_viewLeft * scrollSteps);

    m_hScrollBar->blockSignals(true);
    m_hScrollBar->setRange(0, maxVal);
    m_hScrollBar->setPageStep(pageStep);
    m_hScrollBar->setValue(currentVal);
    m_hScrollBar->blockSignals(false);
}

void FlameGraphWidget::onScrollBarChanged(int value)
{
    const int scrollSteps = 10000;
    double newLeft = static_cast<double>(value) / scrollSteps;
    m_viewLeft = std::clamp(newLeft, 0.0, 1.0 - m_viewWidth);
    update();
}
