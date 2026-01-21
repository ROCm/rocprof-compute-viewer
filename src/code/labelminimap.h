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

#pragma once

#include <QScrollArea>
#include <QTableWidget>
#include <QWidget>
#include <string>
#include <vector>
#include "asmcode.h"
#include "util/memtracker.h"

/**
 * @brief Data structure representing a label in the code
 */
struct LabelInfo
{
    std::string name;         ///< Label name
    int64_t address;          ///< Virtual address
    int64_t codeobj;          ///< Code object ID
    int64_t latency;          ///< Total latency for instructions following this label
    int64_t pcsamples;        ///< Total PC samples for instructions following this label
    int line_index;           ///< Line index in ASMCodeline::line_vec
    double latency_percent;   ///< Latency as percentage of total
    double pcsamples_percent; ///< PC samples as percentage of total
};

/**
 * @brief Widget that displays a minimap of all labels in the code
 *
 * Labels are identified by text starting with "label" or "; _"
 * Each entry shows: name, address, codeobj, and latency percentage
 * Clicking an entry scrolls the code view to that label
 */
class LabelMinimap : public QWidget
{
    Q_OBJECT;
    set_tracked();

public:
    explicit LabelMinimap(QWidget* parent = nullptr);
    virtual ~LabelMinimap();

    /**
     * @brief Populate the minimap from the current ASMCodeline data
     */
    void Populate();

    /**
     * @brief Clear all labels from the minimap
     */
    void Clear();

    /**
     * @brief Check if a text line represents a label
     * @param text The instruction text to check
     * @return true if the text is a label (starts with "label" or "; _")
     */
    static bool IsLabel(const std::string& text);

    /**
     * @brief Extract the clean label name from a label line
     * @param text The full label text
     * @return The label name without prefixes like "; "
     */
    static std::string ExtractLabelName(const std::string& text);

private slots:
    void onLabelClicked(int row, int column);

private:
    /**
     * @brief Calculate latency for a label (sum of following instructions until next label)
     * @param labelIndex Index in line_vec where the label is
     * @return Total latency sum for instructions following this label
     */
    int64_t calculateLabelLatency(size_t labelIndex) const;

    /**
     * @brief Calculate PC samples for a label (sum of following instructions until next label)
     * @param labelIndex Index in line_vec where the label is
     * @return Total PC samples for instructions following this label
     */
    int64_t calculateLabelPCSamples(size_t labelIndex) const;

    QTableWidget* table = nullptr;
    std::vector<LabelInfo> labels;
};
