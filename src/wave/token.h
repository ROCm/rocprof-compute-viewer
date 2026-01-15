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

#include <QPushButton>
#include <array>
#include <map>
#include "config/config.hpp"
#include "util/custom_layouts.h"
#include "util/highlight.h"
#include "util/wave_utils.h"

#define TOKEN_POSY()    2
#define TOKEN_HEIGHT()  15
#define SLOT_OFFSET()   1
#define WSTATE_POSY()   (TOKEN_POSY() + TOKEN_HEIGHT() + SLOT_OFFSET())
#define WSTATE_HEIGHT() 4

// Re-export for backward compatibility
using WaveUtils::mipShiftLeft;
using WaveUtils::mipShiftRight;

struct Token
{
    int64_t clock = 0;
    int8_t slot = 0;
    int8_t type = 0;
    // TOOD: combine with slot/type to represent longer stalls
    uint16_t stall = 0;
    int cycles = 0;
    int code_line = -1;
    uint32_t iteration_exec_and_overlap = 0;

    // Nth time the same ISA address was executed by this wave
    uint32_t iteration() const { return iteration_exec_and_overlap >> 2; };
    void setIteration(uint32_t iter)
    {
        iteration_exec_and_overlap &= 3u;
        iteration_exec_and_overlap |= iter << 2;
    };

    // Is a previous token overlapping with this?
    bool overlapped() const { return iteration_exec_and_overlap & 1; }
    void setOverlapped(bool b)
    {
        iteration_exec_and_overlap &= ~1u;
        iteration_exec_and_overlap |= b ? 1 : 0;
    };

    // Is a previous token overlapping with this?
    bool hiddenStall() const { return (iteration_exec_and_overlap >> 1) & 1; }
    void setHideStall(bool b)
    {
        iteration_exec_and_overlap &= ~2u;
        iteration_exec_and_overlap |= b ? 2 : 0;
    };

    int64_t end_time() const { return clock + cycles; }
    bool operator<(const Token& other) const
    {
        auto order = ((clock << 4) | slot);
        auto other_order = ((other.clock << 4) | other.slot);

        if (order == other_order) return cycles > other.cycles;
        return order < other_order;
    }
    bool inClock(int64_t _clock) const { return clock <= _clock && end_time() > _clock; }

    std::string ToolTip() const;
    void DrawToken(class QPainter& painter, int64_t viewstart, int64_t viewend, float penwidth) const;

    const StyleColor& GetColor() const { return GetColor(type); }
    const QColor& GetQColor() const { return GetQColor(type); }
    const QColor& GetToneColor() const { return GetToneColor(type); }
    const std::string_view GetName() const { return GetName(type); }

    static size_t GetNumColors() { return Config::TokenColors().size(); };
    static const StyleColor& GetColor(int i) { return Config::TokenColors()[i % Config::TokenColors().size()]; };
    static const QColor& GetQColor(int i) { return GetColor(i).qcolor; };
    static const QColor& GetToneColor(int i);
    static const std::string_view GetName(int i) { return GetColor(i).name; }

    static int64_t PosToClock(int64_t value);
    static int64_t GetTokenSize(int64_t value)
    {
        int64_t rounding = mipShiftLeft(1, mipmap_level) >> 1;
        return mipShiftRight((value * (bIsNaviWave ? 12 : 3) / 2) + rounding, mipmap_level);
    }
    static int SlotHeightReduction() { return mipmap_level >= 1 ? (2 - mipmap_level) : 3; }
    static void updateColors();

    static int mipmap_level;
    static bool bIsNaviWave;
};

class TokenMap : public std::vector<Token>
{
public:
    auto upper_bound(int64_t time) const
    {
        return std::upper_bound(this->begin(), this->end(), Token{time, 0, 0, 0, 0, 0, 0});
    }
    void Compile()
    {
        std::stable_sort(begin(), end());
        shrink_to_fit();
    }
    TokenMap::const_iterator get_token_in_clock(int64_t clock) const;
};

struct WaveState
{
    int64_t clock = 0;
    int64_t duration = 0;
    int state = 0;

    void DrawState(class QPainter& painter, int64_t viewstart, int64_t viewend);

    const QColor& GetColor() const { return GetStateColor(this->state); }
    const std::string& GetName() const { return GetStateName(this->state); }

    static const QColor& GetStateColor(int i) { return STATE_COLORS[i % STATE_COLORS.size()]; }
    static const std::string& GetStateName(int i) { return STATE_NAMES[i % STATE_NAMES.size()]; }
    static void updateColors();

    bool operator<(const WaveState& other) { return clock < other.clock; }
    bool operator==(const WaveState& other) { return clock == other.clock; }

private:
    static std::vector<QColor> STATE_COLORS;
    static std::vector<std::string> STATE_NAMES;
};
