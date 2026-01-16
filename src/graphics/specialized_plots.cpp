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
#include <fstream>
#include <set>
#include "container/datanode.h"
#include "data/wavedata.h"
#include "mainwindow.h"
#include "util/jsonrequest.hpp"
#include "util/version.h"

using namespace std;

constexpr size_t kMaxPlotsPerDerived = 10;

static std::vector<std::string> UtilTypes = {"MISC", "FLAT", "SCA", "LDS", "VMEM", "VALU"};
static std::vector<std::string> MopsTypes = {"I8", "F8", "F16", "BF16", "F32", "F64", "XF32", "F6F4"};

std::vector<std::string> WavePlotView::state_names = {"Empty", "Idle", "Exec", "Wait", "Stall"};

static QColor& DispatchColor(int id) { return MainWindow::dispatchcolors[id % MainWindow::dispatchcolors.size()]; }

std::vector<QColor> WavePlotView::colors = {
    {255, 255, 255},
    {150, 150, 150},
    { 32, 255,  32},
    {255, 255,   0},
    {255,  32,  32}
};

void WavePlotView::LoadWaveStateData(const std::string& pathname, int SE)
{
    auto load_v3 = [](const std::string& filename)
    {
        std::vector<WeightedPoint> moveable;
        JsonRequest file(filename, false);
        if (!file.bValid) return moveable;

        auto& time = file.data["time"];
        auto& state = file.data["state"];
        if (time.size() != state.size()) std::cerr << "Warning: States have nonmatching array sizes" << std::endl;

        for (size_t i = 0; i < time.size() && i < state.size(); i++)
            moveable.emplace_back(WeightedPoint{time.at(i), state.at(i), 1});

        return moveable;
    };

    auto load_state = [&](int state)
    {
        int stepsize = 1;
        std::vector<WeightedPoint> ret;

        if (Version::Get().tool_major)
        {
            ret = load_v3(pathname + "wstates" + std::to_string(state) + ".json");
            stepsize = 1;
        }

        return std::tuple<int, int, decltype(ret)>{state, stepsize, ret};
    };

    std::vector<std::future<decltype(load_state(0))>> threads;

    for (int state = 2; state < 5; state++) threads.push_back(std::async(std::launch::async, load_state, state));

    for (auto& thread : threads)
    {
        auto [state, stepsize, moveable] = thread.get();
        if (moveable.size() < 2) continue;

        AddData(state_names.at(state), colors[state % colors.size()], std::move(moveable));
    }
}

void WavePlotView::UpdateGraphTable(float mousepos)
{
    for (int i = 0; i < 3; i++)
    {
        if (i >= curves.size() || !curves.at(i).lods.size()) continue;
        MainWindow::window->UpdateGraphInfo(state_names.at(i + 2), curves.at(i).lods.at(0).search(mousepos));
    }
}

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

std::vector<double> CounterPlotView::GetPeakRates()
{
    std::vector<double> result{};

    for (auto& node : rootnodes)
    {
        std::vector<CounterData> counters = node->AccumFromMask(~0ul, ~0ul);

        for (int c = 0; c < CNT_BANK; c++)
        {
            auto& maxv = result.emplace_back(0);
            for (auto& counter : counters) maxv = std::max<double>(maxv, counter.events[c]);
        }
    }

    return result;
}

DerivedCounter::Tensor CounterPlotView::GetAvgRates()
{
    size_t num_se = 0;
    for (auto& node : rootnodes) num_se = std::max(num_se, node->numSEs());

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

        for (size_t xcc = 0; xcc < summed.shape().getXCC(); xcc++)
            for (size_t se = 0; se < summed.shape().getSE(); se++)
                for (size_t cu = 0; cu < summed.shape().getCU(); cu++)
                    result.at(xcc, se, cu, i) += summed.at(xcc, se, cu, 0);
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
    if (derived.empty()) return;

    // Update disabled state for raw counters based on derived success
    if (derived.size() == derivedmanager->derivedCounterNames().size())
        for (size_t i = 0; i < rawCounterCount && i < curves.size(); i++) curves[i].disabled = true;

    int derived_index = counter_names.size();
    std::shared_ptr<const DerivedCounter::Tensor> time_data;
    try
    {
        if (!derivedmanager->context().hasCounter("SCLOCK")) return;
        time_data = derivedmanager->context().getCounter("SCLOCK");
    }
    catch (const std::exception&)
    {
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

void CounterPlotView::buildDerivedManager()
{
    derivedmanager = std::make_shared<DerivedCounter::DerivedCounterManager>();
    // Compute the full tensor shape from the raw counter data
    // Shape is (num_banks/XCC, num_SEs, NUM_CU, num_time_samples)

    // Find the global time range across all nodes
    int64_t global_min_time = INT64_MAX;
    int64_t global_max_time = INT64_MIN;
    size_t max_num_ses = 0;

    for (auto& node : rootnodes)
    {
        int64_t node_min, node_max;
        node->getTimeRange(delta, node_min, node_max);
        if (node_min != INT64_MAX)
        {
            global_min_time = std::min(global_min_time, node_min);
            global_max_time = std::max(global_max_time, node_max);
        }
        max_num_ses = std::max(max_num_ses, node->numSEs());
    }

    if (global_min_time == INT64_MAX || delta <= 0) return;

    // Calculate number of time samples based on delta
    size_t num_time_samples = static_cast<size_t>((global_max_time - global_min_time) / delta);
    if (num_time_samples == 0) num_time_samples = 1;

    // Create time data array for plotting
    std::vector<float> time_data(num_time_samples);
    for (size_t i = 0; i < num_time_samples; i++) time_data[i] = static_cast<float>(global_min_time + i * delta);

    // Shape: (1, num_SEs, NUM_CU, num_time_samples) - one tensor per counter
    // Each counter only has data from its respective bank
    size_t num_banks = rootnodes.size();
    DerivedCounter::Shape counterShape(1, max_num_ses, NUM_CU, num_time_samples);

    // Create tensors for each counter (num_banks * CNT_BANK total), filled with zeros initially
    size_t total_counters = num_banks * CNT_BANK;
    std::vector<std::shared_ptr<DerivedCounter::Tensor>> counter_tensors;
    counter_tensors.reserve(total_counters);
    for (size_t i = 0; i < total_counters; i++)
        counter_tensors.emplace_back(std::make_shared<DerivedCounter::Tensor>(counterShape, 0.0f));

    // Populate tensors from raw CU data
    for (size_t b = 0; b < num_banks; b++)
    {
        auto& node = rootnodes[b];
        for (size_t se_idx = 0; se_idx < node->numSEs(); se_idx++)
        {
            SECounterNode* se_node = node->getSE(se_idx);
            if (!se_node) continue;

            size_t se = static_cast<size_t>(se_node->se);
            if (se >= max_num_ses) continue;

            for (size_t cu = 0; cu < NUM_CU; cu++)
            {
                if (!se_node->cu_nodes[cu]) continue;

                for (const auto& counter : se_node->cu_nodes[cu]->data)
                {
                    // Calculate time index: maps interval [t-delta/4, t+3*delta/4] to same index
                    // Equivalent to: time_idx = (sample_time - min_time + delta/4) / delta
                    size_t time_idx = static_cast<size_t>((counter.time - global_min_time + delta / 4) / delta);
                    if (time_idx >= num_time_samples) continue;

                    for (int c = 0; c < CNT_BANK; c++)
                    {
                        size_t tensor_idx = c + b * CNT_BANK;
                        counter_tensors[tensor_idx]->at(0, se, cu, time_idx) += counter.events[c];
                    }
                }
            }
        }
    }

    // Register tensors with the derived counter manager
    for (size_t b = 0; b < num_banks; b++)
    {
        for (int c = 0; c < CNT_BANK; c++)
        {
            int index = c + static_cast<int>(b) * CNT_BANK;
            auto name = index < counter_names.size() ? counter_names.at(index) : ("UNK_" + std::to_string(c));

            derivedmanager->context().setCounter(name, counter_tensors[index]);
        }
    }

    // Create SCLOCK tensor from time points (broadcast across all dimensions)
    {
        DerivedCounter::Shape sclockShape(1, 1, 1, num_time_samples);
        derivedmanager->context().setCounter(
            "SCLOCK", std::make_shared<DerivedCounter::Tensor>(sclockShape, time_data)
        );
    }
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
        for (auto& util : UtilTypes)
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

    {
        bool bLegacy = true;
        try
        {
            if (std::string(file.data["version"]).at(0) == '3') bLegacy = false;
        }
        catch (...)
        {}
        QWARNING(!bLegacy, "Legacy version not supported!", return );
    }

    int c = 0;
    for (auto& [SE, array] : file.data.items())
    {
        if (array.size() == 0) continue;
        try
        {
            [[maybe_unused]] bool b = std::to_string(std::stoi(SE)) == "SE";
        }
        catch (...)
        {
            continue;
        }

        int accum = 0;
        std::vector<WeightedPoint> datapoints{};

        {
            occupancy_data data = occupancy_data::build(array[0]);
            datapoints.push_back({(float) (data.time - 1), 0.0f});
        }

        for (auto& v : array)
        {
            occupancy_data data = occupancy_data::build(v);
            accum += 2 * data.enable - 1;
            datapoints.push_back({(float) data.time, (float) accum});
        }
        AddData("SE" + SE, DispatchColor(c++), std::move(datapoints));
    }
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

    {
        bool bLegacy = true;
        try
        {
            if (std::string(file.data["version"]).at(0) == '3') bLegacy = false;
        }
        catch (...)
        {}
        QWARNING(!bLegacy, "Legacy version not supported!", return );
    }

    std::unordered_map<int, std::string> kernel_names;
    for (auto& [id, name] : file.data["dispatches"].items()) kernel_names[stoi(id)] = name;

    int num_dispatch_ids = kernel_names.size();

    std::vector<occupancy_data> occupancy{};
    se_list.clear();

    for (auto& [SE, array] : file.data.items())
    {
        if (array.size() == 0) continue;
        try
        {
            se_list.push_back(std::stoi(SE));
        }
        catch (...)
        {
            continue;
        }

        for (auto& v : array) occupancy.push_back(occupancy_data::build(v));
    }

    std::sort(
        occupancy.begin(),
        occupancy.end(),
        [](const occupancy_data& a, const occupancy_data& b) { return a.time < b.time; }
    );
    std::vector<std::vector<WeightedPoint>> datapoints(num_dispatch_ids, std::vector<WeightedPoint>{});

    int64_t time = 0;
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
            AddData(std::to_string(id) + '-' + kernel_names[id], DispatchColor(id), std::move(datapoints[id]));
}

std::string TraceCounterPlotView::getBuiltin() const
{
    std::string derived = "_reduce_busy := sum[BUSY_CU_CYCLES, axis=[XCC,SE,CU]] + 1E-6";

    for (const std::string& name : UtilTypes)
        derived += "\n" + name + "_util := 100 * sum[ACTIVE_INST_" + name + ", axis=[XCC,SE,CU]] / _reduce_busy";

    derived += "\nMFMA_util := 25 * sum[VALU_MFMA_BUSY_CYCLES, axis=[XCC,SE,CU]] / _reduce_busy";

    derived += "\n_clock_delta := select[RCLOCK, -1, axis=TIME] - select[RCLOCK, 0, axis=TIME]";
    derived += "\n_frequency := 1E8 * select[_clock_delta, 0, axis=CU] / select[_clock_delta, 1, axis=CU]";
    derived += "\n_delta_seconds := min[delta[SCLOCK, axis=TIME]] / _frequency + 1E-13";

    for (const std::string& name : MopsTypes)
        derived += "\n" + name + "_TFLOPS := 512E-12 * sum[INSTS_VALU_MFMA_MOPS_" + name +
                   ", axis=[XCC,SE,CU]] / _delta_seconds";

    return derived;
}
