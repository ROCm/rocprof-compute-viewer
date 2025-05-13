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

#include <QObject>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "json/include/nlohmann/json.hpp"
#include "wavemanager.h"

//! Class containing a string to be parsed by WaveReader. Can be requested by disk or network.
class StreamRequest : public QObject,
                      public std::stringstream
{
    Q_OBJECT
    set_tracked();

public:
    StreamRequest(const std::string& path);

protected:
    class QNetworkAccessManager* manager = nullptr;
    void ReadFromFile(const std::string& path);
    void ReadFromNetwork(const std::string& path);
    void replyFinished(class QNetworkReply* reply);
};

//! Requests a json file by disk or network, depending on path
class JsonRequest : public StreamRequest
{
    Q_OBJECT
    set_tracked();

public:
    JsonRequest(const std::string& path, bool bWarn = true);
    bool bValid = false;
    nlohmann::json data;
};

struct occupancy_data
{
    int64_t time{0};
    int8_t cu{0};
    int8_t simd{0};
    int8_t slot{0};
    int8_t enable{0};
    int kernel_id{0};

    occupancy_data() = default;

    template <typename T> static occupancy_data build(T& v)
    {
        occupancy_data occ;
        occ.time = (int64_t) v[0];
        occ.cu = (int8_t) v[1];
        occ.simd = (int8_t) v[2];
        occ.slot = (int8_t) v[3];
        occ.enable = (int8_t) v[4];
        occ.kernel_id = (int) v[5];
        return occ;
    };
};
