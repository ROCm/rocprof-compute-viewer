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

#include <QMetaType>
#include <QString>
#include <QTabWidget>
#include <QWidget>
#include <vector>
#include "hotspot.hpp"
#include "textelement.h"
#include "util/custom_layouts.h"

class SourceLine : public TextLineElement
{
    using Super = TextLineElement;

public:
    SourceLine(int number, const char* _line, class SourceFile* _parent) :
    TextLineElement(_line), line_number(number), parent(_parent)
    {}
    ~SourceLine()
    {
        if (last_pressed_line == this) last_pressed_line = nullptr;
    }

    virtual void paint(class QPainter& painter, int posx, int posy, int stepy, int overline, int numlines_width);
    virtual void onMousePress() override;
    virtual void setMouseHover(bool value) override;
    virtual void setRefHighlight(bool value, bool click) override;

    void scrollTo();

    void add_latency(int type, Latency sqtt, Latency pcs);

    const int line_number;
    SourceFile* const parent;

    std::vector<std::weak_ptr<class ASMCodeline>> refs{};
    HorizontalHotspot hotspot{};

    static SourceLine* last_pressed_line;
    static std::unordered_map<std::string, std::shared_ptr<SourceLine>> all_lines;
    static bool bDisplayLineNumber;

private:
    int next_index = 0;
};

class SourceFile : public QTextElement
{
    Q_OBJECT;
    set_tracked();
    friend class SourceFileTab;
    using Super = QTextElement;

public:
    SourceFile(const std::string& filename, const std::string& snappath, class SourceFileTab* _parent);
    virtual void paintEvent(class QPaintEvent* event) override;
    virtual QSize sizeHint() const override { return QSize(std::max(sizex, 300), sizey); };

    virtual LineElement* getelement(int index) override
    {
        if (index < 0 || index >= lines.size()) return nullptr;
        return lines.at(index).get();
    };
    virtual int line_height() override { return stepy; };
    void scrollTo(int number);

    class SourceFileTab* const parent;
    const std::string filename;

    HorizontalHotspot latency{};
    int64_t max_sqtt_latency = 1; // Start at one to avoid division by zero
    int64_t max_pcs_latency = 1;  // Start at one to avoid division by zero
    static int64_t global_max_sqtt_latency;
    static int64_t global_max_pcs_latency;

private:
    int sizex = 0;
    int sizey = 0;
    int stepy = 0;

    std::vector<std::shared_ptr<SourceLine>> lines{};
};

class SourceFileTab : public QTabWidget
{
    Q_OBJECT;
    set_tracked();

public:
    virtual ~SourceFileTab() { clear(); }

    void resetLatency();
    void refreshHiddenLatencyFromAsm();
    void refreshLatencyDisplay();
    void clear();
    void addFile(const std::string& filename, const std::string& snappath);
    void setSnapFile(const std::string& snappath);

    void scrollTo(const std::string& filename, int number);
    static std::string getFilename(const std::string& linepath);

    std::unordered_map<std::string, std::pair<class QScrollArea*, SourceFile*>> files{};
    std::unordered_map<std::string, std::string> snap_to_filename{};
};
