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

#include "specialized_plots.h"
#include <fstream>
#include <set>
#include "container/datanode.h"
#include "data/wavedata.h"
#include "mainwindow.h"
#include "util/version.h"

using namespace std;

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
    float total = 0;
    std::array<float, 3> values{};

    for (int i = 0; i < 3; i++)
    {
        if (i >= curves.size() || !curves.at(i).lods.size()) continue;

        values.at(i) = curves.at(i).lods.at(0).search(mousepos);
        total += values.at(i);
    }
    total = std::max(total, 1.0f) / 100.0f; // Display as percentage
    for (int i = 0; i < 3; i++)
        MainWindow::window->UpdateGraphInfo(state_names.at(i + 2), values[i], values[i] / total);
}

CounterPlotView::CounterAccum TraceCounterPlotView::LoadCounterData(JsonRequest& file, int shader_engine)
{
    std::array<std::vector<CounterData>, BANKS> data;
    data[0].reserve(file.data["data"].size());
    data[1].reserve(file.data["data"].size());

    for (auto& event : file.data["data"])
        data.at(int8_t(event[6]) & 0x3)
            .push_back(CounterData({
                int64_t(event[0]),
                {int(event[1]), int(event[2]), int(event[3]), int(event[4])},
                int8_t(event[5]),
                int8_t(shader_engine)
        }));

    for (int b = 0; b < BANKS; b++)
        if (data[b].size()) rootnodes.at(b)->Insert(shader_engine, data[b]);

    CounterPlotView::CounterAccum ret{};

    for (size_t b = 0; b < data.size(); b++)
        for (auto& counter : data[b])
            for (int c = 0; c < 4; c++) ret.at(counter.cu).at(4 * b + c) += counter.events[c];

    return ret;
}

CounterPlotView::CounterList TraceCounterPlotView::GetPeakRates()
{
    CounterPlotView::CounterList ret{};

    for (int b = 0; b < BANKS; b++)
    {
        if (!rootnodes.at(b)) continue;
        std::vector<CounterData> counters = rootnodes[b]->AccumFromMask(~0ul, ~0ul);

        for (auto& counter : counters)
        {
            for (int c = 0; c < 4; c++)
            {
                auto& maxv = ret.at(4 * b + c);
                maxv = std::max<double>(maxv, counter.events[c]);
            }
        }
    }

    return ret;
}

void TraceCounterPlotView::UpdateDataSelection(
    const std::vector<std::string>& counter_names,
    uint64_t se_mask,
    uint64_t cu_mask,
    std::unordered_set<std::string>& disable_counters
)
{
    this->xmin = 0;
    this->ymin = 0;
    this->xmax = 0;
    this->ymax = 0;

    this->curves.clear();

    int64_t delta = verify_skew(rootnodes.at(0)->getDelta(), rootnodes.back()->getDelta());

    for (int b = 0; b < BANKS; b++)
    {
        if (!rootnodes.at(b).get()) continue;

        rootnodes[b]->fillDelta(delta);
        std::vector<CounterData> counters_loaded = rootnodes[b]->AccumFromMask(se_mask, cu_mask);
        if (counters_loaded.size() == 0) continue;

        std::array<std::vector<WeightedPoint>, 4> datapoints;
        for (auto& dp : datapoints) dp.reserve(counters_loaded.size());

        for (auto& counter : counters_loaded)
        {
            for (int i = 0; i < 4; i++) datapoints[i].push_back({(float) counter.time, (float) counter.events[i]});
        }

        for (int i = 0; i < 4 && i + 4 * b < counter_names.size(); i++)
        {
            if (disable_counters.find(counter_names[i + 4 * b]) != disable_counters.end()) datapoints[i] = {};
            AddData(counter_names[i + 4 * b], Config::PlotColors(i + 4 * b), std::move(datapoints[i]));
        }
    }
}

TraceCounterPlotView::TraceCounterPlotView(QWidget* parent)
{
    for (int i = 0; i < BANKS; i++) rootnodes.at(i) = std::make_unique<GPUCounterNode>();
}

void CounterPlotView::UpdateGraphTable(float mousepos)
{
    for (auto& curve : curves)
        if (curve.lods.size())
            MainWindow::window->UpdateGraphInfo(curve.fullname, curve.lods.at(0).search(mousepos), 0);
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
        if (datapoints[id].size() > 2)
            AddData(std::to_string(id) + '-' + kernel_names[id], DispatchColor(id), std::move(datapoints[id]));
}
