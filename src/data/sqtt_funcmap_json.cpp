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

#include "data/sqtt_funcmap_json.h"

#include "util/diagnostic_log.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

namespace
{
bool readUnsigned(const nlohmann::json& value, uint64_t max_value, uint64_t& out)
{
    if (value.is_number_unsigned())
    {
        uint64_t v = value.get<uint64_t>();
        if (v > max_value) return false;
        out = v;
        return true;
    }
    if (value.is_number_integer())
    {
        int64_t v = value.get<int64_t>();
        if (v < 0 || static_cast<uint64_t>(v) > max_value) return false;
        out = static_cast<uint64_t>(v);
        return true;
    }
    return false;
}

std::string normalizedKind(std::string kind)
{
    std::transform(
        kind.begin(), kind.end(), kind.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); }
    );
    return kind;
}

bool readKind(const nlohmann::json& value, MarkerKind& out)
{
    if (!value.is_string()) return false;

    const std::string kind = normalizedKind(value.get<std::string>());
    if (kind == "f" || kind == "function" || kind == "func")
    {
        out = MarkerKind::Function;
        return true;
    }
    if (kind == "k" || kind == "kernel")
    {
        out = MarkerKind::Kernel;
        return true;
    }
    if (kind == "u" || kind == "user" || kind == "user_scope" || kind == "userscope")
    {
        out = MarkerKind::UserScope;
        return true;
    }
    if (kind == "p" || kind == "point")
    {
        out = MarkerKind::Point;
        return true;
    }
    return false;
}
} // namespace

SqttFuncmapJson SqttFuncmapJson::LoadFromCodeJson(const std::string& path)
{
    SqttFuncmapJson out;

    std::ifstream file(path);
    if (!file.is_open()) return out;

    try
    {
        nlohmann::json data = nlohmann::json::parse(file);
        out = FromJson(data);
    }
    catch (const std::exception& e)
    {
        RCV_LOG();
        out.diags.push_back("failed to parse " + path + ": " + e.what());
    }
    return out;
}

SqttFuncmapJson SqttFuncmapJson::FromJson(const nlohmann::json& data)
{
    SqttFuncmapJson out;
    if (!data.is_object() || !data.contains("sqtt_funcmap")) return out;

    const auto& table = data["sqtt_funcmap"];
    if (!table.is_array())
    {
        out.diags.push_back("sqtt_funcmap must be an array");
        return out;
    }

    for (size_t i = 0; i < table.size(); ++i)
    {
        const auto& row = table[i];
        if (!row.is_array())
        {
            out.addDiagnostic(i, "row must be an array");
            continue;
        }
        if (row.size() < 4)
        {
            out.addDiagnostic(i, "row must contain codeobj_id, marker_id, kind, and name");
            continue;
        }

        uint64_t codeobj_id = 0;
        if (!readUnsigned(row[0], std::numeric_limits<uint64_t>::max(), codeobj_id) || codeobj_id == 0)
        {
            out.addDiagnostic(i, "invalid codeobj_id");
            continue;
        }

        uint64_t marker_id = 0;
        if (!readUnsigned(row[1], std::numeric_limits<uint32_t>::max(), marker_id))
        {
            out.addDiagnostic(i, "invalid marker_id");
            continue;
        }

        MarkerKind kind = MarkerKind::Unknown;
        if (!readKind(row[2], kind))
        {
            out.addDiagnostic(i, "unknown marker kind");
            continue;
        }

        if (!row[3].is_string())
        {
            out.addDiagnostic(i, "marker name must be a string");
            continue;
        }
        std::string name = row[3].get<std::string>();
        if (name.empty())
        {
            out.addDiagnostic(i, "marker name must not be empty");
            continue;
        }

        std::string source_loc;
        if (row.size() >= 5)
        {
            if (!row[4].is_string() && !row[4].is_null())
            {
                out.addDiagnostic(i, "source_loc must be a string or null");
                continue;
            }
            if (row[4].is_string()) source_loc = row[4].get<std::string>();
        }

        ResolvedMarker resolved;
        resolved.found = true;
        resolved.kind = kind;
        resolved.name = std::move(name);
        resolved.source_loc = std::move(source_loc);
        out.entries_by_codeobj[codeobj_id][static_cast<uint32_t>(marker_id)] = std::move(resolved);
    }

    return out;
}

ResolvedMarker SqttFuncmapJson::Resolve(uint32_t marker_id, uint64_t codeobj_id) const
{
    if (codeobj_id == 0) return {};

    auto co_it = entries_by_codeobj.find(codeobj_id);
    if (co_it == entries_by_codeobj.end()) return {};

    ResolvedMarker out;
    out.metadata_available = true;

    auto marker_it = co_it->second.find(marker_id);
    if (marker_it == co_it->second.end()) return out;

    out = marker_it->second;
    out.metadata_available = true;
    return out;
}

uint64_t SqttFuncmapJson::CodeobjForKernelName(const std::string& name) const
{
    uint64_t match = 0;
    for (const auto& [codeobj_id, entries] : entries_by_codeobj)
    {
        for (const auto& entry : entries)
        {
            const auto& marker = entry.second;
            if (marker.kind != MarkerKind::Kernel || marker.name != name) continue;
            if (match != 0 && match != codeobj_id) return 0;
            match = codeobj_id;
        }
    }
    return match;
}

void SqttFuncmapJson::addDiagnostic(size_t row, const std::string& message)
{
    std::ostringstream ss;
    ss << "sqtt_funcmap row " << row << ": " << message;
    diags.push_back(ss.str());
}
