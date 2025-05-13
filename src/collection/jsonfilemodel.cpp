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

#include "jsonfilemodel.h"
#include <QBrush>
#include <QFileIconProvider>
#include <QPainter>
#include "code/sourcefile.h"
#include "mainwindow.h"

JsonFileModel::JsonFileModel(QObject* parent, QAbstractItemView* view) :
QAbstractItemModel(parent), view(view), rootNode(std::make_shared<Node>("root", QJsonValue(), nullptr))
{
    QFileIconProvider iconProvider;
    folderIcon = iconProvider.icon(QFileIconProvider::Folder);
    fileIcon = iconProvider.icon(QFileIconProvider::File);
}

void JsonFileModel::setJson(const QJsonObject& json)
{
    rootNode->children.clear();
    parseJson(json, rootNode);
    updateAllHotspotsAndLatencies(rootNode);
    endResetModel();
}

float JsonFileModel::calculateMaximumLatency() const
{
    float maxLatency = 0.0f;
    std::function<void(std::shared_ptr<Node>)> findMaxLatency = [&](std::shared_ptr<Node> node)
    {
        if (!node) return;
        maxLatency = std::max(maxLatency, static_cast<float>(node->hotspot.total_latency));
        for (auto& child : node->children) { findMaxLatency(child); }
    };
    findMaxLatency(rootNode);
    return maxLatency;
}

void JsonFileModel::parseJson(const QJsonObject& json, std::shared_ptr<Node> parent)
{
    for (auto it = json.begin(); it != json.end(); ++it)
    {
        QString key = it.key();
        QJsonValue value = it.value();

        if (value.isObject())
        {
            QString concatenatedKey = key;
            QJsonValue concatenatedValue = value;
            QJsonObject childObject = value.toObject();
            while (childObject.size() == 1 && childObject.begin().value().isObject())
            {
                concatenatedKey += "/" + childObject.begin().key();
                concatenatedValue = childObject.begin().value();
                childObject = concatenatedValue.toObject();
            }
            auto node = std::make_shared<Node>(concatenatedKey, concatenatedValue, parent);
            parent->children.append(node);
            parseJson(childObject, node);
        }
        else
        {
            auto node = std::make_shared<Node>(key, value, parent);
            parent->children.append(node);
        }
    }
}

QVariant JsonFileModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) { return QString("Source Files"); }
    return QVariant();
}

QModelIndex JsonFileModel::index(int row, int column, const QModelIndex& parent) const
{
    if (!hasIndex(row, column, parent)) return QModelIndex();

    std::shared_ptr<Node> parentNode;
    if (parent.isValid())
    {
        parentNode = std::shared_ptr<Node>(rootNode, static_cast<Node*>(parent.internalPointer()));
    }
    else { parentNode = rootNode; }
    std::shared_ptr<Node> childNode = parentNode->children.at(row);

    return childNode ? createIndex(row, column, childNode.get()) : QModelIndex();
}

QModelIndex JsonFileModel::parent(const QModelIndex& index) const
{
    if (!index.isValid()) return QModelIndex();

    Node* childNode = static_cast<Node*>(index.internalPointer());
    std::shared_ptr<Node> parentNode = childNode->parent.lock();

    if (!parentNode || parentNode == rootNode) return QModelIndex();

    std::shared_ptr<Node> grandParentNode = parentNode->parent.lock();
    if (!grandParentNode) return QModelIndex();

    int row = std::distance(
        grandParentNode->children.begin(),
        std::find_if(
            grandParentNode->children.begin(),
            grandParentNode->children.end(),
            [parentNode](const std::shared_ptr<Node>& node) { return node.get() == parentNode.get(); }
        )
    );

    return createIndex(row, 0, parentNode.get());
}

int JsonFileModel::rowCount(const QModelIndex& parent) const
{
    if (!parent.isValid()) { return rootNode->children.size(); }

    Node* parentNode = static_cast<Node*>(parent.internalPointer());
    return parentNode->children.size();
}

int JsonFileModel::columnCount(const QModelIndex& parent) const { return 1; }

void JsonFileModel::updateAllHotspotsAndLatencies(std::shared_ptr<Node> node) const
{
    if (!node) return;

    int64_t acc_latency = 0;
    for (auto& child : node->children)
    {
        updateAllHotspotsAndLatencies(child);
        acc_latency += child->hotspot.total_latency;
    }

    QWARNING(MainWindow::window && MainWindow::window->source_filetab, "No source file tab", return );

    auto* tab = MainWindow::window->source_filetab;
    auto it = tab->snap_to_filename.find(MainWindow::GetUIDir() + node->value.toString().toStdString());
    if (it != tab->snap_to_filename.end())
    {
        auto it2 = tab->files.find(it->second); // filename
        if (it2 != tab->files.end())
        {
            node->hotspot = it2->second.second->latency; // Initialize the hotspot
            acc_latency += it2->second.second->latency.total_latency;
        }
    }

    node->hotspot.total_latency = acc_latency;
}

QVariant JsonFileModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) return QVariant();

    Node* node = static_cast<Node*>(index.internalPointer());

    if (role == Qt::DisplayRole)
    {
        QString text = node->name;
        QFont font = QApplication::font();
        font.setPointSize(MainWindow::font() - 1);
        QFontMetrics fontMetrics(font);

        // Calculate the maximum text width based on the file explorer viewport width
        int maxTextWidth = view->viewport()->width() - MAX_HOTSPOT_WIDTH - 75;
        QString elidedText = fontMetrics.elidedText(text, Qt::ElideRight, maxTextWidth);
        return elidedText;
    }
    else if (role == Qt::FontRole)
    {
        QFont font = QApplication::font();
        font.setPointSize(MainWindow::font() - 1);
        return font;
    }
    else if (role == Qt::DecorationRole)
    {
        if (node->value.isObject()) { return folderIcon; }
        else { return fileIcon; }
    }
    else if (role == Qt::ToolTipRole) { return node->name; }
    else if (role == Qt::UserRole + 1) { return QVariant::fromValue(node->hotspot); }
    return QVariant();
}
