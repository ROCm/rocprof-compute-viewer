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

int64_t clampHiddenValue(int64_t value, int64_t upper)
{
    return std::clamp<int64_t>(value, 0, std::max<int64_t>(0, upper));
}

struct LatencySplit
{
    int64_t hiddenIdle = 0;
    int64_t hiddenStall = 0;
    int64_t hiddenIssue = 0;
    int64_t visibleIdle = 0;
    int64_t visibleStall = 0;
    int64_t visibleIssue = 0;

    int64_t hidden() const { return hiddenIdle + hiddenStall + hiddenIssue; }
};

LatencySplit splitLatency(const Latency& latency, bool includeIdle)
{
    const int64_t active = std::max<int64_t>(0, latency.latency);
    const int64_t stall = std::clamp<int64_t>(latency.stalled, 0, active);
    const int64_t issue = active - stall;
    const int64_t idle = includeIdle ? std::max<int64_t>(0, latency.idle) : 0;

    LatencySplit split;
    split.hiddenIdle = clampHiddenValue(includeIdle ? latency.hidden.idle : 0, idle);
    split.hiddenStall = clampHiddenValue(latency.hidden.stall, stall);
    split.hiddenIssue = clampHiddenValue(latency.hidden.issue, issue);
    split.visibleIdle = idle - split.hiddenIdle;
    split.visibleStall = stall - split.hiddenStall;
    split.visibleIssue = issue - split.hiddenIssue;
    return split;
}

std::string formatPercent(int64_t value, int64_t total)
{
    const double percent = 100.0 * static_cast<double>(value) / static_cast<double>(total);
    std::stringstream ss;
    ss << std::fixed << std::setprecision(percent < 10.0 ? 1 : 0) << percent << "%";
    return ss.str();
}

void appendPercentRow(std::ostream& out, const QColor& color, const std::string& label, int64_t value, int64_t total)
{
    out << "  <tr>\n"
        << "    <td><font color='" << color.name().toStdString() << "'>&#9632;</font></td>\n"
        << "    <td>" << label << ":</td>\n"
        << "    <td align='right'>" << formatPercent(value, total) << "</td>\n"
        << "  </tr>\n";
}

void appendValueRow(std::ostream& out, const std::string& label, int64_t value)
{
    out << "  <tr>\n"
        << "    <td></td>\n"
        << "    <td>" << label << ":</td>\n"
        << "    <td align='right'>" << value << "</td>\n"
        << "  </tr>\n";
}

} // namespace

bool HorizontalHotspot::is_pcs_enabled = false;
bool HorizontalHotspot::is_sqtt_enabled = true;
bool HorizontalHotspot::show_idle_time = true;
bool HorizontalHotspot::source_include_hidden_latency = true;
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

        if (source_include_hidden_latency && split.hidden() > 0)
            Draw(split.hidden() * NORM, Config::HiddenLatencyColor(), _posy, _height);
        if (split.visibleIdle > 0) Draw(split.visibleIdle * NORM, transparentNoneColor(), _posy, _height);
        if (split.visibleStall > 0) Draw(split.visibleStall * NORM, Config::StallColor(), _posy, _height);
        if (split.visibleIssue > 0) Draw(split.visibleIssue * NORM, Config::IssueColor(), _posy, _height);
    };

    auto drawType = [&](const std::array<int64_t, 16>& array, int _posy, int _height)
    {
        const auto& colors = is_pcs_enabled ? Config::StallReasonColors() : Config::TokenColors();
        const LatencySplit split = splitLatency(sqtt, show_idle_time);
        const int64_t visibleIdle =
            !is_pcs_enabled && !source_include_hidden_latency ? split.visibleIdle : std::max<int64_t>(0, sqtt.idle);
        const double activeScale =
            !is_pcs_enabled && !source_include_hidden_latency && sqtt.latency > 0
                ? static_cast<double>(split.visibleStall + split.visibleIssue) / static_cast<double>(sqtt.latency)
                : 1.0;

        if (!is_pcs_enabled && show_idle_time && visibleIdle > 0)
            Draw(visibleIdle * NORM, transparentNoneColor(), _posy, _height);

        float barWidth = 0;
        for (int c = 0; c < array.size() && c < colors.size(); c++)
        {
            barWidth += static_cast<float>(array[c] * activeScale * NORM);
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
    std::stringstream ss;

    const Latency& latency = is_pcs_enabled ? pcs : sqtt;
    const int64_t totalCycles = latency.total(show_idle_time);

    if (totalCycles > 0)
    {
        const LatencySplit split = splitLatency(latency, show_idle_time);

        ss << "<table cellspacing='2'>\n";
        if (split.hidden() > 0)
            appendPercentRow(ss, Config::HiddenLatencyColor(), "Hidden", split.hidden(), totalCycles);
        appendPercentRow(ss, Config::StallColor(), "Stall", split.visibleStall, totalCycles);
        appendPercentRow(ss, Config::IssueColor(), "Issue", split.visibleIssue, totalCycles);
        if (show_idle_time && split.visibleIdle > 0)
            appendPercentRow(ss, Config::TokenColors().at(0).qcolor, "Idle", split.visibleIdle, totalCycles);
        if (split.hidden() > 0)
        {
            appendValueRow(ss, "Hidden idle", split.hiddenIdle);
            appendValueRow(ss, "Hidden stall", split.hiddenStall);
            appendValueRow(ss, "Hidden issue", split.hiddenIssue);
        }
        appendValueRow(ss, "Total", totalCycles);
        ss << "</table>\n";
    }

    return ss.str();
}

namespace
{

// Append (Idle+)Stall+Issue segments onto `out`. Idle (the gaps between tokens)
// is drawn first in the NONE color (TokenColors[0]). Returns true if non-empty.
bool pushStallIssue(const Latency& lat, std::vector<Annotation::Component>& out, bool includeHidden)
{
    if (lat.total(HorizontalHotspot::show_idle_time) <= 0) return false;

    bool any = false;
    auto push = [&](int64_t value, const QColor& color)
    {
        if (value <= 0) return;
        out.push_back({static_cast<double>(value), color});
        any = true;
    };

    const LatencySplit split = splitLatency(lat, HorizontalHotspot::show_idle_time);
    if (includeHidden) push(split.hidden(), Config::HiddenLatencyColor());
    push(split.visibleIdle, transparentNoneColor());
    push(split.visibleStall, Config::StallColor());
    push(split.visibleIssue, Config::IssueColor());
    return any;
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
    for (const char* id : {"inst_latency", "nonhidden_latency", "stall_reasons", "latency_stall"}) reg.clear(id);

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

    auto inst = makeCat("inst_latency", "Total latency", 1, normTotal);
    auto nonhidden = makeCat("nonhidden_latency", "Nonhidden Latency", 1, 0.0);
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
            if (pushStallIssue(lat, ld.rows[0], true))
            {
                ld.tooltip = tipInst;
                inst->per_line[idx] = std::move(ld);
            }
        }
        {
            Annotation::LineData ld;
            if (pushStallIssue(lat, ld.rows[0], false))
            {
                ld.tooltip = tipInst;
                nonhidden->per_line[idx] = std::move(ld);
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
            const bool si = pushStallIssue(lat, ld.rows[0], true);
            const bool br = pushReasons(breakdown, lat, ld.rows[1]);
            if (si || br)
            {
                ld.tooltip = tipInst + tipStall;
                both->per_line[idx] = std::move(ld);
            }
        }
    }

    if (!inst->per_line.empty()) reg.publish(std::move(inst));
    if (!nonhidden->per_line.empty()) reg.publish(std::move(nonhidden));
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
        ss << "<br>\n"
           << "<b>Stall Reasons: " << total << "</b>\n"
           << "<table cellspacing='2'>\n";
        if (includeIdle) appendPercentRow(ss, Config::TokenColors().at(0).qcolor, "Idle", sqtt.idle, total);
        for (size_t i = 0; i < data.size() && i < colors.size(); i++)
        {
            if (data[i] == 0) continue;
            appendPercentRow(ss, colors[i].qcolor, colors[i].name, data[i], total);
        }
        ss << "</table>\n";
    }

    return ss.str();
}
