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

#include "textelement.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include "config/config.hpp"

QTextElement::QTextElement() { setMouseTracking(true); }

int QTextElement::getLineIndex(int posy)
{
    return posy < 0 || line_height() <= 0 ? -1 : (posy + scrollposy) / line_height();
}

void QTextElement::setScroll(int posy)
{
    if (scrollposy != posy) clearHover();
    scrollposy = posy;
    update();
}

void QTextElement::clearHover()
{
    if (auto* element = getelement(hoveringOverLine)) element->setMouseHover(false);
    hoveringOverLine = -1;
    setToolTip({});
    unsetCursor();
    update();
}

void QTextElement::clearHighlight()
{
    delete timer.timer;
    timer.timer = nullptr;
    timer.vis = 1.0f;
    highlight_begin = highlight_end = -1;
    update();
}

void QTextElement::mousePressEvent(QMouseEvent* ev)
{
    this->Super::mousePressEvent(ev);
    if (auto elem = getelement(getLineIndex(ev->pos().y()))) elem->onMousePress();
}

std::optional<int> QTextElement::Highlight(const Color& color, int lbegin, int lend)
{
    highlight_begin = lbegin;
    highlight_end = lend;
    highlightColor = color;
    const int lineheight = line_height();

    timer.Highlight();
    QObject::connect(timer.timer, &QTimer::timeout, this, &QTextElement::IncrementHighlight, Qt::UniqueConnection);
    update();

    int newscroll = 0;
    if ((lbegin + 1) * lineheight - scrollposy > height())
        newscroll = (lbegin + 1) * lineheight - 2 * height() / 3;
    else if (lend * lineheight - scrollposy < 0)
        newscroll = lend * lineheight;
    else
        return std::nullopt;

    return newscroll;
}

void QTextElement::IncrementHighlight()
{
    timer.IncrementHighlight(0.015f);
    update();
}

void QTextElement::mouseMoveEvent(QMouseEvent* ev)
{
    this->Super::mouseMoveEvent(ev);
    int newhover = getLineIndex(ev->pos().y());

    if (!isMouseHover || hoveringOverLine == newhover) return;

    if (auto ref = getelement(hoveringOverLine)) ref->setMouseHover(false);
    if (auto ref = getelement(newhover)) ref->setMouseHover(true);

    hoveringOverLine = newhover;
    update();
}

void QTextElement::enterEvent(IF_QT6_ELSE(QEnterEvent, QEvent) * event)
{
    this->Super::enterEvent(event);
    isMouseHover = true;
}

void QTextElement::leaveEvent(QEvent* event)
{
    this->Super::leaveEvent(event);
    isMouseHover = false;

    clearHover();
}

int TextLineElement::width(QFontMetrics& fm)
{
    if (width_cache < 0 || !width_metrics || *width_metrics != fm)
    {
        width_cache = fm.horizontalAdvance(this->text);
        width_metrics = fm;
    }

    return width_cache;
}

void TextLineElement::paint(class QPainter& painter, int posx, int posy, int stepy, int overline)
{
    if (!text.size()) return;
    QFontMetrics fm(painter.font());
    width(fm);

    if (bRefHighlight)
    {
        QPainterPath path;
        QRect rect(posx, posy - stepy, width_cache + 2 * overline, stepy);
        path.addRoundedRect(rect, 3, 3);
        painter.fillPath(path, QBrush(WindowColors::LineRefHighlight(bHighlightMode)));
    }

    if (bHovering)
    {
        QLinearGradient grad(0, posy - stepy, 0, posy);

        Color background = WindowColors::Background();
        background *= 0.95f;
        grad.setColorAt(0.0, background);
        background *= 0.95f;
        grad.setColorAt(0.5, background);
        QBrush brush(grad);
        painter.setBrush(brush);

        QPainterPath path;
        path.addRoundedRect(QRectF(posx, posy - stepy, width_cache + 2 * overline, stepy), 2, 2);
        painter.fillPath(path, brush);

        QPen pen = painter.pen();
        painter.setPen(QPen(WindowColors::textColor(), 0.4));
        painter.drawPath(path);

        painter.setPen(pen);
    }

    drawText(painter, posx + overline, posy - overline);
}

void TextLineElement::drawText(QPainter& painter, int x, int baseline) { painter.drawText(x, baseline, text); }
