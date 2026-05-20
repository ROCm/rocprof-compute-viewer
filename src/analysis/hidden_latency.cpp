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

#include "code/asmcode.h"
#include "config/config.hpp"
#include "data/datastore.h"
#include "data/wavemanager.h"

namespace HiddenLatencyAnalysis
{

namespace
{

std::string uppercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::toupper(c); });
    return value;
}

Pipe pipeForTokenType(int type)
{
    const auto& colors = Config::TokenColors();
    if (type < 0 || type >= static_cast<int>(colors.size())) return Pipe::Scalar;

    const auto name = uppercase(colors.at(type).name);
    auto has = [&name](std::string_view match) { return name.find(match) != std::string::npos; };

    if (has("IMMED") || has("TRAP")) return Pipe::Immediate;
    if (has("MSG")) return Pipe::Message;
    if (has("WMMA") || has("MFMA") || has("MATRIX")) return Pipe::Wmma;
    if (has("VALU")) return Pipe::Valu;
    if (has("LDS")) return Pipe::Lds;
    if (has("BVH") || has("RAY")) return Pipe::Ray;
    if (has("FLAT") || has("VMEM")) return Pipe::Vmem;
    if (has("BRANCH") || has("JUMP") || has("NEXT")) return Pipe::Jump;
    return Pipe::Scalar;
}

void analyzeWave(int simd, const WaveInstance& wave, Summary& summary)
{
    if (simd < 0 || simd >= 4) return;
    for (const auto& token : wave.tokens) summary.pipe_sequences[{pipeForTokenType(token.type), simd}].push_back(token);
    summary.operations += static_cast<int64_t>(wave.tokens.size());
}

} // namespace

Summary analyze(DataStore& store, std::atomic<int>* progress)
{
    if (progress) progress->store(0);

    Summary summary;
    store.forEachWave(
        [&store, &summary, progress](const DataStore::WaveCoordinate& coord, const WaveEntry& entry)
        {
            try
            {
                auto wave = store.getWave(entry);
                if (wave)
                {
                    ++summary.waves;
                    analyzeWave(coord.hwid.simd, *wave, summary);
                }
            }
            catch (const std::exception& e)
            {
                std::cerr << "hidden latency: failed to load wave " << entry.id << ": " << e.what() << "\n";
            }
            catch (...)
            {
                std::cerr << "hidden latency: failed to load wave " << entry.id << "\n";
            }

            if (progress) progress->fetch_add(1);
        }
    );

    for (auto& [_, sequence] : summary.pipe_sequences) sequence.Compile();
    return summary;
}

void finalize(const Summary& summary)
{
    for (auto& line : ASMCodeline::line_vec)
        if (line) line->hotspot.sqtt.clearHidden();

    for (const auto& [line_number, hidden] : summary.by_line)
    {
        auto it = ASMCodeline::line_map.find(line_number);
        if (it == ASMCodeline::line_map.end() || !it->second) continue;

        auto& latency = it->second->hotspot.sqtt;
        latency.hidden_valu_stall += hidden.hidden_valu_stall;
        latency.hidden_valu_idle += hidden.hidden_valu_idle;
        latency.hidden_any_stall += hidden.hidden_any_stall;
        latency.hidden_any_idle += hidden.hidden_any_idle;
    }
}

} // namespace HiddenLatencyAnalysis
