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

#include "jsonselector.h"
#include <QTableWidget>
#include <QWidget>
#include <fstream>
#include "code/qcodelist.h"
#include "data/wavedata.h"
#include "mainwindow.h"
#include "util/jsonrequest.hpp"
#include "util/version.h"
#include "wave/waveview.h"

static void sort_by_id(std::vector<std::string>& names)
{
    try
    {
        std::sort(
            names.begin(),
            names.end(),
            [](const std::string& v1, const std::string& v2) -> bool { return stoi(v1) < stoi(v2); }
        );
    }
    catch (std::exception& e)
    {
        QWARNING(false, "Could not sort filenames:" << e.what(), return );
    }
}

void Selector::addSortedNames()
{
    names.clear();
    for (auto& [name, value] : data_json.items())
    {
        if (value.size() == 0) continue;
        names.push_back(name);
    }

    sort_by_id(names);

    for (auto& name : names) this->addItem(QString(name.c_str()));
}

SESelector::SESelector(JsonRequest& request) :
Selector(request.data["wave_filenames"], MainWindow::window->shaderSel, nullptr)
{
    try
    {
        Token::bIsNaviWave = std::string(request.data["gfxv"]) == "navi";
    }
    catch (std::exception& e)
    {
        Token::bIsNaviWave = false;
    }

    try
    {
        auto version = std::string(request.data["version"]);
        size_t major_pos = version.find('.');
        size_t minor_pos = version.find('.', major_pos + 1);

        Version::Get().tool_major = std::stoi(version.substr(0, major_pos));
        Version::Get().tool_minor = std::stoi(version.substr(major_pos + 1, major_pos + 1 - minor_pos));
        Version::Get().tool_rev = std::stoi(version.substr(minor_pos + 1));

        std::cout << "Version: " << Version::Get().viewer_major << '.' << Version::Get().viewer_major << '.'
                  << Version::Get().viewer_major << std::endl;
        std::cout << "Tool: " << Version::Get().tool_major << '.' << Version::Get().tool_minor << '.'
                  << Version::Get().tool_rev << std::endl;
    }
    catch (...)
    {}

    // Possible new set of files with same name, so json cache needs to be flushed
    WaveInstance::InvalidadeCache();
    if (names.size() > 0)
        this->textChanged(names[0].c_str());
    else
        this->setVisible(false);
    QObject::connect(this, &QComboBox::currentTextChanged, this, &SESelector::textChanged);
}

void SESelector::textChanged(const QString& text)
{
    if (this->selection) delete this->selection;
    this->selection = new SimdSelector(data_json[text.toStdString()], this);
    this->setVisible(true);
}

SimdSelector::SimdSelector(json& _data, SESelector* parent) : Selector(_data, MainWindow::window->simdSel, parent)
{
    if (names.size() > 0) this->textChanged(names[0].c_str());
    QObject::connect(this, &QComboBox::currentTextChanged, this, &SimdSelector::textChanged);
}

void SimdSelector::textChanged(const QString& text)
{
    if (this->selection) delete this->selection;
    this->selection = new WSlotSelector(data_json[text.toStdString()], this);
}

WSlotSelector::WSlotSelector(json& _data, SimdSelector* parent) : Selector(_data, MainWindow::window->wslSel, parent)
{
    if (names.size() > 0) this->textChanged(names[0].c_str());
    QObject::connect(this, &QComboBox::currentTextChanged, this, &WSlotSelector::textChanged);
}

void WSlotSelector::textChanged(const QString& text)
{
    if (this->selection) delete this->selection;
    this->selection = new WaveIDSelector(data_json[text.toStdString()], this);
}

WaveIDSelector::WaveIDSelector(json& _data, WSlotSelector* parent) : Selector(_data, MainWindow::window->widSel, parent)
{
    MainWindow::window->widSelector = this;
    if (names.size() > 0) this->textChanged(names[0].c_str());
    QObject::connect(this, &QComboBox::currentTextChanged, this, &WaveIDSelector::textChanged);
}

WaveIDSelector::~WaveIDSelector()
{
    if (MainWindow::window && MainWindow::window->widSelector == this) MainWindow::window->widSelector = nullptr;
}

void WaveIDSelector::textChanged(const QString& text)
{
    if (this->selection) delete this->selection;

    Selector* simdSel = selector_parent->selector_parent;
    Selector* shaderSel = simdSel->selector_parent;

    MainWindow::window->SetMainWave(
        shaderSel->getValue(), simdSel->getValue(), selector_parent->getValue(), getValue()
    );
}
