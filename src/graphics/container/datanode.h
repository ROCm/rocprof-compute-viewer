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

#include <array>
#include <map>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <vector>
#include "util/custom_layouts.h"

#define NUM_CU 16

struct CounterData
{
    int64_t time = 0;
    int32_t events[4] = {0, 0, 0, 0};
    int8_t cu = 0;
    int8_t se = 0;

    std::string toString() const
    {
        std::stringstream ss;
        ss << "t:" << time << " c:" << int(cu) << " se:" << int(se) << " ev: ";
        ss << events[0] << " " << events[1] << " " << events[2] << " " << events[3] << '\n';
        return ss.str();
    }
    CounterData& operator+=(const CounterData& other)
    {
        for (int i = 0; i < 4; i++) events[i] += other.events[i];
        return *this;
    }
    CounterData& operator-=(const CounterData& other)
    {
        for (int i = 0; i < 4; i++) events[i] -= other.events[i];
        return *this;
    }

    bool operator==(const CounterData& other) const { return this->time == other.time; }
    bool operator<(const CounterData& other) const { return this->time < other.time; }
    bool operator<=(const CounterData& other) const { return this->time <= other.time; }

    int linearpos() const { return se * NUM_CU + cu; }

    // static std::unordered_map<int8_t, int64_t> shader_order_offset;
};

class CUCounterNode
{
    set_tracked();

public:
    CUCounterNode(int _cu, std::vector<CounterData>&& _data) : cu(_cu), data{std::move(_data)} {};
    int64_t getDelta() const;
    void fillDelta(int64_t delta);

    const int cu;
    std::vector<CounterData> data;
};

class SECounterNode
{
    set_tracked();

public:
    SECounterNode(int se, const std::vector<CounterData>& data);
    void AccumFromMask(std::vector<CounterData>& out, uint64_t cu_mask);
    int64_t getDelta() const;
    void fillDelta(int64_t delta);

    const int se;
    std::array<std::unique_ptr<CUCounterNode>, NUM_CU> cu_nodes;
};

// Root = GPU
class GPUCounterNode
{
    set_tracked();

public:
    void Insert(int SE, const std::vector<CounterData>& data);
    int64_t getDelta() const;
    void fillDelta(int64_t delta);
    std::vector<CounterData> AccumFromMask(uint64_t se_mask, uint64_t cu_mask);

protected:
    std::vector<std::unique_ptr<SECounterNode>> se_nodes;
};

inline int64_t verify_skew(int64_t first, int64_t second)
{
    auto arg1 = std::min(first, second);
    auto arg2 = std::max(first, second);
    return (arg2 <= 8 * arg1 / 7) ? arg2 : arg1;
}
