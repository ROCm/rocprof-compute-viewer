// MIT License
// Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.

#include <QMouseEvent>
#include <QPainter>
#include "qcodelist.h"

namespace
{
constexpr int fold_gutter = 18;
constexpr int default_max_width = 420;
} // namespace

QElementList::QElementList(ASMCodeline::Element element, const Isa::Rows& rows) : elementtype(element), rows(rows)
{
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
}

int QElementList::contentWidth()
{
    const auto font = this->font();
    if (width_cache >= 0 && width_font == font) return width_cache;
    width_font = font;
    QFontMetrics fm(font);
    int result = 0;
    for (const auto& line : ASMCodeline::line_vec)
        if (const auto& element = line->elements.at(elementtype)) result = std::max(result, element->width(fm));
    const int padding = 8 + 2 * (fm.height() - fm.overlinePos());
    width_cache = std::clamp(result + padding + (isASM() ? fold_gutter : 0), isASM() ? 160 : 48, default_max_width);
    return width_cache;
}

void QElementList::paintEvent(QPaintEvent* event)
{
    Super::paintEvent(event);
    const int heighty = line_height();
    QPainter painter(this);
    QFontMetrics fm(painter.font());
    const int overline = fm.height() - fm.overlinePos();
    const auto [first, end] = rows.visibleRange(scrollposy, height(), heighty);

    painter.fillRect(rect(), WindowColors::Background());
    for (int row = first; row < end; ++row)
        if (row % 2)
            painter.fillRect(0, row * heighty - scrollposy, width(), heighty, WindowColors::StripeBackground());

    if (timer.timer)
    {
        const int top = highlight_begin * heighty - scrollposy;
        const int bottom = (highlight_end + 1) * heighty - scrollposy;
        QColor color = highlightColor;
        color.setAlphaF(std::clamp(1.0f - timer.vis, 0.0f, 1.0f));
        painter.fillRect(0, top, width(), bottom - top, color);
    }

    painter.setPen(WindowColors::textColor());
    for (int row = first; row < end; ++row)
    {
        const int line = rows.lineAt(row);
        auto* element = getelement(line);
        if (!element) continue;
        const int posy = (row + 1) * heighty - scrollposy;
        const int posx = isASM() ? (rows.foldingEnabled() ? fold_gutter : 0)
                                 : std::max(0, width() - element->width(fm) - 2 * overline - 4);
        element->paint(painter, posx, posy, heighty, overline);

        if (!isASM() || !rows.foldingEnabled()) continue;
        const auto* section = rows.sectionAt(line);
        if (!section || section->end <= line + 1) continue;
        const int middle = posy - heighty / 2;
        QPolygon triangle;
        if (section->collapsed)
            triangle << QPoint(5, middle - 4) << QPoint(5, middle + 4) << QPoint(10, middle);
        else
            triangle << QPoint(3, middle - 2) << QPoint(11, middle - 2) << QPoint(7, middle + 3);
        painter.setBrush(WindowColors::textColor());
        painter.drawPolygon(triangle);
        if (section->collapsed)
            painter.drawText(
                posx + element->width(fm) + 2 * overline,
                posy - overline,
                QString("  [%1 hidden]").arg(section->end - line - 1)
            );
    }
}

LineElement* QElementList::getelement(int index)
{
    if (index < 0 || index >= static_cast<int>(ASMCodeline::line_vec.size())) return nullptr;
    return ASMCodeline::line_vec[index]->elements.at(elementtype).get();
}

int QElementList::getLineIndex(int posy) { return rows.lineAt(Super::getLineIndex(posy)); }
int QElementList::line_height() { return QCodelist::lineheight(); }
QSize QElementList::sizeHint() const { return QSize(std::max(width_cache, isASM() ? 160 : 48), 0); }

QASMElementList::QASMElementList(QCodelist& viewer) : Super(ASMCodeline::EASM, viewer.rowMapping()), viewer(viewer)
{
    setAttribute(Qt::WA_AlwaysShowToolTips, true);
}

void QASMElementList::mousePressEvent(QMouseEvent* event)
{
    const int line = getLineIndex(event->pos().y());
    if (event->button() == Qt::LeftButton && rows.foldingEnabled() && event->pos().x() < fold_gutter &&
        rows.sectionAt(line))
    {
        viewer.toggleSection(line);
        event->accept();
        return;
    }
    Super::mousePressEvent(event);
}

void QASMElementList::mouseMoveEvent(QMouseEvent* event)
{
    Super::mouseMoveEvent(event);
    const int index = getLineIndex(event->pos().y());
    const auto* section = rows.foldingEnabled() ? rows.sectionAt(index) : nullptr;
    if (section && section->end > index + 1 && event->pos().x() < fold_gutter)
    {
        setCursor(Qt::PointingHandCursor);
        setToolTip(QString("%1 section (%2 instructions)")
                       .arg(section->collapsed ? "Expand" : "Collapse")
                       .arg(section->end - index - 1));
        return;
    }
    unsetCursor();
    const auto* element = dynamic_cast<ASMLine*>(getelement(index));
    if (!element)
    {
        setToolTip({});
        return;
    }

    // Source and ISA text may contain '<', '&', etc.; never treat it as markup.
    QString tooltip = QString("<div style=\"white-space: nowrap;\"><table><tr><th>l:%1 cid:%2 "
                              "vaddr:0x%3</th><th>&nbsp;|&nbsp;</th><th>%4</th></tr>")
                          .arg(element->line_number)
                          .arg(element->codeobj)
                          .arg(element->addr, 0, 16)
                          .arg(element->getText().toHtmlEscaped());
    for (const auto& [file, line] : element->callstack())
        tooltip += QString("<tr><td>%1</td><td>&nbsp;|&nbsp;</td><td>%2</td></tr>")
                       .arg(QString::fromStdString(file).toHtmlEscaped(), QString::fromStdString(line).toHtmlEscaped());
    setToolTip(tooltip + "</table></div>");
}
