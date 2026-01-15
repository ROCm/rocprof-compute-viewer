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
#include <QFontMetrics>
#include <QImage>
#include <QPainter>

// ============================================================================
// TextLineElement - Minimal implementation for testing
// Line element with text rendering, caching, and hover states
// ============================================================================

#include <QString>
#include <string>

class TextLineElement
{
public:
    TextLineElement(const std::string& _str) : stdtext(_str), text(_str.c_str()) {}

    int width(QFontMetrics& fm)
    {
        if (width_cache <= 1) width_cache = fm.horizontalAdvance(this->text);
        return width_cache;
    }

    const QString& getText() const { return this->text; }
    const std::string& getStdText() const { return stdtext; }
    void InvalidateCache() const { width_cache = -1; }

    void setRefHighlight(bool value, bool click)
    {
        bRefHighlight = value;
        bHighlightMode = click;
    }

    void setMouseHover(bool value) { bHovering = value; }
    bool isHovering() const { return bHovering; }
    bool isRefHighlight() const { return bRefHighlight; }
    bool isHighlightMode() const { return bHighlightMode; }
    int getCachedWidth() const { return width_cache; }

protected:
    mutable int width_cache = -1;
    std::string stdtext;
    QString text;

    bool bHovering = false;
    bool bRefHighlight = false;
    bool bHighlightMode = false;
};

// ============================================================================
// TextLineElement Tests
// ============================================================================

class TextLineElementTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // QApplication is required for QFontMetrics
    }
};

TEST_F(TextLineElementTest, ConstructorSetsText)
{
    TextLineElement elem("Hello World");

    EXPECT_EQ(elem.getStdText(), "Hello World");
    EXPECT_EQ(elem.getText(), QString("Hello World"));
}

TEST_F(TextLineElementTest, EmptyStringConstructor)
{
    TextLineElement elem("");

    EXPECT_EQ(elem.getStdText(), "");
    EXPECT_TRUE(elem.getText().isEmpty());
}

TEST_F(TextLineElementTest, WidthCalculationCachesResult)
{
    TextLineElement elem("Test String");
    QFont font;
    QFontMetrics fm(font);

    // First call calculates width
    int width1 = elem.width(fm);
    EXPECT_GT(width1, 0);

    // Cache should now be populated
    EXPECT_EQ(elem.getCachedWidth(), width1);

    // Second call should return same value (from cache)
    int width2 = elem.width(fm);
    EXPECT_EQ(width1, width2);
}

TEST_F(TextLineElementTest, InvalidateCacheResetsWidth)
{
    TextLineElement elem("Test");
    QFont font;
    QFontMetrics fm(font);

    elem.width(fm);
    EXPECT_GT(elem.getCachedWidth(), 0);

    elem.InvalidateCache();
    EXPECT_EQ(elem.getCachedWidth(), -1);
}

TEST_F(TextLineElementTest, HoverStateManagement)
{
    TextLineElement elem("Hover Test");

    EXPECT_FALSE(elem.isHovering());

    elem.setMouseHover(true);
    EXPECT_TRUE(elem.isHovering());

    elem.setMouseHover(false);
    EXPECT_FALSE(elem.isHovering());
}

TEST_F(TextLineElementTest, RefHighlightStates)
{
    TextLineElement elem("Highlight Test");

    EXPECT_FALSE(elem.isRefHighlight());
    EXPECT_FALSE(elem.isHighlightMode());

    // Set highlight without click
    elem.setRefHighlight(true, false);
    EXPECT_TRUE(elem.isRefHighlight());
    EXPECT_FALSE(elem.isHighlightMode());

    // Set highlight with click
    elem.setRefHighlight(true, true);
    EXPECT_TRUE(elem.isRefHighlight());
    EXPECT_TRUE(elem.isHighlightMode());

    // Clear highlight
    elem.setRefHighlight(false, false);
    EXPECT_FALSE(elem.isRefHighlight());
    EXPECT_FALSE(elem.isHighlightMode());
}

TEST_F(TextLineElementTest, UnicodeTextSupport)
{
    TextLineElement elem("日本語テスト");
    QFont font;
    QFontMetrics fm(font);

    EXPECT_EQ(elem.getStdText(), "日本語テスト");
    // Width should be calculated for unicode
    int width = elem.width(fm);
    EXPECT_GT(width, 0);
}

TEST_F(TextLineElementTest, LongTextWidth)
{
    std::string longText(1000, 'A');
    TextLineElement elem(longText);
    QFont font;
    QFontMetrics fm(font);

    int width = elem.width(fm);
    EXPECT_GT(width, 100); // Should be quite wide
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
