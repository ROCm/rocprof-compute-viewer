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

#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include "graphics/canvas.h"
#include "util/custom_layouts.h"
#include "wave/token.h"

struct CodeData
{
    CodeData(
        int index,
        int hitcount,
        int64_t addr,
        int64_t codeobj_id,
        int64_t latency_sum,
        int64_t idle_sum,
        int64_t stall_sum,
        const std::string& line,
        const std::string& cppline
    ) :
    exec(nullptr)
    {
        this->line =
            std::make_shared<Line>(index, hitcount, addr, codeobj_id, latency_sum, idle_sum, stall_sum, line, cppline);
    };
    CodeData(const CodeData& other) { *this = other; }
    CodeData& operator=(const CodeData& other)
    {
        exec = nullptr;
        line = other.line;
        return *this;
    }

    struct Exec
    {
        Exec(int _wave_id) : wave_id(_wave_id){};
        int wave_id = -1;
        std::vector<int64_t> clock{};
        std::vector<int> latency{};
        std::vector<int> idle{};
    };
    struct Line
    {
        Line(
            int _index,
            int _hitcount,
            int64_t _addr,
            int64_t _codeobj_id,
            int64_t _latency_sum,
            int64_t _idle_sum,
            int64_t _stall_sum,
            const std::string& _inst,
            const std::string& _cpp
        ) :
        index(_index),
        hitcount(_hitcount),
        custom_type(0),
        addr(_addr),
        codeobj_id(_codeobj_id),
        latency_sum(_latency_sum),
        idle_sum(_idle_sum),
        stall_sum(_stall_sum),
        inst(_inst),
        cppline(_cpp)
        {}

        std::atomic<int> index{-1};
        std::atomic<int> type{0};
        const int hitcount = 0;
        std::atomic<int> custom_type{0};
        const int64_t addr = 0;
        const int64_t codeobj_id = 0;
        const int64_t latency_sum = 0;
        const int64_t idle_sum = 0;
        const int64_t stall_sum = 0;
        const std::string inst;
        const std::string cppline;
    };

    std::unique_ptr<Exec> exec{nullptr};
    std::shared_ptr<Line> line{nullptr};
};

struct WaveInfo
{
    std::string name;
    int64_t value;
    int64_t stalls;
};

struct TokenGroup
{
    struct TokenArray
    {
        Token token{};
        std::array<int, 16> cycles{};
        TokenArray& operator+=(const TokenArray& other)
        {
            token.cycles = std::max<int64_t>(token.cycles, other.token.clock + other.token.cycles - token.clock);
            for (size_t i = 0; i < cycles.size(); i++) cycles[i] += other.cycles[i];
            return *this;
        }
        Token finalize(int64_t res);
    };
    int64_t wave_begin = 0;
    int64_t wave_end = 0;
    TokenMap tokens;
    std::map<int64_t, WaveState> timeline;
    std::array<TokenMap, 9> token_mip;
    bool bInitialized = false;

    void Draw(class QPainter& painter, int64_t viewstart, int64_t viewend, int posy);
    void SetMipN();
    void SetMipN(const std::vector<TokenArray>& array, size_t M);
};

struct WaveInstance : public TokenGroup
{
    WaveInstance(const std::string& path);
    virtual ~WaveInstance();

    std::vector<CodeData> code;
    std::vector<WaitList> waitcnt;
    std::vector<WaveInfo> wave_info;
    std::string path;

    std::map<int, std::vector<int64_t>> line_to_clock{};

    int64_t WaveBegin() const { return wave_begin; }
    int64_t WaveEnd() const { return wave_end; }

    static int64_t GetMainClock(int code_line, int iteration);

    static int64_t BaseClock() { return Token::bIsNaviWave ? 1 : 4; };
    static std::shared_ptr<WaveInstance> Get(const std::string& path);
    static void InvalidadeCache();

    static std::shared_ptr<WaveInstance> main_wave;
};
