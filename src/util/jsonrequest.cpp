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

#include <vector>
#include "json/include/nlohmann/json.hpp"
#ifndef _WIN32
#    include <cxxabi.h>
#endif

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <fstream>
#include "jsonrequest.hpp"

using namespace std;

StreamRequest::StreamRequest(const std::string& path)
{
    if (path.find("http://") != std::string::npos)
        ReadFromNetwork(path);
    else
        ReadFromFile(path);
}

void StreamRequest::ReadFromFile(const std::string& path)
{
    std::ifstream f(path, std::fstream::in);
    if (f.good())
        *this << f.rdbuf();
    else
        setstate(std::ios_base::eofbit | std::ios_base::failbit);
}

void StreamRequest::ReadFromNetwork(const std::string& path)
{
    std::cout << "Request: " << path << std::endl;
    QNetworkAccessManager manager(this);

    QTimer timer;
    timer.setSingleShot(true);
    timer.setInterval(5000);

    QEventLoop loop;
    connect(&manager, &QNetworkAccessManager::finished, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    QNetworkReply* reply = manager.get(QNetworkRequest(QUrl(path.c_str())));
    timer.start();
    loop.exec();

    replyFinished(reply);
}

void StreamRequest::replyFinished(QNetworkReply* reply)
{
    QByteArray arr = reply->readAll();
    *this << arr.data();

    if (str().size() == 0 || reply->error() != 0)
    {
        setstate(std::ios_base::eofbit | std::ios_base::failbit);
        std::cout << "Response: Error - " << reply->error() << std::endl;
    }
}

JsonRequest::JsonRequest(const std::string& path, bool bWarn) : StreamRequest(path)
{
    if (fail() || bad())
    {
        QWARNING(!bWarn, "Could not parse: " << path, return );
        return;
    }
    this->data = nlohmann::json::parse(*this);
    bValid = true;
}
