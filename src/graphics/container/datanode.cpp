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

#include "datanode.h"
#include <array>
#include <set>

// std::unordered_map<int8_t, int64_t> CounterData::shader_order_offset = {};

#define MINIMUM_DELTA_TIME (8 * 4)
#define MAXIMUM_DELTA_TIME (16 * 32 * 2 * 4)

int64_t CUCounterNode::getDelta() const
{
    int64_t min_delta = MAXIMUM_DELTA_TIME;
    int64_t last_time = -INT_MAX;
    for (auto& counter : data)
    {
        int64_t delta = counter.time - last_time;
        last_time = counter.time;

        if (delta >= MINIMUM_DELTA_TIME) min_delta = std::min<int64_t>(min_delta, delta);
    }
    return min_delta;
}

int64_t SECounterNode::getDelta() const
{
    int64_t min_delta = MAXIMUM_DELTA_TIME;
    for (auto& node : cu_nodes)
        if (node.get()) min_delta = std::min<int64_t>(min_delta, node->getDelta());
    return min_delta;
}

int64_t GPUCounterNode::getDelta() const
{
    std::set<int64_t> deltas{};
    for (auto& node : se_nodes)
        if (node) deltas.insert(node->getDelta());

    deltas.erase(MAXIMUM_DELTA_TIME);

    if (deltas.empty()) return MAXIMUM_DELTA_TIME;

    int64_t min_delta = *deltas.begin();
    if (deltas.size() == 1) return min_delta;

    return verify_skew(min_delta, *std::next(deltas.begin()));
}

void CUCounterNode::fillDelta(int64_t delta)
{
    if (!data.size()) return;

    std::vector<CounterData> new_data;
    new_data.reserve(data.size());

    CounterData zerocounter = data.at(0);
    for (int i = 0; i < 4; i++) zerocounter.events[i] = 0;

    for (auto& counter : data)
    {
        if (counter.time > zerocounter.time) new_data.push_back(zerocounter);
        new_data.push_back(counter);
        zerocounter.time = counter.time + delta;
    }

    this->data = std::move(new_data);
}

void SECounterNode::fillDelta(int64_t delta)
{
    for (auto& node : cu_nodes)
        if (node.get()) node->fillDelta(delta);
}

void GPUCounterNode::fillDelta(int64_t delta)
{
    std::cout << "Counters delta: " << delta << std::endl;
    for (auto& node : se_nodes)
        if (node.get()) node->fillDelta(delta);
}

SECounterNode::SECounterNode(int _se, const std::vector<CounterData>& data) : se(_se)
{
    std::array<std::vector<CounterData>, NUM_CU> cu_data;
    for (auto& perf : data) cu_data.at(perf.cu & 0xF).push_back(perf);

    for (size_t cu = 0; cu < NUM_CU; cu++)
        cu_nodes[cu] = std::make_unique<CUCounterNode>(cu, std::move(cu_data.at(cu)));
}

void SECounterNode::AccumFromMask(std::vector<CounterData>& out, uint64_t cu_mask)
{
    {
        size_t ret_size = 0;
        for (size_t cu = 0; cu < NUM_CU; cu++)
            if (cu_nodes.at(cu).get() && ((cu_mask >> cu) & 1)) ret_size += cu_nodes.at(cu)->data.size();

        out.reserve(out.size() + ret_size);
    }

    for (size_t cu = 0; cu < NUM_CU; cu++)
        if (cu_nodes.at(cu).get() && ((cu_mask >> cu) & 1))
            out.insert(out.end(), cu_nodes.at(cu)->data.begin(), cu_nodes.at(cu)->data.end());
}

template <typename Type> std::vector<Type> FromMask(std::vector<Type>& ret, int64_t res, int max_linear_pos)
{
    std::sort(ret.begin(), ret.end(), [](const Type& d1, const Type& d2) -> bool { return d1.time < d2.time; });

    if (!ret.size()) return {};

    std::vector<Type> accumulated{};
    accumulated.push_back(Type{});

    std::vector<Type> last_state{};
    last_state.resize(max_linear_pos);

    for (const Type& counter : ret)
    {
        Type newcounter = counter;
        newcounter += accumulated.back();
        newcounter -= last_state.at(counter.linearpos());
        if (accumulated.back().time + res < counter.time)
            accumulated.push_back(newcounter);
        else
            accumulated.back() = newcounter;
        last_state.at(counter.linearpos()) = counter;
    }

    return accumulated;
}

std::vector<CounterData> GPUCounterNode::AccumFromMask(uint64_t se_mask, uint64_t cu_mask)
{
    std::vector<CounterData> ret;

    for (auto& shader : se_nodes)
        if (shader.get())
            if ((se_mask >> shader->se) & 0x1) shader->AccumFromMask(ret, cu_mask);

    int max_se_num = 0;
    for (auto& shader : se_nodes)
        if (shader.get()) max_se_num = std::max(max_se_num, shader->se);

    return FromMask(ret, 4, (1 + max_se_num) * NUM_CU);
}

void GPUCounterNode::Insert(int SE, const std::vector<CounterData>& data)
{
    se_nodes.push_back(std::make_unique<SECounterNode>(SE, data));
}

void GPUCounterNode::getTimeRange(int64_t delta, int64_t& min_time, int64_t& max_time) const
{
    min_time = INT64_MAX;
    max_time = INT64_MIN;

    for (const auto& se_node : se_nodes)
    {
        if (!se_node) continue;
        for (size_t cu = 0; cu < NUM_CU; cu++)
        {
            if (!se_node->cu_nodes[cu]) continue;
            for (const auto& counter : se_node->cu_nodes[cu]->data)
            {
                min_time = std::min(min_time, counter.time);
                max_time = std::max(max_time, counter.time);
            }
        }
    }

    // Align to delta boundaries
    if (min_time != INT64_MAX && delta > 0)
    {
        min_time = (min_time / delta) * delta;
        max_time = ((max_time / delta) + 1) * delta;
    }
}
