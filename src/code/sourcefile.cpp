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

#include "sourcefile.h"
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolTip>
#include <fstream>
#include <string>
#include "../config/config.hpp"
#include "asmcode.h"
#include "mainwindow.h"
#include "qcodelist.h"
#include "util/custom_layouts.h"

bool SourceLine::bDisplayLineNumber = true;

SourceLine* SourceLine::last_pressed_line = nullptr;

std::unordered_map<std::string, std::shared_ptr<SourceLine>> SourceLine::all_lines{};
int64_t SourceFile::global_max_sqtt_latency = 1;
int64_t SourceFile::global_max_pcs_latency = 1;

HorizontalHotspot::DrawFormat SourceLine::drawformat = HorizontalHotspot::DrawFormat::DRAWTYPE;

void SourceLine::scrollTo()
{
    if (parent) parent->scrollTo(line_number);
}

SourceFile::SourceFile(const std::string& _filename, const std::string& snappath, SourceFileTab* _parent) :
parent(_parent), filename(_filename)
{
    std::ifstream file(snappath, std::fstream::in);

    while (file.good())
    {
        std::string ss;
        std::getline(file, ss);

        for (size_t i = 0; i < ss.size(); i++)
            if (ss[i] == '\t')
            {
                ss[i] = ' ';
                ss.insert(i, "   ");
            }

        lines.emplace_back(std::make_shared<SourceLine>(lines.size(), ss.c_str(), this));
        SourceLine::all_lines[filename + ':' + std::to_string(lines.size())] = lines.back();
    }

    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);

    this->setAutoFillBackground(true);
    QPalette pal = QPalette();
    pal.setColor(QPalette::Window, WindowColors::Background());
    this->setPalette(pal);

    setMouseTracking(true);
}

void SourceFile::paintEvent(class QPaintEvent* event)
{
    int HISTOGRAM_WIDTH = HorizontalHotspot::HISTOGRAM_WIDTH;

    QPainter painter(this);

    QFont font = MainWindow::default_font.isEmpty() ? painter.font() : QFont(MainWindow::default_font);
    font.setPointSize(MainWindow::font());
    painter.setFont(font);

    QFontMetrics fm(font);
    const int overline = fm.height() - fm.overlinePos();
    stepy = fm.height();

    int posy = overline + stepy;
    QRect rect = event->rect();

    int num_lines_width =
        SourceLine::bDisplayLineNumber ? fm.horizontalAdvance(QString::number(10 * lines.size() + 10)) : 0;

    painter.setPen(QPen(WindowColors::textColor(), 0.4));
    painter.drawLine(HISTOGRAM_WIDTH + num_lines_width, 0, HISTOGRAM_WIDTH + num_lines_width, height());

    for (auto& line : lines)
    {
        if (posy + 2 * stepy >= rect.y() && posy <= rect.y() + rect.height() + stepy)
            line->paint(painter, overline, posy, stepy, overline, num_lines_width);

        posy += stepy;
    }

    if (sizey != posy)
    {
        sizey = posy;
        updateGeometry();
        update();
    }

    if (sizex == 0)
    {
        for (auto& line : lines)
            sizex = std::max(sizex, HISTOGRAM_WIDTH + fm.horizontalAdvance(line->getText()) + 2 * overline);

        updateGeometry();
        update();
    }

    this->Super::paintEvent(event);
}

void SourceFile::scrollTo(int number)
{
    QWARNING(parent, "Invalid parent widget", return );
    parent->scrollTo(this->filename, number);
}

void SourceLine::add_latency(int type, Latency sqtt, Latency pcs)
{
    hotspot.add_latency(type, sqtt, pcs);
    parent->latency.add_latency(type, sqtt, pcs);

    parent->max_sqtt_latency = std::max(parent->max_sqtt_latency, hotspot.sqtt.latency);
    parent->max_pcs_latency = std::max(parent->max_pcs_latency, hotspot.pcs.latency);
    // TODO: Reset this when reloading code
    SourceFile::global_max_sqtt_latency = std::max(SourceFile::global_max_sqtt_latency, parent->latency.sqtt.latency);
    SourceFile::global_max_pcs_latency = std::max(SourceFile::global_max_pcs_latency, parent->latency.pcs.latency);
}

void SourceLine::paint(QPainter& painter, int posx, int posy, int sizey, int overline, int numlines_width)
{
    int HISTOGRAM_WIDTH = HorizontalHotspot::HISTOGRAM_WIDTH;
    if (width_cache <= 1) width_cache = painter.fontMetrics().horizontalAdvance(text);

    hotspot.paint(
        painter, 0, posy - sizey, sizey, parent->max_sqtt_latency, parent->max_pcs_latency, drawformat, false, false
    );

    posx += HISTOGRAM_WIDTH;

    if (bDisplayLineNumber) painter.drawText(posx, posy - overline, QString::number(line_number + 1));
    this->Super::paint(painter, numlines_width + posx, posy, sizey, overline);
}

void SourceLine::onMousePress()
{
    if (!QCodelist::singleton) return;

    if (last_pressed_line)
    {
        auto* tmp = last_pressed_line;
        last_pressed_line = nullptr;
        tmp->setMouseHover(false);

        if (tmp == this) return;
    }

    last_pressed_line = this;

    for (auto& ref : refs)
        if (auto refptr = ref.lock()) refptr->setRefHighlight(true, true);

    for (auto& ref : refs)
        if (auto refptr = ref.lock())
        {
            QCodelist::singleton->Highlight(refptr->line_number, refptr->line_number, true);
            return;
        }
};

void SourceLine::setMouseHover(bool value)
{
    bHovering = value;
    parent->update();

    bool bIsPressed = last_pressed_line == this;

    for (auto& ref : refs)
        if (auto refptr = ref.lock()) refptr->setRefHighlight(bIsPressed | value, bIsPressed && !value);

    if (refs.size() && QCodelist::singleton) QCodelist::singleton->update();
};

void SourceLine::setRefHighlight(bool value, bool click)
{
    bRefHighlight = value;
    parent->update();
};

void SourceFileTab::addFile(const std::string& filename, const std::string& snappath)
{
    size_t found = filename.rfind('/');
    if (found == std::string::npos || found + 3 >= filename.size()) found = 0;

    auto* newfile = new SourceFile(filename, snappath, this);
    if (!newfile->lines.size())
    {
        delete newfile;
        return;
    };

    auto* source_scrollArea = new QScrollArea();
    source_scrollArea->setWidgetResizable(true);

    QVBox* box = new QVBox();
    box->addWidget(newfile);

    source_scrollArea->setLayout(box);
    source_scrollArea->setWidget(newfile);

    addTab(source_scrollArea, filename.substr(found + 1).c_str());
    files[filename] = {source_scrollArea, newfile};
    snap_to_filename[snappath] = filename;
}

void SourceFileTab::setSnapFile(const std::string& snapname)
{
    try
    {
        setCurrentWidget(files.at(snap_to_filename.at(snapname)).first);
    }
    catch (std::out_of_range&)
    {
        QWARNING(false, "Invalid file " << snapname, return );
    }
}

std::string SourceFileTab::getFilename(const std::string& linepath)
{
    size_t pos = linepath.rfind(':');
    if (pos == std::string::npos) return linepath;
    return linepath.substr(0, pos);
}

void SourceFileTab::scrollTo(const std::string& filename, int number)
{
    if (files.find(filename) == files.end()) return;

    auto& filepair = files.at(filename);
    setCurrentWidget(filepair.first);

    int stepy = filepair.second->stepy;
    int posy = number * stepy;
    auto* bar = filepair.first->verticalScrollBar();

    if (bar->value() + 2 * height() / 3 < posy + stepy) { bar->setValue(posy - 2 * height() / 3 - stepy); }
    else if (bar->value() > posy) { bar->setValue(posy); }
}

void SourceFileTab::clear()
{
    files.clear();
    snap_to_filename.clear();
    SourceLine::all_lines.clear();
    while (count()) removeTab(0);
}

void SourceFileTab::resetLatency()
{
    for (auto& [_, file] : files)
        if (file.second)
        {
            for (auto& line : file.second->lines)
                if (line) line->hotspot = HorizontalHotspot{};

            file.second->latency = HorizontalHotspot{};
            file.second->max_sqtt_latency = 1;
            file.second->max_pcs_latency = 1;
        }
    SourceFile::global_max_sqtt_latency = 1;
    SourceFile::global_max_pcs_latency = 1;
}
