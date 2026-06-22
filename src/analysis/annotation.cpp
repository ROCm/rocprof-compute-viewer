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

#include "analysis/annotation.h"

#include <algorithm>
#include <utility>

namespace Annotation
{

double LineData::segmentTotal(int row) const
{
    double sum = 0.0;
    for (const Component& c : rows[row])
        if (c.kind == Component::Kind::Segment) sum += c.value;
    return sum;
}

double LineData::drawExtent(int row) const
{
    double max_whisker = 0.0;
    for (const Component& c : rows[row])
        if (c.kind == Component::Kind::Whisker) max_whisker = std::max(max_whisker, c.value);
    return segmentTotal(row) + max_whisker;
}

void Category::recomputeMaxTotal()
{
    const int n = std::clamp(row_count, 1, 2);
    for (int r = 0; r < 2; ++r)
    {
        if (forced_max[r] > 0.0)
        {
            max_total[r] = forced_max[r];
            continue;
        }
        max_total[r] = 0.0;
        if (r >= n) continue;
        for (const auto& [idx, line] : per_line) max_total[r] = std::max(max_total[r], line.drawExtent(r));
    }
}

Registry& Registry::instance()
{
    static Registry inst;
    return inst;
}

void Registry::publish(std::unique_ptr<Category> cat)
{
    if (!cat) return;
    cat->recomputeMaxTotal();
    for (auto& c : m_categories)
    {
        if (c->id == cat->id)
        {
            c = std::move(cat);
            return;
        }
    }
    m_categories.push_back(std::move(cat));
}

void Registry::clear(const std::string& id)
{
    m_categories.erase(
        std::remove_if(m_categories.begin(), m_categories.end(), [&](const auto& c) { return c->id == id; }),
        m_categories.end()
    );
}

void Registry::clearAll() { m_categories.clear(); }

std::vector<const Category*> Registry::categories() const
{
    std::vector<const Category*> out;
    out.reserve(m_categories.size());
    for (const auto& c : m_categories) out.push_back(c.get());
    return out;
}

const Category* Registry::find(const std::string& id) const
{
    for (const auto& c : m_categories)
        if (c->id == id) return c.get();
    return nullptr;
}

} // namespace Annotation
