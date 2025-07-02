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
#include "asmcode.h"
#include "util/highlight.h"

class QElementList : public QTextElement
{
    Q_OBJECT;
    set_tracked();
    using Super = QTextElement;

public:
    explicit QElementList(ASMCodeline::Element _elem);
    virtual ~QElementList(){};

    virtual void paintEvent(QPaintEvent* event) override;
    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override { return sizeHint(); };

    const ASMCodeline::Element elementtype;
    bool isASM() const { return elementtype == ASMCodeline::Element::EASM; }

    void InvalidateCache() { cachevalid = false; };
    virtual LineElement* getelement(int index) override;
    virtual int line_height() override;

protected:
    void updateCache(class QFontMetrics& fm);

    int width_cache = -1;
    bool cachevalid = false;
};

class QASMElementList : public QElementList
{
    Q_OBJECT;
    set_tracked();
    using Super = QElementList;

public:
    QASMElementList() : Super(ASMCodeline::Element::EASM) { setAttribute(Qt::WA_AlwaysShowToolTips, true); };
    virtual void mouseMoveEvent(class QMouseEvent* event) override;
    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;
};

class QCodelist : public QWidget
{
    Q_OBJECT;
    set_tracked();

    using Element = ASMCodeline::Element;
    using Super = QWidget;

public:
    explicit QCodelist(QWidget* parent = nullptr);
    virtual ~QCodelist();

    virtual void paintEvent(QPaintEvent* event) override;
    virtual void wheelEvent(QWheelEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;
    void Populate(WaveInstance& wave);

    void onScroll(int value);
    void scheduleRedraw();

    void Highlight(int lbegin, int lend, bool bIntoView, const Color& color = WindowColors::LineSlowHighlight());

    std::array<QElementList*, Element::ENUMTYPES> elements{};

    class QScrollBar* scrollbar = nullptr;

    class QGridLayout* layout_main = nullptr;
    class ArrowCanvas* connector = nullptr;
    std::vector<struct WaitList> waitcnt{};

    static int lineheight();
    static int line_height;
    static QCodelist* singleton;
};

class CycleModeSelector : public QComboBox
{
    Q_OBJECT;
    set_tracked();
    using Super = QComboBox;

public:
    CycleModeSelector(QCodelist* _parent);
    void changeStrategy(const QString& text);

    QCodelist* parent = nullptr;
};
