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
#include <vector>

enum class InputType
{
    JSON_DIR,
    ATT_FILES,
    ROCPD,
    UNKNOWN
};

/// Parsed metadata for a single .att file with rocprofv3 naming.
/// Filename format: <pid>_<agent>_shader_engine_<SE>_<dispatch>.att
/// Fields are -1 when the corresponding component could not be parsed.
struct AttFileInfo
{
    std::string path;
    int pid = -1;
    int agent = -1;
    int se = -1;
    int dispatch = -1; ///< capture/dispatch index from filename
    uint64_t file_size = 0;
};

struct InputInfo
{
    InputType type = InputType::UNKNOWN;
    std::string base_path;
    std::vector<std::string> att_files;
    std::vector<std::string> out_files;
    /// Parallel parsed metadata for att_files (same order, same length).
    std::vector<AttFileInfo> att_file_info;
    std::string rocpd_path;
    std::string code_json_override;
    std::string snapshots_json_override;
};

InputInfo detectInput(const std::string& path);

/// Parse a single .att path's filename (rocprofv3 convention
/// "<pid>_<agent>_shader_engine_<SE>_<dispatch>.att") into AttFileInfo.
/// Components that can't be parsed remain at -1. file_size is populated when
/// the file exists. Exposed so the file-picker path (OpenAttFiles) can keep
/// InputInfo::att_file_info in sync with att_files (the docs require it to be
/// parallel and same-length).
AttFileInfo parseAttFilename(const std::string& path);
