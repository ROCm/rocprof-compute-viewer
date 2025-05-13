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

#pragma once

#include <QBoxLayout>
#include <QScrollBar>
#include <QWidget>
#include <memory>
#include "util/custom_layouts.h"
#include "wave/measure.h"
#include "wave/token.h"

class QCustomScroll : public QWidget
{
    Q_OBJECT
public:
    explicit QCustomScroll(std::shared_ptr<class ScrollValue> _view);
    ~QCustomScroll();

    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void update();
    void updatebar(bool bForce);
    void onScroll(int value)
    {
        updatebar(true);
        update();
    }
    void goToClock(int64_t clock)
    {
        scrollbar->setValue(clock - clock_cutoff_start);
        update();
    }

    class QScrollBar* scrollbar;
    static int64_t clock_cutoff_start;
    static int64_t clock_cutoff_end;
    std::shared_ptr<class ScrollValue> view{};
    std::shared_ptr<MeasureTool> tool{nullptr};

    int64_t prev_cutoff_start = -1;
    int64_t prev_cutoff_end = -1;
signals:
    void valueupdated();
};

struct ScrollValue
{
    ScrollValue() = default;
    void notify();

    std::vector<QCustomScroll*> parents{};
    std::atomic<int64_t> start{0};
    std::atomic<int64_t> range{0};
};
