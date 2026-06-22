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
#include "analysis/annotation.h"
#include "config/appconfig.h"
#include "data/datastore.h"
#include "data/wavemanager.h"
#include "graphics/canvas.h"
#include "labelminimap.h"
#include "mainwindow.h"
#include "sourcefile.h"
#include "util/custom_layouts.h"

#define ASM_MAX_LINE_WIDTH 420

int QCodelist::line_height = 20;
QCodelist* QCodelist::singleton = nullptr;

// Built-in (non-annotation) View rows. Index in this array == row index in the
// dropdown; annotation rows are appended after these.
static const std::array<std::pair<const char*, Canvas::DrawType>, 2> kBuiltinRows = {
    {
     {"View: Waitcnt", Canvas::DrawType::DrawArrows},
     {"Branch targets", Canvas::DrawType::DrawBranch},
     }
};

// Sentinel value stashed in Qt::UserRole for built-in rows; annotation rows
// store their Category id as a QString.
static constexpr int kBuiltinUserRole = 0; // value isn't read; we check QVariant type

static bool hiddenLatencyAnalysisAvailable()
{
    auto* mw = MainWindow::window;
    return mw && mw->data_store && mw->data_store->hidden_latency_analyzed;
}

class DrawTypeSelector : public QComboBox
{
    Q_OBJECT;
    set_tracked();
    using Super = QComboBox;

public:
    DrawTypeSelector(QCodelist* _parent);
    void rebuildAnnotationRows();
    void onIndexChanged(int index);

    QCodelist* parent = nullptr;

private:
    bool m_rebuilding = false;
};

std::array<std::string, (int) CyclesLabel::Strategy::LAST> strategy_names = {
    "Latency: Sum all",
    "Latency: Mean all",
    "Latency: Iteration",
    "Latency: Sum Wave",
    "Latency: Mean Wave",
    "Latency: Max Wave"};

DrawTypeSelector::DrawTypeSelector(QCodelist* _parent) : parent(_parent)
{
    for (const auto& [label, type] : kBuiltinRows)
    {
        addItem(QString(label));
        setItemData(count() - 1, kBuiltinUserRole, Qt::UserRole);
    }

    setCurrentIndex(0);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

    QObject::connect(this, qOverload<int>(&QComboBox::currentIndexChanged), this, &DrawTypeSelector::onIndexChanged);
}

void DrawTypeSelector::rebuildAnnotationRows()
{
    m_rebuilding = true;

    while (count() > static_cast<int>(kBuiltinRows.size())) removeItem(count() - 1);

    const bool hiddenLatencyAvailable = hiddenLatencyAnalysisAvailable();
    const int enabled = static_cast<int>(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    const int disabled = static_cast<int>(Qt::NoItemFlags);
    int selectRow = -1;
    for (const Annotation::Category* cat : Annotation::Registry::instance().categories())
    {
        addItem(QString::fromStdString(cat->display_name));
        const int row = count() - 1;
        const bool rowEnabled = cat->id != "nonhidden_latency" || hiddenLatencyAvailable;
        setItemData(row, QString::fromStdString(cat->id), Qt::UserRole);
        model()->setData(model()->index(row, 0), rowEnabled ? enabled : disabled, Qt::UserRole - 1);

        if (rowEnabled && cat->id == Canvas::active_annotation_id) selectRow = row;
    }

    if (selectRow >= 0)
    {
        if (currentIndex() != selectRow) setCurrentIndex(selectRow);
    }
    else if (Canvas::drawtype == Canvas::DrawType::Annotation)
    {
        // Active annotation was removed — fall back to the first built-in so
        // the canvas paints something coherent instead of going blank.
        Canvas::drawtype = kBuiltinRows[0].second;
        Canvas::active_annotation_id.clear();
        if (currentIndex() != 0) setCurrentIndex(0);
        if (parent && parent->connector)
        {
            parent->connector->updateGeometry();
            parent->connector->update();
        }
    }

    m_rebuilding = false;
}

void DrawTypeSelector::onIndexChanged(int index)
{
    if (m_rebuilding || index < 0) return;

    const QVariant data = itemData(index, Qt::UserRole);
    if (data.userType() == QMetaType::QString)
    {
        Canvas::drawtype = Canvas::DrawType::Annotation;
        Canvas::active_annotation_id = data.toString().toStdString();
    }
    else if (index < static_cast<int>(kBuiltinRows.size()))
    {
        Canvas::drawtype = kBuiltinRows[index].second;
        Canvas::active_annotation_id.clear();
    }

    if (parent && parent->connector)
    {
        parent->connector->updateGeometry();
        parent->connector->update();
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

void QCodelist::setDrawType(Canvas::DrawType type)
{
    QWARNING(drawselector, "invalid selector", return );
    // Only built-in DrawTypes are addressable this way; annotations go through
    // selectAnnotation(id).
    for (size_t i = 0; i < kBuiltinRows.size(); ++i)
        if (kBuiltinRows[i].second == type)
        {
            drawselector->setCurrentIndex(static_cast<int>(i));
            return;
        }
}

void QCodelist::selectAnnotation(const std::string& id)
{
    QWARNING(drawselector, "invalid selector", return );

    // Only flip to annotation mode if the category actually exists.
    if (!Annotation::Registry::instance().find(id)) return;

    Canvas::drawtype = Canvas::DrawType::Annotation;
    Canvas::active_annotation_id = id;

    for (int i = 0; i < drawselector->count(); ++i)
    {
        const QVariant data = drawselector->itemData(i, Qt::UserRole);
        if (data.userType() == QMetaType::QString && data.toString().toStdString() == id)
        {
            if (drawselector->currentIndex() != i) drawselector->setCurrentIndex(i);
            return;
        }
    }
}

void QCodelist::refreshAnnotations()
{
    if (drawselector) drawselector->rebuildAnnotationRows();
    if (connector)
    {
        connector->updateGeometry();
        connector->update();
    }
}

void QCodelist::refreshLatencyAnnotations()
{
    max_sqtt_latency = 1;
    max_pcs_latency = 1;
    for (const auto& codeline : ASMCodeline::line_vec)
    {
        if (!codeline) continue;
        max_sqtt_latency = std::max(max_sqtt_latency, codeline->hotspot.sqtt.total(HorizontalHotspot::show_idle_time));
        max_pcs_latency = std::max(max_pcs_latency, codeline->hotspot.pcs.total());
    }

    HorizontalHotspot::PublishCategories(max_sqtt_latency, max_pcs_latency);
    scheduleRedraw();
}

void QCodelist::Populate(const std::vector<CodeData>& code)
{
    QPalette pal = QPalette();
    pal.setColor(QPalette::Window, WindowColors::Background());
    this->setPalette(pal);

    ASMCodeline::Populate(code);

    // Repopulating rebuilds ASMCodeline with fresh line_index values, so any
    // externally-published category keyed by the old indices (e.g. Memory
    // Latency from the latency dialog) is now stale and must be dropped. The
    // built-in categories are re-cleared and republished by PublishCategories
    // below.
    Annotation::Registry::instance().clear("memory_latency");

    // Build/refresh the hotspot annotation categories from the per-line data
    // we just populated. This fires the registry listener which rebuilds the
    // dropdown rows.
    refreshLatencyAnnotations();

    QWARNING(drawselector, "invalid selector", return );

    auto* model = drawselector->model();
    auto* view = qobject_cast<QListView*>(drawselector->view());
    QWARNING(model && view, "no model/view", return );

    int enabled = static_cast<int>(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    // Gate the two built-in Waitcnt/Branch rows on SQTT data presence.
    for (int idx : {0, 1})
    {
        int flags = HorizontalHotspot::is_sqtt_enabled ? enabled : static_cast<int>(Qt::NoItemFlags);
        model->setData(model->index(idx, 0), flags, Qt::UserRole - 1);
        view->setRowHidden(idx, !HorizontalHotspot::is_sqtt_enabled);
    }

    // Default to Inst Latency when PCS data is available (matches prior UX).
    if (HorizontalHotspot::is_pcs_enabled) selectAnnotation("inst_latency");

    // Update column visibility based on data availability flags
    updateColumnVisibility();

    // Update label minimap
    if (MainWindow::window && MainWindow::window->label_minimap) MainWindow::window->label_minimap->Populate();
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
