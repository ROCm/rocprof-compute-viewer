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

#include <QLabel>
#include <QPushButton>
#include <QWidget>
#include <map>
#include <vector>
#include "data/wavemanager.h"
#include "hotspot.hpp"
#include "textelement.h"
#include "util/custom_layouts.h"

/**
 *  Widget that displays information about an assembly instruction.
 *  To be used as a element of QCodelist.
 */
class ASMCodeline
{
public:
    enum Element
    {
        EASM = 0,
        EHIT,
        ELATENCY,
        EIDLE,
        ESOURCEREF,
        ENUMTYPES
    };

    explicit ASMCodeline(const CodeData& codedata, int line_number);
    virtual ~ASMCodeline();

    void setRefHighlight(bool value, bool click)
    {
        if (auto elem = elements.at(EASM).get()) elem->setRefHighlight(value, click);
    };

    const int line_index;
    const int line_number;
    std::array<std::unique_ptr<TextLineElement>, Element::ENUMTYPES> elements{};

    static void Populate(const std::vector<CodeData>& codedata);
    static void Clear()
    {
        line_map.clear();
        line_vec.clear();
    };

    HorizontalHotspot hotspot{};

    static std::map<int, std::shared_ptr<ASMCodeline>> line_map;
    static std::vector<std::shared_ptr<ASMCodeline>> line_vec;
};

//! A text widget containing an assembly instruction
class ASMLine : public TextLineElement
{
    using Super = TextLineElement;

public:
    // ASMLine(int line_number, const std::string& line, int64_t _codeobj, int64_t _addr, const std::string& sourceref);
    ASMLine(int line_number, const CodeData::Line& line);

    void setMouseHover(bool value) override;
    void onMousePress() override;
    std::vector<std::pair<std::string, std::string>> callstack() const;

    std::vector<std::weak_ptr<class SourceLine>> line_ref{};

    const int line_number;
    const int64_t codeobj;
    const int64_t addr;
};

//! A text widget containing the number of hits a instruction received
class NumberLabel : public TextLineElement
{
    using Super = TextLineElement;

public:
    NumberLabel(int64_t num) : Super(num ? std::to_string(num) : ""), number(num) {}

    const size_t number;
};

//! A text widget containing the number of hits a instruction received
class HitcountLabel : public NumberLabel
{
    using Super = NumberLabel;

public:
    HitcountLabel(int64_t num) : Super(num) {}
};

//! A text widget containing the cycles used for a particular instruction.
class CyclesLabel : public TextLineElement
{
    using Super = TextLineElement;

public:
    enum class Strategy
    {
        SUM_ALL = 0, ///< Sum over all waves
        MEAN_ALL,    ///< Mean over all waves
        ITERATION,   ///< Fetch current token iteration
        SUM,         ///< Sum over this wave
        MEAN,        ///< Mean over this wave
        MAX,         ///< Max over this wave
        LAST
    };

    CyclesLabel(const std::vector<int>& cycles, int64_t cycles_sum, int64_t hitcount) :
    Super((cycles_sum || hitcount) ? std::to_string(cycles_sum) : ""),
    cycles(cycles),
    all_cycles_sum(cycles_sum),
    all_hitcount(hitcount)
    {}

    void updateStrategy();

    static void setStrategy(Strategy strat) { global_strategy = strat; };

    static Strategy global_strategy;
    Strategy local_strategy = Strategy::SUM_ALL;

    virtual void paint(class QPainter& painter, int posx, int posy, int stepy, int overline) override;
    virtual int width(class QFontMetrics& fm) override;

private:
    std::vector<int> cycles;
    int64_t all_cycles_sum;
    int64_t all_hitcount;
};
