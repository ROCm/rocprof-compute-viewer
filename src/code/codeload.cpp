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

#include "codeload.hpp"
#include <QPainter>
#include <set>
#include <shared_mutex>
#include "json/include/nlohmann/json.hpp"
#include "util/version.h"
#include "wave/scroll.h"
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

std::vector<CodeData> CodeData::GetCode()
{
    return cache;
}

std::vector<CodeData> CodeData::LoadCode(const std::string& path)
{
    std::unique_lock<std::mutex> lk(code_mutex);

    if (!cache.empty() && loaded_cache == path) return cache;

    JsonRequest coderequest(path);

    if (coderequest.fail() || coderequest.bad()) throw std::exception{};

    cache.clear();
    loaded_cache = path;

    int i = 0;
    for (auto& c : coderequest.data["code"])
    {
        std::string cppline = c[3].is_null() ? "" : std::string(c[3]);

        int64_t idle = int64_t(c[9]);
        int64_t stall = int64_t(c[8]);

        int64_t pcissues = 0;
        int64_t pcstalls = 0;

        cache.push_back(
            {int(c[2]),
            int(c[6]),
            int64_t(c[5]),
            int64_t(c[4]),
            int64_t(c[7]),
            idle,
            stall,
            pcissues + pcstalls,
            pcstalls,
            std::string(c[0]),
            cppline,
            {}}
        );

        for (auto& [custom_token, custom_type] : Config::CustomTokens())
            if (cache.back().line->inst.find(custom_token) == 0)
                cache.back().line->custom_type = custom_type;
        i += 1;
    }

    return cache;
}
