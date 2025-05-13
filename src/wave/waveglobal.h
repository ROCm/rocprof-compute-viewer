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

#include <QWidget>
#include <iostream>
#include <memory>
#include <unordered_set>
#include "data/wavemanager.h"
#include "measure.h"

struct WaveTraceData
{
    int64_t begin;
    int64_t end;
    int kid;
};

//! A Block of tokens with pre-defined size.
class QOutsideWaveView : public QWidget
{
    Q_OBJECT
    set_tracked();
    using Super = QWidget;

public:
    QOutsideWaveView(
        int se, int cu, int simd, int slot, std::vector<WaveTraceData>& waves, std::shared_ptr<MeasureTool>& tool
    );
    virtual ~QOutsideWaveView()
    {
        if (tool) tool->update_list.erase(this);
    };

    int64_t DrawWave(QPainter& painter, int64_t start, const WaveTraceData& wave, const QRect& rect);

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override { return sizeHint(); };

    virtual void paintEvent(QPaintEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;

    bool bIsVisible = true;

protected:
    float height_multiplier;
    const int se;
    const int cu;
    const int simd;
    const int slot;
    const std::vector<WaveTraceData> waves;
    std::shared_ptr<MeasureTool> tool;

public:
signals:
};

class QGlobalView : public QWidget
{
    Q_OBJECT
    set_tracked();
    using Super = QWidget;

public:
    QGlobalView(const std::string& filename);
    virtual ~QGlobalView()
    {
        if (tool) tool->update_list.erase(this);
    }

    static int64_t PosToClock(int64_t value) { return value * Delta() + begintime; }
    static int64_t ClockToPos(int64_t value) { return (value - begintime) / Delta(); }
    static int64_t Delta() { return 1 << mipmap_level; }
    static int64_t HEIGHT() { return 5 - mipmap_level / 6; }

    static int GetMip() { return mipmap_level; }
    void SetMip(int new_mipmap_level)
    {
        mipmap_level = new_mipmap_level;
        for (auto* view : views) view->updateGeometry();
        this->updateGeometry();
        this->repaint();
    }

    virtual void paintEvent(QPaintEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;

    static std::unordered_map<int, std::string> kernel_names;
    std::shared_ptr<MeasureTool> tool = std::make_shared<MeasureTool>();

private:
    static int64_t maxtime;
    static int64_t begintime;
    static int mipmap_level;
    std::vector<QOutsideWaveView*> views;
};
