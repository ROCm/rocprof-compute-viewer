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
#include <array>
#include <memory>
#include <unordered_set>
#include "analysis/derived_counter.h"
#include "container/datanode.h"
#include "data/datastore.h"
#include "plot.h"

//! Class for visualizing performance counters
class CounterPlotView : public PlotGraph
{
public:
    CounterPlotView() : CounterPlotView(nullptr){};
    CounterPlotView(class QWidget* parent) : PlotGraph(1, parent){};
    virtual ~CounterPlotView() = default;

    virtual void UpdateDataSelection(
        const std::vector<std::string>& counters_names,
        uint64_t se_mask,
        uint64_t cu_mask,
        const std::string& derivedDefinitions = ""
    );

    //! Update only the derived counters without reloading raw counter data
    void UpdateDerivedCounters(const std::string& derivedDefinitions, bool suppress);

    std::shared_ptr<DerivedCounter::DerivedCounterManager> getDerivedManager() { return derivedmanager; };
    std::vector<std::pair<std::string, std::shared_ptr<const DerivedCounter::Tensor>>> getDerived(
        const std::string& derived, bool suppress
    );

    void setDisablesCounters(const std::vector<std::pair<std::string, bool>>& names);

    virtual void UpdateGraphTable(float timepos) override;
    std::vector<double> GetPeakRates();
    // XCC vs SE vs CU vs CounterID
    DerivedCounter::Tensor GetAvgRates();

    virtual std::string getBuiltin() const = 0;
    virtual bool isBuiltin(const std::string& name) const = 0;

protected:
    virtual void buildDerivedManager();
    std::shared_ptr<DerivedCounter::DerivedCounterManager> derivedmanager{nullptr};

    std::vector<std::unique_ptr<class GPUCounterNode>> rootnodes{};
    std::vector<std::string> counter_names{};
    int64_t delta = INT64_MAX;
};

//! Class for visualizing GPU occupancy
class OccupancyPlotView : public PlotGraph
{
public:
    OccupancyPlotView() : OccupancyPlotView(nullptr){};
    OccupancyPlotView(class QWidget* parent) : PlotGraph(2, parent){};
    virtual void LoadOccupancyData(const std::string& filename);
    void LoadOccupancyData(const DataStore& store);
    virtual void UpdateGraphTable(float timepos) override;

protected:
};

//! Class for visualizing GPU occupancy
class DispatchPlotView : public OccupancyPlotView
{
public:
    DispatchPlotView(class QWidget* parent) : OccupancyPlotView(parent){};
    virtual void LoadOccupancyData(const std::string& filename) override;
    void LoadOccupancyData(DataStore& store);
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

    void LoadCounterData(const std::string& dirpath, int SE);

    virtual std::string getBuiltin() const override;
    virtual bool isBuiltin(const std::string& name) const override;

protected:
    virtual void buildDerivedManager() override;

private:
    // Maps SE to rclock samples
    std::unordered_map<int, std::vector<std::pair<int64_t, int64_t>>> rclock{};
    double rclock_frequency = 1E8;
};
