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
#include <memory>
#include <unordered_set>
#include "container/datanode.h"
#include "plot.h"

#define BANKS 2

//! Class for visualizing wave states
class WavePlotView : public PlotGraph
{
public:
    WavePlotView() : WavePlotView(nullptr){};
    WavePlotView(class QWidget* parent) : PlotGraph(1, parent){};
    void LoadWaveStateData(const std::string& filename, int SE);
    virtual void UpdateGraphTable(float timepos) override;

    static std::vector<std::string> state_names;

private:
    static std::vector<QColor> colors;
};

//! Class for visualizing performance counters
class CounterPlotView : public PlotGraph
{
public:
    // List of [compute_unit][counter_id] -> sum of counter
    using CounterList = std::array<double, 4 * BANKS>;
    // First = per CU counter, second = Peak rates
    using CounterAccum = std::array<CounterList, NUM_CU>;

    CounterPlotView() : CounterPlotView(nullptr){};
    CounterPlotView(class QWidget* parent) : PlotGraph(1, parent){};
    virtual ~CounterPlotView() = default;

    virtual void UpdateDataSelection(
        const std::vector<std::string>& counters_names,
        uint64_t se_mask,
        uint64_t cu_mask,
        std::unordered_set<std::string>& disable_counters
    ) = 0;
    virtual void UpdateGraphTable(float timepos) override;
};

//! Class for visualizing GPU occupancy
class OccupancyPlotView : public PlotGraph
{
public:
    OccupancyPlotView() : OccupancyPlotView(nullptr){};
    OccupancyPlotView(class QWidget* parent) : PlotGraph(2, parent){};
    virtual void LoadOccupancyData(const std::string& filename);
    virtual void UpdateGraphTable(float timepos) override;

protected:
};

//! Class for visualizing GPU occupancy
class DispatchPlotView : public OccupancyPlotView
{
public:
    DispatchPlotView(class QWidget* parent) : OccupancyPlotView(parent){};
    virtual void LoadOccupancyData(const std::string& filename) override;
    virtual void UpdateGraphTable(float timepos) override{};

    const std::vector<int>& seList() const { return se_list; }

private:
    std::vector<int> se_list{};
};

//! Class for visualizing performance counters
class TraceCounterPlotView : public CounterPlotView
{
public:
    TraceCounterPlotView() : TraceCounterPlotView(nullptr){};
    TraceCounterPlotView(class QWidget* parent);
    virtual ~TraceCounterPlotView() = default;

    // Returns per CU accumulated counters
    CounterAccum LoadCounterData(class JsonRequest& file, int SE);
    void UpdateDataSelection(
        const std::vector<std::string>& counters_names,
        uint64_t se_mask,
        uint64_t cu_mask,
        std::unordered_set<std::string>& disable_counters
    ) override;

    CounterList GetPeakRates();

private:
    std::array<std::unique_ptr<class GPUCounterNode>, BANKS> rootnodes{};
};
