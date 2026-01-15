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

#include "highlight.h"
#include <QTimer>
#include <QWidget>
#include <iostream>
#include "util/custom_layouts.h"

void HighlightTimer::Highlight()
{
    if (vis < 1)
        vis -= 0.5f;
    else
        vis = 0.0f;

    if (timer != nullptr) return;

    timer = new QTimer();
    timer->setSingleShot(false);
    timer->setInterval(25);
    timer->start();
}

void HighlightTimer::IncrementHighlight(float amount)
{
    this->vis += amount;
    if (this->vis < 1) return;

    QWARNING(timer, "Empty timer!", return );

    timer->stop();
    delete timer;
    timer = nullptr;
}

HighlightTimer::~HighlightTimer()
{
    if (timer) delete timer;
    timer = nullptr;
}