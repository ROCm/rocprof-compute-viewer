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

#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QScrollBar>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "../analysis/annotation.h"
#include "../config/config.hpp"
#include "asmcode.h"
#include "mainwindow.h"
#include "qcodelist.h"
#include "sourcefile.h"
#include "util/custom_layouts.h"

namespace
{

QColor transparentNoneColor()
{
    QColor color = Config::TokenColors().at(0).qcolor;
    color.setAlphaF(0.5);
    return color;
}

QColor hiddenIdleColor()
{
    QColor color = transparentNoneColor().darker(135);
    color.setAlphaF(0.75);
    return color;
}

QColor hiddenStallColor() { return Config::StallColor().lighter(145); }

int64_t clampHiddenValue(int64_t value, int64_t upper)
{
    return std::clamp<int64_t>(value, 0, std::max<int64_t>(0, upper));
}

struct LatencySplit
{
    int64_t visibleIdle = 0;
    int64_t hiddenIdle = 0;
    int64_t visibleStall = 0;
    int64_t hiddenStall = 0;
    int64_t issue = 0;
};

LatencySplit splitLatency(const Latency& latency, bool includeIdle)
{
    const int64_t active = std::max<int64_t>(0, latency.latency);
    const int64_t stall = std::clamp<int64_t>(latency.stalled, 0, active);
    const int64_t idle = includeIdle ? std::max<int64_t>(0, latency.idle) : 0;

    const int64_t hiddenAnyStall = clampHiddenValue(latency.hidden_any_stall, stall);
    const int64_t hiddenAnyIdle = clampHiddenValue(includeIdle ? latency.hidden_any_idle : 0, idle);

    LatencySplit split;
    split.visibleIdle = idle - hiddenAnyIdle;
    split.hiddenIdle = hiddenAnyIdle;
    split.visibleStall = stall - hiddenAnyStall;
    split.hiddenStall = hiddenAnyStall;
    split.issue = active - stall;
    return split;
}

} // namespace

bool HorizontalHotspot::is_pcs_enabled = false;
bool HorizontalHotspot::is_sqtt_enabled = true;
bool HorizontalHotspot::show_idle_time = true;
int HorizontalHotspot::HISTOGRAM_WIDTH = 100;

void HorizontalHotspot::SetHistogramWidth(int value) { HISTOGRAM_WIDTH = std::max(0, value); }

void HorizontalHotspot::add_latency(int type, Latency _sqtt, Latency _pcs)
{
    QWARNING(type >= 0 && type < typed_latency.size(), "Invalid latency type" << type, type = 0);

    sqtt += _sqtt;
    pcs += _pcs;
    typed_latency[type] += _sqtt.latency;
}

void HorizontalHotspot::paint(
    QPainter& painter,
    const int posx,
    const int posy,
    const int sizex,
    const int sizey,
    const float sqtt_maxvalue,
    const float pcs_maxvalue,
    DrawFormat format,
    bool rightToLeft,
    bool highlighted
) const
{
    const int padding = format == DrawFormat::DRAWBOTH ? 1 : 2;
    const int reducedHeight = sizey - 2 * padding;
    const float dir = rightToLeft ? -1 : 1;

    QPen savedPen = painter.pen();
    QBrush savedBrush = painter.brush();

    painter.setPen(Qt::NoPen);

    int xstart = posx;
    float pos = posx;
    int totalWidth = 0;

    const float NORM = (sizex > 0 ? sizex : HISTOGRAM_WIDTH) / (is_pcs_enabled ? pcs_maxvalue : sqtt_maxvalue);

    auto Draw = [&](float width, const QColor& color, int _posy, int _height)
    {
        pos += dir * width;
        if (std::abs(xstart - pos) >= 1.0f)
        {
            int start = std::min(xstart, static_cast<int>(pos));
            int end = std::max(xstart, static_cast<int>(pos));
            painter.setBrush(QBrush(color));

            painter.drawRect(start, _posy, end - start, _height);

            xstart = pos;
            totalWidth = std::max(totalWidth, std::abs(static_cast<int>(pos) - posx));
        }
    };

    auto drawStall = [&](const Latency& latency, int _posy, int _height)
    {
        const LatencySplit split = splitLatency(latency, show_idle_time);

        if (split.visibleIdle > 0) Draw(split.visibleIdle * NORM, transparentNoneColor(), _posy, _height);
        if (split.hiddenIdle > 0) Draw(split.hiddenIdle * NORM, hiddenIdleColor(), _posy, _height);
        if (split.visibleStall > 0) Draw(split.visibleStall * NORM, Config::StallColor(), _posy, _height);
        if (split.hiddenStall > 0) Draw(split.hiddenStall * NORM, hiddenStallColor(), _posy, _height);
        if (split.issue > 0) Draw(split.issue * NORM, Config::IssueColor(), _posy, _height);
    };

    auto drawType = [&](const std::array<int64_t, 16>& array, int _posy, int _height)
    {
        const auto& colors = is_pcs_enabled ? Config::StallReasonColors() : Config::TokenColors();

        if (!is_pcs_enabled && show_idle_time && sqtt.idle > 0)
            Draw(sqtt.idle * NORM, transparentNoneColor(), _posy, _height);

        float barWidth = 0;
        for (int c = 0; c < array.size() && c < colors.size(); c++)
        {
            barWidth += array[c] * NORM;
            if (barWidth < 0.1f) continue;
            const QColor color = !is_pcs_enabled && c == 0 ? transparentNoneColor() : colors.at(c).qcolor;
            Draw(barWidth, color, _posy, _height);
            barWidth = 0;
        }
    };

    {
        int _height = reducedHeight;
        int _posy = posy + padding;
        if (format == DrawFormat::DRAWBOTH) _height /= 2;

        if (format & DrawFormat::DRAWSTALL) drawStall(is_pcs_enabled ? pcs : sqtt, _posy, _height);

        _posy += reducedHeight - _height;
        xstart = pos = posx;

        if (format & DrawFormat::DRAWTYPE) drawType(is_pcs_enabled ? stall_reason : typed_latency, _posy, _height);
    }

    // Draw border around entire hotspot area (thicker when highlighted)
    // Always draw at full HISTOGRAM_WIDTH when highlighted to show the full interactive area
    if (totalWidth > 0)
    {
        QPen pen;
        pen.setColor(WindowColors::HotspotOutline());
        pen.setWidth(1);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);

        painter.drawRect(rightToLeft ? (posx - totalWidth) : posx, posy + padding, totalWidth, reducedHeight);
    }

    if (highlighted)
    {
        QPen pen;
        pen.setColor(WindowColors::HotspotOutline());
        pen.setWidth(2);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);

        painter.drawRect(posx + 1, posy + padding, (sizex > 0 ? sizex : HISTOGRAM_WIDTH) - 1, reducedHeight);
    }

    painter.setPen(savedPen);
    painter.setBrush(savedBrush);
}

std::string HorizontalHotspot::getTooltip() const
{
    // Build tooltip with all information from this line
    std::stringstream ss;

    const Latency& latency = is_pcs_enabled ? pcs : sqtt;

    const int64_t totalCycles = latency.total(show_idle_time);

    if (totalCycles > 0)
    {
        int64_t issue = latency.latency - latency.stalled;
        const int64_t stall = std::max<int64_t>(latency.stalled, 0);
        const int64_t idle = show_idle_time ? std::max<int64_t>(latency.idle, 0) : 0;
        const int64_t stallAndIdle = stall + idle;
        const int64_t hiddenAnyStall = clampHiddenValue(latency.hidden_any_stall, stall);
        const int64_t hiddenAnyIdle = clampHiddenValue(show_idle_time ? latency.hidden_any_idle : 0, idle);
        const int64_t hiddenAny = hiddenAnyStall + hiddenAnyIdle;
        int64_t hiddenValu = clampHiddenValue(latency.hiddenValu(show_idle_time), hiddenAny);
        int64_t hiddenOther = hiddenAny - hiddenValu;
        double total = static_cast<double>(totalCycles) / 100.0;
        double stallIdleTotal = std::max<double>(static_cast<double>(stallAndIdle) / 100.0, 1.0);

        // Get colors as hex strings for HTML
        QColor stallColor = Config::StallColor();
        QColor issueColor = Config::IssueColor();
        QColor idleColor = Config::TokenColors().at(0).qcolor;

        // Use Unicode square character (■) with color styling - Qt rich text supports this
        ss << "<table cellspacing='2'>";
        ss << "<tr><td><font color='" << stallColor.name().toStdString() << "'>&#9632;</font></td>"
           << "<td>Stall:</td><td align='right'>" << std::fixed << std::setprecision(0) << latency.stalled / total
           << "%</td></tr>";
        ss << "<tr><td><font color='" << issueColor.name().toStdString() << "'>&#9632;</font></td>"
           << "<td>Issue:</td><td align='right'>" << std::fixed << std::setprecision(0) << issue / total
           << "%</td></tr>";
        if (show_idle_time && latency.idle > 0)
            ss << "<tr><td><font color='" << idleColor.name().toStdString() << "'>&#9632;</font></td>"
               << "<td>Idle:</td><td align='right'>" << std::fixed << std::setprecision(0) << latency.idle / total
               << "%</td></tr>";
        if (hiddenAny > 0 && stallAndIdle > 0)
        {
            ss << "<tr><td><font color='" << hiddenStallColor().name().toStdString() << "'>&#9632;</font></td>"
               << "<td>Hidden stall:</td><td align='right'>" << hiddenAnyStall << " (" << std::fixed
               << std::setprecision(0) << hiddenAnyStall / stallIdleTotal << "% stall+idle)</td></tr>";
            if (show_idle_time && hiddenAnyIdle > 0)
                ss << "<tr><td><font color='" << hiddenIdleColor().name().toStdString() << "'>&#9632;</font></td>"
                   << "<td>Hidden idle:</td><td align='right'>" << hiddenAnyIdle << " (" << std::fixed
                   << std::setprecision(0) << hiddenAnyIdle / stallIdleTotal << "% stall+idle)</td></tr>";
            ss << "<tr><td></td><td>Hidden by VALU:</td><td align='right'>" << hiddenValu << " (" << std::fixed
               << std::setprecision(0) << hiddenValu / stallIdleTotal << "% stall+idle)</td></tr>";
            if (hiddenOther > 0)
                ss << "<tr><td></td><td>Hidden by other:</td><td align='right'>" << hiddenOther << " (" << std::fixed
                   << std::setprecision(0) << hiddenOther / stallIdleTotal << "% stall+idle)</td></tr>";
        }
        ss << "<tr><td></td><td>Total:</td><td align='right'>" << totalCycles << "</td></tr>";
        ss << "</table>";
    }

    return ss.str();
}

namespace
{

// Append (Idle+)Stall+Issue segments onto `out`. Idle (the gaps between tokens)
// is drawn first in the NONE color (TokenColors[0]). Returns true if non-empty.
bool pushStallIssue(const Latency& lat, std::vector<Annotation::Component>& out)
{
    if (lat.total(HorizontalHotspot::show_idle_time) <= 0) return false;

    auto push = [&](int64_t value, const QColor& color)
    {
        if (value > 0) out.push_back({static_cast<double>(value), color});
    };

    const LatencySplit split = splitLatency(lat, HorizontalHotspot::show_idle_time);
    push(split.visibleIdle, transparentNoneColor());
    push(split.hiddenIdle, hiddenIdleColor());
    push(split.visibleStall, Config::StallColor());
    push(split.hiddenStall, hiddenStallColor());
    push(split.issue, Config::IssueColor());
    return true;
}

// Append per-reason breakdown segments. Returns true if non-empty.
bool pushReasons(const std::array<int64_t, 16>& data, const Latency& lat, std::vector<Annotation::Component>& out)
{
    const auto& colors = HorizontalHotspot::is_pcs_enabled ? Config::StallReasonColors() : Config::TokenColors();
    bool any = false;
    if (!HorizontalHotspot::is_pcs_enabled && HorizontalHotspot::show_idle_time && lat.idle > 0)
    {
        out.push_back({static_cast<double>(lat.idle), transparentNoneColor()});
        any = true;
    }
    for (size_t i = 0; i < data.size() && i < colors.size(); ++i)
    {
        if (data[i] <= 0) continue;
        const QColor color = !HorizontalHotspot::is_pcs_enabled && i == 0 ? transparentNoneColor() : colors[i].qcolor;
        out.push_back({static_cast<double>(data[i]), color});
        any = true;
    }
    return any;
}

std::unique_ptr<Annotation::Category> makeCat(const char* id, std::string name, int row_count, double normTotal)
{
    auto c = std::make_unique<Annotation::Category>();
    c->id = id;
    c->display_name = std::move(name);
    c->row_count = row_count;
    // Shared scale: keeps row 0 (Stall+Issue) and row 1 (per-reason breakdown)
    // visually comparable in latency_stall, and lines normalized across all
    // three categories for single-row views.
    c->forced_max[0] = normTotal;
    c->forced_max[1] = normTotal;
    return c;
}

} // namespace

void HorizontalHotspot::PublishCategories(int64_t max_sqtt_latency, int64_t max_pcs_latency)
{
    Annotation::Registry& reg = Annotation::Registry::instance();

    // Idempotent re-publish: drop stale categories first. The dropdown must be
    // refreshed even on the early-return paths so removed rows disappear.
    for (const char* id : {"inst_latency", "stall_reasons", "latency_stall"}) reg.clear(id);

    struct RefreshGuard
    {
        ~RefreshGuard()
        {
            if (QCodelist::singleton) QCodelist::singleton->refreshAnnotations();
        }
    } guard;

    if (!is_sqtt_enabled && !is_pcs_enabled) return;

    const bool usePcs = is_pcs_enabled;
    const double normTotal = static_cast<double>(usePcs ? max_pcs_latency : max_sqtt_latency);
    if (normTotal <= 0.0) return;

    auto inst = makeCat("inst_latency", "Inst Latency", 1, normTotal);
    auto reason = makeCat("stall_reasons", usePcs ? "Stall Reasons" : "Stall Categories", 1, normTotal);
    auto both = makeCat("latency_stall", "Latency + Stall", 2, normTotal);

    for (const auto& line : ASMCodeline::line_vec)
    {
        if (!line) continue;
        const HorizontalHotspot& h = line->hotspot;
        const int idx = line->line_index;
        const Latency& lat = usePcs ? h.pcs : h.sqtt;
        const auto& breakdown = usePcs ? h.stall_reason : h.typed_latency;

        // Pre-build the per-line tooltips once so we don't need to keep a
        // snapshot of the hotspot around just for hover formatting.
        const QString tipInst = QString::fromStdString(h.getTooltip());
        const QString tipStall = QString::fromStdString(h.getStallTip());

        {
            Annotation::LineData ld;
            if (pushStallIssue(lat, ld.rows[0]))
            {
                ld.tooltip = tipInst;
                inst->per_line[idx] = std::move(ld);
            }
        }
        {
            Annotation::LineData ld;
            if (pushReasons(breakdown, lat, ld.rows[0]))
            {
                ld.tooltip = tipStall;
                reason->per_line[idx] = std::move(ld);
            }
        }
        {
            Annotation::LineData ld;
            const bool si = pushStallIssue(lat, ld.rows[0]);
            const bool br = pushReasons(breakdown, lat, ld.rows[1]);
            if (si || br)
            {
                ld.tooltip = tipInst + tipStall;
                both->per_line[idx] = std::move(ld);
            }
        }
    }

    if (!inst->per_line.empty()) reg.publish(std::move(inst));
    if (!reason->per_line.empty()) reg.publish(std::move(reason));
    if (!both->per_line.empty()) reg.publish(std::move(both));
}

std::string HorizontalHotspot::getStallTip() const
{
    std::stringstream ss;

    // Use stall_reason with StallReasonColors if pcs enabled, else typed_latency with TokenColors
    const auto& colors = is_pcs_enabled ? Config::StallReasonColors() : Config::TokenColors();
    const auto& data = is_pcs_enabled ? stall_reason : typed_latency;

    int64_t total = 0;
    for (size_t i = 0; i < data.size() && i < colors.size(); i++) total += data[i];
    const bool includeIdle = !is_pcs_enabled && show_idle_time && sqtt.idle > 0;
    if (includeIdle) total += sqtt.idle;

    if (total > 0)
    {
        double totalNorm = static_cast<double>(total) / 100.0;

        ss << "<br><b>Stall Reasons: " << total << "</b>";
        ss << "<table cellspacing='2'>";
        if (includeIdle)
            ss << "<tr><td><font color='" << Config::TokenColors().at(0).qcolor.name().toStdString()
               << "'>&#9632;</font></td>"
               << "<td>Idle:</td><td align='right'>" << std::fixed << std::setprecision(0) << sqtt.idle / totalNorm
               << "%</td></tr>";
        for (size_t i = 0; i < data.size() && i < colors.size(); i++)
        {
            if (data[i] == 0) continue;
            ss << "<tr><td><font color='" << colors[i].qcolor.name().toStdString() << "'>&#9632;</font></td>"
               << "<td>" << colors[i].name << ":</td><td align='right'>" << std::fixed << std::setprecision(0)
               << data[i] / totalNorm << "%</td></tr>";
        }
        ss << "</table>";
    }

    return ss.str();
}
