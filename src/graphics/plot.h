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

#include <QPainter>
#include <QtWidgets/QWidget>

#ifdef RCV_DISABLE_OPENGL
typedef QWidget BasePlotWidget;
#else
#    include <QOpenGLWidget>
typedef QOpenGLWidget BasePlotWidget;
#endif

struct PlotPoint
{
    float time;
    float value;
};

struct WeightedPoint
{
    float time;
    float value;
    float weight;
};

struct LODCurve
{
    std::vector<PlotPoint> data{};
    float min_interval = 1;

    void set(const std::vector<WeightedPoint>& points)
    {
        for (auto& point : points) data.emplace_back(PlotPoint{point.time, point.value});
    }
    float search(float time) const;
};

struct PlotCurve
{
    std::string fullname;
    std::string shortname;
    QColor color;
    std::vector<LODCurve> lods = {};
    int lod = 0;
    bool disabled = false;
    double ymax = 1E-3;

    const LODCurve& get() const { return lods.at(lod); }
    void SetData(std::vector<WeightedPoint>&& data);
    bool CreateLODs(int mip, std::vector<WeightedPoint>& points);
    void UpdateLOD(float range, int width, bool bAuto);
};

class PlotGraph : public BasePlotWidget
{
    Q_OBJECT

public:
    explicit PlotGraph(int _penwidth, QWidget* parent = nullptr);
    ~PlotGraph(){};

    void AddData(std::string name, QColor color, std::vector<WeightedPoint>&& data);
    inline int smallwidth() const { return width() - left_space - margin; }
    inline double scaledwidth() const { return smallwidth() * xscale; }
    inline double posToPixel(double x) const { return left_space + (x + xoffset) * scaledwidth() / xmax; }
    inline double pixelToPos(double x) const { return (x - left_space) * xmax / scaledwidth() - xoffset; }

    void SetBarPos(double x)
    {
        barpos = x;
        update();
    };

    void setAutoLod(bool bAutoLod)
    {
        this->bAutoLod = bAutoLod;
        update();
    };

    void setDisabled(const std::string& name, bool disable)
    {
        for (auto& curve : curves)
            if (curve.fullname == name) curve.disabled = disable;
    }

    void setDisabled(const std::vector<std::pair<std::string, bool>>& names)
    {
        for (auto& name : names) setDisabled(name.first, name.second);
        update();
    }

    std::vector<std::pair<std::string, bool>> getDisabled() const
    {
        std::vector<std::pair<std::string, bool>> ret;
        for (auto& curve : curves) ret.push_back({curve.fullname, curve.disabled});
        return ret;
    }

private slots:
    void wheelEvent(QWheelEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

protected:
    void paintEvent(QPaintEvent*) override;
    virtual void UpdateGraphTable(float timepos) = 0;
    bool bAutoLod = true;

    double xmin = 0;
    double xmax = 1;

    double xscale = 1;
    double xoffset = 0;
    double yscale = 1;
    double yoffset = 0;

    float barpos = -1;
    QPoint mousepos{0, 0};
    QPoint lclickpos{0, 0};
    bool bLClick = false;
    std::vector<PlotCurve> curves;

private:
    const int bottom_space = 20;
    const int top_space = 35;
    const int left_space = 50;
    const int margin = 6;
    const int penwidth;
};