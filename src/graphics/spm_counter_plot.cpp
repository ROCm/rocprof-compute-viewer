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

#include "specialized_plots.h"

#include <algorithm>

#include "analysis/spm_series.h"
#include "config/config.hpp"

namespace
{
std::vector<WeightedPoint> weightedPoints(std::vector<SpmSeries::Point>&& points)
{
    std::vector<WeightedPoint> result;
    result.reserve(points.size());
    for (const auto& point : points) result.push_back({point.time, point.value});
    return result;
}
} // namespace

void SPMCounterPlotView::LoadCounterData(const DataStore& store)
{
    sampled_counters.clear();
    sampled_clock.reset();
    sampled_spm_clock.reset();
    sampled_counts.clear();

    if (store.spm.empty()) return;

    sampled_counts = store.spm.sample_counts;
    const DerivedCounter::Shape clock_shape(sampled_counts.size(), 1, 1, store.spm.sample_count);
    sampled_clock = std::make_shared<DerivedCounter::Tensor>(clock_shape, store.spm.clock);
    sampled_spm_clock = std::make_shared<DerivedCounter::Tensor>(clock_shape, SpmSeries::timestampDeltas(store.spm));

    sampled_counters.reserve(store.spm.counters.size());
    for (const auto& counter : store.spm.counters)
        sampled_counters.push_back(std::make_shared<DerivedCounter::Tensor>(
            DerivedCounter::Shape(counter.xcc_count, counter.se_count, counter.instance_count, store.spm.sample_count),
            counter.values
        ));
}

void SPMCounterPlotView::addRawCounters(uint64_t, uint64_t)
{
    if (!sampled_clock) return;

    for (size_t counter_index = 0; counter_index < sampled_counters.size(); ++counter_index)
    {
        const auto summed = SpmSeries::sumSpatialForPlot(*sampled_counters[counter_index]);
        const std::string name = counter_index < counter_names.size() ? counter_names[counter_index]
                                                                      : "UNK_" + std::to_string(counter_index);
        auto datapoints = weightedPoints(
            SpmSeries::mergeXcc(summed, *sampled_clock, sampled_counts, summed.shape().getSamples(), 0, 0)
        );
        AddData(name, Config::PlotColors(counter_index), std::move(datapoints));
        raw_curve_sources.push_back(name);
    }
}

void SPMCounterPlotView::addDerivedSeries(
    const std::string& name, const DerivedCounter::Tensor& values, const DerivedCounter::Tensor& clock, int& color_index
)
{
    const auto plot_values = SpmSeries::sumSpatialForPlot(values);
    const size_t num_samples = std::min(plot_values.shape().getSamples(), clock.shape().getSamples());
    if (num_samples < 2) return;

    std::vector<WeightedPoint> datapoints;
    if (plot_values.shape().getXCC() > 1)
    {
        datapoints = weightedPoints(SpmSeries::mergeXcc(plot_values, clock, sampled_counts, num_samples, 0, 0));
    }
    else
    {
        const size_t plot_samples = SpmSeries::validSampleCount(plot_values, sampled_counts, 0, num_samples);
        datapoints = weightedPoints(SpmSeries::interval(plot_values, clock, 0, 0, 0, plot_samples));
    }

    AddData(name, Config::PlotColors(color_index++), std::move(datapoints));
}

void SPMCounterPlotView::buildDerivedManager()
{
    derivedmanager = std::make_shared<DerivedCounter::DerivedCounterManager>();
    if (!sampled_clock) return;

    for (size_t i = 0; i < sampled_counters.size() && i < counter_names.size(); ++i)
        derivedmanager->context().setCounter(counter_names[i], sampled_counters[i]);

    derivedmanager->context().setCounter("SCLOCK", sampled_clock);
    if (sampled_spm_clock) derivedmanager->context().setCounter("SPM_CLOCK", sampled_spm_clock);
}
