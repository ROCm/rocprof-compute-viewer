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

#include "custom_layouts.h"

namespace
{
void clearLayout(QLayout* layout)
{
    if (!layout) return;

    while (QLayoutItem* child = layout->takeAt(0))
    {
        if (auto* child_layout = child->layout()) clearLayout(child_layout);
        if (auto* widget = child->widget())
        {
            widget->setParent(nullptr);
            delete widget;
        }
        delete child;
    }
}
} // namespace

QVBox::~QVBox() { clearLayout(this); }
QHBox::~QHBox() { clearLayout(this); }

QBox::~QBox() { clearLayout(this); }

int MemTracker::count = 0;
std::unordered_map<std::string, int> MemTracker::classes;
