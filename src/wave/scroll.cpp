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

#include "scroll.h"
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>

int64_t QCustomScroll::clock_cutoff_start = 0;
int64_t QCustomScroll::clock_cutoff_end = 1E3;

QCustomScroll::QCustomScroll(std::shared_ptr<class ScrollValue> _view) : view(_view)
{
    view->parents.push_back(this);

    this->setLayout(new QVBox());
    scrollbar = new QScrollBar(Qt::Horizontal);
    connect(scrollbar, &QScrollBar::valueChanged, this, &QCustomScroll::onScroll);

    scrollbar->setMinimum(0);

    view->range = Token::PosToClock(width());
    scrollbar->setMaximum(clock_cutoff_end - clock_cutoff_start);
    layout()->addWidget(scrollbar);
}

QCustomScroll::~QCustomScroll()
{
    if (view)
        for (auto it = view->parents.begin(); it != view->parents.end(); it++)
            if (*it == this)
            {
                view->parents.erase(it);
                break;
            }
    delete scrollbar;
}

void QCustomScroll::update()
{
    QWidget::update();
    updatebar(false);
}

void QCustomScroll::updatebar(bool bForce)
{
    if (bForce)
    {
        scrollbar->setPageStep(Token::PosToClock(width()) / 2);
        scrollbar->setSingleStep(Token::PosToClock(32));
        if (prev_cutoff_end != clock_cutoff_end || prev_cutoff_start != clock_cutoff_start)
        {
            int64_t new_pos = prev_cutoff_start + view->start - clock_cutoff_start;
            int64_t zero = 0;
            new_pos = std::max(new_pos, zero);
            new_pos = std::min(new_pos, clock_cutoff_end - view->range);
            scrollbar->setValue(new_pos);
        }
    }
    prev_cutoff_start = clock_cutoff_start;
    prev_cutoff_end = clock_cutoff_end;

    if (scrollbar->maximum() != clock_cutoff_end - clock_cutoff_start)
        scrollbar->setMaximum(std::max<int64_t>(clock_cutoff_end - clock_cutoff_start - view->range, 0));

    int64_t new_value = scrollbar->value();
    if (bForce || view->start != new_value)
    {
        view->start = scrollbar->value();
        emit valueupdated();
        view->notify();
    }
}

void QCustomScroll::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
    view->range = Token::PosToClock(width());
    updatebar(false);
}

void QCustomScroll::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    view->range = Token::PosToClock(width());
    update();
}

void ScrollValue::notify()
{
    for (auto& parent : parents)
        if (parent) parent->scrollbar->setValue(start);
}
