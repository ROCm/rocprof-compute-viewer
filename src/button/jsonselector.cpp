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
#include <QWidget>
#include "data/datastore.h"
#include "mainwindow.h"
#include "wave/waveview.h"

SESelector::SESelector(DataStore& store) : QComboBox(nullptr), store(store)
{
    MainWindow::window->shaderSel->addWidget(this);

    Token::bIsNaviWave = (store.gfxv == "navi");

    // Populate SE options from wave hierarchy
    for (auto& [se_id, _] : store.wave_hierarchy) this->addItem(QString::number(se_id));

    if (this->count() > 0)
        this->textChanged(this->itemText(0));
    else
        this->setVisible(false);

    QObject::connect(this, &QComboBox::currentTextChanged, this, &SESelector::textChanged);
}

SESelector::~SESelector() { delete this->selection; }

void SESelector::textChanged(const QString& text)
{
    if (this->selection) delete this->selection;
    this->selection = nullptr;
    int se = text.toInt();
    this->selection = new SimdSelector(store, se);
    this->setVisible(true);
}

SimdSelector::SimdSelector(DataStore& store, int se) : QComboBox(nullptr), store(store), se(se)
{
    MainWindow::window->simdSel->addWidget(this);

    auto it = store.wave_hierarchy.find(se);
    if (it != store.wave_hierarchy.end())
    {
        for (auto& [simd_id, _] : it->second) this->addItem(QString::number(simd_id));
    }

    if (this->count() > 0) this->textChanged(this->itemText(0));
    QObject::connect(this, &QComboBox::currentTextChanged, this, &SimdSelector::textChanged);
}

SimdSelector::~SimdSelector() { delete this->selection; }

void SimdSelector::textChanged(const QString& text)
{
    if (this->selection) delete this->selection;
    this->selection = nullptr;
    int simd = text.toInt();
    this->selection = new WSlotSelector(store, se, simd);
}

WSlotSelector::WSlotSelector(DataStore& store, int se, int simd) : QComboBox(nullptr), store(store), se(se), simd(simd)
{
    MainWindow::window->wslSel->addWidget(this);

    auto se_it = store.wave_hierarchy.find(se);
    if (se_it != store.wave_hierarchy.end())
    {
        auto simd_it = se_it->second.find(simd);
        if (simd_it != se_it->second.end())
        {
            for (auto& [slot_id, _] : simd_it->second) this->addItem(QString::number(slot_id));
        }
    }

    if (this->count() > 0) this->textChanged(this->itemText(0));
    QObject::connect(this, &QComboBox::currentTextChanged, this, &WSlotSelector::textChanged);
}

WSlotSelector::~WSlotSelector() { delete this->selection; }

void WSlotSelector::textChanged(const QString& text)
{
    if (this->selection) delete this->selection;
    this->selection = nullptr;
    int sl = text.toInt();
    this->selection = new WaveIDSelector(store, se, simd, sl);
}

WaveIDSelector::WaveIDSelector(DataStore& store, int se, int simd, int slot) :
QComboBox(nullptr), store(store), se(se), simd(simd), slot(slot)
{
    MainWindow::window->widSel->addWidget(this);
    MainWindow::window->widSelector = this;

    auto se_it = store.wave_hierarchy.find(se);
    if (se_it != store.wave_hierarchy.end())
    {
        auto simd_it = se_it->second.find(simd);
        if (simd_it != se_it->second.end())
        {
            auto slot_it = simd_it->second.find(slot);
            if (slot_it != simd_it->second.end())
            {
                for (auto& [wid, _] : slot_it->second) this->addItem(QString::number(wid));
            }
        }
    }

    if (this->count() > 0) this->textChanged(this->itemText(0));
    QObject::connect(this, &QComboBox::currentTextChanged, this, &WaveIDSelector::textChanged);
}

WaveIDSelector::~WaveIDSelector()
{
    if (MainWindow::window && MainWindow::window->widSelector == this) MainWindow::window->widSelector = nullptr;
}

void WaveIDSelector::textChanged(const QString& text) { MainWindow::window->SetMainWave(se, simd, slot, getValue()); }
