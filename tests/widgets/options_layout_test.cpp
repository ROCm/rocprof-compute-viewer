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

#include <gtest/gtest.h>
#include <QApplication>
#include <QMainWindow>
#include <QScrollBar>
#include "ui_mainwindow.h"

TEST(OptionsLayoutTest, KeepsGraphOptionsHeightAndScrolls)
{
    QMainWindow window;
    Ui::MainWindow ui;
    ui.setupUi(&window);
    ui.tabWidget_3->setCurrentWidget(ui.tab);

    window.resize(1200, 900);
    window.show();
    QApplication::processEvents();
    const int graph_options_height = ui.frame_5->height();

    window.resize(800, 300);
    QApplication::processEvents();

    EXPECT_EQ(ui.frame_5->height(), graph_options_height);
    EXPECT_GT(ui.options_scroll_area->verticalScrollBar()->maximum(), 0);
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
