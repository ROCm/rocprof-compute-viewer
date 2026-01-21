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

#include "labelminimap.h"
#include <QHeaderView>
#include <QLabel>
#include <QScrollBar>
#include <QVBoxLayout>
#include <algorithm>
#include <cstring>
#include <sstream>
#include "config/config.hpp"
#include "mainwindow.h"
#include "qcodelist.h"

LabelMinimap::LabelMinimap(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(2);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* titleLabel = new QLabel("Labels", this);
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    table = new QTableWidget(this);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(18);
    table->setShowGrid(false);
    table->setAlternatingRowColors(false);

    layout->addWidget(table);
    setLayout(layout);

    connect(table, &QTableWidget::cellClicked, this, &LabelMinimap::onLabelClicked);
}

LabelMinimap::~LabelMinimap() {}

bool LabelMinimap::IsLabel(const std::string& text)
{
    if (text.empty()) return false;

    // Find the first non-whitespace character
    size_t start = 0;
    while (start < text.size() && (text[start] == ' ' || text[start] == '\t')) start++;

    if (start >= text.size()) return false;

    // Check if starts with "label" (after whitespace)
    if (text.size() - start >= 5 && text.compare(start, 5, "label") == 0) return true;

    // Check if starts with "; _" (after whitespace)
    if (text.size() - start >= 3 && text.compare(start, 3, "; _") == 0) return true;

    return false;
}

std::string LabelMinimap::ExtractLabelName(const std::string& text)
{
    // Find the first non-whitespace character
    size_t start = 0;
    while (start < text.size() && (text[start] == ' ' || text[start] == '\t')) start++;

    if (start >= text.size()) return text;

    if (text.size() - start >= 3 && text.compare(start, 2, "; ") == 0)
    {
        return text.substr(start + 2); // Return everything after "; "
    }

    // Otherwise return from the start of non-whitespace
    return text.substr(start);
}

int64_t LabelMinimap::calculateLabelLatency(size_t labelIndex) const
{
    int64_t totalLatency = 0;

    // Sum latency from instructions following this label until next label or end
    for (size_t i = labelIndex + 1; i < ASMCodeline::line_vec.size(); i++)
    {
        auto& line = ASMCodeline::line_vec[i];
        if (!line) continue;

        auto* asmElem = dynamic_cast<ASMLine*>(line->elements.at(ASMCodeline::Element::EASM).get());
        if (!asmElem) continue;

        // Stop at next label
        if (IsLabel(asmElem->getStdText())) break;

        // Get latency from the hotspot data
        totalLatency += line->hotspot.sqtt.latency;
    }

    return totalLatency;
}

int64_t LabelMinimap::calculateLabelPCSamples(size_t labelIndex) const
{
    int64_t totalSamples = 0;

    // Sum PC samples from instructions following this label until next label or end
    for (size_t i = labelIndex + 1; i < ASMCodeline::line_vec.size(); i++)
    {
        auto& line = ASMCodeline::line_vec[i];
        if (!line) continue;

        auto* asmElem = dynamic_cast<ASMLine*>(line->elements.at(ASMCodeline::Element::EASM).get());
        if (!asmElem) continue;

        // Stop at next label
        if (IsLabel(asmElem->getStdText())) break;

        // Get PC samples from the hotspot data
        totalSamples += line->hotspot.pcs.latency;
    }

    return totalSamples;
}

void LabelMinimap::Populate()
{
    Clear();

    int64_t totalLatency = 0;
    int64_t totalPCSamples = 0;

    // First pass: find all labels and calculate totals
    for (size_t i = 0; i < ASMCodeline::line_vec.size(); i++)
    {
        auto& line = ASMCodeline::line_vec[i];
        if (!line) continue;

        auto* asmElem = dynamic_cast<ASMLine*>(line->elements.at(ASMCodeline::Element::EASM).get());
        if (!asmElem) continue;

        const std::string& text = asmElem->getStdText();

        if (IsLabel(text))
        {
            LabelInfo info;
            info.name = text;
            info.address = asmElem->addr;
            info.codeobj = asmElem->codeobj;
            info.line_index = line->line_index;
            info.latency = calculateLabelLatency(i);
            info.pcsamples = calculateLabelPCSamples(i);
            info.latency_percent = 0.0;
            info.pcsamples_percent = 0.0;

            labels.push_back(info);
            totalLatency += info.latency;
            totalPCSamples += info.pcsamples;
        }
    }

    // Calculate percentages
    for (auto& label : labels)
    {
        if (totalLatency > 0) label.latency_percent = (static_cast<double>(label.latency) / totalLatency) * 100.0;
        if (totalPCSamples > 0)
            label.pcsamples_percent = (static_cast<double>(label.pcsamples) / totalPCSamples) * 100.0;
    }

    // Determine which columns to show
    bool showLatency = (totalLatency > 0);
    bool showPCSamples = (totalPCSamples > 0);

    // Build column headers dynamically
    QStringList headers;
    headers << "Name";
    if (showLatency) headers << "Lat %";
    if (showPCSamples) headers << "PCS %";
    headers << "Obj"
            << "Addr";

    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);

    // Set up column resize modes
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int col = 1; col < headers.size(); col++)
        table->horizontalHeader()->setSectionResizeMode(col, QHeaderView::ResizeToContents);

    table->setRowCount(static_cast<int>(labels.size()));

    // Get stripe color from config
    Color stripeColor = WindowColors::StripeBackground();
    stripeColor.setAlpha(254);

    for (size_t i = 0; i < labels.size(); i++)
    {
        const auto& label = labels[i];
        int col = 0;
        QList<QTableWidgetItem*> rowItems;

        // Name column
        std::string displayName = ExtractLabelName(label.name);
        auto* nameItem = new QTableWidgetItem(QString::fromStdString(displayName));
        table->setItem(static_cast<int>(i), col++, nameItem);
        rowItems << nameItem;

        // Latency percentage column (optional)
        if (showLatency)
        {
            auto* latItem = new QTableWidgetItem(QString::number(label.latency_percent, 'f', 1));
            latItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table->setItem(static_cast<int>(i), col++, latItem);
            rowItems << latItem;
        }

        // PC samples percentage column (optional)
        if (showPCSamples)
        {
            auto* pcsItem = new QTableWidgetItem(QString::number(label.pcsamples_percent, 'f', 1));
            pcsItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table->setItem(static_cast<int>(i), col++, pcsItem);
            rowItems << pcsItem;
        }

        // Codeobj column
        auto* objItem = new QTableWidgetItem(QString::number(label.codeobj));
        objItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table->setItem(static_cast<int>(i), col++, objItem);
        rowItems << objItem;

        // Address column (hex)
        std::stringstream addrSS;
        addrSS << std::hex << label.address;
        auto* addrItem = new QTableWidgetItem(QString::fromStdString(addrSS.str()));
        addrItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table->setItem(static_cast<int>(i), col++, addrItem);
        rowItems << addrItem;

        // Apply stripe coloring for odd rows
        if (i % 2 == 1)
        {
            for (auto* item : rowItems) item->setBackground(stripeColor);
        }
    }

    // Force table to update
    table->viewport()->update();
    table->update();
}

void LabelMinimap::Clear()
{
    labels.clear();
    table->setRowCount(0);
}

void LabelMinimap::onLabelClicked(int row, int column)
{
    (void) column; // unused

    if (row < 0 || row >= static_cast<int>(labels.size())) return;

    const auto& label = labels[row];
    int lineIndex = label.line_index;

    // Scroll the QCodelist to the label
    if (auto* codelist = QCodelist::singleton)
    {
        int scrollPos = QCodelist::lineheight() * lineIndex;

        if (codelist->scrollbar)
        {
            int viewHeight = codelist->height();
            int targetPos = std::max(0, scrollPos - viewHeight / 3);
            codelist->scrollbar->setValue(targetPos);
        }

        codelist->Highlight(lineIndex, lineIndex, true);
    }
}
