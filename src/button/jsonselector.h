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

#include <QComboBox>
#include <memory>
#include "json/include/nlohmann/json.hpp"
#include "util/custom_layouts.h"
using json = nlohmann::json;

//! A combobox selector class for selecting json files.
class Selector : public QComboBox
{
    Q_OBJECT
    set_tracked();

public:
    Selector(json& data, class QLayout* w, Selector* sel_parent) :
    QComboBox(nullptr), selector_parent(sel_parent), ui_elem(w), data_json(data)
    {
        w->addWidget(this);
        addSortedNames();
    }
    virtual ~Selector()
    {
        if (selection) delete selection;
    }
    int64_t getValue() { return std::stoll(currentText().toStdString()); }

    void addSortedNames();
    std::vector<std::string> names; ///< Possible (partial) names for json files
    json data_json;
    class QWidget* selection = nullptr;
    class QLayout* ui_elem = nullptr; ///< Layout for combobox in mainwindow.ui
    class Selector* selector_parent;  ///< Selector up in the selection chain
};

//! A combobox for selecting the target shader engine
class SESelector : public Selector
{
    Q_OBJECT
public:
    SESelector(class JsonRequest& request);
    virtual ~SESelector() {}
    void textChanged(const QString& text);
signals:
};

//! A combobox for selecting the target SIMD unit
class SimdSelector : public Selector
{
    Q_OBJECT
public:
    SimdSelector(json& data, SESelector* parent);
    virtual ~SimdSelector() {}
    void textChanged(const QString& text);
};

//! A combobox for selecting the target wave slot
class WSlotSelector : public Selector
{
    Q_OBJECT
public:
    WSlotSelector(json& data, SimdSelector* parent);
    virtual ~WSlotSelector(){};
    void textChanged(const QString& text);
};

//! A combobox for selecting the target wave id
class WaveIDSelector : public Selector
{
    Q_OBJECT
public:
    WaveIDSelector(json& data, WSlotSelector* parent);
    virtual ~WaveIDSelector();
    void textChanged(const QString& text);
};
