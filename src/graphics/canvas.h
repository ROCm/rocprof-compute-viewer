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
#include <QWidget>
#include "util/custom_layouts.h"

// This class will paint arrows on top of a QCodelist
class Canvas : public QWidget
{
    Q_OBJECT
    set_tracked();

public:
    Canvas()
    {
        setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Ignored);
        setMouseTracking(true);
    };

    struct arrow_t
    {
        int wait_number;
        int mem_line;
        int prev_slot_n;
        bool bIsInterior;
    };

    struct WaitList
    {
        int code_line;
        std::vector<std::pair<int, int>> sources;
    };

    enum class DrawType
    {
        DrawArrows,
        DrawBranch,
        DrawStall,
        DrawReasons,
        DrawStallAndReason,
        DrawLast
    };

    virtual QSize sizeHint() const override;
    virtual void paintEvent(class QPaintEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void leaveEvent(QEvent* event) override;
    void buildBranchConnections(const std::vector<WaitList>& waitcnt);
    void buildWaitConnections(const std::vector<WaitList>& waitcnt);
    bool checkConnectionsCache(const std::vector<WaitList>& waitcnt);

    bool Connect(QPainter& painter, int l1, int l2, int xslot, QColor& color);

    void setScroll(int posy)
    {
        scrollposy = posy;
        update();
    }

    int indexToYpos(int line_index, int lineheight) const { return lineheight * line_index + padding - scrollposy; }

    const int padding = 2;

private:
    int max_wait_alloc = 0;
    int max_branch_alloc = 0;
    int scrollposy = 0;
    int hovered_line_index = -1;

    void handleHotspotHover(QMouseEvent* event);
    void setHoveredLine(int line_index);
    void paintArrows();
    void paintStalls();
    void paintBranch();

    std::vector<arrow_t> branches{};
    std::vector<arrow_t> arrows{};
    std::mutex mut{};

public:
    static std::vector<QColor> arrow_colors;
    static DrawType drawtype;
};
