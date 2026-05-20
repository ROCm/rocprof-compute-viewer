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

#include <QComboBox>
#include <QWidget>
#include <algorithm>
#include <optional>
#include "util/custom_layouts.h"
#include "util/highlight.h"

struct Latency
{
    int64_t latency = 0;           // active execution time (issue + stall)
    int64_t stalled = 0;           // stalled sub-portion of `latency` (stalled <= latency)
    int64_t idle = 0;              // gap/dead time between tokens; not part of `latency`. SQTT-only (pcs leaves it 0).
    int64_t hidden_valu_stall = 0; // stalled cycles hidden by VALU-family pipe utilization.
    int64_t hidden_valu_idle = 0;  // idle cycles hidden by VALU-family pipe utilization.
    int64_t hidden_any_stall = 0;  // stalled cycles hidden by any pipe utilization.
    int64_t hidden_any_idle = 0;   // idle cycles hidden by any pipe utilization.

    // Total wall time attributable to the line. Idle can be excluded for views
    // that need to show only active instruction latency.
    int64_t total(bool include_idle = true) const { return latency + (include_idle ? idle : 0); }
    int64_t hiddenValu(bool include_idle = true) const
    {
        return hidden_valu_stall + (include_idle ? hidden_valu_idle : 0);
    }
    int64_t hiddenAny(bool include_idle = true) const
    {
        return hidden_any_stall + (include_idle ? hidden_any_idle : 0);
    }
    int64_t nonHidden(bool include_idle = true) const
    {
        const int64_t stallAndIdle = stalled + (include_idle ? idle : 0);
        return total(include_idle) - std::clamp<int64_t>(hiddenAny(include_idle), 0, stallAndIdle);
    }
    void clearHidden()
    {
        hidden_valu_stall = 0;
        hidden_valu_idle = 0;
        hidden_any_stall = 0;
        hidden_any_idle = 0;
    }

    Latency& operator+=(const Latency& other)
    {
        latency += other.latency;
        stalled += other.stalled;
        idle += other.idle;
        hidden_valu_stall += other.hidden_valu_stall;
        hidden_valu_idle += other.hidden_valu_idle;
        hidden_any_stall += other.hidden_any_stall;
        hidden_any_idle += other.hidden_any_idle;
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

    int64_t combined() const { return sqtt.total(show_idle_time) + pcs.latency; }

    static int GetHistogramWidth() { return HISTOGRAM_WIDTH; }
    static void SetHistogramWidth(int width);

    static bool is_pcs_enabled;
    static bool is_sqtt_enabled;
    static bool show_idle_time;

    /// Walk ASMCodeline::line_vec and publish three Annotation::Categories
    /// ("inst_latency", "stall_reasons", "latency_stall") reflecting the
    /// current hotspot data. Call once after Populate fills the hotspots.
    static void PublishCategories(int64_t max_sqtt_latency, int64_t max_pcs_latency);

private:
    static int HISTOGRAM_WIDTH;
};
Q_DECLARE_METATYPE(HorizontalHotspot)
