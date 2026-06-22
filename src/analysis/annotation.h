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

// Per-instruction annotation registry. Each Category declares per-line stacked
// bars (Segments and ± Whisker error ticks) plus a pre-formatted HTML tooltip.
// Canvas paints the bar; QCodelist's View dropdown lists categories and
// publishes the active selection by id (held on Canvas).
//
// Adding a new analysis: populate a Category, publish, ask QCodelist to refresh.
// Registry is a passive container — UI owns active state. Main-thread only.

#include <QColor>
#include <QString>
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Annotation
{

struct Component
{
    enum class Kind
    {
        Segment, ///< Filled bar segment; stacked left→right with siblings on the same row.
        Whisker  ///< ± error tick (value = half-width) at the right edge of the row's segments.
    };

    double value = 0.0;
    QColor color;
    Kind kind = Kind::Segment;
};

struct LineData
{
    /// Up to two visual rows; row 1 only used when Category::row_count == 2.
    std::array<std::vector<Component>, 2> rows;

    /// Pre-formatted hover HTML. Empty => no tooltip shown.
    QString tooltip;

    double segmentTotal(int row) const;
    /// segmentTotal + largest whisker value on that row (whiskers extend ±value).
    double drawExtent(int row) const;
};

struct Category
{
    std::string id;
    std::string display_name;
    std::map<int /*code_index*/, LineData> per_line;

    /// Cached normalization per row; populated by recomputeMaxTotal().
    std::array<double, 2> max_total = {0.0, 0.0};

    /// When forced_max[r] > 0, row r normalizes against it instead of per-row
    /// drawExtent. Use to keep stacked sub-bars visually comparable.
    std::array<double, 2> forced_max = {0.0, 0.0};

    int row_count = 1; ///< 1 (full bar) or 2 (two half-height bars).

    void recomputeMaxTotal();
};

class Registry
{
public:
    static Registry& instance();

    /// Replace any category with the same id; recomputes max_total. Takes ownership.
    void publish(std::unique_ptr<Category> cat);

    /// Remove by id. No-op if absent.
    void clear(const std::string& id);

    /// Remove all categories.
    void clearAll();

    std::vector<const Category*> categories() const; ///< Insertion order.
    const Category* find(const std::string& id) const;

private:
    Registry() = default;
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

    std::vector<std::unique_ptr<Category>> m_categories;
};

} // namespace Annotation
