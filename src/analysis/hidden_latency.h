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

#include <atomic>
#include <cstdint>
#include <map>
#include <vector>

#include "wave/token.h"

class DataStore;

namespace HiddenLatencyAnalysis
{

enum class Pipe
{
    Scalar,
    Valu,
    Wmma,
    Vmem,
    Lds,
    Ray,
    Jump,
    Message,
    Immediate
};

struct PipeTrack
{
    Pipe pipe = Pipe::Scalar;
    int simd = 0;

    bool operator<(const PipeTrack& other) const
    {
        if (pipe != other.pipe) return pipe < other.pipe;
        return simd < other.simd;
    }
};

using PipeSequences = std::map<PipeTrack, TokenMap>;

struct HiddenLatency
{
    int64_t stall = 0;
    int64_t idle = 0;
    int64_t hidden_valu_stall = 0;
    int64_t hidden_valu_idle = 0;
    int64_t hidden_any_stall = 0;
    int64_t hidden_any_idle = 0;
    int64_t instructions = 0;

    int64_t hiddenValu() const { return hidden_valu_stall + hidden_valu_idle; }
    int64_t hiddenAny() const { return hidden_any_stall + hidden_any_idle; }

    HiddenLatency& operator+=(const HiddenLatency& other)
    {
        stall += other.stall;
        idle += other.idle;
        hidden_valu_stall += other.hidden_valu_stall;
        hidden_valu_idle += other.hidden_valu_idle;
        hidden_any_stall += other.hidden_any_stall;
        hidden_any_idle += other.hidden_any_idle;
        instructions += other.instructions;
        return *this;
    }
};

struct Summary
{
    std::map<int, HiddenLatency> by_line;
    PipeSequences pipe_sequences;
    HiddenLatency total;
    int64_t waves = 0;
    int64_t operations = 0;
};

Summary analyze(DataStore& store, std::atomic<int>* progress = nullptr);
void finalize(const Summary& summary);

} // namespace HiddenLatencyAnalysis
