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

#include "asmcode.h"
#include <QPainter>
#include <QScrollArea>
#include <QTextLayout>
#include <algorithm>
#include <sstream>
#include "config/config.hpp"
#include "data/wavemanager.h"
#include "mainwindow.h"
#include "sourcefile.h"
#include "util/diagnostic_log.h"

std::map<int, std::shared_ptr<ASMCodeline>> ASMCodeline::line_map{};
std::vector<std::shared_ptr<ASMCodeline>> ASMCodeline::line_vec{};

CyclesLabel::Strategy CyclesLabel::global_strategy = CyclesLabel::Strategy::SUM_ALL;

ASMCodeline::ASMCodeline(const CodeData& codedata, int _line_number) :
line_index(line_vec.size()), line_number(_line_number)
{
    QASSERT(codedata.line, "Empty line in codedata!");
    auto& line = *codedata.line;

    static const std::vector<int> empty{};
    auto& latency = codedata.exec ? codedata.exec->latency : empty;
    auto& idle = codedata.exec ? codedata.exec->idle : empty;

    elements.at(Element::EASM) = std::make_unique<ASMLine>(_line_number, line);
    elements.at(Element::EHIT) =
        std::make_unique<CyclesLabel>(std::vector<int>{(int) latency.size()}, line.hitcount, line.hitcount > 0 ? 1 : 0);
    elements.at(Element::EIDLE) = std::make_unique<CyclesLabel>(idle, line.idle_sum, line.hitcount);
    elements.at(Element::ELATENCY) = std::make_unique<CyclesLabel>(latency, line.latency_sum, line.hitcount);

    elements.at(Element::EPCSamples) = std::make_unique<NumberLabel>(codedata.line->pcsamples);
    elements.at(Element::EPCIssued) = std::make_unique<NumberLabel>(codedata.line->pcsamples - codedata.line->pcstalls);
    elements.at(Element::EPCStalls) = std::make_unique<NumberLabel>(codedata.line->pcstalls);

    std::string cppline = line.cppline;
    constexpr size_t max_source_chars = 32;
    if (cppline.size() > max_source_chars) cppline = "[...]" + cppline.substr(cppline.size() - max_source_chars);

    std::stringstream ss;
    ss << std::hex << line.addr << ' ';
    elements.at(Element::ECODEOBJ) = std::make_unique<TextLineElement>(std::to_string(line.codeobj_id));
    elements.at(Element::EADDRESS) = std::make_unique<TextLineElement>(ss.str());
    elements.at(Element::ESOURCEREF) = std::make_unique<TextLineElement>(cppline);

    hotspot.add_latency(line.type, {line.latency_sum, line.stall_sum, line.idle_sum}, {line.pcsamples, line.pcstalls});

    for (size_t i = 0; i < line.stallreasons.size(); i++) hotspot.stall_reason.at(i) = line.stallreasons.at(i);
}

ASMCodeline::~ASMCodeline() {}

int CyclesLabel::width(QFontMetrics& fm)
{
    refreshText();
    return this->Super::width(fm);
}

void CyclesLabel::paint(class QPainter& painter, int posx, int posy, int stepy, int overline)
{
    refreshText();
    this->Super::paint(painter, posx, posy, stepy, overline);
}

void CyclesLabel::refreshText()
{
    const int iteration = MainWindow::window ? MainWindow::window->iteration_current.second : -1;
    if (local_strategy != global_strategy || (global_strategy == Strategy::ITERATION && cached_iteration != iteration))
    {
        local_strategy = global_strategy;
        cached_iteration = iteration;
        updateStrategy();
        InvalidateCache();
    }
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
            for (int64_t v : cycles) value = std::max(value, v);
            break;
        case Strategy::ITERATION:
        {
            int iter = MainWindow::window ? MainWindow::window->iteration_current.second : -1;
            value = (iter >= 0 && iter < cycles.size()) ? cycles.at(iter) : 0;
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
    line_vec.reserve(code.size());

    for (auto& line : code)
    {
        QWARNING(line.line, "Null line", continue);

        auto newline = std::make_shared<ASMCodeline>(line, line.line->index);

        line_vec.push_back(newline);
        line_map[line.line->index] = newline;

        auto asmelement = newline->elements.at(Element::EASM).get();
        if (auto* casted = dynamic_cast<ASMLine*>(asmelement))
        {
            for (auto& ref : casted->line_ref)
                if (auto lock = ref.lock()) lock->refs.push_back(newline);
        }
    }
}

std::vector<std::pair<std::string, std::string>> ASMLine::callstack() const
{
    std::vector<std::pair<std::string, std::string>> callstack{};

    for (auto& ref : line_ref)
    {
        if (auto locked = ref.lock())
        {
            auto name = locked->parent ? locked->parent->filename : "Unknown";
            auto pos = name.find_last_of('/');
            if (pos != std::string::npos) name = name.substr(pos + 1);

            auto text = name + ':' + std::to_string(locked->line_number + 1);
            callstack.push_back({text, locked->getStdText()});
        }
    }

    return callstack;
}

void ASMLine::setMouseHover(bool value)
{
    bHovering = value;
    for (auto& ref : line_ref)
        if (auto locked = ref.lock()) locked->setRefHighlight(value, false);
}

ASMLine::ASMLine(int _line_number, const CodeData::Line& line) :
TextLineElement(line.inst),
line_number(_line_number),
codeobj(line.codeobj_id),
addr(line.addr),
instruction_kind(Isa::classifyInstruction(line.inst))
{
    const auto mnemonic = Isa::instructionMnemonic(stdtext);
    if (!mnemonic.empty())
    {
        // Only ASCII indentation precedes the mnemonic. Cache UTF-16 bounds
        // once per instruction rather than parsing during every repaint.
        mnemonic_start = static_cast<int>(mnemonic.data() - stdtext.data());
        mnemonic_length = QString::fromUtf8(mnemonic.data(), static_cast<int>(mnemonic.size())).size();
    }

    constexpr std::string_view separator = " -> ";
    std::string_view references = line.cppline;
    while (!references.empty())
    {
        const auto end = references.find(separator);
        const auto reference = references.substr(0, end);
        references = end == std::string_view::npos ? std::string_view{} : references.substr(end + separator.size());
        // Missing snapshots are normal, not exceptional. Avoid throwing for
        // every instruction in a large listing with partial debug information.
        const auto found = SourceLine::all_lines.find(std::string(reference));
        if (found == SourceLine::all_lines.end() || !found->second) continue;
        const auto& source = found->second;
        line_ref.push_back(source);
        source->add_latency(
            line.type, {line.latency_sum, line.stall_sum, line.idle_sum}, {line.pcsamples, line.pcstalls}
        );
    }
}

void ASMLine::drawText(QPainter& painter, int x, int baseline)
{
    if (instruction_kind == Isa::InstructionKind::Other)
    {
        Super::drawText(painter, x, baseline);
        return;
    }

    // Shape the visible line once, applying color to just the mnemonic. This
    // preserves operand spacing (including tabs) without splitting or drawing
    // the string twice. No layouts or widgets are retained for offscreen rows.
    QTextLayout layout(text, painter.font(), painter.device());
    QTextLayout::FormatRange mnemonic;
    mnemonic.start = mnemonic_start;
    mnemonic.length = mnemonic_length;
    mnemonic.format.setForeground(Isa::instructionColor(instruction_kind, WindowColors::isDark(), painter.pen().color())
    );
    layout.setFormats({mnemonic});
    layout.beginLayout();
    const auto line = layout.createLine();
    layout.endLayout();
    layout.draw(&painter, QPointF(x, baseline - line.ascent()));
}

void ASMLine::onMousePress()
{
    QASSERT(MainWindow::window, "Invalid window");

    MainWindow::window->SetSearchText(getStdText());

    int iteration = MainWindow::window ? MainWindow::window->iteration_current.second : -1;
    int64_t clock = WaveInstance::GetMainClock(line_number, iteration);
    if (clock >= 0) MainWindow::window->ScrollViewsTo(clock);

    // First, we attempt to scroll to the current file being displayed
    if (auto* sourcetab = MainWindow::window->source_filetab)
    {
        auto* source = dynamic_cast<QScrollArea*>(sourcetab->currentWidget());
        if (source)
        {
            for (auto& ref : line_ref)
            {
                if (auto locked = ref.lock())
                {
                    if (locked->parent == source->widget())
                    {
                        locked->scrollTo();
                        return;
                    }
                }
            }
        }
    }

    // If current widget is not one of our source files, we scroll to the first one
    for (auto& ref : line_ref)
        if (auto locked = ref.lock())
        {
            locked->scrollTo();
            return;
        }
}
