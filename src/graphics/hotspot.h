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

#include <array>
#include <atomic>
#include <vector>
#include "data/wavemanager.h"
#include "util/custom_layouts.h"

#ifdef RCV_DISABLE_OPENGL
typedef QWidget BaseHistogramWidget;
#else
#    include <QOpenGLWidget>
typedef QOpenGLWidget BaseHistogramWidget;
#endif

class HotspotView : public BaseHistogramWidget
{
    Q_OBJECT
    set_tracked();

    struct HotspotBin
    {
        int center_line = -1;
        int draw_start_x = -1;
        int draw_end_x = -1;
        int draw_y = -1;
        std::array<std::atomic<int64_t>, 16> cycles{};
    };

public:
    HotspotView(int begin, int end, int bins, int64_t max_value);
    void Add(const TokenMap& tokens);
    void Compile();
    void paintEvent(QPaintEvent*) override;

    void HighlightLines(int bin);
    virtual void mouseMoveEvent(QMouseEvent*) override;
    virtual void mousePressEvent(QMouseEvent*) override;

protected:
    int getBin(int line) { return (line - code_begin) / step; }
    int64_t max_value;
    const int code_begin;
    const int code_end;
    int step;
    std::vector<HotspotBin> bins;
};
