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

struct occupancy_data
{
    int64_t time{0};
    uint8_t cu{0};
    int8_t simd{0};
    int8_t slot{0};
    int8_t enable{0};
    int kernel_id{0};

    occupancy_data() = default;

    template <typename T> static occupancy_data build(T& v)
    {
        occupancy_data occ;
        occ.time = (int64_t) v[0];
        occ.cu = (uint8_t) v[1];
        occ.simd = (int8_t) v[2];
        occ.slot = (int8_t) v[3];
        occ.enable = (int8_t) v[4];
        occ.kernel_id = (int) v[5];
        return occ;
    };
};
