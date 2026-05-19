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

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "code/codeload.hpp"
#include "graphics/canvas.h"
#include "wave/token.h"

struct LineWaitcnt
{
    int line_number{0};
    std::vector<int> dependencies{};
};

class MemoryCounter
{
public:
    enum Ordering
    {
        MEMORY_SEQUENTIAL = 0,
        MEMORY_PARALLEL
    };

    MemoryCounter(std::string_view _name) : name(_name) {}

    int64_t extract_waitcnt(const std::string& str) const;
    std::vector<int> join_and_reset(int64_t offset, std::vector<int>& flats);
    std::optional<std::vector<int>> handle_mem_op(const std::string& inst, std::vector<int>& flat_list);

    void clearTo(std::vector<int>& out)
    {
        out.insert(out.end(), list.begin(), list.end());
        list.clear();
    };

    const std::string name;
    Ordering order = Ordering::MEMORY_SEQUENTIAL;
    std::vector<int> list{};
};

std::vector<LineWaitcnt> waitcnt_gfx9(const TokenMap& tokens, const std::vector<CodeData>& code);
std::vector<LineWaitcnt> waitcnt_gfx10(const TokenMap& tokens, const std::vector<CodeData>& code);
std::vector<LineWaitcnt> waitcnt_gfx12(const TokenMap& tokens, const std::vector<CodeData>& code);

std::vector<Canvas::WaitList> buildWaitcntFromTokens(
    int gfxip, const TokenMap& tokens, const std::vector<CodeData>& code
);
