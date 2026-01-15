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

#include "qcodelist.h"
#include <QLabel>
#include <QListView>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <sstream>
#include <unordered_set>
#include <vector>
#include "config/appconfig.h"
#include "data/wavemanager.h"
#include "graphics/canvas.h"
#include "mainwindow.h"
#include "sourcefile.h"
#include "util/custom_layouts.h"

#define ASM_MAX_LINE_WIDTH 420

int QCodelist::line_height = 20;
QCodelist* QCodelist::singleton = nullptr;

class DrawTypeSelector : public QComboBox
{
    Q_OBJECT;
    set_tracked();
    using Super = QComboBox;

public:
    DrawTypeSelector(QCodelist* _parent);
    void changeDrawType(const QString& text);

    QCodelist* parent = nullptr;
};

std::array<std::string, (int) CyclesLabel::Strategy::LAST> strategy_names = {
    "Latency: Sum all",
    "Latency: Mean all",
    "Latency: Iteration",
    "Latency: Sum Wave",
    "Latency: Mean Wave",
    "Latency: Max Wave"};

std::array<std::string, (int) Canvas::DrawType::DrawLast> drawtype_names = {
    "View: Waitcnt", "Branch targets", "Inst Latency", "Stall Reasons", "Latency + Stall", "Memory Latency"};

DrawTypeSelector::DrawTypeSelector(QCodelist* _parent) : parent(_parent)
{
    for (auto& name : drawtype_names) addItem(QString(name.c_str()));

    setCurrentIndex((int) Canvas::DrawType::DrawArrows);

    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

    QObject::connect(this, &QComboBox::currentTextChanged, this, &DrawTypeSelector::changeDrawType);
}

void DrawTypeSelector::changeDrawType(const QString& text)
{
    for (int i = 0; i < (int) drawtype_names.size(); i++)
        if (drawtype_names.at(i) == text.toStdString())
        {
            Canvas::drawtype = Canvas::DrawType(i);
            if (parent && parent->connector)
            {
                parent->connector->updateGeometry();
                parent->connector->update();
            }
        }
}

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

    layout_main->addWidget(new QLabel("Instruction"), 0, Element::EASM + 1);
    layout_main->addWidget(new QLabel("Hitcount "), 0, Element::EHIT + 1);
    layout_main->addWidget(new CycleModeSelector(this), 0, Element::ELATENCY + 1);
    layout_main->addWidget(new QLabel(" Idle "), 0, Element::EIDLE + 1);
    layout_main->addWidget(new QLabel(" Samples "), 0, Element::EPCSamples + 1);
    layout_main->addWidget(new QLabel(" Issued "), 0, Element::EPCIssued + 1);
    layout_main->addWidget(new QLabel(" Stalls "), 0, Element::EPCStalls + 1);
    layout_main->addWidget(new QLabel(" Codeobj"), 0, Element::ECODEOBJ + 1);
    layout_main->addWidget(new QLabel(" Vaddr"), 0, Element::EADDRESS + 1);
    layout_main->addWidget(new QLabel(" Source link"), 0, Element::ESOURCEREF + 1);

    connector = new Canvas();
    drawselector = new DrawTypeSelector(this);
    layout_main->addWidget(connector, 1, 0);
    layout_main->addWidget(drawselector, 0, 0);

    elements.at(Element::EASM) = new QASMElementList();
    for (int e = 0; e < Element::ENUMTYPES; e++)
    {
        if (e != Element::EASM) elements.at(e) = new QElementList(Element(e));
        layout_main->addWidget(elements.at(e), 1, e + 1);
    }

    // Apply column visibility from config
    AppConfig& config = AppConfig::getInstance();
    for (int e = Element::EHIT; e < Element::ENUMTYPES; e++)
        setColumnVisibility(static_cast<Element>(e), config.getColumnVisible(e));

    // Set column stretch factors - column 0 can shrink, others get more stretch
    layout_main->setColumnStretch(0, 0);
    layout_main->setColumnStretch(Element::EASM + 1, 3);
    layout_main->setColumnStretch(Element::EHIT + 1, 0);
    layout_main->setColumnStretch(Element::ELATENCY + 1, 1);
    layout_main->setColumnStretch(Element::EIDLE + 1, 0);

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

void QCodelist::setColumnVisibility(ASMCodeline::Element elem, bool visible)
{
    if (elem == Element::EASM) return; // EASM is always visible

    // SQTT-dependent columns: EHIT, ELATENCY, EIDLE
    if (elem == Element::EHIT || elem == Element::ELATENCY || elem == Element::EIDLE)
        visible &= HorizontalHotspot::is_sqtt_enabled;

    // PCS-dependent columns: EPCSamples, EPCStalls, EPCIssued
    if (elem == Element::EPCSamples || elem == Element::EPCStalls || elem == Element::EPCIssued)
        visible &= HorizontalHotspot::is_pcs_enabled;

    if (auto* element = elements.at(elem)) element->setVisible(visible);

    // Also hide/show the header label
    if (layout_main)
    {
        if (auto* item = layout_main->itemAtPosition(0, elem + 1))
            if (auto* widget = item->widget()) widget->setVisible(visible);
    }

    updateGeometry();
    update();
}

void QCodelist::updateColumnVisibility()
{
    // Re-apply visibility settings from config, which will also apply data-type filters
    AppConfig& config = AppConfig::getInstance();
    for (int e = Element::EHIT; e < Element::ENUMTYPES; e++)
        setColumnVisibility(static_cast<Element>(e), config.getColumnVisible(e));
}

void QCodelist::updateMemoryLatencyEnabled()
{
    QWARNING(drawselector, "invalid selector", return );

    auto* model = drawselector->model();
    auto* view = qobject_cast<QListView*>(drawselector->view());
    QWARNING(model && view, "no model/view", return );

    int idx = (int) Canvas::DrawType::MemoryLatency;
    bool hasData = Canvas::max_memory_latency > 0.0;
    int enabled = static_cast<int>(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    int flags = hasData ? enabled : static_cast<int>(Qt::NoItemFlags);
    model->setData(model->index(idx, 0), flags, Qt::UserRole - 1);
    view->setRowHidden(idx, !hasData);
}

void QCodelist::setDrawType(Canvas::DrawType type)
{
    QWARNING(drawselector, "invalid selector", return );
    drawselector->setCurrentIndex((int) type);
    drawselector->currentTextChanged(drawtype_names.at((int) type).c_str());
}

void QCodelist::Populate(const std::vector<CodeData>& code)
{
    QPalette pal = QPalette();
    pal.setColor(QPalette::Window, WindowColors::Background());
    this->setPalette(pal);

    ASMCodeline::Populate(code);

    max_sqtt_latency = 1;
    max_pcs_latency = 1;
    for (auto& codeline : code)
    {
        max_sqtt_latency = std::max(max_sqtt_latency, codeline.line->latency_sum);
        max_pcs_latency = std::max(max_pcs_latency, codeline.line->pcsamples);
    }

    scheduleRedraw();

    QWARNING(drawselector, "invalid selector", return );
    if (HorizontalHotspot::is_pcs_enabled)
    {
        drawselector->setCurrentIndex((int) Canvas::DrawType::DrawStall);
        drawselector->currentTextChanged(drawtype_names.at((int) Canvas::DrawType::DrawStall).c_str());
    }

    auto* model = drawselector->model();
    auto* view = qobject_cast<QListView*>(drawselector->view());
    QWARNING(model && view, "no model/view", return );

    int enabled = static_cast<int>(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    for (int idx : {(int) Canvas::DrawType::DrawArrows, (int) Canvas::DrawType::DrawBranch})
    {
        int flags = HorizontalHotspot::is_sqtt_enabled ? enabled : static_cast<int>(Qt::NoItemFlags);
        model->setData(model->index(idx, 0), flags, Qt::UserRole - 1);
        view->setRowHidden(idx, !HorizontalHotspot::is_sqtt_enabled);
    }

    for (int idx : {(int) Canvas::DrawType::DrawReasons, (int) Canvas::DrawType::DrawStallAndReason})
    {
        int flags = HorizontalHotspot::is_pcs_enabled ? enabled : static_cast<int>(Qt::NoItemFlags);
        model->setData(model->index(idx, 0), flags, Qt::UserRole - 1);
        view->setRowHidden(idx, !HorizontalHotspot::is_pcs_enabled);
    }

    // Memory Latency option - hidden until analysis is run
    {
        int idx = (int) Canvas::DrawType::MemoryLatency;
        bool hasData = Canvas::max_memory_latency > 0.0;
        int flags = hasData ? enabled : static_cast<int>(Qt::NoItemFlags);
        model->setData(model->index(idx, 0), flags, Qt::UserRole - 1);
        view->setRowHidden(idx, !hasData);
    }

    // Update column visibility based on data availability flags
    updateColumnVisibility();
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
    if (connector) connector->setScroll(value);
    this->scrollposy = value;
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

    const int heighty = lineheight();
    auto elementpos = [this]()
    {
        for (auto& element : elements)
            if (element) return element->pos();
        return QPoint();
    }();

    Color color = WindowColors::StripeBackground();
    color.setAlpha(254);

    for (auto& line : ASMCodeline::line_vec)
        if (line && line->line_index % 2)
        {
            int posy = heighty * (1 + line->line_index) - scrollposy + elementpos.y() + 1;
            if (posy < -2 * heighty) continue;
            if (posy > height() + heighty) break;

            painter.fillRect(QRect(elementpos.x(), posy, width() - elementpos.x(), heighty), color);
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
    {
        int posy = heighty * (1 + line->line_index) - scrollposy;
        if (posy < -2 * heighty) continue;
        if (posy > height() + heighty) break;

        if (auto element = line->elements.at(elementtype).get())
        {
            int posx = isASM() ? 0 : std::max(0, width_cache - element->width(fm));
            element->paint(painter, posx, posy, heighty, overline);
        }
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
    tooltip << "<div style= \"white-space: nowrap;\"><table>\n<tr><th>"
            << "l:" << asm_elem->line_number << " cid:" << asm_elem->codeobj << " vaddr:0x" << std::hex
            << asm_elem->addr << std::dec << "</th>\n<th>&nbsp;|&nbsp;</th>\n<th>" << asm_elem->getStdText()
            << "</th>\n</tr>";

    auto callstack = asm_elem->callstack();
    for (auto& [file, line] : callstack)
        tooltip << "<tr>\n<td>" << file << "</td>\n<td>&nbsp;|&nbsp;</td>\n<td>" << line << "</td>\n</tr>\n";

    tooltip << "</table></div>";
    this->setToolTip(tooltip.str().c_str());
}

#include "qcodelist.moc"
