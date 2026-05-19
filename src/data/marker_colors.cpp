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

#include "data/marker_colors.h"

#include <QHash>

#include <algorithm>
#include <cctype>
#include <sstream>

#include "config/config.hpp"

const char* MarkerKindName(MarkerKind k)
{
    switch (k)
    {
        case MarkerKind::Function: return "function";
        case MarkerKind::Kernel: return "kernel";
        case MarkerKind::UserScope: return "scope";
        case MarkerKind::Point: return "point";
        case MarkerKind::Unknown: return "unknown";
    }
    return "unknown";
}

const QColor& MarkerBaseColor(MarkerKind k)
{
    switch (k)
    {
        case MarkerKind::Function: return WindowColors::MarkerFunctionColor();
        case MarkerKind::Kernel: return WindowColors::MarkerKernelColor();
        case MarkerKind::UserScope: return WindowColors::MarkerScopeColor();
        case MarkerKind::Point: return WindowColors::MarkerPointColor();
        case MarkerKind::Unknown: return WindowColors::MarkerUnknownColor();
    }
    return WindowColors::MarkerUnknownColor();
}

QColor MarkerColor(MarkerKind k, const std::string& name)
{
    if (k == MarkerKind::Unknown || name.empty()) return WindowColors::MarkerUnknownColor();

    if (k == MarkerKind::Point)
    {
        auto token_color = [](const std::string& token_name) -> std::optional<QColor>
        {
            for (const auto& token : Config::TokenColors())
                if (token.name == token_name) return token.qcolor;
            return std::nullopt;
        };

        std::string lname = name;
        std::transform(
            lname.begin(),
            lname.end(),
            lname.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); }
        );
        if (lname.find("vmem") != std::string::npos)
        {
            if (auto color = token_color("VMEM")) return *color;
        }
        else if (lname.find("barrier") != std::string::npos)
        {
            if (auto color = token_color("MSG")) return *color;
        }
    }

    QColor base = MarkerBaseColor(k);
    int hue, s, v;
    base.getHsv(&hue, &s, &v);
    if (hue < 0) hue = 0;
    int shift = static_cast<int>(qHash(QString::fromStdString(name)) % 60u) - 30;
    hue = ((hue + shift) % 360 + 360) % 360;
    // Slight saturation bump so the base palette reads as distinctly colored
    // bands rather than washed-out tints over the wave background.
    s = std::min(255, s + 20);
    QColor out;
    out.setHsv(hue, s, v);
    return out;
}

std::string FormatMarkerTooltip(const MarkerSpan& s, std::optional<MarkerCoord> coord, bool include_depth)
{
    std::ostringstream ss;
    const std::string& name = s.name.empty() ? std::string{"(unresolved)"} : s.name;
    ss << name << "  [" << MarkerKindName(s.kind) << ']';
    if (!s.source_loc.empty()) ss << "\nsrc:    " << s.source_loc;
    if (include_depth) ss << "\ndepth:  " << s.depth;
    if (coord)
    {
        ss << "\nse=" << coord->se << " cu=" << coord->cu << " simd=" << coord->simd << " slot=" << coord->slot;
    }
    if (s.is_point)
        ss << "\ntime:   " << s.enter_time;
    else if (s.is_open)
        ss << "\nrange:  " << s.enter_time << " – open";
    else
        ss << "\nrange:  " << s.enter_time << " – " << s.exit_time << "  (Δ " << (s.exit_time - s.enter_time) << ')';
    return ss.str();
}

void MarkerRenderCache::Reset(MarkerSpanVec new_spans)
{
    spans = std::move(new_spans);
    colors.clear();
    open_indices.clear();
    max_depth = 0;
    max_closed_dur = 0;
    if (!spans || spans->empty()) return;
    const auto& v = *spans;
    colors.reserve(v.size());
    for (size_t i = 0; i < v.size(); ++i)
    {
        const auto& s = v[i];
        colors.push_back(MarkerColor(s.kind, s.name));
        if (s.depth > max_depth) max_depth = s.depth;
        if (s.is_open)
            open_indices.push_back(static_cast<int>(i));
        else if (!s.is_point)
        {
            int64_t dur = s.exit_time - s.enter_time;
            if (dur > max_closed_dur) max_closed_dur = dur;
        }
    }
}

std::vector<MarkerSpan>::const_iterator MarkerRenderCache::FirstCandidate(int64_t cursor_clock) const
{
    const int64_t search_from = cursor_clock - max_closed_dur;
    return std::lower_bound(
        spans->begin(), spans->end(), search_from, [](const MarkerSpan& s, int64_t c) { return s.enter_time < c; }
    );
}
