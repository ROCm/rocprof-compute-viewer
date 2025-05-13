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
#include <QColor>

//! A QColor with common math operations
class Color : public QColor
{
public:
    Color() = default;
    Color(int r, int g, int b) : QColor(clip(r), clip(g), clip(b)){};
    Color(const QColor& c) : QColor(c){};
    Color& operator=(const QColor& o)
    {
        setRed(o.red());
        setGreen(o.green());
        setBlue(o.blue());
        return *this;
    }
    Color& operator*=(float f)
    {
        setRed(clip(red() * f));
        setGreen(clip(green() * f));
        setBlue(clip(blue() * f));
        return *this;
    }
    Color& operator/=(float f)
    {
        *this *= (1.0f / f);
        return *this;
    }
    Color& operator*=(const Color& o)
    {
        setRed(clip(red() * o.red()));
        setGreen(clip(green() * o.green()));
        setBlue(clip(blue() * o.blue()));
        return *this;
    }
    Color& operator+=(const Color& o)
    {
        setRed(clip(red() + o.red()));
        setGreen(clip(green() + o.green()));
        setBlue(clip(blue() + o.blue()));
        return *this;
    }

    Color operator*(float f)
    {
        Color color = *this;
        color *= f;
        return color;
    }
    Color operator/(float f)
    {
        Color color = *this;
        color /= f;
        return color;
    }
    Color operator*(const Color& o)
    {
        Color color = *this;
        color *= o;
        return color;
    }
    Color operator+(const Color& o)
    {
        Color color = *this;
        color += o;
        return color;
    }

    static float clip(float f) { return std::max(0.0f, std::min(255.0f, f)); }
};

//! A QTimer used for highlighting (token, lines)
class HighlightTimer
{
public:
    virtual void Highlight();
    virtual void IncrementHighlight(float amount = 0.02f);
    virtual ~HighlightTimer();
    class QTimer* timer = nullptr;
    float vis = 1.0f;
};
