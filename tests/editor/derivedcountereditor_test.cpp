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

#include <gtest/gtest.h>
#include <QApplication>
#include <QDir>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QToolButton>

#include "derivedcountereditor.h"

// Test fixture for DerivedCounterEditor tests
class DerivedCounterEditorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        rawCounters = {
            {"RAW_COUNTER_1", "SE x CU x Time"},
            {"RAW_COUNTER_2", "SE x CU x Time"},
        };
        derivedCounters = {
            {"DERIVED_1", "SE x CU x Time"},
        };
        builtinText = "# Derived counter definitions\nDERIVED_1 = RAW_COUNTER_1 + RAW_COUNTER_2\n";
    }

    std::vector<DerivedCounterEditor::CounterInfo> rawCounters;
    std::vector<DerivedCounterEditor::CounterInfo> derivedCounters;
    QString builtinText;
};

// ============================================================================
// Construction and UI Structure
// ============================================================================

TEST_F(DerivedCounterEditorTest, HasRequiredUIElements)
{
    DerivedCounterEditor editor(builtinText, rawCounters, derivedCounters);

    EXPECT_NE(editor.findChild<QTabWidget*>(), nullptr);
    EXPECT_NE(editor.findChild<QListWidget*>(), nullptr);
    EXPECT_GE(editor.findChildren<QPushButton*>().size(), 2);
    EXPECT_GE(editor.findChildren<QToolButton*>().size(), 2);
}

TEST_F(DerivedCounterEditorTest, CounterListShowsProvidedCounters)
{
    DerivedCounterEditor editor(builtinText, rawCounters, derivedCounters);
    auto* counterList = editor.findChild<QListWidget*>();

    bool foundRaw = false, foundDerived = false;
    for (int i = 0; i < counterList->count(); ++i)
    {
        QString text = counterList->item(i)->text();
        if (text.contains("RAW_COUNTER_1")) foundRaw = true;
        if (text.contains("DERIVED_1")) foundDerived = true;
    }
    EXPECT_TRUE(foundRaw);
    EXPECT_TRUE(foundDerived);
}

TEST_F(DerivedCounterEditorTest, EmptyCounterListsDoNotCrash)
{
    DerivedCounterEditor editor(builtinText, {}, {});
    EXPECT_NE(editor.findChild<QListWidget*>(), nullptr);
}

// ============================================================================
// Tab Behavior
// ============================================================================

TEST_F(DerivedCounterEditorTest, DefaultTabContainsBuiltinText)
{
    DerivedCounterEditor editor(builtinText, rawCounters, derivedCounters);

    QString content = editor.getCurrentTabContent();
    EXPECT_FALSE(content.isEmpty());
}

TEST_F(DerivedCounterEditorTest, TabsAreClosableAndMovable)
{
    DerivedCounterEditor editor(builtinText, rawCounters, derivedCounters);
    auto* tabWidget = editor.findChild<QTabWidget*>();

    EXPECT_TRUE(tabWidget->tabsClosable());
    EXPECT_TRUE(tabWidget->isMovable());
}

TEST_F(DerivedCounterEditorTest, ModifyingTextMarksTabAsModified)
{
    DerivedCounterEditor editor(builtinText, rawCounters, derivedCounters);
    auto* tabWidget = editor.findChild<QTabWidget*>();
    auto* textEditor = qobject_cast<QPlainTextEdit*>(tabWidget->widget(0));

    textEditor->appendPlainText("\n# New comment");
    QApplication::processEvents();

    EXPECT_TRUE(tabWidget->tabText(0).endsWith("*"));
}

// ============================================================================
// Editor Configuration
// ============================================================================

TEST_F(DerivedCounterEditorTest, EditorIsEditableWithNoLineWrap)
{
    DerivedCounterEditor editor(builtinText, rawCounters, derivedCounters);
    auto* tabWidget = editor.findChild<QTabWidget*>();
    auto* textEditor = qobject_cast<QPlainTextEdit*>(tabWidget->widget(0));

    EXPECT_FALSE(textEditor->isReadOnly());
    EXPECT_EQ(textEditor->lineWrapMode(), QPlainTextEdit::NoWrap);
}

// ============================================================================
// Static Methods
// ============================================================================

TEST_F(DerivedCounterEditorTest, GetDefinitionsDirCreatesValidPath)
{
    QString dir = DerivedCounterEditor::getDefinitionsDir();

    EXPECT_TRUE(dir.contains("derived_counters"));
    EXPECT_TRUE(QDir(dir).exists());
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
