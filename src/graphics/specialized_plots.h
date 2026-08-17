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
#include <optional>
#include <unordered_set>
#include <vector>
#include "analysis/derived_counter.h"
#include "container/datanode.h"
#include "data/datastore.h"
#include "plot.h"

struct CounterSummary
{
    std::vector<double> peak_rates;
    DerivedCounter::Tensor accumulated;
};

//! Class for visualizing the precomputed wave-state JSON series.
class WavePlotView : public PlotGraph
{
public:
    WavePlotView() : WavePlotView(nullptr){};
    WavePlotView(class QWidget* parent) : PlotGraph(1, parent){};

    void LoadWaveStateData(const DataStore& store);
    virtual void UpdateGraphTable(float timepos) override;

    static std::vector<std::string> state_names;

private:
    static std::vector<QColor> colors;
};

//! Class for visualizing performance counters
class CounterPlotView : public PlotGraph
{
public:
    CounterPlotView() : CounterPlotView(nullptr){};
    CounterPlotView(class QWidget* parent) : PlotGraph(1, parent){};
    virtual ~CounterPlotView() = default;

    virtual void LoadCounterData(const DataStore& store) = 0;

    void UpdateDataSelection(
        const std::vector<std::string>& counters_names, const std::string& derivedDefinitions = ""
    );

    //! Update only the derived counters without reloading raw counter data
    void UpdateDerivedCounters(const std::string& derivedDefinitions, bool suppress);

    std::shared_ptr<DerivedCounter::DerivedCounterManager> getDerivedManager() { return derivedmanager; };
    size_t getRawCurveCount() const { return raw_curve_count; }

    virtual void UpdateGraphTable(float timepos) override;
    // TODO(SPM): Return an SPM summary when its summary semantics are defined.
    virtual std::optional<CounterSummary> GetSummary() { return std::nullopt; }

    virtual std::string getBuiltin() const = 0;
    virtual bool isBuiltin(const std::string& name) const = 0;

protected:
    virtual void addRawCounters() = 0;
    virtual void addDerivedSeries(
        const std::string& name,
        const DerivedCounter::Tensor& values,
        const DerivedCounter::Tensor& clock,
        int& color_index
    ) = 0;
    void buildDerivedManager();
    virtual void registerCounters(DerivedCounter::CounterContext& context) = 0;

    std::shared_ptr<DerivedCounter::DerivedCounterManager> derivedmanager{nullptr};

    std::vector<std::string> counter_names{};
    std::vector<std::string> raw_curve_sources{};
    size_t raw_curve_count = 0;

private:
    std::vector<std::pair<std::string, std::shared_ptr<const DerivedCounter::Tensor>>> getDerived(
        const std::string& derived, bool suppress
    );
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

    virtual void LoadCounterData(const DataStore& store) override;
    virtual std::optional<CounterSummary> GetSummary() override;

    virtual std::string getBuiltin() const override;
    virtual bool isBuiltin(const std::string& name) const override;

protected:
    virtual void addRawCounters() override;
    virtual void addDerivedSeries(
        const std::string& name,
        const DerivedCounter::Tensor& values,
        const DerivedCounter::Tensor& clock,
        int& color_index
    ) override;
    virtual void registerCounters(DerivedCounter::CounterContext& context) override;

private:
    std::vector<double> getPeakRates();
    // XCC vs SE vs CU vs CounterID
    DerivedCounter::Tensor getAvgRates();
    std::vector<std::unique_ptr<class GPUCounterNode>> rootnodes{};
    // Maps SE to rclock samples
    std::unordered_map<int, std::vector<std::pair<int64_t, int64_t>>> rclock{};
    int64_t delta = INT64_MAX;
};

//! Class for visualizing sampled performance counters
class SPMCounterPlotView : public CounterPlotView
{
public:
    SPMCounterPlotView() : SPMCounterPlotView(nullptr){};
    SPMCounterPlotView(class QWidget* parent) : CounterPlotView(parent){};
    virtual ~SPMCounterPlotView() = default;

    virtual void LoadCounterData(const DataStore& store) override;

    virtual std::string getBuiltin() const override { return {}; }
    virtual bool isBuiltin(const std::string&) const override { return false; }

protected:
    virtual void addRawCounters() override;
    virtual void addDerivedSeries(
        const std::string& name,
        const DerivedCounter::Tensor& values,
        const DerivedCounter::Tensor& clock,
        int& color_index
    ) override;
    virtual void registerCounters(DerivedCounter::CounterContext& context) override;

private:
    std::vector<std::shared_ptr<DerivedCounter::Tensor>> sampled_counters{};
    std::shared_ptr<DerivedCounter::Tensor> sampled_clock{};
    std::shared_ptr<DerivedCounter::Tensor> sampled_spm_clock{};
};
