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
#include <QCheckBox>
#include <QDialog>
#include <QFrame>
#include <vector>
#include "util/memtracker.h"

class QAllBox : public QCheckBox
{
    Q_OBJECT
    set_tracked();

public:
    QAllBox(std::vector<class QCheckBox*>& boxes);

private:
    void checkAll(int state);
    std::vector<class QCheckBox*>& boxes;
};

class CounterDialog : public QDialog
{
    Q_OBJECT
    set_tracked();

public:
    CounterDialog(
        std::vector<bool>& cu_list, std::vector<bool>& se_list, std::vector<std::pair<std::string, bool>>& counter_list
    );
    virtual ~CounterDialog();

    void SelectAllSE(bool enable);
    void SelectAllCU(bool enable);
    void SaveExit();

    std::vector<bool>& cu_list;
    std::vector<bool>& se_list;
    std::vector<std::pair<std::string, bool>>& counter_list;

private:
    class QVBox* layout = nullptr;
    std::vector<class QCheckBox*> compute_boxes = {};
    std::vector<class QCheckBox*> shader_boxes = {};
    std::vector<class QCheckBox*> counter_boxes = {};
};
