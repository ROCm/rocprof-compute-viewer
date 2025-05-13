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

#include "asmcode.h"
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <algorithm>
#include <sstream>
#include "config/config.hpp"
#include "graphics/canvas.h"
#include "mainwindow.h"
#include "sourcefile.h"
#include "wave/waveview.h"

#define SHORT_CPPLINE_MAXCHARS 32

using namespace std;

static float sq(float x) { return x * x; }

std::map<int, std::shared_ptr<ASMCodeline>> ASMCodeline::line_map{};
std::vector<std::shared_ptr<ASMCodeline>> ASMCodeline::line_vec{};

CyclesLabel::Strategy CyclesLabel::global_strategy = CyclesLabel::Strategy::SUM_ALL;

ASMCodeline::ASMCodeline(const CodeData& codedata, int _line_number) :
line_index(line_vec.size()), line_number(_line_number)
{
    QASSERT(codedata.line, "Empty line in codedata!");
    auto& line = *codedata.line;

    static std::vector<int> empty{};
    auto& latency = codedata.exec ? codedata.exec->latency : empty;
    auto& idle = codedata.exec ? codedata.exec->idle : empty;

    elements.at(Element::EASM) = std::make_unique<ASMLine>(_line_number, line);
    elements.at(Element::EHIT) = std::make_unique<HitcountLabel>(line.hitcount);
    if (line.idle_sum) elements.at(Element::EIDLE) = std::make_unique<CyclesLabel>(idle, line.idle_sum, line.hitcount);
    elements.at(Element::ELATENCY) = std::make_unique<CyclesLabel>(latency, line.latency_sum, line.hitcount);

    std::string cppline = line.cppline;
    if (cppline.size() > SHORT_CPPLINE_MAXCHARS)
        cppline = "[...]" + cppline.substr(cppline.size() - SHORT_CPPLINE_MAXCHARS);

    elements.at(Element::ESOURCEREF) = std::make_unique<TextLineElement>(cppline);
}

ASMCodeline::~ASMCodeline() {}

int CyclesLabel::width(QFontMetrics& fm)
{
    if (local_strategy != global_strategy)
    {
        local_strategy = global_strategy;
        updateStrategy();
        InvalidateCache();
    }

    return this->Super::width(fm);
}

void CyclesLabel::paint(class QPainter& painter, int posx, int posy, int stepy, int overline)
{
    if (local_strategy != global_strategy)
    {
        local_strategy = global_strategy;
        updateStrategy();
        InvalidateCache();
    }
    this->Super::paint(painter, posx, posy, stepy, overline);
}

void CyclesLabel::updateStrategy()
{
    if (all_hitcount == 0) return;

    if (cycles.size() == 0 && local_strategy > Strategy::MEAN_ALL)
    {
        this->text = "0";
        this->stdtext = "0";
        return;
    }

    int64_t value = 0;
    int64_t hit = cycles.size();

    switch (local_strategy)
    {
        case Strategy::SUM:
        case Strategy::MEAN:
            for (int v : cycles) value += v;
            break;
        case Strategy::MAX:
            for (int64_t v : cycles) value = max(value, v);
            break;
        case Strategy::ITERATION:
        {
            int iter = MainWindow::window->iteration_current.second;
            value = (iter >= 0 && iter < cycles.size()) ? cycles.at(MainWindow::window->iteration_current.second) : 0;
            break;
        }
        case Strategy::SUM_ALL:
        case Strategy::MEAN_ALL:
            value = all_cycles_sum;
            hit = all_hitcount;
            break;
        default: QWARNING(false, "Invalid strategy", return );
    }
    if (local_strategy == Strategy::MEAN || local_strategy == Strategy::MEAN_ALL)
        value = (value + hit / 2) / std::max<int64_t>(hit, 1);

    this->stdtext = std::to_string(value);
    this->text = stdtext.c_str();
}

void ASMCodeline::Populate(const std::vector<CodeData>& code)
{
    Clear();

    for (auto& line : code)
    {
        QWARNING(line.line, "Null line", continue);

        auto newline = std::make_shared<ASMCodeline>(line, line.line->index);

        line_vec.push_back(newline);
        line_map[line.line->index] = newline;

        auto asmelement = newline->elements.at(Element::EASM).get();
        if (auto* casted = dynamic_cast<ASMLine*>(asmelement))
        {
            if (auto lock = casted->line_ref.lock()) lock->refs.push_back(newline);
        }
    }
}

void ASMLine::setMouseHover(bool value)
{
    bHovering = value;
    if (auto ref = line_ref.lock()) ref->setRefHighlight(value, false);
}

ASMLine::ASMLine(int _line_number, const CodeData::Line& line) :
TextLineElement(line.inst), line_number(_line_number), codeobj(line.codeobj_id), addr(line.addr)
{
    try
    {
        auto shared = SourceLine::all_lines.at(line.cppline);
        line_ref = shared;
        shared->add_latency(line.type, line.latency_sum);
    }
    catch (std::exception&)
    {
        line_ref = {};
    }
}

void ASMLine::onMousePress()
{
    QASSERT(MainWindow::window, "Invalid window");

    int iteration = MainWindow::window ? MainWindow::window->iteration_current.second : -1;
    int64_t clock = WaveInstance::GetMainClock(line_number, iteration);
    if (clock >= 0) MainWindow::window->ScrollViewsTo(clock);

    if (auto ref = line_ref.lock()) ref->scrollTo();
}
