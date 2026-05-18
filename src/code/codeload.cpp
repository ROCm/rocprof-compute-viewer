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

#include "codeload.hpp"
#include <mutex>
#include <set>
#include "config/config.hpp"
#include "util/jsonrequest.hpp"

std::mutex code_mutex;
std::string loaded_cache = "";
std::vector<CodeData> cache{};

void CodeData::InvalidadeCache()
{
    std::unique_lock<std::mutex> lk2(code_mutex);
    loaded_cache = "";
    cache.clear();
}

std::vector<CodeData> CodeData::GetCode() { return cache; }

void CodeData::ApplyCustomType(const std::shared_ptr<Line>& line)
{
    if (!line) return;
    for (auto& [custom_token, custom_type] : Config::CustomTokens())
        if (line->inst.find(custom_token) == 0) line->custom_type = custom_type;
}

std::vector<CodeData> CodeData::LoadCode(const std::string& path)
{
    std::unique_lock<std::mutex> lk(code_mutex);

    if (!cache.empty() && loaded_cache == path) return cache;

    JsonRequest coderequest(path);

    if (coderequest.fail() || coderequest.bad()) throw std::exception{};

    int stall_idx = -1;
    int issue_idx = -1;
    int reasons_idx = -1;

    // Header parsing is best-effort: if a column is malformed we still try to use whatever
    // indices we did manage to identify before the throw.
    try
    {
        int index = 0;
        for (auto& _entry : coderequest.data["header"])
        {
            auto entry = std::string(_entry);
            if (entry == "PC_Issued") issue_idx = index;
            if (entry == "PC_Stalled") stall_idx = index;
            if (entry == "Stall_Reasons") reasons_idx = index;

            index++;
        }
    }
    catch (...)
    {
        RCV_LOG();
    }

    // Build into a local first so a mid-loop throw doesn't leave a partially-populated
    // cache stamped as valid. Per-row try/catch keeps one bad row from killing the rest.
    std::vector<CodeData> built;
    int row_failures = 0;
    for (auto& c : coderequest.data["code"])
    {
        try
        {
            std::vector<int64_t> reasons;
            if (reasons_idx >= 0)
                for (auto& entry : c[reasons_idx]) reasons.push_back(int64_t(entry));

            int64_t pcissues = issue_idx >= 0 ? int64_t(c[issue_idx]) : 0;
            int64_t pcstalls = stall_idx >= 0 ? int64_t(c[stall_idx]) : 0;

            built.push_back(
                {int(c[2]),
                 int(c[6]),
                 int64_t(c[5]),
                 int64_t(c[4]),
                 int64_t(c[7]),
                 int64_t(c[9]),
                 int64_t(c[8]),
                 pcissues + pcstalls,
                 pcstalls,
                 std::string(c[0]),
                 c[3].is_null() ? "" : std::string(c[3]),
                 reasons}
            );
            ApplyCustomType(built.back().line);
        }
        catch (const std::exception& e)
        {
            if (row_failures++ == 0) std::cerr << "Warning: code.json row skipped: " << e.what() << std::endl;
        }
    }

    if (row_failures > 1)
        std::cerr << "Warning: " << row_failures << " code.json rows failed to parse (" << path << ")" << std::endl;

    cache = std::move(built);
    loaded_cache = path;
    return cache;
}
