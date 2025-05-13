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

#include <QComboBox>
#include <QWidget>
#include "util/custom_layouts.h"
#include "util/highlight.h"

class LineElement
{
public:
    virtual int width(class QFontMetrics& fm) = 0;
    virtual void paint(class QPainter& painter, int posx, int posy, int stepy, int overline) = 0;
    virtual const std::string& getStdText() const = 0;
    virtual void InvalidateCache() const = 0;
    virtual void onMousePress() = 0;
    virtual void setMouseHover(bool value) = 0;
};

class QTextElement : public QWidget
{
    Q_OBJECT;
    set_tracked();
    using Super = QWidget;

public:
    explicit QTextElement();
    virtual ~QTextElement(){};

    void setScroll(int posy)
    {
        this->scrollposy = posy;
        update();
    };
    int getLineIndex(int posy);

    void enterEvent(IF_QT6_ELSE(QEnterEvent, QEvent) * event) override;
    void leaveEvent(QEvent* event) override;

    void mousePressEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;

    std::optional<int> Highlight(const Color& color, int lbegin, int lend);
    void IncrementHighlight();

    virtual LineElement* getelement(int index) = 0;
    virtual int line_height() = 0;

protected:
    int scrollposy = 0;
    bool isMouseHover = false;
    int hoveringOverLine = -1;

    // Highlight
    int highlight_begin = -1;
    int highlight_end = -1;
    Color highlightColor;
    HighlightTimer timer;
};

class TextLineElement : public LineElement
{
public:
    TextLineElement(const std::string& _str) : stdtext(_str), text(_str.c_str()) {}

    virtual void paint(class QPainter& painter, int posx, int posy, int stepy, int overline) override;
    virtual int width(class QFontMetrics& fm) override;

    virtual const std::string& getStdText() const override { return stdtext; };
    virtual void InvalidateCache() const override { width_cache = -1; }
    virtual void setRefHighlight(bool value, bool click)
    {
        bRefHighlight = value;
        bHighlightMode = click;
    };

    virtual void onMousePress() override{};
    virtual void setMouseHover(bool value) override { bHovering = value; };

protected:
    mutable int width_cache = -1;
    std::string stdtext;
    QString text;

    bool bHovering = false;
    bool bRefHighlight = false;
    bool bHighlightMode = false;
};