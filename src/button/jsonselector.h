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

#include <QComboBox>
#include "util/custom_layouts.h"

class DataStore;

//! A combobox for selecting the target shader engine
class SESelector : public QComboBox
{
    Q_OBJECT
    set_tracked();

public:
    SESelector(DataStore& store);
    ~SESelector() override;
    void textChanged(const QString& text);

    int64_t getValue() { return std::stoll(currentText().toStdString()); }

private:
    DataStore& store;
    class QWidget* selection = nullptr;
};

//! A combobox for selecting the target SIMD unit
class SimdSelector : public QComboBox
{
    Q_OBJECT
    set_tracked();

public:
    SimdSelector(DataStore& store, int se);
    ~SimdSelector() override;
    void textChanged(const QString& text);

    int64_t getValue() { return std::stoll(currentText().toStdString()); }

private:
    DataStore& store;
    int se;
    class QWidget* selection = nullptr;
};

//! A combobox for selecting the target wave slot
class WSlotSelector : public QComboBox
{
    Q_OBJECT
    set_tracked();

public:
    WSlotSelector(DataStore& store, int se, int simd);
    ~WSlotSelector() override;
    void textChanged(const QString& text);

    int64_t getValue() { return std::stoll(currentText().toStdString()); }

private:
    DataStore& store;
    int se;
    int simd;
    class QWidget* selection = nullptr;
};

//! A combobox for selecting the target wave id
class WaveIDSelector : public QComboBox
{
    Q_OBJECT
    set_tracked();

public:
    WaveIDSelector(DataStore& store, int se, int simd, int slot);
    ~WaveIDSelector() override;
    void textChanged(const QString& text);

    int64_t getValue() { return std::stoll(currentText().toStdString()); }

private:
    DataStore& store;
    int se;
    int simd;
    int slot;
};
