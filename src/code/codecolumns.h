// MIT License
// Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.

#pragma once

#include <QWidget>
#include <vector>

// A native resizable header above one painted widget per column. There are no
// per-row widgets or model items; the enclosing QScrollArea handles overflow.
class CodeColumns : public QWidget
{
    Q_OBJECT
public:
    explicit CodeColumns(QWidget* parent = nullptr);
    void addColumn(const QString& title, QWidget* content, QWidget* control = nullptr);
    void setHeaderFontSize(int point_size);
    void setColumnVisible(int column, bool visible);
    void setColumnWidth(int column, int width);
    int columnWidth(int column) const;
    int headerWidthHint(int column) const;
    int bodyHeight() const;

signals:
    void columnResized(int column, int width);
    void autoSizeRequested(int column);
    void bodyHeightChanged();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void layoutColumns();
    class QHeaderView* header;
    class QStandardItemModel* model;
    QWidget* body;
    struct Column
    {
        QWidget* content;
        QWidget* control;
    };
    std::vector<Column> columns;
};
