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

#include "qcodelist.h"
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <sstream>
#include <unordered_set>
#include <vector>
#include "data/wavemanager.h"
#include "graphics/canvas.h"
#include "mainwindow.h"
#include "sourcefile.h"
#include "util/custom_layouts.h"

#define ASM_MAX_LINE_WIDTH 420

int QCodelist::line_height = 20;
QCodelist* QCodelist::singleton = nullptr;

std::array<std::string, (int) CyclesLabel::Strategy::LAST> strategy_names = {
    "Latency: Sum all",
    "Latency: Mean all",
    "Latency: Iteration",
    "Latency: Sum Wave",
    "Latency: Mean Wave",
    "Latency: Max Wave"};

CycleModeSelector::CycleModeSelector(QCodelist* _parent) : parent(_parent)
{
    for (auto& name : strategy_names) addItem(QString(name.c_str()));

    QObject::connect(this, &QComboBox::currentTextChanged, this, &CycleModeSelector::changeStrategy);
}

void CycleModeSelector::changeStrategy(const QString& text)
{
    for (int i = 0; i < (int) CyclesLabel::Strategy::LAST; i++)
        if (strategy_names.at(i) == text.toStdString()) CyclesLabel::setStrategy(CyclesLabel::Strategy(i));

    parent->scheduleRedraw();
}

void QCodelist::scheduleRedraw()
{
    update();
    updateGeometry();
    if (connector) connector->update();

    for (auto& line : ASMCodeline::line_vec)
        if (auto element = line->elements.at(ASMCodeline::Element::ELATENCY).get()) element->InvalidateCache();

    for (auto& elem : elements)
        if (elem)
        {
            elem->InvalidateCache();
            elem->update();
        }

    scrollbar->setMaximum(std::max<int>(line_height * (ASMCodeline::line_vec.size() + 2) - height(), 0));
}

QCodelist::QCodelist(QWidget* parent)
{
    singleton = this;
    layout_main = new QBox(this);
    this->setLayout(layout_main);

    layout_main->addWidget(new QLabel("Instruction"), 0, 1);
    layout_main->addWidget(new QLabel("Hitcount "), 0, 2);
    layout_main->addWidget(new CycleModeSelector(this), 0, 3);
    layout_main->addWidget(new QLabel(" Idle "), 0, 4);

    connector = new ArrowCanvas();

    layout_main->addWidget(connector, 1, 0);

    elements.at(Element::EASM) = new QASMElementList();
    for (int e = 0; e < Element::ENUMTYPES; e++)
    {
        if (e != Element::EASM) elements.at(e) = new QElementList(Element(e));
        layout_main->addWidget(elements.at(e), 1, e + 1);
    }

    scrollbar = new QScrollBar(Qt::Vertical);
    // layout_main->addWidget(scrollbar, 1, (int)Element::ENUMTYPES+1);

    connect(scrollbar, &QScrollBar::valueChanged, this, &QCodelist::onScroll);

    this->setAutoFillBackground(true);
}

QCodelist::~QCodelist()
{
    if (singleton == this) singleton = nullptr;
    if (connector) delete connector;
    if (layout_main) delete layout_main;
}

void QCodelist::Populate(WaveInstance& wave)
{
    QPalette pal = QPalette();
    pal.setColor(QPalette::Window, WindowColors::Background());
    this->setPalette(pal);

    ASMCodeline::Populate(wave.code);

    scheduleRedraw();
}

void QCodelist::resizeEvent(QResizeEvent* event)
{
    Super::resizeEvent(event);
    scheduleRedraw();
}

int QCodelist::lineheight() { return line_height; };

void QCodelist::onScroll(int value)
{
    for (auto& elem : elements)
        if (elem) elem->setScroll(value);
    connector->setScroll(value);
    update();
}

void QCodelist::Highlight(int lbegin, int lend, bool bIntoView, const Color& color)
{
    auto elem = elements.at(ASMCodeline::Element::EASM);
    QWARNING(elem, "No code element", return );

    auto scroll = elem->Highlight(color, lbegin, lend);

    if (scroll && bIntoView) scrollbar->setValue(*scroll);
}

void QCodelist::wheelEvent(QWheelEvent* event)
{
    this->Super::wheelEvent(event);
    scrollbar->setValue(scrollbar->value() - event->angleDelta().y());
}

void QCodelist::paintEvent(QPaintEvent* event)
{
    this->Super::paintEvent(event);

    QPainter painter(this);
    QFont font = painter.font();
    font.setPointSize(MainWindow::font());

    QFontMetrics fm(font);

    if (fm.height() != line_height)
    {
        line_height = fm.height();

        scrollbar->setPageStep(line_height * 10);
        scrollbar->setSingleStep(line_height);

        scheduleRedraw();
    }

    this->QWidget::paintEvent(event);
}

QElementList::QElementList(ASMCodeline::Element _elem) : elementtype(_elem)
{
    if (!isASM()) setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Ignored);
}

void QElementList::updateCache(QFontMetrics& fm)
{
    int width = 0;

    for (auto& line : ASMCodeline::line_vec)
        if (auto element = line->elements.at(elementtype).get())
        {
            element->InvalidateCache();
            width = std::max(width, element->width(fm));
        }

    width += 2 + 2 * fm.height() - 2 * fm.overlinePos();

    width_cache = std::min(std::max(width, width_cache), ASM_MAX_LINE_WIDTH);
    updateGeometry();
    cachevalid = true;
}

void QElementList::paintEvent(QPaintEvent* event)
{
    this->Super::paintEvent(event);

    const int heighty = QCodelist::lineheight();

    QPainter painter(this);
    {
        QFont font = MainWindow::default_font.isEmpty() ? painter.font() : QFont(MainWindow::default_font);
        font.setPointSize(MainWindow::font());
        painter.setFont(font);
    }
    QFontMetrics fm(painter.font());
    int overline = fm.height() - fm.overlinePos();

    if (!cachevalid) updateCache(fm);

    if (timer.timer && ASMCodeline::line_vec.size())
    {
        int b = highlight_begin * heighty - scrollposy;
        int e = (highlight_end + 1) * heighty - scrollposy;

        if (b < height() && e > 0 && b < e)
        {
            QPainterPath path;
            path.addRoundedRect(QRectF(0, b, width(), e - b), 3, 3);

            QColor color = WindowColors::LineSlowHighlight();
            QBrush brush(color);
            painter.fillPath(path, brush);
        }
    }

    painter.setPen(QPen(WindowColors::textColor(), 1));

    for (auto& line : ASMCodeline::line_vec)
        if (auto element = line->elements.at(elementtype).get())
        {
            int posy = heighty * (1 + line->line_index) - scrollposy;
            if (posy < -2 * heighty) continue;
            if (posy > height() + heighty) break;

            int posx = isASM() ? 0 : std::max(0, width_cache - element->width(fm));

            element->paint(painter, posx, posy, heighty, overline);
        }
}

LineElement* QElementList::getelement(int index)
{
    if (index >= 0 && index < ASMCodeline::line_vec.size())
        return ASMCodeline::line_vec.at(index)->elements.at(elementtype).get();

    return nullptr;
};

int QElementList::line_height() { return QCodelist::lineheight(); };

QSize QElementList::sizeHint() const { return QSize(std::max(width_cache + 8, 48), Super::sizeHint().height()); }

QSize QASMElementList::sizeHint() const { return QSize(std::max(width_cache + 8, 128), Super::sizeHint().height()); }

QSize QASMElementList::minimumSizeHint() const
{
    return QSize(std::max(std::min(width_cache + 8, 256), 128), Super::minimumSizeHint().height());
}

void QASMElementList::mouseMoveEvent(class QMouseEvent* event)
{
    this->Super::mouseMoveEvent(event);

    auto* asm_elem = dynamic_cast<ASMLine*>(getelement(getLineIndex(event->pos().y())));
    if (!asm_elem) return;

    std::stringstream tooltip;
    tooltip << "l:" << asm_elem->line_number << " cid:" << asm_elem->codeobj << " vaddr:0x" << std::hex
            << asm_elem->addr << std::dec << " - " << asm_elem->getStdText();

    this->setToolTip(tooltip.str().c_str());
}