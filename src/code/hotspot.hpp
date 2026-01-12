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

#include <QComboBox>
#include <QWidget>
#include <optional>
#include "util/custom_layouts.h"
#include "util/highlight.h"

struct Latency
{
    int64_t latency = 0;
    int64_t stalled = 0;

    Latency& operator+=(const Latency& other)
    {
        latency += other.latency;
        stalled += other.stalled;
        return *this;
    }
};

class HorizontalHotspot
{
public:
    enum DrawFormat
    {
        DRAWNONE = 0,
        DRAWSTALL = 1,
        DRAWTYPE = 2,
        DRAWBOTH = 3
    };

    std::array<int64_t, 16> typed_latency{};
    std::array<int64_t, 16> stall_reason{};
    Latency sqtt{};
    Latency pcs{};

    void add_latency(int type, Latency sqtt, Latency pcs);
    void paint(
        class QPainter& painter,
        const int posx,
        const int posy,
        const int sizex, // if <= 0, use histogram default width
        const int sizey,
        const float sqtt_maxvalue,
        const float pcs_maxvalue,
        DrawFormat format,
        bool rightToLeft,
        bool highlighted = false
    ) const;

    // Get tooltip text for a given mouse position relative to the hotspot
    std::string getTooltip() const;
    std::string getStallTip() const;

    HorizontalHotspot& operator+=(const HorizontalHotspot& other)
    {
        sqtt += other.sqtt;
        pcs += other.pcs;
        for (size_t i = 0; i < typed_latency.size(); i++) typed_latency[i] += other.typed_latency[i];
        for (size_t i = 0; i < stall_reason.size(); i++) stall_reason[i] += other.stall_reason[i];
        return *this;
    }

    int64_t combined() const { return sqtt.latency + pcs.latency; }

    static int GetHistogramWidth() { return HISTOGRAM_WIDTH; }
    static void SetHistogramWidth(int width);

    static bool is_pcs_enabled;
    static bool is_sqtt_enabled;

private:
    static int HISTOGRAM_WIDTH;
};
Q_DECLARE_METATYPE(HorizontalHotspot)
