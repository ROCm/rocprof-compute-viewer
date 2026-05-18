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
#include <string>
#include <unordered_map>
#include <vector>
#include "data/marker_walker.h"
#include "json/include/nlohmann/json.hpp"

/// Parser for SQTT marker funcmaps embedded in code.json.
///
/// Expected top-level shape:
///   "sqtt_funcmap": [
///       [codeobj_id, marker_id, kind, name, source_loc?],
///       ...
///   ]
///
/// marker_id values are scoped to codeobj_id. Duplicate marker_id values in
/// different code objects are valid and are resolved only with the active
/// code object ID. Accepted kind strings are:
///   F, function, func
///   K, kernel
///   U, user, user_scope, userscope
///   P, point
///
/// Kernel rows can also be searched by name for JSON-only traces whose wave
/// instruction rows do not carry code-object IDs. Duplicate kernel names across
/// different code objects are treated as ambiguous.
class SqttFuncmapJson
{
public:
    static SqttFuncmapJson LoadFromCodeJson(const std::string& path);
    static SqttFuncmapJson FromJson(const nlohmann::json& data);

    bool empty() const { return entries_by_codeobj.empty(); }
    const std::vector<std::string>& diagnostics() const { return diags; }

    ResolvedMarker Resolve(uint32_t marker_id, uint64_t codeobj_id) const;
    uint64_t CodeobjForKernelName(const std::string& name) const;

private:
    using EntryMap = std::unordered_map<uint32_t, ResolvedMarker>;

    void addDiagnostic(size_t row, const std::string& message);

    std::unordered_map<uint64_t, EntryMap> entries_by_codeobj;
    std::vector<std::string> diags;
};
