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

#include "derivedcountereditor.h"

#include <cmath>

namespace
{
QString lastSelectedFilename; // Persists across dialog invocations
}

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTextStream>

DerivedCounterEditor::DerivedCounterEditor(
    const QString& builtinText,
    const std::vector<CounterInfo>& rawCounters,
    const std::vector<CounterInfo>& derivedCounters,
    QWidget* parent
) :
QDialog(parent), builtinText(builtinText), rawCounters(rawCounters), derivedCounters(derivedCounters)
{
    setWindowTitle("Derived Counter Editor");
    setMinimumSize(1000, 600);
    setupUI();
    loadExistingDefinitions();
}

QString DerivedCounterEditor::getDefinitionsDir()
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QString definitionsDir = configDir + "/derived_counters";

    QDir dir(definitionsDir);
    if (!dir.exists()) { dir.mkpath("."); }

    return definitionsDir;
}

std::string DerivedCounterEditor::loadDefinitions()
{
    QString definitionsDir = getDefinitionsDir();
    QDir dir(definitionsDir);
    QStringList filters;
    filters << "*.txt"
            << "*.def";
    QFileInfoList files = dir.entryInfoList(filters, QDir::Files, QDir::Name);

    if (!files.isEmpty())
    {
        QFile file(files.first().absoluteFilePath());
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QTextStream in(&file);
            std::string definitions = in.readAll().toStdString();
            file.close();
            return definitions;
        }
    }
    return {};
}

QString DerivedCounterEditor::getCurrentTabContent() const
{
    int index = tabWidget->currentIndex();
    if (index < 0) return QString();

    auto* editor = qobject_cast<QPlainTextEdit*>(tabWidget->widget(index));
    if (!editor) return QString();

    return editor->toPlainText();
}

void DerivedCounterEditor::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);

    // Create splitter for counter list and tab widget
    splitter = new QSplitter(Qt::Horizontal, this);

    // Left panel: Available counters list
    auto* leftWidget = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    auto* counterLabel = new QLabel("Available Counters:", leftWidget);
    leftLayout->addWidget(counterLabel);

    counterList = new QListWidget(leftWidget);
    counterList->setSelectionMode(QAbstractItemView::SingleSelection);

    // Populate counter list with raw counters section
    if (!rawCounters.empty())
    {
        auto* rawHeader = new QListWidgetItem("── Raw Counters ──");
        rawHeader->setFlags(Qt::NoItemFlags); // Not selectable
        QFont headerFont = rawHeader->font();
        headerFont.setBold(true);
        rawHeader->setFont(headerFont);
        rawHeader->setBackground(QBrush(QColor(200, 200, 200)));
        counterList->addItem(rawHeader);

        for (const auto& counter : rawCounters)
        {
            QString displayText = QString("%1  [%2]").arg(counter.name, counter.shape);
            auto* item = new QListWidgetItem(displayText);
            item->setData(Qt::UserRole, counter.name); // Store name for easy access
            item->setToolTip(buildTooltip(counter, false));
            counterList->addItem(item);
        }
    }

    // Populate counter list with derived counters section
    if (!derivedCounters.empty())
    {
        auto* spacer = new QListWidgetItem("");
        spacer->setFlags(Qt::NoItemFlags);
        spacer->setSizeHint(QSize(0, 12)); // 12px spacing
        counterList->addItem(spacer);

        auto* derivedHeader = new QListWidgetItem("── Derived Counters ──");
        derivedHeader->setFlags(Qt::NoItemFlags); // Not selectable
        QFont headerFont = derivedHeader->font();
        headerFont.setBold(true);
        derivedHeader->setFont(headerFont);
        derivedHeader->setBackground(QBrush(QColor(200, 200, 200)));
        counterList->addItem(derivedHeader);

        for (const auto& counter : derivedCounters)
        {
            QString displayText = QString("%1  [%2]").arg(counter.name, counter.shape);
            auto* item = new QListWidgetItem(displayText);
            item->setData(Qt::UserRole, counter.name); // Store name for easy access
            item->setToolTip(buildTooltip(counter, true));
            counterList->addItem(item);
        }
    }

    leftLayout->addWidget(counterList);
    leftWidget->setMinimumWidth(200);
    leftWidget->setMaximumWidth(350);

    // Right panel: Tab widget with editors
    tabWidget = new QTabWidget(this);
    tabWidget->setTabsClosable(true);
    tabWidget->setMovable(true);

    splitter->addWidget(leftWidget);
    splitter->addWidget(tabWidget);
    splitter->setStretchFactor(0, 1); // Counter list gets less stretch
    splitter->setStretchFactor(1, 3); // Tab widget gets more stretch
    splitter->setSizes({250, 750});

    mainLayout->addWidget(splitter);

    // Button layout
    auto* buttonLayout = new QHBoxLayout();

    // Add "+" button on the left side of the button bar
    addButton = new QToolButton(this);
    addButton->setText("+ New");
    addButton->setToolTip("Add new definition file");
    buttonLayout->addWidget(addButton);

    // Add "X Delete" button next to the add button
    deleteButton = new QToolButton(this);
    deleteButton->setText("X Delete");
    deleteButton->setToolTip("Delete current definition file");
    buttonLayout->addWidget(deleteButton);

    // Add "? Help" button with syntax documentation
    helpButton = new QToolButton(this);
    helpButton->setText("? Help");
    helpButton->setToolTip(helpText());
    buttonLayout->addWidget(helpButton);

    buttonLayout->addStretch();

    saveButton = new QPushButton("Save Current", this);
    saveButton->setToolTip("Save the current tab's definition");
    buttonLayout->addWidget(saveButton);

    saveAllButton = new QPushButton("Save All", this);
    saveAllButton->setToolTip("Save all modified definitions");
    buttonLayout->addWidget(saveAllButton);

    closeButton = new QPushButton("Close", this);
    buttonLayout->addWidget(closeButton);

    mainLayout->addLayout(buttonLayout);

    // Connections
    connect(addButton, &QToolButton::clicked, this, &DerivedCounterEditor::addNewDefinition);
    connect(deleteButton, &QToolButton::clicked, this, &DerivedCounterEditor::deleteCurrentDefinition);
    connect(helpButton, &QToolButton::clicked, this, &DerivedCounterEditor::showHelp);
    connect(saveButton, &QPushButton::clicked, this, &DerivedCounterEditor::saveCurrentDefinition);
    connect(saveAllButton, &QPushButton::clicked, this, &DerivedCounterEditor::saveAllDefinitions);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(tabWidget, &QTabWidget::tabCloseRequested, this, &DerivedCounterEditor::closeTab);
    connect(tabWidget, &QTabWidget::currentChanged, this, &DerivedCounterEditor::onTabChanged);
    connect(tabWidget, &QTabWidget::currentChanged, this, &DerivedCounterEditor::updateDeleteButtonState);
}

void DerivedCounterEditor::loadExistingDefinitions()
{
    isLoading = true;

    QString dir = getDefinitionsDir();
    QDir definitionsDir(dir);

    QStringList filters;
    filters << "*.txt"
            << "*.def";
    QFileInfoList files = definitionsDir.entryInfoList(filters, QDir::Files, QDir::Name);

    for (const QFileInfo& fileInfo : files)
    {
        QFile file(fileInfo.absoluteFilePath());
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QTextStream in(&file);
            QString content = in.readAll();
            file.close();

            addTab(fileInfo.fileName(), content, false);
        }
    }

    // If no files exist, create a default tab with builtin text
    if (tabWidget->count() == 0 && !builtinText.isEmpty()) { addTab("derived_counters.txt", builtinText, true); }

    // Restore the last selected tab if available
    if (!lastSelectedFilename.isEmpty())
    {
        for (int i = 0; i < tabWidget->count(); i++)
        {
            if (tabFilenames[i] == lastSelectedFilename)
            {
                tabWidget->setCurrentIndex(i);
                break;
            }
        }
    }

    isLoading = false;
    updateDeleteButtonState();
}

void DerivedCounterEditor::addTab(const QString& filename, const QString& content, bool isNew)
{
    auto* editor = new QPlainTextEdit(this);
    editor->setPlainText(content);
    editor->setReadOnly(false);

    // Use monospace font
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(10);
    editor->setFont(font);

    // Line numbers and word wrap
    editor->setLineWrapMode(QPlainTextEdit::NoWrap);

    // Ensure the editor can receive input
    editor->setFocusPolicy(Qt::StrongFocus);

    int index = tabWidget->addTab(editor, filename);
    tabFilenames[index] = filename;
    tabModified[index] = isNew;

    if (isNew) tabWidget->setTabText(index, filename + "*");

    // Track modifications - use editor pointer instead of index
    connect(
        editor,
        &QPlainTextEdit::textChanged,
        this,
        [this, editor]()
        {
            int currentIndex = tabWidget->indexOf(editor);
            if (currentIndex >= 0 && !tabModified[currentIndex])
            {
                tabModified[currentIndex] = true;
                QString currentName = tabWidget->tabText(currentIndex);
                if (!currentName.endsWith("*")) tabWidget->setTabText(currentIndex, currentName + "*");
            }
        }
    );

    tabWidget->setCurrentIndex(index);
    editor->setFocus();
}

QString DerivedCounterEditor::promptForFilename()
{
    bool ok;
    QString filename = QInputDialog::getText(
        this,
        "New Definition File",
        "Enter filename (without path, e.g., 'my_counters.txt'):",
        QLineEdit::Normal,
        "derived_counters.txt",
        &ok
    );

    if (!ok || filename.isEmpty()) { return QString(); }

    // Remove any path components the user might have entered
    filename = QFileInfo(filename).fileName();

    // Ensure it has an extension
    if (!filename.contains('.')) { filename += ".txt"; }

    // Check if file already exists
    QString fullPath = getDefinitionsDir() + "/" + filename;
    if (QFile::exists(fullPath))
    {
        QMessageBox::warning(
            this,
            "File Exists",
            QString("A file named '%1' already exists. Please choose a different name.").arg(filename)
        );
        return QString();
    }

    // Check if tab with same name is already open
    for (int i = 0; i < tabWidget->count(); i++)
    {
        QString tabName = tabFilenames[i];
        if (tabName == filename)
        {
            QMessageBox::warning(
                this, "Tab Exists", QString("A tab with filename '%1' is already open.").arg(filename)
            );
            return QString();
        }
    }

    return filename;
}

void DerivedCounterEditor::addNewDefinition()
{
    QString filename = promptForFilename();
    if (filename.isEmpty()) return;

    addTab(filename, builtinText, true);
    updateDeleteButtonState();
}

void DerivedCounterEditor::saveCurrentDefinition()
{
    int index = tabWidget->currentIndex();
    if (index >= 0) saveDefinitionToFile(index);
}

void DerivedCounterEditor::saveAllDefinitions()
{
    for (int i = 0; i < tabWidget->count(); i++)
    {
        if (tabModified[i]) saveDefinitionToFile(i);
    }
}

bool DerivedCounterEditor::saveDefinitionToFile(int tabIndex)
{
    if (tabIndex < 0 || tabIndex >= tabWidget->count()) return false;

    auto* editor = qobject_cast<QPlainTextEdit*>(tabWidget->widget(tabIndex));
    if (!editor) return false;

    QString filename = tabFilenames[tabIndex];
    QString fullPath = getDefinitionsDir() + "/" + filename;

    QFile file(fullPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::critical(
            this, "Save Error", QString("Could not save file: %1\n%2").arg(fullPath, file.errorString())
        );
        return false;
    }

    QTextStream out(&file);
    out << editor->toPlainText();
    file.close();

    tabModified[tabIndex] = false;
    tabWidget->setTabText(tabIndex, filename);

    return true;
}

void DerivedCounterEditor::closeTab(int index)
{
    if (tabModified[index])
    {
        QString filename = tabFilenames[index];
        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            "Unsaved Changes",
            QString("The file '%1' has unsaved changes. Save before closing?").arg(filename),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel
        );

        if (reply == QMessageBox::Save)
        {
            if (!saveDefinitionToFile(index))
            {
                return; // Save failed, don't close
            }
        }
        else if (reply == QMessageBox::Cancel)
        {
            return; // User cancelled, don't close
        }
    }

    // Update maps for tabs after this one
    QMap<int, QString> newFilenames;
    QMap<int, bool> newModified;

    for (auto it = tabFilenames.begin(); it != tabFilenames.end(); ++it)
    {
        if (it.key() < index)
        {
            newFilenames[it.key()] = it.value();
            newModified[it.key()] = tabModified[it.key()];
        }
        else if (it.key() > index)
        {
            newFilenames[it.key() - 1] = it.value();
            newModified[it.key() - 1] = tabModified[it.key()];
        }
    }

    tabFilenames = newFilenames;
    tabModified = newModified;

    tabWidget->removeTab(index);
    updateDeleteButtonState();
}

QString DerivedCounterEditor::formatValue(float value)
{
    float absVal = std::abs(value);

    // Special case for exact zero
    if (value == 0.0f) return "0";

    // Within [0.1, 1000] range - use fixed precision based on magnitude
    if (absVal >= 0.1f && absVal < 1000.0f)
    {
        // 1% precision means:
        // - values >= 100: 0 decimal places
        // - values >= 10: 1 decimal place
        // - values >= 1: 2 decimal places
        // - values < 1: 2 decimal places
        int decimals;
        if (absVal >= 100.0f)
            decimals = 0;
        else if (absVal >= 10.0f)
            decimals = 1;
        else
            decimals = 2;

        return QString::number(value, 'f', decimals);
    }

    // Outside range - use scientific notation with 2 decimal places
    return QString::number(value, 'e', 2);
}

QString DerivedCounterEditor::buildTooltip(const CounterInfo& counter, bool isDerived) const
{
    QString tooltip;
    QString prefix = isDerived ? "Derived" : "Counter";
    tooltip = QString("%1: %2\nShape: %3").arg(prefix, counter.name, counter.shape);

    // Add first few values if available
    if (!counter.firstValues.empty())
    {
        QStringList valueStrs;
        size_t count = std::min(counter.firstValues.size(), static_cast<size_t>(kMaxTooltipValues));
        for (size_t i = 0; i < count; i++) valueStrs << formatValue(counter.firstValues[i]);

        if (counter.firstValues.size() > kMaxTooltipValues) valueStrs << "...";

        tooltip += QString("\nValues: [%1]").arg(valueStrs.join(", "));
    }

    return tooltip;
}

void DerivedCounterEditor::onTabChanged(int index)
{
    // Remember the selected tab filename for next time the editor is opened
    // Skip during initial loading to preserve the previously saved selection
    if (!isLoading && index >= 0 && tabFilenames.contains(index)) lastSelectedFilename = tabFilenames[index];
}

void DerivedCounterEditor::updateDeleteButtonState()
{
    // Disable delete button if there's only one tab
    deleteButton->setEnabled(tabWidget->count() > 1);
}

QString DerivedCounterEditor::helpText()
{
    return QStringLiteral(
        "<h3>Derived Counter Syntax</h3>"
        "<p><b>Basic Operations:</b></p>"
        "<ul>"
        "<li><code>+</code>, <code>-</code>, <code>*</code>, <code>/</code> — Arithmetic operators</li>"
        "<li><code>()</code> — Grouping</li>"
        "</ul>"
        "<p><b>Indexing Functions:</b></p>"
        "<ul>"
        "<li><code>select[counter, index, axis=TIME]</code> — Select single index along axis</li>"
        "<li><code>select[counter, start:stop, axis=TIME]</code> — Select range [start, stop) with step 1</li>"
        "<li><code>select[counter, start:stop:step, axis=TIME]</code> — Select range with custom step</li>"
        "<li><code>remove[counter, index, axis=TIME]</code> — Remove single index (supports negative: -1 = last)</li>"
        "<li><code>delta[counter, axis=TIME]</code> — Difference to previous element</li>"
        "</ul>"
        "<p><b>Reduction Functions:</b></p>"
        "<ul>"
        "<li><code>sum[counter, axis=TIME]</code> — Sum along axis</li>"
        "<li><code>mean[counter, axis=TIME]</code> — Mean along axis</li>"
        "<li><code>max[counter, axis=TIME]</code> — Maximum along axis</li>"
        "<li><code>min[counter, axis=TIME]</code> — Minimum along axis</li>"
        "</ul>"
        "<p><b>Generator Functions:</b></p>"
        "<ul>"
        "<li><code>linear(size, axis=TIME)</code> — Generate linear sequence 0, 1, 2, ...</li>"
        "</ul>"
        "<p><b>Axes:</b> <code>TIME</code> (default), <code>XCC</code>, <code>SE</code>, <code>CU</code></p>"
        "<p><b>Example:</b><br/><code>delta[SQ_WAVES, axis=TIME] / delta[GRBM_GUI_ACTIVE]</code></p>"
    );
}

void DerivedCounterEditor::showHelp()
{
    QMessageBox helpBox(this);
    helpBox.setWindowTitle("Derived Counter Syntax Help");
    helpBox.setTextFormat(Qt::RichText);
    helpBox.setText(helpText());
    helpBox.exec();
}

void DerivedCounterEditor::deleteCurrentDefinition()
{
    int index = tabWidget->currentIndex();
    if (index < 0) return;

    // Don't allow deletion if it's the last tab
    if (tabWidget->count() <= 1)
    {
        QMessageBox::warning(
            this, "Cannot Delete", "Cannot delete the last definition file. At least one file must remain."
        );
        return;
    }

    QString filename = tabFilenames[index];
    QString fullPath = getDefinitionsDir() + "/" + filename;

    // Confirm deletion
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "Delete Definition",
        QString("Are you sure you want to delete '%1'?\n\nThis will permanently delete the file from disk.")
            .arg(filename),
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply != QMessageBox::Yes) return;

    // Delete the file if it exists on disk
    QFile file(fullPath);
    if (file.exists())
    {
        if (!file.remove())
        {
            QMessageBox::critical(
                this, "Delete Error", QString("Could not delete file: %1\n%2").arg(fullPath, file.errorString())
            );
            return;
        }
    }

    // Update maps for tabs after this one
    QMap<int, QString> newFilenames;
    QMap<int, bool> newModified;

    for (auto it = tabFilenames.begin(); it != tabFilenames.end(); ++it)
    {
        if (it.key() < index)
        {
            newFilenames[it.key()] = it.value();
            newModified[it.key()] = tabModified[it.key()];
        }
        else if (it.key() > index)
        {
            newFilenames[it.key() - 1] = it.value();
            newModified[it.key() - 1] = tabModified[it.key()];
        }
    }

    tabFilenames = newFilenames;
    tabModified = newModified;

    tabWidget->removeTab(index);
    updateDeleteButtonState();
}
