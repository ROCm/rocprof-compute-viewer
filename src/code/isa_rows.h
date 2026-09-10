// MIT License
// Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.

#pragma once

#include <string_view>
#include <utility>
#include <vector>

namespace Isa
{
bool isLabel(std::string_view text);

// Maps display rows to stable ASMCodeline::line_index values. Only rebuilding
// the listing or changing a fold is O(lines); scrolling and hit testing are O(1).
class Rows
{
public:
    struct Section
    {
        int label;
        int end; // exclusive, up to the next label (no nesting)
        bool collapsed = false;
    };

    void reset(const std::vector<std::string_view>& lines);
    void setFoldingEnabled(bool enabled);
    bool foldingEnabled() const { return folding_enabled; }
    bool toggle(int label);
    bool reveal(int first, int last);
    void expandAll();

    int count() const { return static_cast<int>(visible_lines.size()); }
    int lineAt(int row) const;
    int rowOf(int line) const; // -1 for hidden or invalid lines
    const Section* sectionAt(int label) const;
    std::pair<int, int> visibleRange(int scroll, int height, int row_height) const;

private:
    void rebuild();
    bool folding_enabled = true; // Labels are foldable, but sections start expanded.
    std::vector<Section> sections;
    std::vector<int> line_sections;
    std::vector<int> visible_lines;
    std::vector<int> display_rows;
};
} // namespace Isa
