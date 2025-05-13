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

#include "measure.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWidget>

void MeasureTool::mouseMoveEvent(int posx, int posy)
{
    measure_start_x = std::min(clickPos_x, posx);
    measure_start_y = std::min(clickPos_y, posy);
    measure_size_x = std::max(clickPos_x, posx) - measure_start_x;
    measure_size_y = std::max(clickPos_y, posy) - measure_start_y;

    if (bClicking) update();
}
void MeasureTool::mousePressEvent(bool clicked, int posx, int posy)
{
    bClicking = clicked;
    measure_start_x = clickPos_x = posx;
    measure_start_y = clickPos_y = posy;
    measure_size_x = 0;
    measure_size_y = 0;
    update();
}

void MeasureTool::update()
{
    for (auto* widget : update_list) widget->update();
}