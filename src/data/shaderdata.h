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

#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <vector>
#include "json/include/nlohmann/json.hpp"

/// A single shaderdata record: ["time","value","cu","simd","wave_id","flags"]
struct ShaderDataRecord
{
    int64_t time{0};
    uint32_t value{0};
    int cu{0};
    int simd{0};
    int wave_id{0};
    int flags{0};
    int se{0}; // Shader Engine, derived from filename
};

/// Shared, sorted vector of shaderdata records (avoids copying across views).
using ShaderDataRecordVec = std::shared_ptr<const std::vector<ShaderDataRecord>>;

/// Manages loading and querying shaderdata records from JSON files.
class ShaderDataManager
{
public:
    ShaderDataManager() = default;

    /// Parse the "shaderdata_filenames" section of filenames.json and load all records.
    /// Files are loaded in parallel using multiple threads.
    /// @param shaderdata_filenames The JSON object: { "SE_num": [["file", begin, end], ...], ... }
    /// @param base_dir The directory containing the shaderdata files.
    void Load(const nlohmann::json& shaderdata_filenames, const std::string& base_dir);

    /// Get shared pointer to records for a given SE, CU, SIMD, and slot (sorted by time).
    /// The shaderdata wave_id field corresponds to the occupancy slot.
    ShaderDataRecordVec GetRecords(int se, int cu, int simd, int slot) const;

    /// Check if any shaderdata was loaded.
    bool HasData() const { return m_has_data; }

private:
    /// Load a single shaderdata JSON file and return its records.
    static std::vector<ShaderDataRecord> LoadFile(const std::string& filepath, int se);

    /// Key: (se, cu, simd, slot/wave_id) -> shared sorted records
    std::map<std::tuple<int, int, int, int>, ShaderDataRecordVec> m_records_by_location;

    bool m_has_data = false;
};
