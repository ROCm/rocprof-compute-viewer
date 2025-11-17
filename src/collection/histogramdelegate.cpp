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

#include "histogramdelegate.h"
#include <QApplication>
#include <QPainter>
#include "../code/sourcefile.h"
#include "collection/jsonfilemodel.h"

HistogramDelegate::HistogramDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

void HistogramDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QStyledItemDelegate::paint(painter, option, index);

    const JsonFileModel* model = qobject_cast<const JsonFileModel*>(index.model());
    QWARNING(model, "Invalid model", return );

    // Skip drawing hotspots for folders
    JsonFileModel::Node* node = static_cast<JsonFileModel::Node*>(index.internalPointer());
    if (!node || node->value.isObject() || node->children.size() > 0) { return; }

    // Update all hotspots on the fly
    model->updateAllHotspotsAndLatencies(model->rootNode);

    // Calculate the maximum latency dynamically
    float maximum_latency = model->calculateMaximumLatency();
    QWARNING(maximum_latency != 0.0f, "Maximum latency is zero", return );

    // Retrieve the HorizontalHotspot data from the model
    QVariant variant = index.data(Qt::UserRole + 1);
    QWARNING(variant.isValid(), "No HorizontalHotspot data found ", return );

    HorizontalHotspot hotspot = variant.value<HorizontalHotspot>();

    QRect rect = option.rect;
    int barHeight = static_cast<int>(rect.height() * 0.8);
    int verticalOffset = (rect.height() - barHeight) / 2;

    // TODO: PC sampling
    float pxratio = maximum_latency;

    painter->save();
    hotspot.paint(*painter, rect.right(), rect.top() + verticalOffset, barHeight, pxratio, pxratio, HorizontalHotspot::DrawFormat::DRAWTYPE, true);
    painter->restore();
}

QSize HistogramDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    size.setHeight(size.height() + 4);
    return size;
}
