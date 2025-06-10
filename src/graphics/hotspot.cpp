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

#include "hotspot.h"
#include <wave/token.h>
#include <QMouseEvent>
#include <QPainter>
#include <sstream>
#include <unordered_map>
#include <vector>
#include "code/asmcode.h"
#include "code/qcodelist.h"
#include "config/config.hpp"
#include "data/wavemanager.h"
#include "mainwindow.h"
#include "util/memtracker.h"

#define MIN_HEIGHT 2

using namespace std;

HotspotView::HotspotView(int _begin, int _end, int n_bins, double _max_value) :
code_begin(_begin), code_end(_end), max_value(_max_value)
{
    this->step = std::max(1, (code_end - code_begin + n_bins - 1) / n_bins);

    bins = std::vector<HotspotBin>(n_bins);
    for (int i = 0; i < bins.size(); i++) bins.at(i).center_line = code_begin + i * step + step / 2;

    QPalette pal = QPalette();
    pal.setColor(QPalette::Window, WindowColors::HotspotBkg());
    this->setAutoFillBackground(true);
    this->setPalette(pal);

    setMouseTracking(true);
    setAttribute(Qt::WA_AlwaysShowToolTips, true);
}

void HotspotView::Add(const TokenMap& tokens)
{
    try
    {
        for (const auto& tok : tokens)
        {
            int code_bin = getBin(tok.code_line);
            if (code_bin >= 0 && code_bin < bins.size()) bins.at(code_bin).cycles.at(tok.type) += tok.cycles;
        }
    }
    catch (std::exception& e)
    {
        QWARNING(false, e.what(), return );
    }
}

void HotspotView::Compile()
{
    if (max_value <= 1)
        for (auto& bin : bins)
        {
            int64_t acc = 0;
            for (int64_t value : bin.cycles) acc += value;
            max_value = std::max(max_value, 1.04 * acc);
        }
}

// Highlights first and last line
void HotspotView::mousePressEvent(QMouseEvent* ev)
{
    for (auto& bin : bins)
    {
        if (ev->pos().x() < bin.draw_start_x || ev->pos().x() > bin.draw_end_x) continue;

        int begin = bin.center_line - step / 2;
        int end = bin.center_line + step / 2;
        int start_success = 100;

        QASSERT(QCodelist::singleton, "Invalid codelist");
        QCodelist::singleton->Highlight(begin, end, true);

        return;
    }
}

void HotspotView::mouseMoveEvent(QMouseEvent* ev)
{
    for (auto& bin : bins)
    {
        if (ev->pos().x() < bin.draw_start_x || ev->pos().x() > bin.draw_end_x) continue;

        int begin = bin.center_line - step / 2;
        int end = bin.center_line + step / 2;

        std::stringstream tooltip;
        tooltip << "Lines:  " << begin << " - " << end << '\n';

        for (int type = 0; type < Token::GetNumColors() && type < bin.cycles.size(); type++)
            if (bin.cycles.at(type) != 0) tooltip << Token::GetName(type) << ":\t" << bin.cycles.at(type) << '\n';
        this->setToolTip(tooltip.str().c_str());
    }
}

void HotspotView::paintEvent(QPaintEvent* event)
{
    int margin = 8;
    const int top_font_value = MainWindow::font() + IF_QT6_ELSE(1, 0);
    const int bottom_font_value = MainWindow::font() - IF_QT6_ELSE(0, 2);

    QPainter painter(this);
    painter.setRenderHint(QPainter::TextAntialiasing);
#ifndef RCV_DISABLE_OPENGL
    painter.setRenderHint(QPainter::Antialiasing);
#endif

    QFont font = MainWindow::default_font.isEmpty() ? painter.font() : QFont(MainWindow::default_font);
    font.setPointSize(top_font_value);
    painter.setFont(font);

    int font_height = QFontMetrics(font).height();
    int top_space = margin * 2 + font_height;
    int bottom_space = margin * 2 + font_height;

    int start_y = height() - bottom_space;
    int num_divs = std::max(3, start_y / 80);
    int left_side_x = 40;
    int width_x = width() - left_side_x;
    double bar_width = width_x / (2 * bins.size() + 1.0);
    double bar_wid_multiplier = 1.25;

    double scaling = (start_y - top_space) / max_value;

    QPen pen = painter.pen();
    pen.setWidth(0);
    font.setPointSize(bottom_font_value);
    painter.setFont(font);

    painter.fillRect(QRect(0, 0, width(), height()), WindowColors::HotspotBkg());

    // Draw axes
    {
        pen.setColor(Qt::lightGray);
        painter.setPen(pen);

        double ystep = double(start_y - top_space) / num_divs;

        for (int y = 0; y <= num_divs; y++)
            painter.drawLine(
                left_side_x + bar_width / 2, start_y - ystep * y, width() - bar_width / 2, start_y - ystep * y
            );

        pen.setColor(WindowColors::textColor());
        painter.setPen(pen);

        char letter = ' ';
        double cvt_val = 1.0;

        std::array<std::pair<char, double>, 5> exps = {
            {{' ', 1.0}, {'K', 1E3}, {'M', 1E6}, {'G', 1E9}, {'T', 1E12}}
        };
        for (auto& [ch, exp_val] : exps)
            if (max_value >= exp_val)
            {
                letter = ch;
                cvt_val = exp_val * num_divs / max_value;
            }

        for (int y = 0; y <= num_divs; y++)
        {
            double value = y / cvt_val;
            auto str = QString("%1").arg(value, 0, 'f', value < 10 ? 2 : (value < 100 ? 1 : 0)) + letter;
            painter.drawText(left_side_x / 3, start_y - ystep * y, str);
        }
    }

    font.setPointSize(bottom_font_value);
    painter.setFont(font);
    pen.setColor(WindowColors::textColor());
    painter.setPen(pen);

    {
        QFontMetrics fm(font);

        double scaling = (start_y - top_space) / double(max_value);
        double xpos = left_side_x;

        for (auto& bin : bins)
        {
            xpos += bar_width;
            bin.draw_start_x = xpos + (1 - bar_wid_multiplier) * bar_width / 2;
            bin.draw_end_x = xpos + bar_wid_multiplier * bar_width;

            int64_t acc_value = 0;
            double scaled_value = 0.0;
            for (int c = 0; c < bin.cycles.size(); c++)
            {
                acc_value += bin.cycles.at(c);
                double fheight = acc_value * scaling - scaled_value;

                if (fheight < MIN_HEIGHT) continue;

                auto& color = Token::GetColor(c).qcolor;
                bin.draw_y = start_y - scaling * acc_value;
                painter.fillRect(QRectF(bin.draw_start_x, bin.draw_y, bar_width * bar_wid_multiplier, fheight), color);

                pen.setColor(WindowColors::HotspotOutline());
                painter.setPen(pen);
                painter.drawRect(QRectF(bin.draw_start_x, bin.draw_y, bar_width * bar_wid_multiplier, fheight + 0.5f));
                pen.setColor(WindowColors::textColor());
                painter.setPen(pen);
                scaled_value = scaling * acc_value;
            }
            // Draw codeline number
            auto line = std::to_string(bin.center_line);
            int font_wid = fm.horizontalAdvance(line.c_str());

            painter.drawText(xpos + bar_width / 2 - font_wid / 2, start_y + fm.height(), line.c_str());
            xpos += bar_width;
        }

        // Draw code_line mid label
        std::string_view linen = "Code line number";
        painter.drawText(width() / 2 - fm.horizontalAdvance(linen.data()) / 2, height() - margin / 2, linen.data());
    }

    font.setPointSize(top_font_value);
    painter.setFont(font);
    pen.setColor(WindowColors::textColor());
    painter.setPen(pen);

    size_t enable_bits = 0;
    for (size_t i = 0; i < Config::TokenColors().size(); i++)
        for (auto& bin : bins)
            if (i < bin.cycles.size() && bin.cycles.at(i) > 0) enable_bits |= 1 << i;

    enable_bits <<= 1;

    QFontMetrics fm(font);
    // Draw token colors
    int total_text_width = Config::TokenColors().size() * font_height * 3 / 2;
    for (auto& color : Config::TokenColors()) total_text_width += fm.horizontalAdvance(color.name.c_str());

    int left_padding = std::max((width() - total_text_width) / 2 + font_height, 0);
    for (auto& color : Config::TokenColors())
    {
        enable_bits >>= 1;
        if ((enable_bits & 1) == 0) continue;

        painter.fillRect(
            QRectF(left_padding + font_height / 4, margin, font_height / 2, font_height / 2), color.qcolor
        );
        painter.drawText(left_padding + font_height, margin + font_height / 2, color.name.c_str());
        left_padding += font_height * 3 / 2 + fm.horizontalAdvance(color.name.c_str());
    }
}
