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

#include "input_detector.h"

#include <algorithm>
#include <filesystem>
#include <string>

#include "util/diagnostic_log.h"

namespace fs = std::filesystem;

/// Parse "<pid>_<agent>_shader_engine_<SE>_<dispatch>.att" filename into AttFileInfo.
/// Falls back gracefully if any component is missing — leaves fields at -1.
AttFileInfo parseAttFilename(const std::string& path)
{
    AttFileInfo info;
    info.path = path;
    info.file_size = fs::is_regular_file(path) ? fs::file_size(path) : 0;

    std::string stem = fs::path(path).stem().string();

    // Look for the marker substring; everything before its leading underscore
    // is "<pid>_<agent>". Everything after is "<SE>_<dispatch>" (possibly more).
    constexpr const char* kMarker = "_shader_engine_";
    auto mpos = stem.find(kMarker);
    if (mpos == std::string::npos) return info;

    // Parse <pid>_<agent> from prefix.
    {
        std::string prefix = stem.substr(0, mpos);
        auto us = prefix.find('_');
        if (us != std::string::npos)
        {
            try
            {
                info.pid = std::stoi(prefix.substr(0, us));
            }
            catch (...)
            {
                RCV_LOG();
            }
            try
            {
                info.agent = std::stoi(prefix.substr(us + 1));
            }
            catch (...)
            {
                RCV_LOG();
            }
        }
    }

    // Parse <SE>_<dispatch> from suffix.
    std::string suffix = stem.substr(mpos + std::string(kMarker).size());
    auto us2 = suffix.find('_');
    if (us2 != std::string::npos)
    {
        try
        {
            info.se = std::stoi(suffix.substr(0, us2));
        }
        catch (...)
        {
            RCV_LOG();
        }
        // dispatch is the next numeric token; stop at any further underscore.
        std::string disp_tok = suffix.substr(us2 + 1);
        auto us3 = disp_tok.find('_');
        if (us3 != std::string::npos) disp_tok.resize(us3);
        try
        {
            info.dispatch = std::stoi(disp_tok);
        }
        catch (...)
        {
            RCV_LOG();
        }
    }
    else
    {
        try
        {
            info.se = std::stoi(suffix);
        }
        catch (...)
        {
            RCV_LOG();
        }
    }
    return info;
}

InputInfo detectInput(const std::string& path)
{
    InputInfo info;
    info.base_path = path;

    fs::path p(path);

    // Single file: check for .rocpd
    if (fs::is_regular_file(p))
    {
        if (p.extension() == ".rocpd")
        {
            info.type = InputType::ROCPD;
            info.rocpd_path = path;
            return info;
        }
        info.type = InputType::UNKNOWN;
        return info;
    }

    if (!fs::is_directory(p))
    {
        info.type = InputType::UNKNOWN;
        return info;
    }

    // Directory: check for filenames.json (JSON_DIR)
    if (fs::exists(p / "filenames.json"))
    {
        info.type = InputType::JSON_DIR;
        return info;
    }

    // Directory: check for .att files (ATT_FILES)
    for (const auto& entry : fs::directory_iterator(p))
    {
        if (!entry.is_regular_file()) continue;

        auto ext = entry.path().extension().string();
        if (ext == ".att")
            info.att_files.push_back(entry.path().string());
        else if (ext == ".out" || ext == ".hsaco")
            info.out_files.push_back(entry.path().string());
    }

    if (!info.att_files.empty())
    {
        std::sort(info.att_files.begin(), info.att_files.end());
        std::sort(info.out_files.begin(), info.out_files.end());
        info.att_file_info.reserve(info.att_files.size());
        for (const auto& p : info.att_files) info.att_file_info.push_back(parseAttFilename(p));
        info.type = InputType::ATT_FILES;
        return info;
    }

    info.type = InputType::UNKNOWN;
    return info;
}
