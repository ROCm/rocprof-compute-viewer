// MIT License
//
// Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.
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

#include "spm_series.h"

#include <algorithm>
#include <queue>
#include <tuple>

namespace SpmSeries
{
namespace
{
size_t clockXcc(const DerivedCounter::Tensor& values, size_t xcc)
{
    const auto& indices = values.xccIndices();
    return indices.size() == values.shape().getXCC() ? indices[xcc] : xcc;
}
} // namespace

std::vector<float> timestampDeltas(const SpmData& spm)
{
    std::vector<float> result(spm.timestamps.size(), 0);
    for (size_t xcc = 0; xcc < spm.sample_counts.size(); ++xcc)
    {
        const size_t count = spm.sample_counts[xcc];
        if (count == 0) continue;

        const size_t base = xcc * spm.sample_count;
        double total = 0;
        for (size_t sample = 1; sample < count; ++sample)
        {
            const uint64_t delta = spm.timestamps[base + sample] - spm.timestamps[base + sample - 1];
            result[base + sample] = static_cast<float>(delta);
            total += delta;
        }

        const float average = count > 1 ? static_cast<float>(total / (count - 1)) : 0.0f;
        result[base] = average;
        for (size_t sample = count; sample < spm.sample_count; ++sample) result[base + sample] = average;
    }
    return result;
}

DerivedCounter::Tensor sumSpatialForPlot(const DerivedCounter::Tensor& values)
{
    std::vector<DerivedCounter::Axis> axes;
    if (values.shape().getSE() > 1) axes.push_back(DerivedCounter::Axis::SE);
    if (values.shape().getCU() > 1) axes.push_back(DerivedCounter::Axis::CU);
    return axes.empty() ? values : values.sum(axes);
}

std::vector<Point> interval(
    const DerivedCounter::Tensor& values,
    const DerivedCounter::Tensor& clock,
    size_t xcc,
    size_t se,
    size_t cu,
    size_t samples
)
{
    std::vector<Point> result;
    if (samples < 2) return result;

    const size_t clock_xcc = clockXcc(values, xcc);
    result.reserve(samples);
    for (size_t sample = 1; sample < samples; ++sample)
        result.push_back({clock.at(clock_xcc, 0, 0, sample - 1), values.at(xcc, se, cu, sample)});
    result.push_back({clock.at(clock_xcc, 0, 0, samples - 1), values.at(xcc, se, cu, samples - 1)});
    return result;
}

std::vector<Point> mergeXcc(
    const DerivedCounter::Tensor& values,
    const DerivedCounter::Tensor& clock,
    const std::vector<size_t>& sample_counts,
    size_t samples,
    size_t se,
    size_t cu
)
{
    using Event = std::tuple<float, size_t, size_t>;
    const size_t xcc_count = values.shape().getXCC();
    std::priority_queue<Event, std::vector<Event>, std::greater<Event>> events;
    std::vector<float> current(xcc_count, 0);
    std::vector<size_t> counts(xcc_count, 0);
    float final_time = 0;

    for (size_t xcc = 0; xcc < xcc_count; ++xcc)
    {
        const size_t clock_xcc = clockXcc(values, xcc);
        if (clock_xcc >= clock.shape().getXCC() || clock_xcc >= sample_counts.size()) continue;
        counts[xcc] = std::min(samples, sample_counts[clock_xcc]);
        if (counts[xcc] < 2) continue;
        events.emplace(clock.at(clock_xcc, 0, 0, 0), xcc, 1);
        final_time = std::max(final_time, clock.at(clock_xcc, 0, 0, counts[xcc] - 1));
    }
    if (events.empty()) return {};

    std::vector<Point> result;
    result.reserve(samples * xcc_count + 1);
    result.push_back({std::get<0>(events.top()) - 1, 0});

    while (!events.empty())
    {
        const float time = std::get<0>(events.top());
        do {
            const auto event = events.top();
            events.pop();
            const size_t xcc = std::get<1>(event);
            size_t sample = std::get<2>(event);
            current[xcc] = values.at(xcc, se, cu, sample);
            if (++sample < counts[xcc]) events.emplace(clock.at(clockXcc(values, xcc), 0, 0, sample - 1), xcc, sample);
        }
        while (!events.empty() && std::get<0>(events.top()) == time);

        float total = 0;
        for (float value : current) total += value;
        result.push_back({time, total});
    }

    if (result.back().time < final_time) result.push_back({final_time, result.back().value});
    return result;
}
} // namespace SpmSeries
