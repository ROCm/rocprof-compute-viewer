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

#include "code/textelement.h"
#include <gtest/gtest.h>
#include <QApplication>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include "config/config.hpp"

namespace
{
QImage render(TextLineElement& element, const QFont& font)
{
    QFontMetrics fm(font);
    const int overline = fm.height() - fm.overlinePos();
    QImage image(
        fm.horizontalAdvance(element.getText()) + 4 * overline, 2 * fm.height(), QImage::Format_ARGB32_Premultiplied
    );
    image.fill(WindowColors::Background());
    QPainter painter(&image);
    const QPen pen(WindowColors::textColor());
    painter.setFont(font);
    painter.setPen(pen);
    element.paint(painter, overline, fm.height(), fm.height(), overline);
    EXPECT_EQ(painter.pen(), pen);
    EXPECT_EQ(painter.font(), font);
    painter.end();
    return image;
}
} // namespace

TEST(TextLineElementTest, MeasurementsFollowFontChangesIncludingShrinking)
{
    for (const std::string text : {std::string(), std::string("日本語テスト"), std::string(1000, 'A')})
    {
        TextLineElement element(text);
        for (int size : {10, 24, 10})
        {
            QFont font;
            font.setPointSize(size);
            QFontMetrics fm(font);
            EXPECT_EQ(element.width(fm), fm.horizontalAdvance(QString::fromStdString(text)));
        }
    }
}

TEST(TextLineElementTest, HighlightGeometryDoesNotDependOnPremeasuringText)
{
    for (bool dark : {false, true})
    {
        WindowColors::setDark(dark);
        TextLineElement cold("v_add_f32 v0, v1, v2");
        TextLineElement warm("v_add_f32 v0, v1, v2");
        QFont font;
        font.setPointSize(14);
        QFontMetrics fm(font);
        warm.width(fm);
        cold.setRefHighlight(true, true);
        warm.setRefHighlight(true, true);
        cold.setMouseHover(true);
        warm.setMouseHover(true);
        EXPECT_EQ(render(cold, font), render(warm, font));
    }
}

TEST(TextLineElementTest, HoverAndReferenceHighlightsRenderAndClear)
{
    for (bool dark : {false, true})
    {
        WindowColors::setDark(dark);
        TextLineElement element("s_load_dwordx4 s[0:3], s[4:5], 0");
        const QFont font;
        const auto plain = render(element, font);
        for (bool hover : {false, true})
        {
            element.setRefHighlight(!hover, false);
            element.setMouseHover(hover);
            EXPECT_NE(render(element, font), plain);
            element.setRefHighlight(false, false);
            element.setMouseHover(false);
            EXPECT_EQ(render(element, font), plain);
        }
    }
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
