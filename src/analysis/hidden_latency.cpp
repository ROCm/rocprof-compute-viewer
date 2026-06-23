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

#include "hidden_latency.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>

#include "code/asmcode.h"
#include "config/config.hpp"
#include "data/datastore.h"
#include "data/wavemanager.h"

namespace HiddenLatencyAnalysis
{

namespace
{

struct Util
{
    int64_t clock;
    int64_t cycles;

    bool operator<(const Util& other) const { return clock < other.clock; }
};

std::string upper(std::string str)
{
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::toupper(c); });
    return str;
}

std::vector<Util> compute_util(std::vector<Util>& in)
{
    if (in.empty()) return in;

    std::sort(in.begin(), in.end());

    std::vector<Util> out{
        {in.front().clock, 0}
    };

    for (auto& util : in)
    {
        if (util.clock > out.back().clock + out.back().cycles)
            out.push_back(util);
        else
            out.back().cycles += util.cycles;
    }

    // Convert to clock end time so search is easier
    for (auto& util : out) util.clock += util.cycles;

    return out;
}

int64_t compute_intersection(const std::vector<Util>& vec, const Util& interval)
{
    if (interval.cycles <= 0) return 0;

    int64_t interval_end = interval.clock + interval.cycles;
    int64_t intersection = 0;

    auto get_begin = [](const Util& util) { return util.clock - util.cycles; };

    // Find first element of util vec such that it intersects with interval
    auto upper = std::lower_bound(vec.begin(), vec.end(), interval);

    while (upper != vec.end() && get_begin(*upper) <= interval_end)
    {
        auto delta = std::min(upper->clock - interval.clock, interval_end - get_begin(*upper));
        auto maxsize = std::min(interval.cycles, upper->cycles);
        intersection += std::max(std::min(delta, maxsize), int64_t{0});
        ++upper;
    }

    return intersection;
}

HiddenLatency compute_interval(const std::vector<Util>& vec, int64_t last_time, const Token& token)
{
    Util idle{last_time, token.clock - last_time};
    Util stall{token.clock, token.stall};
    Util issue{token.clock + token.stall, token.cycles - token.stall};

    int64_t hidden_idle = compute_intersection(vec, idle);
    int64_t hidden_stall = compute_intersection(vec, stall);
    int64_t hidden_issue = compute_intersection(vec, issue);

    return {hidden_idle, hidden_stall, hidden_issue};
}

std::vector<Util> compute_union(const std::vector<Util>& va, const std::vector<Util>& vb)
{
    std::vector<Util> out;
    out.reserve(va.size() + vb.size());

    auto append = [&](const Util& util)
    {
        const int64_t begin = util.clock - util.cycles;
        const int64_t end = util.clock;
        if (end <= begin) return;

        if (out.empty())
        {
            out.push_back(util);
            return;
        }

        const int64_t out_begin = out.back().clock - out.back().cycles;
        const int64_t out_end = out.back().clock;

        if (begin > out_end)
        {
            out.push_back(util);
            return;
        }
        else
        {
            const int64_t merged_end = std::max(out_end, end);
            out.back().clock = merged_end;
            out.back().cycles = merged_end - out_begin;
        }
    };

    auto a = va.begin();
    auto b = vb.begin();

    while (a != va.end() && b != vb.end())
    {
        int64_t abeg = a->clock - a->cycles;
        int64_t bbeg = b->clock - b->cycles;

        if (abeg <= bbeg)
            append(*a++);
        else
            append(*b++);
    }

    while (a != va.end()) append(*a++);

    while (b != vb.end()) append(*b++);

    return out;
}

bool analyzeScoped(DataStore& store, int SE, int SIMD)
{
    int VALU = -1;
    int WMMA = -1;
    int VMEM = -1;
    int FLAT = -1;
    int LDS = -1;
    int SALU = -1;
    int SMEM = -1;

    for (int i = 0; i < static_cast<int>(Config::TokenColors().size()); i++)
    {
        auto name = upper(Config::TokenColors().at(i).name);

        auto sfind = [&name](std::string_view match) { return name.find(match) != std::string::npos; };

        if (sfind("VALU")) VALU = i;
        if (sfind("MFMA") || sfind("MATRIX") || sfind("WMMA")) WMMA = i;
        if (sfind("LDS")) LDS = i;
        if (sfind("VMEM")) VMEM = i;
        if (sfind("FLAT")) FLAT = i;
        if (sfind("SALU")) SALU = i;
        if (sfind("SMEM")) SMEM = i;
    }

    QWARNING(WMMA >= 0 && VALU >= 0, " invalid WMMA or VALU type ", return false);

    std::vector<Util> wmma{};
    std::vector<Util> valu{};
    std::vector<Util> vmem{};
    std::vector<Util> scal{};

    auto buildUtil = [&](const WaveInstance& wave)
    {
        // TODO: Exclude the current token's own issue interval when revisiting self-overlap handling.
        for (const auto& token : wave.tokens)
        {
            int64_t clock = token.clock + token.stall;
            int64_t cycles = token.cycles - token.stall;

            if (token.type == WMMA)
            {
                wmma.push_back({clock, cycles});
                valu.push_back({clock, 3 * cycles / 4});
            }
            else if (token.type == VALU)
                valu.push_back({clock, cycles});
            else if (token.type == LDS || token.type == VMEM || token.type == FLAT)
                vmem.push_back({clock, cycles});
            else if (token.type == SALU || token.type == SMEM)
                scal.push_back({clock, cycles});
        }
    };

    try
    {
        store.forEachWave(
            [&](const DataStore::WaveCoordinate& coord, const WaveEntry& entry)
            {
                if (coord.hwid.se != SE || coord.hwid.simd != SIMD) return;

                auto wave = store.getWave(entry);
                if (wave) buildUtil(*wave);
            }
        );
    }
    catch (const std::exception& e)
    {
        QWARNING(false, "failed to analyze waves " << e.what(), return false);
    }
    catch (...)
    {
        QWARNING(false, "failed to analyze waves", return false);
    }

    if (auto it = store.other_simd_by_se.find(SE); it != store.other_simd_by_se.end())
        for (const auto& rec : it->second)
            if (rec.cycles > 0) vmem.push_back({rec.time, rec.cycles});

    wmma = compute_util(wmma);
    valu = compute_util(valu);
    vmem = compute_util(vmem);
    scal = compute_util(scal);

    auto math_union = compute_union(valu, wmma);
    auto vector_union = compute_union(math_union, vmem);
    auto all_union = compute_union(vector_union, scal);

    std::unordered_map<int, HiddenLatency> line_to_hidden{};

    auto analyzeWave = [&](const WaveInstance& wave)
    {
        int64_t last_time = wave.WaveBegin();
        for (const auto& token : wave.tokens)
        {
            auto hidden = HiddenLatency{};

            // WMMA hides all, VALU hides all but WMMA, VMEM hides SCAL and other, SCAL hides only other (MSG, IMMED)
            if (token.type == WMMA)
            {
                auto valu_hidden = compute_interval(valu, last_time, token);
                auto wmma_hidden = compute_interval(wmma, last_time, token);
                wmma_hidden.issue = 0;
                valu_hidden.issue = 0;
                hidden = (valu_hidden.total() > wmma_hidden.total()) ? valu_hidden : wmma_hidden;
            }
            else if (token.type == VALU)
            {
                auto valu_hidden = compute_interval(valu, last_time, token);
                auto wmma_hidden = compute_interval(wmma, last_time, token);
                valu_hidden.issue = 0;
                hidden = (valu_hidden.total() > wmma_hidden.total()) ? valu_hidden : wmma_hidden;
            }
            else if (token.type == VMEM || token.type == LDS || token.type == FLAT)
            {
                auto math_hidden = compute_interval(math_union, last_time, token);
                auto vmem_hidden = compute_interval(vmem, last_time, token);
                vmem_hidden.issue = 0;
                hidden = (math_hidden.total() > vmem_hidden.total()) ? math_hidden : vmem_hidden;
            }
            else if (token.type == SALU || token.type == SMEM)
            {
                auto vector_hidden = compute_interval(vector_union, last_time, token);
                auto scal_hidden = compute_interval(scal, last_time, token);
                scal_hidden.issue = 0;
                hidden = (vector_hidden.total() > scal_hidden.total()) ? vector_hidden : scal_hidden;
            }
            else { hidden = compute_interval(all_union, last_time, token); }

            line_to_hidden[token.code_line] += hidden;
            last_time = token.clock + token.cycles;
        }
    };

    store.forEachWave(
        [&](const DataStore::WaveCoordinate& coord, const WaveEntry& entry)
        {
            if (coord.hwid.se != SE || coord.hwid.simd != SIMD) return;

            auto wave = store.getWave(entry);
            if (wave) analyzeWave(*wave);
        }
    );

    for (const auto& [line_number, hidden] : line_to_hidden) store.hidden_latency_by_line[line_number] += hidden;

    return true;
}

void clearAsmHidden()
{
    for (auto& line : ASMCodeline::line_vec)
        if (line) line->hotspot.sqtt.clearHidden();
}

} // namespace

void applyToAsm(const DataStore& store)
{
    clearAsmHidden();

    for (const auto& [line_number, hidden] : store.hidden_latency_by_line)
    {
        auto it = ASMCodeline::line_map.find(line_number);
        QWARNING(it != ASMCodeline::line_map.end() && it->second, "Could not find line: " << line_number, continue);

        it->second->hotspot.sqtt.hidden += hidden;
    }
}

bool analyze(DataStore& store)
{
    clearAsmHidden();
    store.hidden_latency_by_line.clear();
    store.hidden_latency_analyzed = false;

    for (const auto& [se, simd_map] : store.wave_hierarchy)
        for (const auto& [simd, _] : simd_map)
            if (!analyzeScoped(store, se, simd))
            {
                store.hidden_latency_by_line.clear();
                return false;
            }

    store.hidden_latency_analyzed = true;
    applyToAsm(store);
    return true;
}

} // namespace HiddenLatencyAnalysis
