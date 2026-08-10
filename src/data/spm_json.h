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

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "util/validity_mask.h"

using SpmValidityMask = Validity::Mask;

struct SpmCounterData
{
    std::string name;
    size_t xcc_count = 1;
    size_t se_count = 1;
    size_t instance_count = 1;
    // Row-major [XCC][SHADER_ENGINE][INSTANCE][sample].
    std::vector<float> values;
    // One bit per value. Empty means every value is valid.
    SpmValidityMask valid;
};

struct SpmData
{
    size_t sample_count = 0;
    // Row-major [XCC][sample], retaining the absolute SPM timestamp.
    std::vector<uint64_t> timestamps;
    // Row-major [XCC][sample], padded with the final valid clock value.
    // Contains shader-clock values after alignment, or relative SPM timestamp
    // values when no thread-trace realtime records are available.
    std::vector<float> clock;
    // One bit per [XCC][sample]. Empty means every sample is valid.
    SpmValidityMask sample_valid;
    std::vector<SpmCounterData> counters;

    bool empty() const { return counters.empty(); }
    size_t xccCount() const { return sample_count ? timestamps.size() / sample_count : 0; }
    bool sampleValid(size_t index) const { return Validity::test(sample_valid, index); }
    size_t validSamples(size_t xcc) const
    {
        size_t count = sample_count;
        while (count > 0 && !sampleValid(xcc * sample_count + count - 1)) --count;
        return count;
    }
};

struct SpmClockAnchor
{
    int se = 0;
    int64_t shader_clock = 0;
    uint64_t realtime_clock = 0;
};

SpmData loadSpmJson(const std::string& path);
bool spmClockRangesOverlap(const SpmData& spm, const std::vector<SpmClockAnchor>& anchors);
bool alignSpmClock(SpmData& spm, const std::vector<SpmClockAnchor>& anchors);
