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
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <vector>
#include "analysis/annotation.h"
#include "codecolumns.h"
#include "config/appconfig.h"
#include "graphics/canvas.h"

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

    const bool hiddenLatencyAvailable = parent->context().hiddenLatencyAvailable();
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

    parent->refreshLayout();
}

void QCodelist::refreshLayout()
{
    const QFont code_font = host.codeFont(font());
    const int top_row = scrollposy / line_height;
    const int previous_height = line_height;
    line_height = QFontMetrics(code_font).height();
    if (code_font != elements.at(Element::EASM)->font()) clearRowInteraction();

    for (auto& elem : elements)
        if (elem)
        {
            elem->setFont(code_font);
            elem->InvalidateCache();
            elem->update();
        }

    updateAutomaticColumnWidths();
    updateScrollRange();
    if (line_height != previous_height) scrollbar->setValue(top_row * line_height);
    update();
    if (connector) connector->update();
}

QCodelist::QCodelist(Isa::Context& context, QWidget* parent) : QWidget(parent), host(context)
{
    singleton = this;
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    columns = new CodeColumns(this);
    layout->addWidget(columns);

    connector = new Canvas();
    drawselector = new DrawTypeSelector(this);
    columns->addColumn("View", connector, drawselector);

    static const std::array<const char*, Element::ENUMTYPES> titles = {
        "Instruction", "Hitcount", "Latency", "Idle", "Samples", "Stalls", "Issued", "Codeobj", "Vaddr", "Source link"};
    elements.at(Element::EASM) = new QASMElementList(*this);
    for (int e = 0; e < Element::ENUMTYPES; e++)
    {
        if (e != Element::EASM) elements.at(e) = new QElementList(Element(e), rows);
        QWidget* control = nullptr;
        if (e == Element::EASM)
            control = createInstructionHeader();
        else if (e == Element::ELATENCY)
            control = new CycleModeSelector(this);
        columns->addColumn(titles[e], elements.at(e), control);
    }

    scrollbar = new QScrollBar(Qt::Vertical, this);
    scrollbar->hide(); // MainWindow places it beside the enclosing horizontal scroll area.
    connect(scrollbar, &QScrollBar::valueChanged, this, &QCodelist::onScroll);
    refreshLayout();
    updateColumnVisibility();
    connect(
        columns,
        &CodeColumns::columnResized,
        this,
        [this](int column, int width)
        {
            if (!updating_columns) AppConfig::getInstance().setColumnWidth(column - 1, width);
            updateScrollRange();
        }
    );
    connect(columns, &CodeColumns::autoSizeRequested, this, &QCodelist::autoSizeColumn);
    connect(columns, &CodeColumns::bodyHeightChanged, this, &QCodelist::updateScrollRange);

    setAttribute(Qt::WA_OpaquePaintEvent);
}

QCodelist::~QCodelist()
{
    if (singleton == this) singleton = nullptr;
}

QWidget* QCodelist::createInstructionHeader()
{
    auto* header = new QWidget();
    auto* layout = new QHBoxLayout(header);
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    layout->setContentsMargins(2, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(new QLabel("Instruction"), 1);
    folding_selector = new QComboBox(header);
    folding_selector->setObjectName("isaFoldingMode");
    folding_selector->addItems({"No folding", "Fold by labels"});
    folding_selector->setCurrentIndex(rows.foldingEnabled() ? 1 : 0);
    folding_selector->setToolTip(
        "Fold sections using the triangle beside each label. No folding keeps every instruction visible."
    );
    folding_selector->setAccessibleName("Instruction folding mode");
    connect(
        folding_selector,
        qOverload<int>(&QComboBox::currentIndexChanged),
        this,
        [this](int index) { setFoldingEnabled(index == 1); }
    );
    layout->addWidget(folding_selector);

    expand_sections = new QToolButton(header);
    expand_sections->setObjectName("isaExpandAll");
    expand_sections->setText("+");
    expand_sections->setAutoRaise(true);
    expand_sections->setToolTip("Expand all sections");
    expand_sections->setAccessibleName("Expand all sections");
    expand_sections->setEnabled(rows.foldingEnabled());
    connect(expand_sections, &QToolButton::clicked, this, &QCodelist::expandAll);
    layout->addWidget(expand_sections);
    return header;
}

void QCodelist::updateAutomaticColumnWidths()
{
    const QScopedValueRollback<bool> guard(updating_columns, true);
    // Header controls contribute to automatic widths, so update their metrics
    // before measuring. Only explicit user resizes should persist pixel widths.
    columns->setHeaderFontSize(elements.at(Element::EASM)->font().pointSize());
    const auto& config = AppConfig::getInstance();
    for (int column = 0; column <= Element::ENUMTYPES; ++column)
    {
        const int saved = config.getColumnWidth(column - 1);
        if (saved > 0)
            columns->setColumnWidth(column, saved);
        else
            autoSizeColumn(column);
    }
}

void QCodelist::autoSizeColumn(int column)
{
    int width = column == 0 ? std::max(connector->sizeHint().width(), 180) : elements.at(column - 1)->contentWidth();
    width = std::max(width, columns->headerWidthHint(column));
    {
        const QScopedValueRollback<bool> guard(updating_columns, true);
        columns->setColumnWidth(column, width);
    }
    if (!updating_columns) AppConfig::getInstance().setColumnWidth(column - 1, -1);
}

void QCodelist::updateScrollRange()
{
    if (!scrollbar || !columns) return;
    scrollbar->setSingleStep(line_height);
    scrollbar->setPageStep(columns->bodyHeight());
    scrollbar->setRange(0, std::max(0, rows.count() * line_height - columns->bodyHeight()));
}

void QCodelist::clearRowInteraction()
{
    for (auto* element : elements)
        if (element)
        {
            element->clearHover();
            element->clearHighlight();
        }
}

void QCodelist::rowsChanged(int anchor_line, int offset)
{
    while (anchor_line >= 0 && rows.rowOf(anchor_line) < 0) --anchor_line;
    updateScrollRange();
    scrollbar->setValue(std::max(0, rows.rowOf(anchor_line)) * line_height + offset);
    onScroll(scrollbar->value());
}

void QCodelist::setFoldingEnabled(bool enabled)
{
    if (enabled == rows.foldingEnabled()) return;
    const int anchor = rows.lineAt(scrollposy / line_height);
    const int offset = scrollposy % line_height;
    clearRowInteraction();
    rows.setFoldingEnabled(enabled);
    folding_selector->setCurrentIndex(enabled ? 1 : 0);
    expand_sections->setEnabled(enabled);
    rowsChanged(anchor, offset);
}

void QCodelist::toggleSection(int label)
{
    const int anchor = rows.lineAt(scrollposy / line_height);
    const int offset = scrollposy % line_height;
    clearRowInteraction();
    if (rows.toggle(label)) rowsChanged(anchor, offset);
}

void QCodelist::expandAll()
{
    const int anchor = rows.lineAt(scrollposy / line_height);
    const int offset = scrollposy % line_height;
    clearRowInteraction();
    rows.expandAll();
    rowsChanged(anchor, offset);
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

    const QScopedValueRollback<bool> guard(updating_columns, true);
    columns->setColumnVisible(elem + 1, visible);

    updateGeometry();
    update();
}

void QCodelist::updateColumnVisibility()
{
    // Re-apply visibility settings from config, which will also apply data-type filters
    AppConfig& config = AppConfig::getInstance();
    for (int e = Element::EHIT; e < Element::ENUMTYPES; e++)
        setColumnVisibility(static_cast<Element>(e), config.getColumnVisible(e, e != Element::ESOURCEREF));
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
    refreshLayout();
}

void QCodelist::Populate(const std::vector<CodeData>& code)
{
    clearRowInteraction();
    ASMCodeline::Populate(code);
    std::vector<std::string_view> instructions;
    instructions.reserve(ASMCodeline::line_vec.size());
    for (const auto& line : ASMCodeline::line_vec)
        instructions.push_back(line->elements.at(Element::EASM)->getStdText());
    rows.reset(instructions);
    onScroll(scrollbar->value());

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

    host.listingChanged();
}

void QCodelist::resizeEvent(QResizeEvent* event)
{
    Super::resizeEvent(event);
    updateScrollRange();
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

    if (lbegin < 0 || lend < lbegin || lend >= static_cast<int>(ASMCodeline::line_vec.size())) return;
    if (bIntoView)
    {
        clearRowInteraction();
        if (rows.reveal(lbegin, lend)) rowsChanged(lbegin, 0);
    }
    const int begin_row = rows.rowOf(lbegin);
    const int end_row = rows.rowOf(lend);
    if (begin_row < 0 || end_row < 0) return;
    auto scroll = elem->Highlight(color, begin_row, end_row);

    if (scroll && bIntoView) scrollbar->setValue(*scroll);
}

void QCodelist::wheelEvent(QWheelEvent* event)
{
    const int delta = event->pixelDelta().isNull() ? event->angleDelta().y() : event->pixelDelta().y();
    if (event->modifiers().testFlag(Qt::ShiftModifier) || delta == 0)
    {
        event->ignore(); // Let the enclosing scroll area handle horizontal scrolling.
        return;
    }
    scrollbar->setValue(scrollbar->value() - delta);
    event->accept();
}

void QCodelist::paintEvent(QPaintEvent* event)
{
    this->Super::paintEvent(event);
    QPainter painter(this);
    painter.fillRect(rect(), WindowColors::Background());
}

#include "qcodelist.moc"
