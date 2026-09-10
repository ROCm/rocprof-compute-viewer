// MIT License
// Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.

#include "isa_rows.h"
#include <algorithm>
#include <cstdint>

namespace Isa
{
bool isLabel(std::string_view text)
{
    const auto start = text.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) return false;
    text.remove_prefix(start);
    // rocprof emits branch labels and mangled function-name comments.
    if (text.starts_with("label") || text.starts_with("; _")) return true;
    const auto token = text.substr(0, text.find_first_of(" \t\r\n"));
    return token.size() > 1 && token.back() == ':';
}

void Rows::reset(const std::vector<std::string_view>& lines)
{
    sections.clear();
    line_sections.assign(lines.size(), -1);
    display_rows.resize(lines.size());
    for (int line = 0; line < static_cast<int>(lines.size()); ++line)
    {
        if (isLabel(lines[line]))
        {
            if (!sections.empty()) sections.back().end = line;
            sections.push_back({line, static_cast<int>(lines.size())});
        }
        if (!sections.empty()) line_sections[line] = static_cast<int>(sections.size()) - 1;
    }
    rebuild();
}

void Rows::rebuild()
{
    visible_lines.clear();
    visible_lines.reserve(display_rows.size());
    std::fill(display_rows.begin(), display_rows.end(), -1);
    for (int line = 0; line < static_cast<int>(display_rows.size()); ++line)
    {
        display_rows[line] = count();
        visible_lines.push_back(line);
        const auto* section = sectionAt(line);
        if (folding_enabled && section && section->collapsed) line = section->end - 1;
    }
}

void Rows::setFoldingEnabled(bool enabled)
{
    folding_enabled = enabled;
    // Switching off is an explicit escape hatch, not a hidden set of folds.
    if (!enabled)
        expandAll();
    else
        rebuild();
}

void Rows::expandAll()
{
    for (auto& section : sections) section.collapsed = false;
    rebuild();
}

bool Rows::toggle(int label)
{
    const auto* section = sectionAt(label);
    if (!folding_enabled || !section || section->end <= label + 1) return false;
    auto& mutable_section = sections[line_sections[label]];
    mutable_section.collapsed = !mutable_section.collapsed;
    rebuild();
    return true;
}

bool Rows::reveal(int first, int last)
{
    bool changed = false;
    if (first > last) return false;
    for (auto& section : sections)
        if (section.collapsed && first < section.end && last >= section.label)
        {
            section.collapsed = false;
            changed = true;
        }
    if (changed) rebuild();
    return changed;
}

int Rows::lineAt(int row) const { return row >= 0 && row < count() ? visible_lines[row] : -1; }

int Rows::rowOf(int line) const
{
    return line >= 0 && line < static_cast<int>(display_rows.size()) ? display_rows[line] : -1;
}

const Rows::Section* Rows::sectionAt(int label) const
{
    if (label < 0 || label >= static_cast<int>(line_sections.size()) || line_sections[label] < 0) return nullptr;
    const auto& section = sections[line_sections[label]];
    return section.label == label ? &section : nullptr;
}

std::pair<int, int> Rows::visibleRange(int scroll, int height, int row_height) const
{
    if (row_height <= 0 || height <= 0) return {0, 0};
    const auto top = std::max<int64_t>(0, scroll);
    return {
        static_cast<int>(std::min<int64_t>(count(), top / row_height)),
        static_cast<int>(std::min<int64_t>(count(), (top + height + row_height - 1) / row_height))};
}
} // namespace Isa
