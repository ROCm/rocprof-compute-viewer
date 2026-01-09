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

#include <cstdint>
#include <string>

namespace WaveUtils
{

// Helper for safe shift that handles negative mipmap_level (allows zooming in more)
// When mipmap_level < 0, left shift becomes right shift and vice versa
inline int64_t mipShiftLeft(int64_t value, int mipmap_level)
{
    return mipmap_level >= 0 ? (value << mipmap_level) : (value >> (-mipmap_level));
}

inline int64_t mipShiftRight(int64_t value, int mipmap_level)
{
    return mipmap_level >= 0 ? (value >> mipmap_level) : (value << (-mipmap_level));
}

// Calculate token size based on value and mipmap level
// bIsNaviWave affects the multiplier (12/2 for Navi, 3/2 for others)
inline int64_t getTokenSize(int64_t value, int mipmap_level, bool bIsNaviWave)
{
    int64_t rounding = mipShiftLeft(1, mipmap_level) >> 1;
    return mipShiftRight((value * (bIsNaviWave ? 12 : 3) / 2) + rounding, mipmap_level);
}

// Calculate slot height reduction based on mipmap level
inline int slotHeightReduction(int mipmap_level) { return mipmap_level >= 1 ? (2 - mipmap_level) : 3; }

} // namespace WaveUtils

namespace SourceUtils
{

// Extract filename from a "filename:linenumber" path
// Returns the original string if no ':' is found
inline std::string getFilename(const std::string& linepath)
{
    size_t pos = linepath.rfind(':');
    if (pos == std::string::npos) return linepath;
    return linepath.substr(0, pos);
}

// Extract line number from a "filename:linenumber" path
// Returns -1 if no valid line number is found
inline int getLineNumber(const std::string& linepath)
{
    size_t pos = linepath.rfind(':');
    if (pos == std::string::npos || pos + 1 >= linepath.size()) return -1;
    try
    {
        return std::stoi(linepath.substr(pos + 1));
    }
    catch (...)
    {
        return -1;
    }
}

// Extract just the basename from a full path
// e.g., "/path/to/file.cpp" -> "file.cpp"
inline std::string getBasename(const std::string& filepath)
{
    size_t found = filepath.rfind('/');
    if (found == std::string::npos || found + 1 >= filepath.size()) return filepath;
    return filepath.substr(found + 1);
}

} // namespace SourceUtils

// Latency accumulator - pure data structure
struct LatencyData
{
    int64_t latency = 0;
    int64_t stalled = 0;

    LatencyData& operator+=(const LatencyData& other)
    {
        latency += other.latency;
        stalled += other.stalled;
        return *this;
    }

    bool operator==(const LatencyData& other) const { return latency == other.latency && stalled == other.stalled; }
};
