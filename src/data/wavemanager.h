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

#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include "code/codeload.hpp"
#include "data/records.h"
#include "graphics/canvas.h"
#include "util/custom_layouts.h"
#include "wave/token.h"

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

    void Draw(class QPainter& painter, int64_t viewstart, int64_t viewend);
    void SetMipN();
    void SetMipN(const std::vector<TokenArray>& array, size_t M);
};

struct WaveInstance : public TokenGroup
{
    WaveInstance(const std::string& path, int64_t time_offset = 0);
    WaveInstance(const wave_record_t& rec, const std::vector<CodeData>& code_data);
    virtual ~WaveInstance();

    std::vector<CodeData> code;
    std::vector<Canvas::WaitList> waitcnt;
    std::vector<WaveInfo> wave_info;
    std::string path;
    int cu = -1;

    std::map<int, std::vector<int64_t>> line_to_clock{};

    int64_t WaveBegin() const { return wave_begin; }
    int64_t WaveEnd() const { return wave_end; }

    std::vector<Canvas::WaitList> get_branch_targets() const;

    /// Compute waitcnt from instruction stream and ISA text (decoder path).
    /// Only call for the selected wave — scans all tokens + code text.
    void buildWaitcnt(int gfxip);

    static int64_t GetMainClock(int code_line, int iteration);

    static int64_t BaseClock() { return Token::bIsNaviWave ? 1 : 4; };
    static std::shared_ptr<WaveInstance> Get(const std::string& path, int64_t time_offset = 0);
    static std::shared_ptr<WaveInstance> GetFromRecord(
        const std::string& id, const wave_record_t& rec, const std::vector<CodeData>& code_data
    );
    static void InvalidadeCache();

    static std::shared_ptr<WaveInstance> main_wave;

private:
    /// Slot-collision handler shared by both constructors. Bumps `token.slot`
    /// up to 3 if the previous slot at the same clock is still busy, then
    /// records the slot's last-seen clock and appends to `tokens`.
    void appendTokenWithSlotBump(Token&& token, std::array<int64_t, 4>& prev_clock, std::array<int64_t, 4>& last_clock);

    /// Walk `tokens` (already populated + Compile()'d), attribute each token
    /// to its `code` entry, populate `line_to_clock` and `code[*].exec`,
    /// and shrink_to_fit. Identical between the JSON and decoder constructors.
    void populateExecMetadata(int wave_id, bool isIdleInfo);
};
