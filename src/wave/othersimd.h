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

#include <string>
#include <vector>
#include "json/include/nlohmann/json.hpp"
#include "token.h"
#include "wave/othersimd_types.h"

// Parse the other_simd_filenames object into per-SE file entries.
OtherSimdFiles ParseOtherSimdFilenames(const nlohmann::json& filenames, const std::string& base_dir);

// Read an other-SIMD JSON file and return records overlapping the current clock window.
std::vector<OtherSimdInstruction> ReadOtherSimdInstructions(
    const std::string& filepath, int64_t clock_start, int64_t clock_end, int64_t time_offset = 0
);

class OtherSimdData
{
public:
    // Store the per-SE other-SIMD file list.
    void SetFiles(OtherSimdFiles files);
    // Store per-SE in-memory records (decoder path).
    void SetRecords(std::map<int, std::vector<OtherSimdInstruction>> records);
    // Clear cached tokens.
    void Clear();
    // True when data is available (files or in-memory records).
    bool HasFiles() const { return !files.empty() || !in_memory_records.empty(); }
    // Load tokens for a given SE and clock window, validating token types.
    const std::vector<Token>& LoadTokens(int se, int64_t clock_start, int64_t clock_end, int color_count);
    // Access the last loaded tokens.
    const std::vector<Token>& Tokens() const { return tokens; }

private:
    OtherSimdFiles files{};
    std::map<int, std::vector<OtherSimdInstruction>> in_memory_records{};
    std::vector<Token> tokens{};
};
