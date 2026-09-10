// MIT License
// Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.

#include "codecolumns.h"
#include <QHeaderView>
#include <QStandardItemModel>
#include <algorithm>

CodeColumns::CodeColumns(QWidget* parent) :
QWidget(parent),
header(new QHeaderView(Qt::Horizontal, this)),
model(new QStandardItemModel(this)),
body(new QWidget(this))
{
    header->setModel(model);
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setMinimumSectionSize(48);
    header->setMaximumSectionSize(4096);
    header->setStretchLastSection(false);
    header->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    header->setToolTip("Drag a column divider to resize. Double-click to restore automatic sizing.");
    connect(
        header,
        &QHeaderView::sectionResized,
        this,
        [this](int column, int, int width)
        {
            layoutColumns();
            if (!header->isSectionHidden(column)) emit columnResized(column, width);
        }
    );
    connect(header, &QHeaderView::sectionHandleDoubleClicked, this, &CodeColumns::autoSizeRequested);
}

void CodeColumns::addColumn(const QString& title, QWidget* content, QWidget* control)
{
    content->setParent(body);
    if (control)
    {
        control->setParent(header->viewport());
        control->setMinimumWidth(0);
        control->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    }
    columns.push_back({content, control});
    model->setColumnCount(static_cast<int>(columns.size()));
    model->setHeaderData(static_cast<int>(columns.size()) - 1, Qt::Horizontal, control ? QString() : title);
    layoutColumns();
}

void CodeColumns::setColumnVisible(int column, bool visible)
{
    header->setSectionHidden(column, !visible);
    layoutColumns();
}

void CodeColumns::setHeaderFontSize(int point_size)
{
    QFont font = header->font();
    if (font.pointSize() == point_size) return;
    // Keep the UI font family, but size both titles and selectors like the rows.
    font.setPointSize(point_size);
    header->setFont(font);
    layoutColumns();
}

void CodeColumns::setColumnWidth(int column, int width)
{
    header->resizeSection(column, std::clamp(width, header->minimumSectionSize(), header->maximumSectionSize()));
}

int CodeColumns::columnWidth(int column) const { return header->sectionSize(column); }
int CodeColumns::headerWidthHint(int column) const
{
    const auto* control = columns.at(column).control;
    return control ? control->sizeHint().width() + 10 : header->sectionSizeHint(column);
}
int CodeColumns::bodyHeight() const { return body->height(); }

void CodeColumns::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    layoutColumns();
}

void CodeColumns::layoutColumns()
{
    const int previous_height = body->height();
    int header_height = header->sizeHint().height();
    for (const auto& column : columns)
        if (column.control) header_height = std::max(header_height, column.control->sizeHint().height() + 4);
    setMinimumWidth(header->length());
    header->setGeometry(0, 0, width(), header_height);
    body->setGeometry(0, header_height, width(), std::max(0, height() - header_height));
    for (int i = 0; i < static_cast<int>(columns.size()); ++i)
    {
        const auto& column = columns[i];
        const bool visible = !header->isSectionHidden(i);
        column.content->setVisible(visible);
        if (column.control) column.control->setVisible(visible);
        if (!visible) continue;
        const int left = header->sectionViewportPosition(i);
        const int size = header->sectionSize(i);
        column.content->setGeometry(left, 0, size, body->height());
        // Leave the right edge free for the header's native resize handle.
        if (column.control) column.control->setGeometry(left + 2, 2, std::max(0, size - 10), header_height - 4);
    }
    if (body->height() != previous_height) emit bodyHeightChanged();
}
