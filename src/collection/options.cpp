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

#include "options.h"
#include <QCheckBox>
#include <QFrame>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>
#include <string>
#include "config/appconfig.h"
#include "mainwindow.h"

OptionsDialogH::OptionsDialogH()
{
    this->setWindowTitle("Options");
    QVBox* layout = new QVBox();
    this->setLayout(layout);

    layout->addWidget(new QLabel("Y axis maximum value (<0 disables):"));

    hotspot_max_value = new QLineEdit(this);
    hotspot_max_value->setValidator(new QDoubleValidator(0, 1E14, 30, this));
    hotspot_max_value->setText(std::to_string(MainWindow::window->hotspot_max_value).c_str());
    layout->addWidget(hotspot_max_value);

    layout->addWidget(new QLabel("Number of bins on hotspot view:"));
    hotspot_bins = new QLineEdit(this);
    hotspot_bins->setValidator(new QIntValidator(4, 100, this));
    hotspot_bins->setText(std::to_string(MainWindow::window->GetHotspotBins()).c_str());
    layout->addWidget(hotspot_bins);

    layout->addWidget(new QLabel("Code line range for hotspot:"));
    QWidget* rangewidget = new QWidget(this);
    QHBox* rangebox = new QHBox();
    rangewidget->setLayout(rangebox);
    layout->addWidget(rangewidget);

    hotspot_begin = new QLineEdit(this);
    hotspot_begin->setValidator(new QIntValidator(0, INT_MAX, this));
    hotspot_begin->setText(std::to_string(MainWindow::window->hotspot_begin).c_str());

    hotspot_end = new QLineEdit(this);
    hotspot_end->setValidator(new QIntValidator(1, INT_MAX, this));
    hotspot_end->setText(std::to_string(MainWindow::window->hotspot_end).c_str());

    rangebox->addWidget(hotspot_begin);
    rangebox->addWidget(hotspot_end);

    QPushButton* but_accept = new QPushButton("Save");
    QPushButton* but_reject = new QPushButton("Cancel");

    QObject::connect(but_accept, &QPushButton::clicked, this, &OptionsDialogH::SaveExit);
    QObject::connect(but_reject, &QPushButton::clicked, this, &OptionsDialogH::reject);

    QFrame* but_frame = new QFrame(this);
    QHBox* but_layout = new QHBox();
    but_frame->setLayout(but_layout);
    but_layout->addWidget(but_accept);
    but_layout->addWidget(but_reject);
    layout->addWidget(but_frame);
}

void OptionsDialogH::SaveExit()
{
    MainWindow::window->hotspot_max_value = hotspot_max_value->displayText().toDouble();
    MainWindow::window->hotspot_n_bins = hotspot_bins->displayText().toInt();
    MainWindow::window->hotspot_begin = hotspot_begin->displayText().toInt();
    MainWindow::window->hotspot_end = hotspot_end->displayText().toInt();

    MainWindow::window->GatherWaves();
    accept();
}
