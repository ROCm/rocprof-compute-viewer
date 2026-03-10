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

#include "mainwindow.h"
#include <stdlib.h>
#include <sys/stat.h>
#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFontDatabase>
#include <QMessageBox>
#include <QPainterPath>
#include <QScrollArea>
#include <QSpinBox>
#include <QTextStream>
#include <algorithm>
#include <cstddef>
#include <fstream>
#include <future>
#include <iostream>
#include <utility>
#include <vector>
#include "./ui_mainwindow.h"
#include "button/historyentry.h"
#include "button/jsonselector.h"
#include "code/labelminimap.h"
#include "code/qcodelist.h"
#include "code/sourcefile.h"
#include "collection/derivedcountereditor.h"
#include "collection/flamegraphwidget.h"
#include "collection/latencyanalysisdialog.h"
#include "collection/license.h"
#include "collection/options.h"
#include "config/appconfig.h"
#include "config/config.hpp"
#include "data/shaderdata.h"
#include "data/wavedata.h"
#include "graphics/canvas.h"
#include "graphics/hotspot.h"
#include "graphics/specialized_plots.h"
#include "summary/summaryview.h"
#include "util/jsonrequest.hpp"
#include "util/version.h"
#include "wave/othersimd.h"
#include "wave/scroll.h"
#include "wave/waveglobal.h"
#include "wave/waveview.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#    include "util/accordionwidget.h"
#endif

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#    include <QGuiApplication>
#    include <QScreen>
#else
#    include <QDesktopWidget>
#endif

inline bool FileExists(const std::string& name)
{
    if (name.find("http://") != std::string::npos)
    {
        try
        {
            // TODO: Only check if file exists on the server.
            JsonRequest try_request(name);
        }
        catch (std::exception& e)
        {
            return false;
        }
        return true;
    }
    struct stat buffer;
    return stat(name.c_str(), &buffer) == 0;
}

std::vector<QColor> MainWindow::dispatchcolors = {
    {0,   255, 0  },
    {0,   0,   255},
    {255, 0,   0  },

    {255, 160, 0  },
    {255, 0,   160},
    {0,   160, 255},
    {160, 0,   255},
    {0,   255, 160},
    {160, 255, 0  },

    {255, 16,  255},
    {255, 255, 16 },
    {16,  255, 255},
};

MainWindow* MainWindow::window = nullptr;

QString MainWindow::default_font{};

MainWindow::MainWindow(std::string uidir) : QMainWindow(nullptr), ui(new Ui::MainWindow)
{
    MainWindow::window = this;
    ui->setupUi(this);

    default_font = []()
    {
        for (const char* desired : {"Consolas", "Arial", "Times"})
            for (auto& font : QFontDatabase().families())
                if (font.toStdString().find(desired) != std::string::npos) return font;
        return QString("");
    }();

    // Load settings from configuration
    loadConfigSettings();

    // Setup configuration connections
    setupConfigConnections();

    this->shaderSel = ui->SESelLay;
    this->simdSel = ui->SMSelLay;
    this->wslSel = ui->WSLSelLay;
    this->widSel = ui->WIDSelLay;

    this->setAutoFillBackground(true);

    ui->splitter->setSizes(QList<int>({height() / 3, 2 * height() / 3}));
    ui->splitter_2->setSizes(QList<int>({height() / 6, height() / 2, 2 * height() / 6}));

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    accordion = new AccordionWidget();
    ui->splitter->replaceWidget(0, accordion);
#endif

    this->history_table = ui->history_table;
    this->history_table->verticalHeader()->setDefaultSectionSize(16);

    // --- Label Minimap ---
    this->label_minimap = new LabelMinimap();
    this->label_minimap->setMinimumHeight(100);
    this->label_minimap->setMaximumHeight(200);
    if (ui->verticalLayout_5)
    {
        int historyIndex = ui->verticalLayout_5->indexOf(ui->label_6);
        if (historyIndex >= 0)
            ui->verticalLayout_5->insertWidget(historyIndex, this->label_minimap);
        else
            ui->verticalLayout_5->addWidget(this->label_minimap);
    }

    this->graph_info_table = ui->graph_info_table;
    this->graph_info_table->setColumnCount(2);
    this->graph_info_table->setRowCount(1);
    this->graph_info_table->setHorizontalHeaderLabels(QList<QString>({"Enable Counter", "Value"}));
    this->graph_info_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    this->graph_info_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    this->graph_info_table->setColumnWidth(1, 60);
    this->graph_info_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    this->graph_info_table->setSelectionMode(QAbstractItemView::NoSelection);
    this->graph_info_table->setSortingEnabled(false);
    this->graph_info_table->horizontalHeader()->setSectionsClickable(false);
    this->graph_info_table->verticalHeader()->setSectionsClickable(false);
    this->graph_info_table->setFocusPolicy(Qt::NoFocus);

    ui->occ_info_table->setColumnCount(3);
    ui->occ_info_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->occ_info_table->setSelectionMode(QAbstractItemView::NoSelection);

    this->hotspot_tab = ui->hotspot_tab;

    // --- Code View ---
    {
        QSplitter* code_splitter = new QSplitter(Qt::Horizontal);

        auto* lay = new QHBox();
        lay->addWidget(code_splitter);
        ui->code_tab_main->setLayout(lay);

        auto* code_wid = new QWidget();
        auto* code_layout = new QHBox();
        code_wid->setLayout(code_layout);

        QVBox* box = new QVBox();
        this->code_scrollarea = new QScrollArea();
        this->code_scrollarea->setWidgetResizable(true);
        this->code_scrollarea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        code_scrollarea->setLayout(box);

        this->code_contents = new QCodelist();
        box->addWidget(this->code_contents);
        this->code_scrollarea->setWidget(this->code_contents);

        code_layout->addWidget(code_scrollarea);
        code_layout->addWidget(code_contents->scrollbar);

        this->source_filetab = new SourceFileTab();
        code_splitter->addWidget(code_wid);
        code_splitter->addWidget(source_filetab);
    }

    // --- CU Waves View ---
    this->cuwaves_v_scrollarea = new QScrollArea(ui->cuwaves_tab);
    this->cuwaves_v_scrollarea->setWidgetResizable(true);
    this->cuwaves_v_scrollarea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->cuwaves_tab->setLayout(new QVBox());
    ui->cuwaves_tab->layout()->addWidget(this->cuwaves_v_scrollarea);

    auto view = std::make_shared<ScrollValue>();
    this->cuwaves_h_scrollarea = new QCustomScroll(view);
    ui->cuwaves_tab->layout()->addWidget(cuwaves_h_scrollarea);
    cuwaves_content = new QWaveSlots(cuwaves_h_scrollarea);
    cuwaves_v_scrollarea->setWidget(cuwaves_content);
    connect(
        cuwaves_v_scrollarea->verticalScrollBar(),
        &QScrollBar::actionTriggered,
        cuwaves_h_scrollarea,
        &QCustomScroll::onScroll
    );

    // ---- Utilization View ----
    ui->utilization_tab->setLayout(new QVBox());
    utilization_v_scrollarea = new QScrollArea(this);
    utilization_v_scrollarea->setWidgetResizable(true);
    utilization_v_scrollarea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->utilization_tab->layout()->addWidget(utilization_v_scrollarea);
    utilization_h_scrollarea = new QCustomScroll(view);
    ui->utilization_tab->layout()->addWidget(utilization_h_scrollarea);
    ui->utilization_tab->setAutoFillBackground(true);

    utilization_content = new QUtilization(utilization_h_scrollarea);
    utilization_v_scrollarea->setWidget(utilization_content);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // In Qt6, we use the accordion widget and need to reparent the scroll areas
    // to container widgets that will be added to the accordion sections
    QWidget* compute_unit_widget = new QWidget(this);
    compute_unit_widget->setLayout(new QVBox());
    compute_unit_widget->layout()->addWidget(cuwaves_v_scrollarea);
    compute_unit_widget->layout()->addWidget(cuwaves_h_scrollarea);

    QWidget* ulitization_widget = new QWidget(this);
    ulitization_widget->setLayout(new QVBox());
    ulitization_widget->layout()->addWidget(utilization_v_scrollarea);
    ulitization_widget->layout()->addWidget(utilization_h_scrollarea);

    accordion->addSection("Counters", nullptr);
    accordion->addSection("Wave States", nullptr);
    accordion->addSection("Hotspot", nullptr);
    accordion->addSection("Occupancy", nullptr);
    accordion->addSection("Kernel Dispatch", nullptr);
    accordion->addSection("Compute Unit", compute_unit_widget, true);
    accordion->addSection("Utilization", ulitization_widget);

    connect(cuwaves_h_scrollarea, &QCustomScroll::valueupdated, accordion, &AccordionWidget::notifyPlotsUpdate);
#endif

    this->global_view_tab = ui->globalview_tab;

    summary_view = new SummaryView(this);
    ui->stats_view->layout()->setSpacing(0);
    ui->stats_view->layout()->setContentsMargins(0, 0, 0, 0);
    ui->stats_view->layout()->addWidget(summary_view);

    // --- MENU ---
    if (uidir != "")
    {
        ui->ConfigNameEdit->setText(uidir.c_str());
        ResetSelector();
    }

    connect(ui->ConfigNameEdit, &QLineEdit::editingFinished, this, &MainWindow::ResetSelector);
    connect(ui->lod_checkBox, &QCheckBox::stateChanged, this, &MainWindow::UpdateGraphAutoLod);

    connect(ui->actionJsons_folder, &QAction::triggered, this, &MainWindow::SetJsonsFolder);
    connect(ui->actionHotOptions, &QAction::triggered, this, &MainWindow::OpenOptionsDialog);
    connect(ui->actionDerived_counters, &QAction::triggered, this, &MainWindow::OpenDerivedCounterEditor);
    connect(
        ui->actionMemoryLatency,
        &QAction::triggered,
        this,
        [this]()
        {
            if (ui_dir.empty())
            {
                QMessageBox::warning(this, "Error", "No UI directory loaded");
                return;
            }
            LatencyAnalysisDialog dialog(ui_dir, this);
            dialog.exec();
        }
    );

    connect(ui->waveview_spin, qOverload<int>(&QSpinBox::valueChanged), this, &MainWindow::SetWaveViewMipmap);
    connect(ui->global_spin, qOverload<int>(&QSpinBox::valueChanged), this, &MainWindow::SetGlobalViewMipmap);

    ui->iteration_edit->setValidator(new QIntValidator(this));
    connect(ui->iteration_edit, &QLineEdit::editingFinished, this, &MainWindow::SetIterationCallback);

    ui->wview_range_min->setValidator(new QIntValidator(this));
    ui->wview_range_max->setValidator(new QIntValidator(this));
    connect(ui->wview_range_min, &QLineEdit::editingFinished, this, &MainWindow::UpdateWaveViewRange);
    connect(ui->wview_range_max, &QLineEdit::editingFinished, this, &MainWindow::UpdateWaveViewRange);

    connect(ui->search_edit, &QLineEdit::editingFinished, this, &MainWindow::NextSearch);
    connect(ui->next_inst, &QPushButton::clicked, this, &MainWindow::NextSearch);
    connect(ui->prev_inst, &QPushButton::clicked, this, &MainWindow::PrevSearch);

    ui->source_hotspot_size_edit->setValidator(new QIntValidator(this));
    connect(ui->source_hotspot_size_edit, &QLineEdit::editingFinished, this, &MainWindow::SourceHotspotSizeEdited);

    connect(ui->display_line_number, &QCheckBox::stateChanged, this, &MainWindow::ToggleDisplayLineNumber);

    connect(ui->scale_edit, &QCheckBox::stateChanged, this, &MainWindow::setScaling);

    ui->fontedit->setValidator(new QIntValidator(this));
    ui->fontedit->setText(std::to_string(font()).c_str());
    connect(ui->fontedit, &QLineEdit::editingFinished, this, &MainWindow::updateFont);

    connect(
        ui->dark_theme_box,
        &QCheckBox::stateChanged,
        [this](int box)
        {
            WindowColors::setDark(box);
            this->lastPath = "";
            this->ResetSelector();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            if (accordion) accordion->updateButtonStyles();
#endif
            this->update();
        }
    );

    connect(ui->actionAbout_QT, &QAction::triggered, this, [this]() { QMessageBox::aboutQt(this, "About QT"); });
    connect(
        ui->actionLicense,
        &QAction::triggered,
        this,
        []()
        {
            LICENSE license;
            license.exec();
        }
    );
    connect(
        ui->actionVersion,
        &QAction::triggered,
        this,
        [this]()
        {
            QString versionText = QString("ROCprof Compute Viewer\nVersion %1.%2.%3")
                                      .arg(Version::Get().viewer_major)
                                      .arg(Version::Get().viewer_minor)
                                      .arg(Version::Get().viewer_rev);
            QMessageBox::information(this, "Version", versionText);
        }
    );
}

int& MainWindow::font()
{
    static int font = 9;
    return font;
}

void MainWindow::updateFont()
{
    if (auto newFontValue = parseLineEditInt(ui->fontedit)) { font() = std::clamp(*newFontValue, 5, 19); }

    update();
    updateGeometry();

    if (ui->tabWidget)
    {
        int currentIndex = ui->tabWidget->currentIndex();
        ui->tabWidget->setCurrentIndex((!currentIndex ? 1 : currentIndex - 1));
        ui->tabWidget->setCurrentIndex(currentIndex);
    }

    if (ui->tabWidget_2)
    {
        int currentIndex = ui->tabWidget_2->currentIndex();
        ui->tabWidget_2->setCurrentIndex((!currentIndex ? 1 : currentIndex - 1));
        ui->tabWidget_2->setCurrentIndex(currentIndex);
    }
}

double MainWindow::_paint_scale = 1.0;
int MainWindow::_scaling_var = 1;

void MainWindow::getScaling(QPainter& painter)
{
    static bool capture = [&]()
    {
        _paint_scale = 1.0 / painter.device()->devicePixelRatio();
        return true;
    }();
    painter.scale(getScaling(), getScaling());
}

double MainWindow::getScaling() { return _scaling_var ? _paint_scale : 1.0; }

void MainWindow::setScaling(int scale)
{
    _scaling_var = scale;
    ui->code_tab_main->setVisible(false);
    ui->code_tab_main->setVisible(true);
}

void MainWindow::SourceHotspotSizeEdited()
{
    if (auto hotspotWidth = parseLineEditInt(ui->source_hotspot_size_edit))
        HorizontalHotspot::SetHistogramWidth(*hotspotWidth);

    if (!source_filetab) return;

    // Hack to force update
    source_filetab->setVisible(false);
    source_filetab->setVisible(true);
}

void MainWindow::ToggleDisplayLineNumber(int display)
{
    SourceLine::bDisplayLineNumber = display != 0;
    if (!source_filetab) return;

    // Hack to force update
    source_filetab->setVisible(false);
    source_filetab->setVisible(true);
}

std::string MainWindow::GetDisplayDir() { return QDir::cleanPath(ui->ConfigNameEdit->displayText()).toStdString(); }
std::string MainWindow::GetUIDir()
{
    QASSERT(window, "");
    return window->ui_dir;
}

void MainWindow::UpdateWaveViewRange()
{
    auto vmin = parseLineEditInt64(ui->wview_range_min);
    auto vmax = parseLineEditInt64(ui->wview_range_max);
    QWARNING(vmin && vmax, "Could not set range values.", return );

    QCustomScroll::clock_cutoff_start = *vmin;
    QCustomScroll::clock_cutoff_end = std::max(*vmax, *vmin + int64_t(128));

    if (force_gather || *vmin < current_loaded_clk_start || *vmax > current_loaded_clk_end) GatherWaves();

    cuwaves_h_scrollarea->updatebar(true);
    utilization_h_scrollarea->updatebar(true);

    if (source_filetab) source_filetab->resetLatency();
    if (WaveInstance::main_wave && code_contents) code_contents->Populate(WaveInstance::main_wave->code);
    if (flameGraph) flameGraph->rebuild();
}

void MainWindow::SetMainWave(int se, int simd, int sl, int wid)
{
    constexpr int64_t WAVE_END_ROOM = 20000;

    force_gather = current_wave_coord_se != se || WaveInstance::main_wave == nullptr;

    current_wave_coord_se = se;
    current_wave_coord_sm = simd;
    current_wave_coord_sl = sl;

    std::stringstream wave_name;
    wave_name << GetUIDir() << "se" << se << "_sm" << simd << "_sl" << sl << "_wv" << wid << ".json";

    auto main_wave = WaveInstance::Get(wave_name.str());
    WaveInstance::main_wave = main_wave;

    QWARNING(main_wave && code_contents && code_contents->connector, "invalid code_contents", return );

    auto thread_wait = std::async(
        std::launch::async,
        [this, main_wave]() { this->code_contents->connector->buildWaitConnections(main_wave->waitcnt); }
    );
    auto thread_branch = std::async(
        std::launch::async,
        [this, main_wave]() mutable
        { this->code_contents->connector->buildBranchConnections(main_wave->get_branch_targets()); }
    );

    ui->wview_range_min->setText(std::to_string(main_wave->wave_begin).c_str());
    ui->wview_range_max->setText(std::to_string(main_wave->wave_end + WAVE_END_ROOM).c_str());

    UpdateWaveViewRange();
    force_gather = false;

    if (thread_wait.valid()) thread_wait.get();
    if (thread_branch.valid()) thread_branch.get();

    QTimer* timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(1);
    QObject::connect(
        timer,
        &QTimer::timeout,
        this,
        [this]()
        {
            if (auto* bar = this->code_scrollarea->horizontalScrollBar()) bar->setValue(bar->maximum());
        }
    );
    timer->start();

    // Print code idle/stall/wait/exec
    int64_t idle = 0;
    int64_t stall = 0;
    int64_t exec = 0;

    if (WaveInstance::main_wave)
        for (auto& code : WaveInstance::main_wave->code)
            if (code.line)
            {
                idle += code.line->idle_sum;
                stall += code.line->stall_sum;
                exec += code.line->latency_sum - code.line->stall_sum;
            }

    auto total = idle + stall + exec;

    // set colors for pie chart before setting data (as legend table takes colors from pie chart)
    auto& colors = Config::StateColors();
    if (colors.size() < 4) { QWARNING(false, "Not enough colors for pie chart", ); }
    else
    {
        QColor stall_color = Color(colors.at(3).qcolor) * 0.35f + Color(colors.at(4).qcolor) * 0.65f;

        // Idle, Stall/Wait, Issue
        summary_view->setPieChartColors({
            {colors.at(1).qcolor, stall_color, colors.at(2).qcolor}
        });
    }

    summary_view->clearPieChartData();
    summary_view->setPieChartData(
        {
            {"Idle",       100 * idle / float(total) },
            {"Stall/Wait", 100 * stall / float(total)},
            {"Issue",      100 * exec / float(total) }  // Rename Exec as Issue
    },
        "Activity Distribution"
    );
}

void MainWindow::OpenOptionsDialog()
{
    OptionsDialogH dialog;
    dialog.exec();
}

void MainWindow::OpenDerivedCounterEditor()
{
    if (!counters_plot)
    {
        QMessageBox::warning(
            this,
            "No Counters Loaded",
            "Cannot open Derived Counter Editor: No counters are currently loaded.\n"
            "Please load a trace with counter data first."
        );
        return;
    }

    // Gather available counter information
    std::vector<DerivedCounterEditor::CounterInfo> rawCounterList;
    std::vector<DerivedCounterEditor::CounterInfo> derivedCounterList;
    auto derivedManager = counters_plot->getDerivedManager();
    if (derivedManager)
    {
        auto& ctx = derivedManager->context();
        // Raw counters
        for (const auto& name : ctx.rawCounterNames())
        {
            try
            {
                auto counter = ctx.getCounter(name);
                auto shape = counter->shape();
                QString shapeStr = QString("%1x%2x%3x%4")
                                       .arg(shape.getXCC())
                                       .arg(shape.getSE())
                                       .arg(shape.getCU())
                                       .arg(shape.getSamples());
                DerivedCounterEditor::CounterInfo info;
                info.name = QString::fromStdString(name);
                info.shape = shapeStr;
                // Store first few values for tooltip
                const auto& data = counter->data();
                size_t valuesToStore = std::min(data.size(), DerivedCounterEditor::kMaxTooltipValues);
                for (size_t i = 0; i < valuesToStore; i++) info.firstValues.push_back(data[i]);
                rawCounterList.push_back(std::move(info));
            }
            catch (const std::exception& e)
            {
                // Skip counters that fail to load
                rawCounterList.push_back({QString::fromStdString(name), "error", {}});
            }
        }
        // Derived counters
        for (const auto& name : ctx.derivedCounterNames())
        {
            // Skip internal counters starting with _
            if (!name.empty() && name[0] == '_') continue;
            try
            {
                auto counter = ctx.getCounter(name);
                auto shape = counter->shape();
                QString shapeStr = QString("%1x%2x%3x%4")
                                       .arg(shape.getXCC())
                                       .arg(shape.getSE())
                                       .arg(shape.getCU())
                                       .arg(shape.getSamples());
                DerivedCounterEditor::CounterInfo info;
                info.name = QString::fromStdString(name);
                info.shape = shapeStr;
                // Store first few values for tooltip
                const auto& data = counter->data();
                size_t valuesToStore = std::min(data.size(), DerivedCounterEditor::kMaxTooltipValues);
                for (size_t i = 0; i < valuesToStore; i++) info.firstValues.push_back(data[i]);
                derivedCounterList.push_back(std::move(info));
            }
            catch (const std::exception& e)
            {
                // Add with error indicator if evaluation fails
                derivedCounterList.push_back({QString::fromStdString(name), QString("error: %1").arg(e.what()), {}});
            }
        }
    }

    QString builtinText = QString::fromStdString(counters_plot->getBuiltin());
    DerivedCounterEditor editor(builtinText, rawCounterList, derivedCounterList, this);
    editor.exec();

    // After dialog closes, update the plot with the active tab's definitions
    QString newDefinitions = editor.getCurrentTabContent();
    if (!newDefinitions.isEmpty())
    {
        counters_plot->UpdateDerivedCounters(newDefinitions.toStdString(), false);
        UpdateCountersPlotSelection();
    }
}

void MainWindow::SetJsonsFolder()
{
    std::string jsons_dir = QFileDialog::getExistingDirectory(this, "Select Dir", ui_dir.c_str()).toStdString();
    ui->ConfigNameEdit->setText(jsons_dir.c_str());
    ResetSelector();
}

void recursive_load(
    const std::string& path, nlohmann::json& data, std::vector<std::pair<std::string, std::string>>& widgets
)
{
    for (auto& [key, value] : data.items())
    {
        std::string newpath = path;
        if (!path.empty() && path.back() != '/') newpath += '/';
        newpath += key;

        if (value.is_string()) { widgets.push_back({newpath, std::string(value)}); }
        else { recursive_load(newpath, value, widgets); }
    }
}

void MainWindow::LoadSourceFiles()
{
    QWARNING(source_filetab, "No source file tab", return );

    if (ui->fileExplorer_tab->layout())
    {
        delete ui->fileExplorer_tab->layout();
        flameGraph = nullptr;
    }

    source_filetab->clear();

    try
    {
        JsonRequest req(GetUIDir() + "/snapshots.json", false);

        if (!req.bValid) throw std::exception();

        std::vector<std::pair<std::string, std::string>> widgets{};
        recursive_load("", req.data, widgets);

        for (auto& [profilingPath, uiFilename] : widgets)
        {
            std::string fullUiPath = ui_dir + std::string(uiFilename);
            source_filetab->addFile(profilingPath, fullUiPath);
        }

        loadJsonFileTree(req.data.dump().c_str());
        ui->tabWidget_2->setTabEnabled(3, true);
    }
    catch (...)
    {
        ui->tabWidget_2->setTabEnabled(3, false);
    }

    // Only show raw source ref if no snapshot is found
    bool hassource = source_filetab->files.size();
    code_contents->elements.at(ASMCodeline::Element::ESOURCEREF)->setVisible(!hassource);
    source_filetab->setVisible(hassource);
}

void MainWindow::ResetSelector()
{
    const QString displayDir = QString::fromStdString(GetDisplayDir());
    if (displayDir.isEmpty()) return;

    QDir dir(displayDir);
    const std::string newpath = dir.filePath("filenames.json").toStdString();
    if (newpath == lastPath) return;

    std::shared_ptr<JsonRequest> request{nullptr};
    try
    {
        request = std::make_shared<JsonRequest>(newpath);
        if (!request || !request->bValid) throw std::exception();
    }
    catch (std::exception& e)
    {
        ui->ConfigNameEdit->setText(QString::fromStdString(ui_dir));
        QMessageBox::warning(
            this, "Path Not Found", QString("Invalid or inaccessible path: %1").arg(QString::fromStdString(newpath))
        );
        QWARNING(false, "Invalid filename " << newpath, return );
    }
    ui_dir = dir.path().toStdString();
    if (!ui_dir.empty() && ui_dir.back() != '/') ui_dir.push_back('/');
    auto other_simd_files = ParseOtherSimdFilenames(request->data["other_simd_filenames"], ui_dir);
    if (utilization_content) utilization_content->SetOtherSimdSources(std::move(other_simd_files));
    lastPath = newpath;

    current_loaded_clk_start = -1;
    current_loaded_clk_end = -1;

    ASMCodeline::Clear();
    Canvas::max_memory_latency = 0.0;
    LoadSourceFiles();
    cuwaves_content->Clear();
    utilization_content->Clear();
    if (hotspot_view) hotspot_view->setVisible(false);
    updateFont(); // Fixme: This is being called to force redraw in case the incoming trace is empty

    HorizontalHotspot::is_sqtt_enabled = true;
    HorizontalHotspot::is_pcs_enabled = false;
    if (label_minimap) label_minimap->Clear();
    try
    {
        HorizontalHotspot::is_sqtt_enabled = request->data["thread_trace"];
        HorizontalHotspot::is_pcs_enabled = request->data["pc_sampling"];
        if (HorizontalHotspot::is_pcs_enabled)
        {
            auto code = CodeData::LoadCode(ui_dir + "code.json");
            if (!code.empty()) code_contents->Populate(code);
            if (flameGraph) flameGraph->rebuild();
        }
    }
    catch (std::exception&)
    {}

    // Load shaderdata early, before SESelector triggers GatherWaves
    if (shaderdata_manager)
    {
        delete shaderdata_manager;
        shaderdata_manager = nullptr;
    }
    try
    {
        if (request->data.contains("shaderdata_filenames"))
        {
            shaderdata_manager = new ShaderDataManager();
            shaderdata_manager->Load(request->data["shaderdata_filenames"], GetUIDir());
        }
    }
    catch (std::exception& e)
    {
        std::cout << "Warning: Failed to load shaderdata: " << e.what() << std::endl;
    }

    if (this->seSelector) delete this->seSelector;
    this->seSelector = nullptr;
    this->seSelector = new SESelector(*request);

    if (history_table) history_table->setRowCount(0);

    counter_values_tableitem.clear();
    occupancy_values_tableitem.clear();

    if (graph_info_table) graph_info_table->setRowCount(1);
    if (ui->occ_info_table) ui->occ_info_table->setRowCount(0);

    try
    {
        CreateWavesPlot();
        CreateOccupancyPlot(false);
        CreateOccupancyPlot(true);
        CreateCountersPlot();

        this->CreateGlobalView();
    }
    catch (std::exception& e)
    {
        QWARNING(false, "Unable to create plots:" << e.what(), return );
    }
}

MainWindow::~MainWindow()
{
    if (code_scrollarea) delete code_scrollarea;

    if (cuwaves_content) delete cuwaves_content;
    if (cuwaves_v_scrollarea) delete cuwaves_v_scrollarea;

    if (waves_plot) delete waves_plot;
    if (counters_plot) delete counters_plot;

    if (counters_plot_layout) delete counters_plot_layout;
    if (waves_plot_layout) delete waves_plot_layout;

    if (dispatch_plot) delete dispatch_plot;
    if (occupancy_plot) delete occupancy_plot;
    if (dispatch_plot_layout) delete dispatch_plot_layout;
    if (occupancy_plot_layout) delete occupancy_plot_layout;

    if (shaderdata_manager) delete shaderdata_manager;
    if (seSelector) delete seSelector;
    if (widSelector) delete widSelector;
    if (ui) delete ui;

    WaveInstance::InvalidadeCache();
    MainWindow::window = nullptr;
}

void MainWindow::CreateCountersPlot()
{
    QWARNING(summary_view && ui->tabWidget_2, "No summary view!", return );

    summary_view->clearTableData();
    summary_view->clearBarChartData();
    ui->tabWidget_2->setTabEnabled(2, false);

    // Load counter names from filenames.json
    auto perfcounter_names = std::vector<std::string>{};
    try
    {
        JsonRequest file(MainWindow::GetUIDir() + "filenames.json", false);
        perfcounter_names = file.data["counter_names"];
    }
    catch (...)
    {}

    for (auto& name : perfcounter_names)
        if (name.size() > 5 && name.find("SQ_") == 0) name = name.substr(3);

    bool load_perf_counters = !perfcounter_names.empty(); // && perfcounter_names.size() <= peak_rates.size();

    auto* traceplot = new TraceCounterPlotView(this);
    this->counters_plot = traceplot;

    if (load_perf_counters)
    {
        int max_se = 0;
        if (auto* dispatch_data = dynamic_cast<DispatchPlotView*>(this->dispatch_plot))
        {
            const auto& list = dispatch_data->seList();
            auto it = std::max_element(list.begin(), list.end());
            if (it != list.end()) max_se = 1 + *it;
        }

        for (int se_num = 0; se_num < max_se; se_num++) traceplot->LoadCounterData(GetUIDir(), se_num);
    }

    if (this->counters_plot_layout) delete this->counters_plot_layout;

    this->counters_plot_layout = new QBox();
    ui->wv_counters_tab->setLayout(this->counters_plot_layout);
    this->counters_plot_layout->addWidget(this->counters_plot);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    accordion->replaceContentByTitle("Counters", this->counters_plot);
#endif

    if (perfcounter_names.empty()) return;

    // Load user-defined derived counters from file
    std::string derived_definitions = DerivedCounterEditor::loadDefinitions();

    this->counters_plot->setAutoLod(ui->lod_checkBox->isChecked());
    this->counters_plot->setGeometry(0, 0, 300, this->counters_plot->size().width());
    this->counters_plot->UpdateDataSelection(perfcounter_names, ~0ULL, ~0ULL, derived_definitions);
    UpdateCountersPlotSelection();

    auto peak_rates = counters_plot->GetPeakRates();
    auto accumulated = counters_plot->GetAvgRates();

    if (peak_rates.empty() || accumulated.shape().totalSize() <= 1) return;
    if (peak_rates.size() != accumulated.shape().getSamples()) peak_rates.resize(accumulated.shape().getSamples());

    if (perfcounter_names.size() != peak_rates.size()) perfcounter_names.resize(peak_rates.size());

    auto acc_index = [&accumulated](int index) -> double
    {
        double acc = 0;
        for (int se = 0; se < accumulated.shape().getSE(); se++)
            for (int cu = 0; cu < accumulated.shape().getCU(); cu++) acc += accumulated.at(0, se, cu, index);

        return acc;
    };

    ui->tabWidget_2->setTabEnabled(2, true);

    std::map<std::string, size_t> util_names{};
    QList<std::tuple<QString, float, QColor>> bar_chart_data;

    int busy_cu_index = -1;

    const auto& tokenColors = Config::TokenColors();
    // Create a lambda function to get the color by name
    auto getColorByName = [&](const std::string& lookupName) -> QColor
    {
        QColor color = tokenColors[0].qcolor; // Default color if name not found
        for (const auto& style_color : tokenColors)
        {
            if (style_color.name == lookupName)
            {
                color = style_color.qcolor;
                return color;
            }
        }
        return color;
    };

    for (int i = 0; i < perfcounter_names.size(); i++)
    {
        auto name = perfcounter_names.at(i);

        if (name == "BUSY_CU_CYCLES")
        {
            // Baseline quadcycle measurement
            busy_cu_index = i;
        }
        else if (name == "VALU_MFMA_BUSY_CYCLES")
        {
            QColor color = getColorByName("MATRIX"); // special case for MFMA
            // This counter increments by cycles, not quadcycles
            bar_chart_data.push_back({"MFMA", acc_index(i) / 4, color});
            bar_chart_data.push_back({"MFMA Peak", peak_rates.at(i) / 4, color});
            util_names["MFMA"] = i;
        }
        else if (name.find("ACTIVE_INST_") == 0)
        {
            name = name.substr(std::string("ACTIVE_INST_").size());
            QColor color = (name == "SCA") ? getColorByName("SMEM") : getColorByName(name); // special case for SCA
            bar_chart_data.push_back({name.c_str(), acc_index(i), color});
            if (name == "VALU") bar_chart_data.push_back({"VALU Peak", peak_rates.at(i), color});
            util_names[name] = i;
        }
    }

    if (busy_cu_index >= 0)
    {
        double busy_cu_cycles = acc_index(busy_cu_index);
        double busy_max_cycles = peak_rates.at(busy_cu_index);

        for (auto& [name, data, _] : bar_chart_data)
        {
            if (name.contains("Peak"))
                data = 100.0 * data / busy_max_cycles;
            else
                data = 100.0 * data / busy_cu_cycles;
        }

        summary_view->setBarChartData(bar_chart_data, "Hardware Utilization");
    }
    else
        util_names.clear();

    {
        QStringList q_perf_names;

        for (auto& [name, _] : util_names) q_perf_names.push_back((name + " Util").c_str());

        for (auto& name : perfcounter_names) q_perf_names.push_back(name.c_str());

        summary_view->setTableHeaders(q_perf_names);
    }

    QStringList row_headers;
    row_headers.push_back("Peak");

    {
        QList<QString> row_data;
        for (auto& [name, index] : util_names)
        {
            double value = 100.0 * peak_rates.at(index) / std::max(peak_rates.at(busy_cu_index), 1.0);
            if (name.find("MFMA") == 0) value /= 4;

            row_data.push_back(QString::number(int(value + 0.5)) + "%");
        }

        for (int cidx = 0; cidx < perfcounter_names.size(); cidx++)
            row_data.push_back(QString::number(peak_rates.at(cidx)));

        summary_view->addTableRow(row_data);
    }

    for (int se = 0; se < accumulated.shape().getSE(); se++)
    {
        for (int cu = 0; cu < accumulated.shape().getCU(); cu++)
        {
            bool nonzero = false;
            QList<QString> row_data;

            for (auto& [name, index] : util_names)
            {
                double div = std::max((double) accumulated.at(0, se, cu, busy_cu_index), 1.0);
                double value = accumulated.at(0, se, cu, index);
                if (name == "MFMA") value /= 4;

                row_data.push_back(QString::number(int(100.0 * value / div + 0.5)) + "%");
            }

            for (int cidx = 0; cidx < perfcounter_names.size(); cidx++)
            {
                double value = accumulated.at(0, se, cu, cidx);
                nonzero |= value > 0;

                row_data.push_back(QString::number(value));
            }

            // Only add rows for CUs which had any counter increment
            if (nonzero)
            {
                row_headers.push_back(QString("SE %1").arg(se) + QString(" CU %1").arg(cu));
                summary_view->addTableRow(row_data);
            }
        }
    }

    summary_view->setTableRowHeaders(row_headers);
}

void MainWindow::UpdateCountersPlotSelection()
{
    if (!this->counters_plot) return;

    auto perfcounter_names = this->counters_plot->getDisabled();
    std::sort(
        perfcounter_names.begin(),
        perfcounter_names.end(),
        [](const auto& a, const auto& b) { return a.first.size() < b.first.size(); }
    );
    graph_info_table->setRowCount(perfcounter_names.size());

    int i = 0;
    for (auto& [name, disabled] : perfcounter_names)
    {
        class QLabel* v_label = new QLabel("");
        counter_values_tableitem[name] = v_label;

        QCheckBox* checkbox = new QCheckBox(name.c_str());
        checkbox->setChecked(!disabled);
        if (WindowColors::isDark())
            checkbox->setStyleSheet("background-color: #282828;");
        else
            checkbox->setStyleSheet("background-color: pallete(mid);");
        std::string counter_name = name;
        connect(
            checkbox,
            &QCheckBox::clicked,
            this,
            [this, counter_name](bool checked)
            {
                this->counters_plot->setDisabled(counter_name, !checked);
                this->counters_plot->update();
            }
        );

        graph_info_table->setCellWidget(i, 0, checkbox);
        graph_info_table->setCellWidget(i, 1, v_label);
        i++;
    }
}

void MainWindow::CreateWavesPlot()
{
    if (this->waves_plot_layout) delete this->waves_plot_layout;

    this->waves_plot = new WavePlotView(this);
    this->waves_plot->setGeometry(0, 0, 300, this->waves_plot->size().width());
    this->waves_plot_layout = new QBox();
    ui->wv_states_tab->setLayout(waves_plot_layout);
    waves_plot_layout->addWidget(this->waves_plot);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    accordion->replaceContentByTitle("Wave States", this->waves_plot);
#endif

    this->waves_plot->LoadWaveStateData(GetUIDir(), 0);
    this->waves_plot->setAutoLod(ui->lod_checkBox->isChecked());

    int wave_state_count = WavePlotView::state_names.size() - 2;
    ui->occ_info_table->setRowCount(wave_state_count);
    for (int i = 0; i < wave_state_count; i++)
    {
        auto& name = WavePlotView::state_names[i + 2];
        class QLabel* v_label = new QLabel("");

        counter_values_tableitem[name] = v_label;

        ui->occ_info_table->setCellWidget(i, 0, new QLabel(("Waves " + name).c_str()));
        ui->occ_info_table->setCellWidget(i, 1, v_label);
    }
    ui->occ_info_table->setToolTip("Displays current values under the mouse pointer. "
                                   "For waves, displays current values and percentage of each wave state.");
}

void MainWindow::CreateOccupancyPlot(bool bDispatch)
{
    auto*& layout = bDispatch ? this->dispatch_plot_layout : this->occupancy_plot_layout;
    auto*& plot = bDispatch ? this->dispatch_plot : this->occupancy_plot;
    if (layout) delete layout;

    plot = bDispatch ? new DispatchPlotView(this) : new OccupancyPlotView(this);
    plot->setGeometry(0, 0, 300, plot->size().width());
    layout = new QBox();
    layout->addWidget(plot);

    if (bDispatch)
    {
        if (ui->dispatch_tab->layout()) delete ui->dispatch_tab->layout();
        ui->dispatch_tab->setLayout(layout);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        accordion->replaceContentByTitle("Kernel Dispatch", plot);
#endif
    }
    else
    {
        if (ui->occupancy_tab->layout()) delete ui->occupancy_tab->layout();
        ui->occupancy_tab->setLayout(layout);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        accordion->replaceContentByTitle("Occupancy", plot);
#endif
    }

    plot->LoadOccupancyData(GetUIDir() + "occupancy.json");
    plot->setAutoLod(ui->lod_checkBox->isChecked());
}

std::shared_ptr<class ScrollValue> MainWindow::getCUScroll()
{
    if (auto* window = MainWindow::window)
        if (auto* waveview = window->cuwaves_h_scrollarea)
            if (auto view = waveview->view) return view;
    return nullptr;
}

void MainWindow::setPlotBarPos(float x)
{
    if (waves_plot) waves_plot->SetBarPos(x);
    if (counters_plot) counters_plot->SetBarPos(x);
    if (occupancy_plot) occupancy_plot->SetBarPos(x);
    if (dispatch_plot) dispatch_plot->SetBarPos(x);
}

void MainWindow::UpdateGraphInfo(const std::string& name, float value)
{
    QLabel* v_label = counter_values_tableitem[name];

    if (!v_label) return;

    std::ostringstream ss;
    if (value == 0)
        ss << 0;
    else if (std::abs(value) >= 100 && std::abs(value) < 10000)
        ss << static_cast<int>(value);
    else if (std::abs(value) >= 1 && std::abs(value) < 100)
        ss << std::fixed << std::setprecision(2) << value;
    else
        ss << std::scientific << std::setprecision(2) << value;

    v_label->setText(ss.str().c_str());
}

void MainWindow::UpdateOccupancyInfo(const std::vector<std::pair<std::string, int>>& values, float norm)
{
    QWARNING(ui->occ_info_table, "No graph info", return );

    for (auto& [k, v] : values)
    {
        std::stringstream ss;
        ss << std::setprecision(4) << v / norm;

        auto& table_entry = occupancy_values_tableitem[k];
        if (table_entry.first == nullptr || table_entry.second == nullptr)
        {
            int cnt = ui->occ_info_table->rowCount();
            if (cnt < 3) cnt = 3; // Reserve first 3 rows for wave states
            ui->occ_info_table->setRowCount(cnt + 1);

            table_entry.first = new QLabel();
            table_entry.second = new QLabel();

            ui->occ_info_table->setCellWidget(cnt, 0, new QLabel(k.c_str()));
            ui->occ_info_table->setCellWidget(cnt, 1, table_entry.first);
            ui->occ_info_table->setCellWidget(cnt, 2, table_entry.second);
        }

        table_entry.first->setText(std::to_string(v).c_str());
        table_entry.second->setText(ss.str().c_str());
    }
}

void MainWindow::UpdateGraphAutoLod(int bAutoLod)
{
    if (waves_plot) waves_plot->setAutoLod((bool) bAutoLod);
    if (counters_plot) counters_plot->setAutoLod((bool) bAutoLod);
    if (occupancy_plot) occupancy_plot->setAutoLod((bool) bAutoLod);
    if (dispatch_plot) dispatch_plot->setAutoLod((bool) bAutoLod);
}

void MainWindow::incrementWaveViewMipmap(int inc, float position)
{
    auto view = getCUScroll();
    QWARNING(view, "Widget not found", return );

    auto* ui = MainWindow::window->ui;

    int value = ui->waveview_spin->value() + inc;
    if (value > 12 || value < 0) return;

    if (inc < 0)
        view->start -= view->range * position;
    else if (inc > 0)
        view->start += view->range / 2 * position;

    view->notify();
    ui->waveview_spin->setValue(value);
}

void MainWindow::SetWaveViewMipmap(int value)
{
    value = std::min(std::max(10 - value, -2), 10);
    Token::mipmap_level = value;

    cuwaves_h_scrollarea->updatebar(true);
    utilization_h_scrollarea->updatebar(true);
}

void MainWindow::incrementGlobalViewMipmap(int inc, int content_mouse_x)
{
    QWARNING(window, "No MainWindow", return );
    QWARNING(window->global_view_scrollarea, "No global_view scroll area", return );
    QWARNING(window->global_view_widget, "No global_view widget", return );

    auto* ui = window->ui;
    int spinValue = ui->global_spin->value() + inc;
    if (spinValue > 15 || spinValue < 0) return;

    int new_mip = QGlobalView::SpinToMip(spinValue);
    int old_mip = QGlobalView::GetMip();
    int old_scroll = window->global_view_scrollarea->horizontalScrollBar()->value();
    int viewport_mouse_x = content_mouse_x - old_scroll;

    int new_scroll = QGlobalView::calcZoomScroll(old_mip, new_mip, old_scroll, viewport_mouse_x);

    // Update spinbox without triggering SetGlobalViewMipmap
    ui->global_spin->blockSignals(true);
    ui->global_spin->setValue(spinValue);
    ui->global_spin->blockSignals(false);

    // Set scroll before mipmap change to avoid flash
    window->global_view_scrollarea->horizontalScrollBar()->setValue(new_scroll);
    window->global_view_widget->SetMip(new_mip);

    // Set scroll again after layout update for zoom out
    if (new_mip > old_mip) window->global_view_scrollarea->horizontalScrollBar()->setValue(new_scroll);
}

void MainWindow::SetGlobalViewMipmap(int spinValue)
{
    QWARNING(global_view_scrollarea, "No global_view scroll area", return );

    int new_mip = QGlobalView::SpinToMip(spinValue);
    int old_mip = QGlobalView::GetMip();
    int old_scroll = global_view_scrollarea->horizontalScrollBar()->value();
    int viewport_center = global_view_scrollarea->width() / 2;

    int new_scroll = QGlobalView::calcZoomScroll(old_mip, new_mip, old_scroll, viewport_center);

    if (global_view_widget) global_view_widget->SetMip(new_mip);

    // Use timer to set scroll after layout updates
    slider_global = new_scroll;
    QTimer* timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(1);
    QObject::connect(
        timer,
        &QTimer::timeout,
        this,
        [this]()
        {
            if (auto* area = this->global_view_scrollarea) area->horizontalScrollBar()->setValue(this->slider_global);
        }
    );
    timer->start();
}

uint64_t MainWindow::ToMask(const std::vector<bool>& list)
{
    uint64_t mask = 0;
    for (int i = 0; i < list.size(); i++) mask |= (list[i] != false) << i;
    return mask;
}

void MainWindow::CreateGlobalView()
{
    if (this->global_view_tab->layout()) delete this->global_view_tab->layout();

    auto* mainLayout = new QVBox();
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    global_view_widget = new QGlobalView(GetUIDir() + "occupancy.json");

    // Load shaderdata from multiple threads before setting up the view
    // (shaderdata_manager is already loaded in ResetSelector, just pass to global view)
    if (shaderdata_manager && shaderdata_manager->HasData()) global_view_widget->SetShaderData(*shaderdata_manager);

    // Create sticky tick header (spans full width)
    QTickHeader* tickHeader = new QTickHeader(this);
    global_view_widget->tickHeader = tickHeader;
    mainLayout->addWidget(tickHeader);

    // Create horizontal layout for label panel + scroll area
    QHBoxLayout* contentLayout = new QHBoxLayout();
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    // Create sticky label panel on the left
    QLabelPanel* labelPanel = new QLabelPanel(this);
    global_view_widget->labelPanel = labelPanel;
    contentLayout->addWidget(labelPanel);

    // Scroll area for waves on the right
    global_view_scrollarea = new QScrollArea(this);
    global_view_scrollarea->setFrameShape(QFrame::NoFrame);
    global_view_scrollarea->setWidgetResizable(true);
    global_view_scrollarea->setWidget(global_view_widget);
    global_view_scrollarea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    contentLayout->addWidget(global_view_scrollarea);

    // Connect scroll bars to sticky elements
    global_view_widget->setScrollArea(global_view_scrollarea);

    // Populate the label panel with data from the global view
    global_view_widget->populateLabelPanel();

    QWidget* contentWidget = new QWidget();
    contentWidget->setLayout(contentLayout);
    mainLayout->addWidget(contentWidget);

    this->global_view_tab->setLayout(mainLayout);
}

void MainWindow::AddHistoryEntry(int64_t cycle, std::string_view type, std::string_view asmline)
{
    auto cycles = std::to_string(cycle);
    int rows = history_table->rowCount();

    if (rows)
    {
        auto last_history = dynamic_cast<HistoryEntry*>(history_table->cellWidget(rows - 1, 0));
        if (last_history && last_history->cycle == cycle) return; // Skip repeated clicks
    }

    history_table->insertRow(rows);
    history_table->setCellWidget(rows, 0, new HistoryEntry(cycle, type));
    history_table->setItem(rows, 1, new QTableWidgetItem(cycles.c_str()));
    history_table->setItem(rows, 2, new QTableWidgetItem(asmline.data()));
}

void MainWindow::SetSearchText(const std::string& text)
{
    ui->search_edit->blockSignals(true);
    ui->search_edit->setText(QString::fromStdString(text));
    ui->search_edit->blockSignals(false);
}

void MainWindow::NextSearch()
{
    QWARNING(code_contents, "No code", return );

    std::string to_search = ui->search_edit->displayText().toStdString();

    for (auto& [line_number, line] : ASMCodeline::line_map)
    {
        if (!line || line_number <= current_search_pos) continue;

        auto& str = line->elements.at(ASMCodeline::Element::EASM);

        if (!str || str->getStdText().find(to_search) == std::string::npos) continue;

        current_search_pos = line_number;
        code_contents->Highlight(line->line_index, line->line_index, true);
        return;
    }

    current_search_pos = -1;
}

void MainWindow::PrevSearch()
{
    QWARNING(code_contents, "No code", return );

    std::string to_search = ui->search_edit->displayText().toStdString();

    auto it = ASMCodeline::line_map.end();
    while (it != ASMCodeline::line_map.begin())
    {
        auto& [line_number, line] = *std::prev(it);
        it = std::prev(it);

        if (!line || line_number >= current_search_pos) continue;

        auto& str = line->elements.at(ASMCodeline::Element::EASM);

        if (!str || str->getStdText().find(to_search) == std::string::npos) continue;

        current_search_pos = line_number;
        code_contents->Highlight(line->line_index, line->line_index, true);
        return;
    }
    current_search_pos = 0x7FFFFFFF;
}

void MainWindow::SetIteration(int code_line, int iteration)
{
    ui->iteration_edit->setText(std::to_string(iteration).c_str());
    iteration_current.first = code_line;
    iteration_current.second = iteration;
}

void MainWindow::SetIterationCallback()
{
    bool ok = false;
    const int next_iteration = ui->iteration_edit->text().toInt(&ok);
    if (!ok) return; // Ignore incomplete or invalid input instead of crashing.

    iteration_current.second = next_iteration;
    int64_t clock = WaveInstance::GetMainClock(iteration_current.first, iteration_current.second);
    if (clock >= 0) ScrollViewsTo(clock);
}

void MainWindow::ScrollViewsTo(int64_t cycle)
{
    cuwaves_h_scrollarea->goToClock(cycle);
    utilization_h_scrollarea->goToClock(cycle);
}

void MainWindow::GatherWaves()
{
    QASSERT(cuwaves_content, "No CU Widget");

    JsonRequest filenames(GetUIDir() + "filenames.json");

    current_loaded_clk_start = QCustomScroll::clock_cutoff_start;
    current_loaded_clk_end = QCustomScroll::clock_cutoff_end;

    cuwaves_content->Clear();
    utilization_content->Clear();
    if (utilization_content)
    {
        utilization_content->PopulateOtherSimdTokens(
            current_wave_coord_se, current_wave_coord_sm, current_loaded_clk_start, current_loaded_clk_end
        );
    }

    int vertical_size = WSTATE_HEIGHT() + WSTATE_POSY() + 4; // Padding

    auto& se_data = filenames.data["wave_filenames"][std::to_string(current_wave_coord_se)];

    if (hotspot_view) delete hotspot_view;

    int _end = std::min<int>(hotspot_end, WaveInstance::main_wave->code.size());
    hotspot_view = new HotspotView(hotspot_begin, _end, hotspot_n_bins, hotspot_max_value);

    auto preload_wave = [](HotspotView* view, std::string str)
    {
        auto instance = WaveInstance::Get(str);
        view->Add(instance->tokens);
    };

    {
        std::array<std::future<void>, 16> threads;
        size_t count = 0;

        for (auto& [simd_name, simd_data] : se_data.items())
        {
            for (auto& [slot_name, slot_data] : simd_data.items())
            {
                int slot_n = std::stoi(slot_name);
                for (auto& [wid_name, wid_data] : slot_data.items())
                {
                    if (int64_t(wid_data[2]) < current_loaded_clk_start ||
                        int64_t(wid_data[1]) > current_loaded_clk_end)
                        continue;

                    threads.at(count % threads.size()) = std::async(
                        std::launch::async, preload_wave, hotspot_view, GetUIDir() + std::string(wid_data[0])
                    );
                    count++;
                }
            }
        }
    }

    int gathered_cu = -1; // CU extracted from any wave instance

    for (auto& [simd_name, simd_data] : se_data.items())
    {
        std::map<int, std::pair<QWidget*, std::string>> simd_views{};
        int simd_value = std::stoi(simd_name);

        for (auto& [slot_name, slot_data] : simd_data.items())
        {
            std::map<int64_t, std::shared_ptr<TokenGroup>> waves{};
            for (auto& [wid_name, wid_data] : slot_data.items())
            {
                if (int64_t(wid_data[2]) < current_loaded_clk_start || int64_t(wid_data[1]) > current_loaded_clk_end)
                    continue;

                auto instance = WaveInstance::Get(GetUIDir() + std::string(wid_data[0]));
                utilization_content->AddTokens(simd_value, instance->tokens);
                waves[instance->wave_begin] = instance;
                if (gathered_cu < 0 && instance->cu >= 0) gathered_cu = instance->cu;
            }
            if (!waves.size()) continue;

            QWaveView* view = new QWaveView(cuwaves_h_scrollarea);
            view->waves = std::move(waves);
            try
            {
                std::string filler = "-";
                if (slot_name.size() <= 1) filler = "-0";
                int slot_n = std::stoi(slot_name);
                simd_views[slot_n] = {view, "SM" + simd_name + filler + slot_name + " "};
            }
            catch (std::exception& e)
            {
                std::cerr << e.what() << std::endl;
                cuwaves_content->AddSlot(view, simd_name + '-' + slot_name, vertical_size);
            }
        }

        for (auto it = simd_views.begin(); it != simd_views.end(); it++)
            cuwaves_content->AddSlot(it->second.first, it->second.second, vertical_size);
    }

    // Add shaderdata tracks for all SIMDs/slots that have data
    if (shaderdata_manager && shaderdata_manager->HasData() && gathered_cu >= 0)
    {
        for (int sd_simd = 0; sd_simd < 4; sd_simd++)
        {
            for (int sd_slot = 0; sd_slot < 32; sd_slot++)
            {
                auto sd_records = shaderdata_manager->GetRecords(current_wave_coord_se, gathered_cu, sd_simd, sd_slot);
                if (sd_records && !sd_records->empty())
                {
                    auto* sd_view = new QShaderDataView(cuwaves_h_scrollarea, std::move(sd_records));
                    std::string filler = sd_slot < 10 ? "-0" : "-";
                    cuwaves_content->AddSlot(
                        sd_view,
                        "SD" + std::to_string(sd_simd) + filler + std::to_string(sd_slot) + " ",
                        vertical_size / 2
                    );
                }
            }
        }
    }

    if (utilization_content)
    {
        const auto& other_tokens = utilization_content->GetOtherSimdTokens();
        if (!other_tokens.empty())
        {
            auto* view = new QUtilView(cuwaves_h_scrollarea);
            std::string label = "SIMD" + std::to_string(utilization_content->GetOtherSimdId()) + ' ';
            view->label = cuwaves_content->AddSlot(view, label, vertical_size);
            view->AddTokens(other_tokens);
        }
    }

    utilization_content->Compile();

    hotspot_view->Compile();
    QWARNING(hotspot_tab && hotspot_tab->layout(), "No hotspot tab", return );
    hotspot_tab->layout()->setSpacing(0);
    hotspot_tab->layout()->setContentsMargins(0, 0, 0, 0);
    hotspot_tab->layout()->addWidget(hotspot_view);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    accordion->replaceContentByTitle("Hotspot", hotspot_view);
#endif
}

void MainWindow::loadJsonFileTree(const char* streambytes)
{
    QJsonDocument jsonDoc = QJsonDocument::fromJson(QByteArray(streambytes));
    if (!jsonDoc.isObject())
    {
        qWarning() << "Invalid JSON file";
        return;
    }

    flameGraph = new FlameGraphWidget();
    QScrollArea* flameScroll = new QScrollArea();
    flameScroll->setWidget(flameGraph);
    flameScroll->setWidgetResizable(true);

    QVBoxLayout* layout = new QVBox();
    ui->fileExplorer_tab->setLayout(layout);
    layout->addWidget(flameScroll);
}

void MainWindow::paintEvent(QPaintEvent* event) { QMainWindow::paintEvent(event); }

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);

    static bool first_time_init = true;
    if (first_time_init)
    {
        first_time_init = false;
        const double pixelRatio = 1.2;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        auto rect = QGuiApplication::primaryScreen()->availableGeometry();
#else
        auto rect = QApplication::desktop()->availableGeometry(this);
#endif
        int _width = std::min<int>(rect.width() / pixelRatio, width());
        int _height = std::min<int>(rect.height() / pixelRatio, height());
        this->setGeometry(_width / 12, _height / 12, _width, _height);
    }
}

void MainWindow::loadConfigSettings()
{
    AppConfig& config = AppConfig::getInstance();

    // Graph Options
    ui->lod_checkBox->setChecked(config.getLevelOfDetail());

    // Source Options
    ui->display_line_number->setChecked(config.getDisplayLineNumber());
    ui->source_hotspot_size_edit->setText(QString::number(config.getSourceHotspotSize()));

    // Display Options
    ui->fontedit->setText(QString::number(config.getFontSize()));
    ui->dark_theme_box->setChecked(config.getDarkTheme());
    ui->scale_edit->setChecked(config.getDisplayScaling());
    ui->separate_lds_pipe_box->setChecked(config.getSeparateLDSPipe());

    // Instruction Column Visibility
    ui->col_hitcount_box->setChecked(config.getColumnVisible(ASMCodeline::Element::EHIT));
    ui->col_latency_box->setChecked(config.getColumnVisible(ASMCodeline::Element::ELATENCY));
    ui->col_idle_box->setChecked(config.getColumnVisible(ASMCodeline::Element::EIDLE));
    ui->col_samples_box->setChecked(config.getColumnVisible(ASMCodeline::Element::EPCSamples));
    ui->col_stalls_box->setChecked(config.getColumnVisible(ASMCodeline::Element::EPCStalls));
    ui->col_issued_box->setChecked(config.getColumnVisible(ASMCodeline::Element::EPCIssued));
    ui->col_codeobj_box->setChecked(config.getColumnVisible(ASMCodeline::Element::ECODEOBJ));
    ui->col_vaddr_box->setChecked(config.getColumnVisible(ASMCodeline::Element::EADDRESS));
    ui->col_sourceref_box->setChecked(config.getColumnVisible(ASMCodeline::Element::ESOURCEREF, false));

    // Apply loaded settings
    font() = config.getFontSize();
    WindowColors::setDark(config.getDarkTheme());
    _scaling_var = config.getDisplayScaling() ? 1 : 0;
    SourceLine::bDisplayLineNumber = config.getDisplayLineNumber();
    HorizontalHotspot::SetHistogramWidth(config.getSourceHotspotSize());
    QUtilization::bSeparateLDSPipe = config.getSeparateLDSPipe();
}

void MainWindow::setupConfigConnections()
{
    // Graph Options
    connect(ui->lod_checkBox, &QCheckBox::stateChanged, this, &MainWindow::saveLevelOfDetailSetting);

    // Source Options
    connect(ui->display_line_number, &QCheckBox::stateChanged, this, &MainWindow::saveDisplayLineNumberSetting);
    connect(ui->source_hotspot_size_edit, &QLineEdit::editingFinished, this, &MainWindow::saveSourceHotspotSizeSetting);

    // Display Options
    connect(ui->fontedit, &QLineEdit::editingFinished, this, &MainWindow::saveFontSizeSetting);
    connect(ui->dark_theme_box, &QCheckBox::stateChanged, this, &MainWindow::saveDarkThemeSetting);
    connect(ui->scale_edit, &QCheckBox::stateChanged, this, &MainWindow::saveDisplayScalingSetting);
    connect(ui->separate_lds_pipe_box, &QCheckBox::stateChanged, this, &MainWindow::saveSeparateLDSPipeSetting);

    // Instruction Column Visibility - using lambdas to pass element enum
    auto connectColumnCheckbox = [this](QCheckBox* checkbox, ASMCodeline::Element elem)
    {
        connect(
            checkbox,
            &QCheckBox::stateChanged,
            this,
            [this, elem](int state) { saveColumnVisibilitySetting(elem, state != 0); }
        );
    };
    connectColumnCheckbox(ui->col_hitcount_box, ASMCodeline::Element::EHIT);
    connectColumnCheckbox(ui->col_latency_box, ASMCodeline::Element::ELATENCY);
    connectColumnCheckbox(ui->col_idle_box, ASMCodeline::Element::EIDLE);
    connectColumnCheckbox(ui->col_samples_box, ASMCodeline::Element::EPCSamples);
    connectColumnCheckbox(ui->col_stalls_box, ASMCodeline::Element::EPCStalls);
    connectColumnCheckbox(ui->col_issued_box, ASMCodeline::Element::EPCIssued);
    connectColumnCheckbox(ui->col_codeobj_box, ASMCodeline::Element::ECODEOBJ);
    connectColumnCheckbox(ui->col_vaddr_box, ASMCodeline::Element::EADDRESS);
    connectColumnCheckbox(ui->col_sourceref_box, ASMCodeline::Element::ESOURCEREF);
}

void MainWindow::saveLevelOfDetailSetting(int state) { AppConfig::getInstance().setLevelOfDetail(state != 0); }

void MainWindow::saveDisplayLineNumberSetting(int state) { AppConfig::getInstance().setDisplayLineNumber(state != 0); }

void MainWindow::saveSourceHotspotSizeSetting()
{
    bool ok;
    int size = ui->source_hotspot_size_edit->text().toInt(&ok);
    if (ok) AppConfig::getInstance().setSourceHotspotSize(size);
}

void MainWindow::saveFontSizeSetting()
{
    bool ok;
    int size = ui->fontedit->text().toInt(&ok);
    if (ok) AppConfig::getInstance().setFontSize(size);
}

void MainWindow::saveDarkThemeSetting(int state) { AppConfig::getInstance().setDarkTheme(state != 0); }

void MainWindow::saveDisplayScalingSetting(int state) { AppConfig::getInstance().setDisplayScaling(state != 0); }

void MainWindow::saveSeparateLDSPipeSetting(int state)
{
    AppConfig::getInstance().setSeparateLDSPipe(state != 0);
    QUtilization::bSeparateLDSPipe = (state != 0);
    QMessageBox::information(this, "Restart Required", "Please restart the viewer for this change to take effect.");
}

void MainWindow::saveColumnVisibilitySetting(int element, bool visible)
{
    AppConfig::getInstance().setColumnVisible(element, visible);
    if (QCodelist::singleton)
        QCodelist::singleton->setColumnVisibility(static_cast<ASMCodeline::Element>(element), visible);
}

std::optional<int> MainWindow::parseLineEditInt(const QLineEdit* edit)
{
    if (!edit) return std::nullopt;
    bool ok = false;
    const int value = edit->text().toInt(&ok);
    return ok ? std::optional<int>(value) : std::nullopt;
}

std::optional<int64_t> MainWindow::parseLineEditInt64(const QLineEdit* edit)
{
    if (!edit) return std::nullopt;
    bool ok = false;
    const qlonglong value = edit->text().toLongLong(&ok);
    return ok ? std::optional<int64_t>(value) : std::nullopt;
}
