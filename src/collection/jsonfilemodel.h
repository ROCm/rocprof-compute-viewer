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

#include <QAbstractItemModel>
#include <QIcon>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <memory>
#include "code/sourcefile.h"

#define MAX_TEXT_WIDTH    100
#define MAX_HOTSPOT_WIDTH 50

class JsonFileModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    JsonFileModel(QObject* parent = nullptr, QAbstractItemView* view = nullptr);
    void setJson(const QJsonObject& json);

    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& index) const override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    struct Node
    {
        QString name;
        QJsonValue value;
        std::weak_ptr<Node> parent;
        QList<std::shared_ptr<Node>> children;

        mutable HorizontalHotspot hotspot;

        Node(const QString& name, const QJsonValue& value, std::shared_ptr<Node> parent) :
        name(name), value(value), parent(parent)
        {}

        ~Node() = default;
    };

    void updateAllHotspotsAndLatencies(std::shared_ptr<Node> node) const;

    std::shared_ptr<Node> rootNode;
    float calculateMaximumLatency() const;

private:

    QIcon folderIcon;
    QIcon fileIcon;
    QAbstractItemView* view;

    void parseJson(const QJsonObject& json, std::shared_ptr<Node> parent);
};
