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

float firstWindowStart(const DerivedCounter::Tensor& clock, size_t xcc, size_t samples)
{
    const float endpoint = clock.at(xcc, 0, 0, 0);
    return samples > 1 ? endpoint - (clock.at(xcc, 0, 0, 1) - endpoint) : endpoint;
}

size_t validSamples(const DerivedCounter::Tensor& values, const DerivedCounter::Tensor& clock, size_t xcc)
{
    const size_t clock_xcc = clockXcc(values, xcc);
    size_t count = std::min(values.shape().getSamples(), clock.shape().getSamples());
    while (count > 0 && !clock.isValid(clock.linearIndex(clock_xcc, 0, 0, count - 1))) --count;
    return count;
}
} // namespace

std::vector<float> timestampDeltas(const SpmData& spm)
{
    std::vector<float> result(spm.timestamps.size(), 0);
    for (size_t xcc = 0; xcc < spm.xccCount(); ++xcc)
    {
        const size_t count = spm.validSamples(xcc);
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

static DerivedCounter::Tensor sumSpatial(const DerivedCounter::Tensor& values)
{
    std::vector<DerivedCounter::Axis> axes;
    if (values.shape().getSE() > 1) axes.push_back(DerivedCounter::Axis::SE);
    if (values.shape().getCU() > 1) axes.push_back(DerivedCounter::Axis::CU);
    return axes.empty() ? values : values.sum(axes);
}

static std::vector<Point> interval(
    const DerivedCounter::Tensor& values, const DerivedCounter::Tensor& clock, size_t xcc
)
{
    std::vector<Point> result;
    const size_t samples = validSamples(values, clock, xcc);
    if (samples == 0) return result;

    const size_t clock_xcc = clockXcc(values, xcc);
    result.reserve(samples + 1);
    for (size_t sample = 0; sample < samples; ++sample)
    {
        const size_t index = values.linearIndex(xcc, 0, 0, sample);
        if (!values.isValid(index)) continue;
        const float time =
            sample == 0 ? firstWindowStart(clock, clock_xcc, samples) : clock.at(clock_xcc, 0, 0, sample - 1);
        result.push_back({time, values[index]});
    }
    if (!result.empty()) result.push_back({clock.at(clock_xcc, 0, 0, samples - 1), result.back().value});
    return result;
}

static std::vector<Point> mergeXcc(const DerivedCounter::Tensor& values, const DerivedCounter::Tensor& clock)
{
    using Event = std::tuple<float, size_t, size_t>;
    const size_t xcc_count = values.shape().getXCC();
    const size_t samples = values.shape().getSamples();
    std::priority_queue<Event, std::vector<Event>, std::greater<Event>> events;
    std::vector<float> current(xcc_count, 0);
    std::vector<size_t> counts(xcc_count, 0);
    float final_time = 0;

    for (size_t xcc = 0; xcc < xcc_count; ++xcc)
    {
        const size_t clock_xcc = clockXcc(values, xcc);
        if (clock_xcc >= clock.shape().getXCC()) continue;
        counts[xcc] = validSamples(values, clock, xcc);
        if (counts[xcc] == 0) continue;
        events.emplace(firstWindowStart(clock, clock_xcc, counts[xcc]), xcc, 0);
        final_time = std::max(final_time, clock.at(clock_xcc, 0, 0, counts[xcc] - 1));
    }
    if (events.empty()) return {};

    std::vector<Point> result;
    result.reserve(samples * xcc_count + 1);
    result.push_back({std::get<0>(events.top()) - 1, 0});

    while (!events.empty())
    {
        const float time = std::get<0>(events.top());
        bool changed = false;
        do {
            const auto event = events.top();
            events.pop();
            const size_t xcc = std::get<1>(event);
            size_t sample = std::get<2>(event);
            const size_t index = values.linearIndex(xcc, 0, 0, sample);
            if (values.isValid(index))
            {
                current[xcc] = values[index];
                changed = true;
            }
            if (++sample < counts[xcc]) events.emplace(clock.at(clockXcc(values, xcc), 0, 0, sample - 1), xcc, sample);
        }
        while (!events.empty() && std::get<0>(events.top()) == time);

        if (changed)
        {
            float total = 0;
            for (float value : current) total += value;
            result.push_back({time, total});
        }
    }

    if (result.back().time < final_time) result.push_back({final_time, result.back().value});
    return result;
}

std::vector<Point> makePlotSeries(const DerivedCounter::Tensor& values, const DerivedCounter::Tensor& clock)
{
    const auto plot_values = sumSpatial(values);
    return plot_values.shape().getXCC() > 1 ? mergeXcc(plot_values, clock) : interval(plot_values, clock, 0);
}
} // namespace SpmSeries
