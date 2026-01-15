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

#include <atomic>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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
        int64_t pcsamples,
        int64_t pcstalls,
        const std::string& line,
        const std::string& cppline,
        const std::vector<int64_t>& stallreasons
    ) :
    exec(nullptr)
    {
        this->line = std::make_shared<Line>(
            index,
            hitcount,
            addr,
            codeobj_id,
            latency_sum,
            idle_sum,
            stall_sum,
            pcsamples,
            pcstalls,
            line,
            cppline,
            stallreasons
        );
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
            int64_t _pcsamples,
            int64_t _pcstalls,
            const std::string& _inst,
            const std::string& _cpp,
            const std::vector<int64_t>& _stallreasons
        ) :
        index(_index),
        hitcount(_hitcount),
        custom_type(0),
        addr(_addr),
        codeobj_id(_codeobj_id),
        latency_sum(_latency_sum),
        idle_sum(_idle_sum),
        stall_sum(_stall_sum),
        pcsamples(_pcsamples),
        pcstalls(_pcstalls),
        inst(_inst),
        cppline(_cpp),
        stallreasons(_stallreasons)
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
        const int64_t pcsamples = 0;
        const int64_t pcstalls = 0;

        const std::string inst;
        const std::string cppline;

        const std::vector<int64_t> stallreasons;
    };

    std::unique_ptr<Exec> exec{nullptr};
    std::shared_ptr<Line> line{nullptr};

    static void InvalidadeCache();
    static std::vector<CodeData> GetCode();
    static std::vector<CodeData> LoadCode(const std::string& path);
};
