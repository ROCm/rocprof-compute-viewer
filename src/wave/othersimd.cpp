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

#include "wave/othersimd.h"
#include <algorithm>
#include <utility>
#include "util/jsonrequest.hpp"

// Parse the other_simd_filenames object into per-SE file entries.
OtherSimdFiles ParseOtherSimdFilenames(const nlohmann::json& filenames, const std::string& base_dir)
{
    OtherSimdFiles files;
    if (!filenames.is_object()) return files;

    std::string dir = base_dir;
    if (!dir.empty() && dir.back() != '/') dir.push_back('/');

    for (auto& [se_name, entry_list] : filenames.items())
    {
        int se_id = -1;
        try
        {
            se_id = std::stoi(se_name);
        }
        catch (...)
        {
            continue;
        }

        if (!entry_list.is_array()) continue;

        auto& entries = files[se_id];
        for (auto& entry : entry_list)
        {
            if (!entry.is_array() || entry.empty() || !entry.at(0).is_string()) continue;
            OtherSimdFileEntry file_entry{};
            file_entry.filepath = dir + entry.at(0).get<std::string>();
            if (entry.size() > 2 && entry.at(1).is_number_integer() && entry.at(2).is_number_integer())
            {
                file_entry.range = {entry.at(1).get<int64_t>(), entry.at(2).get<int64_t>()};
            }
            entries.push_back(std::move(file_entry));
        }
    }

    return files;
}

// Read an other-SIMD JSON file and return records overlapping the current clock window.
std::vector<OtherSimdInstruction> ReadOtherSimdInstructions(
    const std::string& filepath, int64_t clock_start, int64_t clock_end
)
{
    JsonRequest request(filepath, false);
    if (!request.bValid) return {};

    size_t time_index = 0;
    size_t duration_index = 1;
    size_t category_index = SIZE_MAX;

    auto schema_it = request.data.find("instructions_schema");
    if (schema_it != request.data.end() && schema_it->is_array())
    {
        size_t idx = 0;
        for (auto& schema : *schema_it)
        {
            if (schema.is_string())
            {
                const auto key = schema.get<std::string>();
                if (key == "time")
                    time_index = idx;
                else if (key == "duration")
                    duration_index = idx;
                else if (key == "category")
                    category_index = idx;
            }
            idx++;
        }
    }

    const auto& instructions_json = request.data["instructions"];
    if (!instructions_json.is_array()) return {};

    std::vector<OtherSimdInstruction> instructions;
    instructions.reserve(instructions_json.size());
    for (auto& item : instructions_json)
    {
        if (!item.is_array()) continue;

        OtherSimdInstruction record{};
        if (time_index < item.size()) record.time = int64_t(item.at(time_index).get<int64_t>());
        if (duration_index < item.size()) record.cycles = item.at(duration_index).get<int>();
        if (category_index < item.size()) record.category = item.at(category_index).get<int>();

        int64_t end_time = record.time + record.cycles;
        if (end_time < clock_start || record.time > clock_end) continue;

        instructions.push_back(record);
    }

    return instructions;
}

void OtherSimdData::SetFiles(OtherSimdFiles files)
{
    this->files = std::move(files);
    tokens.clear();
}

void OtherSimdData::Clear() { tokens.clear(); }

const std::vector<Token>& OtherSimdData::LoadTokens(int se, int64_t clock_start, int64_t clock_end, int color_count)
{
    tokens.clear();
    if (color_count <= 0) return tokens;

    auto files_it = files.find(se);
    if (files_it == files.end())
    {
        QWARNING(false, "No other SIMD files for SE " << se, );
        return tokens;
    }

    for (const auto& entry : files_it->second)
    {
        if (entry.range)
        {
            auto [start, end] = *entry.range;
            if (end < clock_start || start > clock_end) continue;
        }

        auto instructions = ReadOtherSimdInstructions(entry.filepath, clock_start, clock_end);
        for (const auto& instruction : instructions)
        {
            int type = instruction.category;
            bool valid_type = type >= 0 && type < color_count;
            QWARNING(valid_type, "Unknown other SIMD token type " << type, continue);

            Token token{};
            token.clock = instruction.time;
            token.cycles = std::max(0, instruction.cycles);
            token.type = type;
            tokens.push_back(token);
        }
    }

    return tokens;
}
