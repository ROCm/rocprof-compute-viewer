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

#include <QObject>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "json/include/nlohmann/json.hpp"
#include "memtracker.h"
#include "util/diagnostic_log.h"

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
