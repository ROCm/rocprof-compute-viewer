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

#include <QDialog>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QString>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <string>
#include <vector>

class DerivedCounterEditor : public QDialog
{
    Q_OBJECT

public:
    //! Counter info: name, shape string, and optional first values for tooltip
    struct CounterInfo
    {
        QString name;
        QString shape;
        std::vector<float> firstValues; //!< First few values for tooltip display
    };

    explicit DerivedCounterEditor(
        const QString& builtinText,
        const std::vector<CounterInfo>& rawCounters = {},
        const std::vector<CounterInfo>& derivedCounters = {},
        QWidget* parent = nullptr
    );
    ~DerivedCounterEditor() = default;

    //! Get the directory where definition files are stored
    static QString getDefinitionsDir();

    //! Load derived counter definitions from the first file found in definitions directory
    static std::string loadDefinitions();

    //! Get the content of the currently active tab
    QString getCurrentTabContent() const;

    //! Format a float value for tooltip display
    static QString formatValue(float value);

    //! Maximum number of values to store for tooltip display
    static constexpr size_t kMaxTooltipValues = 10;

private slots:
    void addNewDefinition();
    void deleteCurrentDefinition();
    void saveCurrentDefinition();
    void saveAllDefinitions();
    void closeTab(int index);
    void onTabChanged(int index);
    void updateDeleteButtonState();
    void showHelp();

private:
    void setupUI();
    void loadExistingDefinitions();
    bool saveDefinitionToFile(int tabIndex);
    void addTab(const QString& filename, const QString& content, bool isNew = false);
    QString promptForFilename();
    QString buildTooltip(const CounterInfo& counter, bool isDerived) const;
    static QString helpText();

    QTabWidget* tabWidget{nullptr};
    QListWidget* counterList{nullptr};
    QSplitter* splitter{nullptr};
    QToolButton* addButton{nullptr};
    QToolButton* deleteButton{nullptr};
    QToolButton* helpButton{nullptr};
    QPushButton* saveButton{nullptr};
    QPushButton* saveAllButton{nullptr};
    QPushButton* closeButton{nullptr};

    QString builtinText;
    std::vector<CounterInfo> rawCounters;
    std::vector<CounterInfo> derivedCounters;
    QMap<int, QString> tabFilenames; // Maps tab index to filename
    QMap<int, bool> tabModified;     // Tracks if tab content was modified
    bool isLoading{false};           // True during initial tab loading
};
