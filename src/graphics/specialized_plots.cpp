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

#include "specialized_plots.h"
#include <QMessageBox>
#include <cstdint>
#include <fstream>
#include <set>
#include "container/datanode.h"
#include "data/dispatch_resolver.h"
#include "data/wavedata.h"
#include "mainwindow.h"
#include "util/jsonrequest.hpp"
#include "util/version.h"
#include "wave/waveglobal.h"

using namespace std;

constexpr size_t kMaxPlotsPerDerived = 10;

static std::vector<std::pair<std::string, int>> UtilTypes = {
    {"MISC", 100},
    {"FLAT", 200},
    {"SCA",  100},
    {"LDS",  200},
    {"VMEM", 200}
};
static std::vector<std::string> MopsTypes = {"I8", "F8", "F16", "BF16", "F32", "F64", "XF32", "F6F4"};

static QColor& DispatchColor(int id) { return MainWindow::dispatchcolors[id % MainWindow::dispatchcolors.size()]; }

namespace
{
using OccupancyBySE = std::map<int, std::vector<occupancy_data>>;

bool parseSEKey(const std::string& key, int& se)
{
    try
    {
        se = std::stoi(key);
        return true;
    }
    catch (...)
    {
        RCV_LOG();
        return false;
    }
}

template <typename JsonT> OccupancyBySE occupancyBySEFromJson(const JsonT& root, std::vector<int>* se_list = nullptr)
{
    OccupancyBySE by_se;
    for (auto& [se_key, array] : root.items())
    {
        if (array.size() == 0) continue;

        int se = -1;
        if (!parseSEKey(se_key, se)) continue;
        if (se_list) se_list->push_back(se);

        auto& records = by_se[se];
        records.reserve(records.size() + array.size());
        for (auto& v : array) records.push_back(occupancy_data::build(v));
    }
    return by_se;
}

template <typename StoreT, typename KernelIdFor>
OccupancyBySE occupancyBySEFromStoreImpl(StoreT& store, KernelIdFor kernel_id_for)
{
    OccupancyBySE by_se;
    for (auto& [se, records] : store.occupancy_by_se)
    {
        if (records.empty()) continue;
        auto& out = by_se[se];
        out.reserve(records.size());

        for (auto& rec : records)
        {
            occupancy_data occ;
            occ.time = rec.time;
            occ.cu = rec.cu;
            occ.simd = rec.simd;
            occ.slot = rec.wave_id;
            occ.enable = rec.start;
            occ.kernel_id = kernel_id_for(rec);
            out.push_back(occ);
        }
    }
    return by_se;
}

OccupancyBySE occupancyBySEFromStore(const DataStore& store)
{
    return occupancyBySEFromStoreImpl(store, [](const occupancy_record_t&) { return 0; });
}

OccupancyBySE occupancyBySEFromStoreWithDispatch(DataStore& store)
{
    return occupancyBySEFromStoreImpl(
        store, [&](const occupancy_record_t& rec) { return store.dispatch_resolver.Resolve(rec.pc); }
    );
}

std::vector<occupancy_data> flattenOccupancy(OccupancyBySE& by_se)
{
    std::vector<occupancy_data> occupancy;
    for (auto& [se, records] : by_se)
    {
        (void) se;
        occupancy.insert(occupancy.end(), records.begin(), records.end());
    }
    return occupancy;
}

void addOccupancySeries(PlotGraph& plot, const OccupancyBySE& by_se)
{
    int c = 0;
    for (const auto& [se, records] : by_se)
    {
        if (records.empty()) continue;

        int accum = 0;
        std::vector<WeightedPoint> datapoints;
        datapoints.push_back({(float) (records.front().time - 1), 0.0f});

        for (const auto& data : records)
        {
            accum += 2 * data.enable - 1;
            datapoints.push_back({(float) data.time, (float) accum});
        }
        plot.AddData("SE" + std::to_string(se), DispatchColor(c++), std::move(datapoints));
    }
}

template <typename NameFor>
void addDispatchSeries(PlotGraph& plot, std::vector<occupancy_data> occupancy, int num_dispatch_ids, NameFor name_for)
{
    if (num_dispatch_ids == 0) return;

    std::sort(
        occupancy.begin(),
        occupancy.end(),
        [](const occupancy_data& a, const occupancy_data& b) { return a.time < b.time; }
    );

    std::vector<std::vector<WeightedPoint>> datapoints(num_dispatch_ids, std::vector<WeightedPoint>{});
    std::vector<int> kernel_occupancy(num_dispatch_ids, 0);

    try
    {
        for (auto& data : occupancy)
        {
            kernel_occupancy.at(data.kernel_id) += 2 * data.enable - 1;
            datapoints.at(data.kernel_id).push_back({(float) data.time, (float) kernel_occupancy.at(data.kernel_id)});
        }
    }
    catch (std::out_of_range& e)
    {
        QWARNING(false, "Warning: Occupancy data corrupted", );
    }

    for (int id = 0; id < num_dispatch_ids; id++)
        if (datapoints[id].size() >= 2)
            plot.AddData(std::to_string(id) + '-' + name_for(id), DispatchColor(id), std::move(datapoints[id]));
}
} // namespace

void TraceCounterPlotView::LoadCounterData(const std::string& dirpath, int shader_engine)
{
    {
        JsonRequest file(dirpath + "se" + std::to_string(shader_engine) + "_perfcounter.json", false);
        if (!file.bValid) return;

        std::array<std::vector<CounterData>, 2> data{};
        data[0].reserve(file.data["data"].size());
        data[1].reserve(file.data["data"].size());

        for (auto& event : file.data["data"])
            data.at(int8_t(event[6]) & 1)
                .push_back(CounterData({
                    int64_t(event[0]),
                    int8_t(event[5]),
                    int8_t(shader_engine),
                    {float(event[1]), float(event[2]), float(event[3]), float(event[4])}
            }));

        while (rootnodes.size() < data.size()) rootnodes.emplace_back(std::make_unique<GPUCounterNode>());
        for (int b = 0; b < data.size(); b++)
            if (data[b].size()) rootnodes[b]->Insert(shader_engine, data[b]);
    }

    if (rclock.empty())
    {
        try
        {
            JsonRequest file(dirpath + "realtime.json", false);
            if (!file.bValid) return;

            int64_t rfreq = int64_t(file.data["metadata"]["frequency"]);
            if (rfreq != 0) rclock_frequency = static_cast<double>(rfreq);

            for (auto& [SE, points] : file.data.items())
            {
                if (std::string(SE).find("SE") != 0) continue;

                int se_number = stoi(std::string(SE).substr(2));
                for (auto& p : points) rclock[se_number].push_back({int64_t(p[0]), int64_t(p[1])});
            }
        }
        catch (std::exception& e)
        {
            QWARNING(false, e.what(), abort());
        }
    }
}

static double GetUtilScale(const std::string& counter_name)
{
    if (counter_name.find("ACTIVE_INST_") == 0)
    {
        std::string suffix = counter_name.substr(std::string("ACTIVE_INST_").size());
        for (auto& [name, mult] : UtilTypes)
            if (name == suffix) return mult / 100.0;
    }
    return 1.0;
}

std::vector<double> CounterPlotView::GetPeakRates()
{
    std::vector<double> result{};

    for (auto& node : rootnodes)
    {
        constexpr uint64_t all_mask = ~uint64_t{0};
        std::vector<CounterData> counters = node->AccumFromMask(all_mask, all_mask);

        for (int c = 0; c < CNT_BANK; c++)
        {
            size_t index = result.size();
            auto& maxv = result.emplace_back(0);
            for (auto& counter : counters) maxv = std::max<double>(maxv, counter.events[c]);

            if (index < counter_names.size()) maxv *= GetUtilScale(counter_names[index]);
        }
    }

    return result;
}

DerivedCounter::Tensor CounterPlotView::GetAvgRates()
{
    size_t num_se = 0;
    for (auto& node : rootnodes) num_se = std::max(num_se, node->Nodes().size());

    DerivedCounter::Tensor result(DerivedCounter::Shape(1, num_se, NUM_CU, counter_names.size()), 0);

    // Ensure derivedmanager is built so raw counters are available
    if (!derivedmanager) buildDerivedManager();
    if (!derivedmanager) return result;

    for (int i = 0; i < counter_names.size(); i++)
    {
        if (!derivedmanager->context().hasCounter(counter_names[i])) continue;

        auto counter_tensor = derivedmanager->context().getCounter(counter_names[i]);
        if (!counter_tensor) continue;

        auto summed = counter_tensor->sum(DerivedCounter::Axis::Time);
        double scale = GetUtilScale(counter_names[i]);

        for (size_t xcc = 0; xcc < summed.shape().getXCC(); xcc++)
            for (size_t se = 0; se < summed.shape().getSE(); se++)
                for (size_t cu = 0; cu < summed.shape().getCU(); cu++)
                    result.at(xcc, se, cu, i) += summed.at(xcc, se, cu, 0) * scale;
    }

    return result;
}

void CounterPlotView::UpdateDataSelection(
    const std::vector<std::string>& _counter_names,
    uint64_t se_mask,
    uint64_t cu_mask,
    const std::string& derivedDefinitions
)
{
    this->counter_names = _counter_names;

    this->xmin = 0;
    this->xmax = 0;

    this->curves.clear();

    QWARNING(rootnodes.size(), "no root node", return );

    this->delta = INT64_MAX;
    for (auto& node : rootnodes) delta = verify_skew(delta, node->getDelta());

    size_t num_samples = 0;
    for (int b = 0; b < rootnodes.size(); b++)
    {
        auto& node = rootnodes.at(b);
        node->fillDelta(delta);

        std::vector<CounterData> counters_loaded = node->AccumFromMask(se_mask, cu_mask);
        if (counters_loaded.empty()) continue;

        num_samples = std::max(num_samples, counters_loaded.size());

        for (int c = 0; c < CNT_BANK; c++)
        {
            int index = c + b * CNT_BANK;
            while (index >= counter_names.size()) counter_names.push_back("UNK_" + std::to_string(c));
            auto& name = counter_names.at(index);

            std::vector<WeightedPoint> datapoints;
            datapoints.reserve(counters_loaded.size());
            for (auto& counter : counters_loaded)
                datapoints.push_back({(float) counter.time, (float) counter.events[c]});
            AddData(name, Config::PlotColors(index), std::move(datapoints));
        }
    }

    // Add derived counters
    UpdateDerivedCounters(derivedDefinitions, true);
}

void CounterPlotView::UpdateDerivedCounters(const std::string& derivedDefinitions, bool suppress)
{
    // Remove existing derived counter curves (those added after the raw counters)
    size_t rawCounterCount = counter_names.size();
    while (curves.size() > rawCounterCount) curves.pop_back();

    std::string builtin_derived = derivedDefinitions.empty() ? getBuiltin() : derivedDefinitions;

    // Rebuild derived manager to clear old definitions
    buildDerivedManager();
    auto derived = getDerived(builtin_derived, suppress);

    {
        int derived_count = 0;
        for (auto& der : derived)
            if (!der.first.empty() && der.first.at(0) != '_') derived_count++;
        if (derived_count == 0) return;

        auto& accessed = derivedmanager->context().accessedRawCounters();
        for (size_t i = 0; i < rawCounterCount && i < curves.size(); i++)
            if (accessed.count(curves[i].fullname)) curves[i].disabled = true;
    }

    int derived_index = counter_names.size();
    std::shared_ptr<const DerivedCounter::Tensor> time_data;
    try
    {
        if (!derivedmanager->context().hasCounter("SCLOCK")) return;
        time_data = derivedmanager->context().getCounter("SCLOCK");
    }
    catch (const std::exception&)
    {
        RCV_LOG();
        return;
    }

    for (auto& [derived_name, result] : derived)
    {
        if (derived_name.empty() || derived_name.at(0) == '_') continue;
        if (!result || !time_data) continue;

        size_t num_samples = result->shape().getSamples();
        if (time_data->shape().getSamples() < num_samples) continue;

        size_t num_xcc = result->shape().getXCC();
        size_t num_se = result->shape().getSE();
        size_t num_cu = result->shape().getCU();

        // If result is already [1,1,1,time], just plot it directly
        if (num_xcc == 1 && num_se == 1 && num_cu == 1)
        {
            std::vector<WeightedPoint> datapoints;
            datapoints.reserve(num_samples);
            for (size_t i = 0; i < num_samples; i++) datapoints.push_back({(*time_data)[i], (*result)[i]});
            AddData(derived_name, Config::PlotColors(derived_index++), std::move(datapoints));
            continue;
        }

        // For multi-dimensional results, create separate plots for each combination
        // Limit total plots to kMaxPlotsPerDerived
        size_t plot_count = 0;
        for (size_t xcc = 0; xcc < num_xcc && plot_count < kMaxPlotsPerDerived; xcc++)
        {
            for (size_t se = 0; se < num_se && plot_count < kMaxPlotsPerDerived; se++)
            {
                for (size_t cu = 0; cu < num_cu && plot_count < kMaxPlotsPerDerived; cu++)
                {
                    // Build plot name with non-trivial indices
                    std::string plot_name = derived_name;
                    if (num_xcc > 1) plot_name += "_XCC" + std::to_string(xcc);
                    if (num_se > 1) plot_name += "_SE" + std::to_string(se);
                    if (num_cu > 1) plot_name += "_CU" + std::to_string(cu);

                    std::vector<WeightedPoint> datapoints;
                    datapoints.reserve(num_samples);
                    for (size_t t = 0; t < num_samples; t++)
                    {
                        float value = result->at(xcc, se, cu, t);
                        datapoints.push_back({(*time_data)[t], value});
                    }
                    AddData(plot_name, Config::PlotColors(derived_index++), std::move(datapoints));
                    plot_count++;
                }
            }
        }
    }

    update();
}

namespace
{

struct TimeGrid
{
    int64_t global_min_time = INT64_MAX;
    int64_t global_max_time = INT64_MIN;
    size_t max_num_ses = 0;
    size_t max_num_cus = 1;
    size_t num_time_samples = 0;
    std::vector<float> time_data;
    /// True when at least one root node reported a valid time range and
    /// `delta > 0`; callers must bail out early when false.
    bool valid = false;
};

/// Scan `rootnodes` to derive the (time, SE, CU) shape of the counter tensor.
/// Computes the global [min, max] sample-time interval, the largest SE/CU
/// indices observed, then packs the per-sample time axis into `time_data`
/// for the SCLOCK broadcast tensor downstream. Pure read of the node tree.
TimeGrid computeTimeGrid(const std::vector<std::unique_ptr<GPUCounterNode>>& rootnodes, int64_t delta)
{
    TimeGrid g;
    for (auto& node : rootnodes)
    {
        int64_t node_min, node_max;
        node->getTimeRange(delta, node_min, node_max);
        if (node_min != INT64_MAX)
        {
            g.global_min_time = std::min(g.global_min_time, node_min);
            g.global_max_time = std::max(g.global_max_time, node_max);
        }
        g.max_num_ses = std::max(g.max_num_ses, node->Nodes().size());
        for (auto& se : node->Nodes())
            if (se)
                for (auto& cu : se->cu_nodes)
                    if (cu && !cu->data.empty()) g.max_num_cus = std::max<size_t>(g.max_num_cus, 1 + cu->cu);
    }

    if (g.global_min_time == INT64_MAX || delta <= 0) return g;

    g.num_time_samples = static_cast<size_t>((g.global_max_time - g.global_min_time) / delta);
    if (g.num_time_samples == 0) g.num_time_samples = 1;

    g.time_data.resize(g.num_time_samples);
    for (size_t i = 0; i < g.num_time_samples; i++) g.time_data[i] = static_cast<float>(g.global_min_time + i * delta);

    g.valid = true;
    return g;
}

/// Allocate one zero-initialized tensor per (bank, counter slot) and
/// accumulate raw counter samples into them. Time bin uses the
/// [t - δ/4, t + 3δ/4] half-open mapping so a sample landing exactly on a
/// grid edge does not split between two bins.
std::vector<std::shared_ptr<DerivedCounter::Tensor>> buildCounterTensors(
    const std::vector<std::unique_ptr<GPUCounterNode>>& rootnodes,
    const DerivedCounter::Shape& counterShape,
    size_t num_banks,
    size_t num_time_samples,
    size_t max_num_ses,
    int64_t global_min_time,
    int64_t delta
)
{
    size_t total_counters = num_banks * CNT_BANK;
    std::vector<std::shared_ptr<DerivedCounter::Tensor>> counter_tensors;
    counter_tensors.reserve(total_counters);
    for (size_t i = 0; i < total_counters; i++)
        counter_tensors.emplace_back(std::make_shared<DerivedCounter::Tensor>(counterShape, 0.0f));

    for (size_t b = 0; b < num_banks; b++)
    {
        auto& node = rootnodes[b];
        for (auto& se_node : node->Nodes())
        {
            if (!se_node || se_node->se >= max_num_ses) continue;
            for (auto& cu_node : se_node->cu_nodes)
            {
                if (!cu_node) continue;
                for (const auto& counter : cu_node->data)
                {
                    size_t time_idx = static_cast<size_t>((counter.time - global_min_time + delta / 4) / delta);
                    if (time_idx >= num_time_samples) continue;
                    for (int c = 0; c < CNT_BANK; c++)
                    {
                        size_t tensor_idx = c + b * CNT_BANK;
                        counter_tensors[tensor_idx]->at(0, se_node->se, cu_node->cu, time_idx) += counter.events[c];
                    }
                }
            }
        }
    }
    return counter_tensors;
}

} // anonymous namespace

void CounterPlotView::buildDerivedManager()
{
    derivedmanager = std::make_shared<DerivedCounter::DerivedCounterManager>();
    // Tensor shape is (num_banks/XCC, num_SEs, NUM_CU, num_time_samples).
    // Phase 1: scan raw nodes to derive that shape + the SCLOCK time axis.
    TimeGrid grid = computeTimeGrid(rootnodes, delta);
    if (!grid.valid) return;

    const size_t num_banks = rootnodes.size();
    DerivedCounter::Shape counterShape(1, grid.max_num_ses, grid.max_num_cus, grid.num_time_samples);

    // Phase 2: allocate per-(bank, counter slot) tensors and bin samples in.
    auto counter_tensors = buildCounterTensors(
        rootnodes, counterShape, num_banks, grid.num_time_samples, grid.max_num_ses, grid.global_min_time, delta
    );

    // Phase 3: register named tensors with the derived-counter manager so
    // user-defined expressions can reference them. Names beyond the loaded
    // counter list fall back to UNK_<slot> rather than aborting.
    for (size_t b = 0; b < num_banks; b++)
    {
        for (int c = 0; c < CNT_BANK; c++)
        {
            int index = c + static_cast<int>(b) * CNT_BANK;
            auto name = index < (int) counter_names.size() ? counter_names.at(index) : ("UNK_" + std::to_string(c));
            derivedmanager->context().setCounter(name, counter_tensors[index]);
        }
    }

    // Phase 4: SCLOCK tensor — broadcast across all (bank, SE, CU) so a
    // derived expression can divide events by elapsed time without rank
    // gymnastics.
    DerivedCounter::Shape sclockShape(1, 1, 1, grid.num_time_samples);
    derivedmanager->context().setCounter(
        "SCLOCK", std::make_shared<DerivedCounter::Tensor>(sclockShape, grid.time_data)
    );
}

void TraceCounterPlotView::buildDerivedManager()
{
    using namespace DerivedCounter;

    CounterPlotView::buildDerivedManager();

    if (rclock.empty() || !derivedmanager) return;

    int64_t initial_rclock = INT64_MAX;
    size_t num_samples = 0;
    int max_se = -1;

    for (auto& [se_number, points] : rclock)
    {
        max_se = std::max(max_se, se_number);
        num_samples = std::max(num_samples, points.size());
        for (auto& [_, realtime] : points) initial_rclock = std::min(initial_rclock, realtime);
    }

    if (max_se < 0 || num_samples == 0) return;

    auto tensor = std::make_shared<Tensor>(Shape(1, max_se + 1, 2, num_samples), 0);

    for (auto& [se_number, points] : rclock)
    {
        size_t i = 0;
        for (auto& [gfx, realtime] : points)
        {
            tensor->at(0, se_number, 0, i) = gfx;
            tensor->at(0, se_number, 1, i) = realtime - initial_rclock;
            i++;
        }
    }

    derivedmanager->context().setCounter("RCLOCK", tensor);
}

std::vector<std::pair<std::string, std::shared_ptr<const DerivedCounter::Tensor>>> CounterPlotView::getDerived(
    const std::string& derived, bool suppress
)
{
    if (!derivedmanager) buildDerivedManager();

    std::vector<std::pair<std::string, std::shared_ptr<const DerivedCounter::Tensor>>> result{};
    try
    {
        derivedmanager->loadDefinitions(derived);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Warning: Failed to load definitions: " << e.what() << std::endl;
        QMessageBox::warning(
            this, "Derived Counter Error", QString("Failed to parse derived counter definitions:\n%1").arg(e.what())
        );
        return result;
    }

    QStringList errors;
    for (const auto& derived_name : derivedmanager->derivedCounterNames())
    {
        try
        {
            result.push_back({derived_name, derivedmanager->evaluate(derived_name)});
        }
        catch (const std::exception& e)
        {
            std::cerr << "Warning: Failed to evaluate derived counter '" << derived_name << "': " << e.what()
                      << std::endl;

            if (isBuiltin(derived_name)) continue;
            errors.append(QString("'%1': %2").arg(QString::fromStdString(derived_name), e.what()));
        }
    }

    if (!errors.isEmpty())
    {
        if (!suppress)
            QMessageBox::warning(
                this,
                "Derived Counter Error",
                QString("Failed to evaluate %1 derived counter(s):\n\n%2").arg(errors.size()).arg(errors.join("\n\n"))
            );
    }

    derivedmanager->context().clearErrorDerivedCounters();
    return result;
}

bool TraceCounterPlotView::isBuiltin(const std::string& name) const
{
    if (name.empty()) return false;

    if (name.find("_TFLOPS") != std::string::npos)
        for (auto& mops : MopsTypes)
            if (name == mops + "_TFLOPS") return true;

    if (name.find("_util") != std::string::npos)
    {
        for (auto& [util, _] : UtilTypes)
            if (name == util + "_util") return true;

        if (name == "MFMA_util") return true;
    }

    if (name == "_reduce_busy") return true;

    return false;
}

TraceCounterPlotView::TraceCounterPlotView(QWidget* parent) {}

void CounterPlotView::UpdateGraphTable(float mousepos)
{
    for (auto& curve : curves)
        if (curve.lods.size()) MainWindow::window->UpdateGraphInfo(curve.fullname, curve.lods.at(0).search(mousepos));
}

void OccupancyPlotView::LoadOccupancyData(const std::string& filename)
{
    JsonRequest file(filename);
    QWARNING(!file.fail() && !file.bad(), "Error opening file " << filename, return );

    addOccupancySeries(*this, occupancyBySEFromJson(file.data));
}

void OccupancyPlotView::LoadOccupancyData(const DataStore& store)
{
    addOccupancySeries(*this, occupancyBySEFromStore(store));
}

void OccupancyPlotView::UpdateGraphTable(float timepos)
{
    std::vector<std::pair<std::string, int>> values{};
    float total = 0;

    for (auto& curve : curves)
    {
        if (!curve.lods.size()) continue;
        values.push_back({curve.fullname, curve.lods.at(0).search(timepos)});
        total += values.back().second;
    }
    total = std::max(total, 1.0f) / 100.0f; // Display as percentage
    MainWindow::window->UpdateOccupancyInfo(values, total);
}

void DispatchPlotView::LoadOccupancyData(const std::string& filename)
{
    JsonRequest file(filename);
    QWARNING(!file.fail() && !file.bad(), "Error opening file " << filename, return );

    std::unordered_map<int, std::string> kernel_names;
    for (auto& [id, name] : file.data["dispatches"].items()) kernel_names[stoi(id)] = name;

    int num_dispatch_ids = kernel_names.size();

    se_list.clear();
    auto by_se = occupancyBySEFromJson(file.data, &se_list);
    addDispatchSeries(*this, flattenOccupancy(by_se), num_dispatch_ids, [&](int id) { return kernel_names[id]; });
}

void DispatchPlotView::LoadOccupancyData(DataStore& store)
{
    auto& resolver = store.dispatch_resolver;

    se_list.clear();
    auto by_se = occupancyBySEFromStoreWithDispatch(store);
    for (auto& [se, records] : by_se)
        if (!records.empty()) se_list.push_back(se);

    int num_dispatch_ids = resolver.Count();
    addDispatchSeries(*this, flattenOccupancy(by_se), num_dispatch_ids, [&](int id) { return resolver.Name(id); });
}

std::string TraceCounterPlotView::getBuiltin() const
{
    std::string derived = "_reduce_busy := sum[max[BUSY_CU_CYCLES, axis=TIME], axis=[XCC,SE,CU]] + 1E-6";

    for (auto& [name, mult] : UtilTypes)
        derived += "\n" + name + "_util := " + std::to_string(mult) + " * sum[ACTIVE_INST_" + name +
                   ", axis=[XCC,SE,CU]] / _reduce_busy";

    derived += "\n_mfmabusy := sum[VALU_MFMA_BUSY_CYCLES, axis=[XCC,SE,CU]] / _reduce_busy / 4"
               "\n_valubusy := sum[ACTIVE_INST_VALU, axis=[XCC,SE,CU]] / _reduce_busy\n"
               "\nVALU_util := 100 * _valubusy"
               "\nMFMA_util := 100 * _mfmabusy\n"
               "\nGPUutil := max(LDS_util, VMEM_util, FLAT_util, min(MFMA_util + VALU_util * (1 - _mfmabusy) / (1.7 - "
               "_mfmabusy), 100))";

    derived += "\n_clock_delta := select[RCLOCK, -1, axis=TIME] - select[RCLOCK, 0, axis=TIME]";
    derived += "\n_frequency := 1E8 * select[_clock_delta, 0, axis=CU] / select[_clock_delta, 1, axis=CU]";
    derived += "\n_delta_seconds := min[delta[SCLOCK, axis=TIME]] / _frequency + 1E-13";

    for (const std::string& name : MopsTypes)
        derived += "\n" + name + "_TFLOPS := 512E-12 * sum[INSTS_VALU_MFMA_MOPS_" + name +
                   ", axis=[XCC,SE,CU]] / _delta_seconds";

    return derived;
}
