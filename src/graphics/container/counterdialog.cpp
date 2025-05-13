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

#include "counterdialog.h"
#include <QCheckBox>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include "util/custom_layouts.h"

#define NUM_CUS 16

QAllBox::QAllBox(std::vector<class QCheckBox*>& _boxes) : QCheckBox("All"), boxes(_boxes)
{
    connect(this, &QCheckBox::stateChanged, this, &QAllBox::checkAll);
}

void QAllBox::checkAll(int state)
{
    for (QCheckBox* box : boxes)
    {
        if (box) box->setChecked(state != 0);
    }
}

CounterDialog::CounterDialog(
    std::vector<bool>& cu_list, std::vector<bool>& se_list, std::vector<std::pair<std::string, bool>>& counter_list
) :
cu_list(cu_list), se_list(se_list), counter_list(counter_list), QDialog()
{
    this->setWindowTitle("Compute Unit and Shader Filter");
    this->layout = new QVBox(this);

    QFrame* cu_frame = new QFrame(this);
    cu_frame->setLayout(new QVBox());
    QFrame* cu_widget = new QFrame(cu_frame);
    QBox* cu_layout = new QBox();
    cu_widget->setLayout(cu_layout);

    cu_frame->layout()->addWidget(new QLabel("Select compute units:"));
    cu_frame->layout()->addWidget(cu_widget);

    QFrame* se_frame = new QFrame(this);
    se_frame->setLayout(new QVBox());
    QFrame* se_widget = new QFrame(se_frame);
    QBox* se_layout = new QBox();
    se_widget->setLayout(se_layout);

    se_frame->layout()->addWidget(new QLabel("Select shader engines:"));
    se_frame->layout()->addWidget(se_widget);

    QFrame* counter_frame = new QFrame(this);
    counter_frame->setLayout(new QVBox());
    QFrame* counter_widget = new QFrame(counter_frame);
    QBox* counter_layout = new QBox();
    counter_widget->setLayout(counter_layout);

    counter_frame->layout()->addWidget(new QLabel("Disable counters:"));
    counter_frame->layout()->addWidget(counter_widget);

    for (int j = 0; j < NUM_CUS / 4; j++)
    {
        for (int i = 0; i < 4; i++)
        {
            if (4 * j + i >= cu_list.size()) break;

            QCheckBox* box = new QCheckBox(std::to_string(j * 4 + i).c_str());
            box->setChecked(cu_list[4 * j + i]);

            cu_layout->addWidget(box, j + 1, i);
            compute_boxes.push_back(box);
        }
    }
    cu_layout->addWidget(new QAllBox(compute_boxes), 0, 0);

    for (int j = 0; j < se_list.size(); j++)
    {
        for (int i = 0; i < 6; i++)
        {
            if (6 * j + i >= se_list.size()) break;

            QCheckBox* box = new QCheckBox(std::to_string(j * 6 + i).c_str());
            box->setChecked(se_list[6 * j + i]);

            se_layout->addWidget(box, j + 1, i);
            shader_boxes.push_back(box);
        }
    }
    se_layout->addWidget(new QAllBox(shader_boxes), 0, 0);

    {
        int count = 1;
        for (auto& [name, _] : counter_list)
        {
            QCheckBox* box = new QCheckBox(name.c_str());
            box->setChecked(false);

            counter_layout->addWidget(box, count, 0);
            counter_boxes.push_back(box);
            count++;
        }
    }
    counter_layout->addWidget(new QAllBox(counter_boxes), 0, 0);

    this->layout->addWidget(cu_frame);
    this->layout->addWidget(se_frame);
    this->layout->addWidget(counter_frame);

    QPushButton* but_accept = new QPushButton("Save");
    QPushButton* but_reject = new QPushButton("Cancel");

    QObject::connect(but_accept, &QPushButton::clicked, this, &CounterDialog::SaveExit);
    QObject::connect(but_reject, &QPushButton::clicked, this, &CounterDialog::reject);

    QFrame* but_frame = new QFrame(this);
    QHBox* but_layout = new QHBox();
    but_frame->setLayout(but_layout);
    but_layout->addWidget(but_accept);
    but_layout->addWidget(but_reject);
    this->layout->addWidget(but_frame);
}

CounterDialog::~CounterDialog()
{
    if (this->layout) delete this->layout;
};

void CounterDialog::SaveExit()
{
    for (int i = 0; i < compute_boxes.size() && i < cu_list.size(); i++) cu_list[i] = compute_boxes[i]->isChecked();

    for (int i = 0; i < shader_boxes.size() && i < se_list.size(); i++) se_list[i] = shader_boxes[i]->isChecked();

    for (int i = 0; i < counter_boxes.size() && i < counter_list.size(); i++)
        counter_list[i].second = counter_boxes[i]->isChecked();

    accept();
}
