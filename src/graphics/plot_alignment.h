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

enum class PlotAlignment
{
    None,
    Detail,
    Global
};

struct PlotAlignmentReference
{
    double clock_at_left;
    double clocks_per_pixel;
    int global_left;
    int pixel_width;
};

struct PlotViewRange
{
    double start;
    double end;
};

inline PlotViewRange alignedPlotRange(
    const PlotAlignmentReference& reference, int plot_global_left, int plot_pixel_width
)
{
    const double start =
        reference.clock_at_left + (plot_global_left - reference.global_left) * reference.clocks_per_pixel;
    return {start, start + plot_pixel_width * reference.clocks_per_pixel};
}
