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

#include "data/waitcnt/analysis.h"

#include <algorithm>

int64_t MemoryCounter::extract_waitcnt(const std::string& str) const
{
    size_t pos = str.find(name);
    size_t iter = pos + name.size() + 1;

    if (pos == std::string::npos || iter >= str.size()) return 0;

    if (str.find("0x") == std::string::npos) return atoi(str.data() + iter);

    while (iter < str.size() && str.at(iter) == ' ') iter++;

    if (iter + 2 < str.size() && str.at(iter) == '0' && str.at(iter + 1) == 'x')
        return std::stoi(str.substr(iter + 2), nullptr, 16);
    else if (iter < str.size())
        return atoi(str.data() + iter);
    else
        return 0;
}

std::vector<int> MemoryCounter::join_and_reset(int64_t offset, std::vector<int>& flats)
{
    std::vector<int> ret = std::move(flats);
    flats.clear();

    offset = std::max(std::min<int64_t>(offset, list.size()), int64_t{0});
    ret.insert(ret.end(), list.begin(), list.begin() + offset);
    list.erase(list.begin(), list.begin() + offset);
    return ret;
}

std::optional<std::vector<int>> MemoryCounter::handle_mem_op(const std::string& inst, std::vector<int>& flat_list)
{
    int64_t wait_n = extract_waitcnt(inst);

    if (wait_n == 0) order = Ordering::MEMORY_SEQUENTIAL;

    if (order == Ordering::MEMORY_SEQUENTIAL)
    {
        auto joined = join_and_reset(list.size() - wait_n, flat_list);
        if (!joined.empty()) return joined;
    }
    return std::nullopt;
}

std::vector<Canvas::WaitList> buildWaitcntFromTokens(
    int gfxip, const TokenMap& tokens, const std::vector<CodeData>& code
)
{
    std::vector<LineWaitcnt> results;

    if (gfxip == 9)
        results = waitcnt_gfx9(tokens, code);
    else if (gfxip == 10 || gfxip == 11)
        results = waitcnt_gfx10(tokens, code);
    else
        results = waitcnt_gfx12(tokens, code);

    std::vector<Canvas::WaitList> waitcnt;
    waitcnt.reserve(results.size());

    for (auto& entry : results)
    {
        Canvas::WaitList list{entry.line_number, {}};
        for (int dep : entry.dependencies) list.sources.push_back({dep, 0});
        waitcnt.push_back(std::move(list));
    }

    return waitcnt;
}
