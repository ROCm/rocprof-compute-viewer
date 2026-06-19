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
#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QMessageBox>
#include <QPainterPath>
#include <QScrollArea>
#include <QSpinBox>
#include <QTextStream>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <set>
#include <utility>
#include <vector>
#include "./ui_mainwindow.h"
#include "analysis/annotation.h"
#include "analysis/hidden_latency.h"
#include "button/historyentry.h"
#include "button/jsonselector.h"
#include "code/labelminimap.h"
#include "code/qcodelist.h"
#include "code/sourcefile.h"
#include "collection/attselector.h"
#include "collection/derivedcountereditor.h"
#include "collection/flamegraphwidget.h"
#include "collection/latencyanalysisdialog.h"
#include "collection/license.h"
#include "collection/markerflamegraphwidget.h"
#include "collection/options.h"
#include "config/appconfig.h"
#include "config/config.hpp"
#include "data/datastore.h"
#include "data/input_detector.h"
#include "data/json_emitter.h"
#include "data/record_handlers.h"
#ifdef RCV_HAS_TRACE_DECODER
#    include "data/rocpd_emitter.h"
#    include "data/trace_decoder_emitter.h"
#endif
#include "data/shaderdata.h"
#include "data/wavedata.h"
#include "graphics/canvas.h"
#include "graphics/hotspot_view.h"
#include "graphics/specialized_plots.h"
#include "summary/summaryview.h"
#include "util/version.h"
#include "wave/othersimd.h"
#include "wave/scroll.h"
#include "wave/waveglobal.h"
#include "wave/waveview.h"

namespace fs = std::filesystem;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#    include "util/accordionwidget.h"
#endif

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#    include <QGuiApplication>
#    include <QScreen>
#else
#    include <QDesktopWidget>
#endif

// Maximum number of instructions to load into Compute Unit / Utilization views at once.
// When all wave data is in memory (decoder path), GatherWaves will expand the clock
// window to show more waves, up to this budget. Tokens are ~24 bytes each and are
// duplicated across the WaveInstance cache and the Utilization view (~3x multiplier).
constexpr size_t GATHER_INSTRUCTION_BUDGET = 50'000'000;

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
std::string MainWindow::cli_code_json_override;
std::string MainWindow::cli_snapshots_json_override;

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
        current_path = uidir;
        ResetSelector();
    }

    connect(ui->lod_checkBox, &QCheckBox::stateChanged, this, &MainWindow::UpdateGraphAutoLod);

    connect(ui->actionJsons_folder, &QAction::triggered, this, &MainWindow::SetJsonsFolder);
    connect(ui->actionAttFiles, &QAction::triggered, this, &MainWindow::OpenAttFiles);
    connect(ui->actionRocpd, &QAction::triggered, this, &MainWindow::OpenRocpd);
    connect(ui->actionHotOptions, &QAction::triggered, this, &MainWindow::OpenOptionsDialog);
    connect(ui->actionDerived_counters, &QAction::triggered, this, &MainWindow::OpenDerivedCounterEditor);
    connect(ui->actionHiddenLatency, &QAction::triggered, this, &MainWindow::OpenHiddenLatencyAnalysis);
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
    if (data_store && data_store->hidden_latency_analyzed)
        refreshHiddenLatencyViews();
    else if (flameGraph)
        flameGraph->rebuild();
}

void MainWindow::SetMainWave(int se, int simd, int sl, int wid)
{
    constexpr int64_t WAVE_END_ROOM = 20000;

    QWARNING(data_store, "No data store", return );
    auto se_it = data_store->wave_hierarchy.find(se);
    QWARNING(se_it != data_store->wave_hierarchy.end(), "Invalid SE: " << se, return );
    auto simd_it = se_it->second.find(simd);
    QWARNING(simd_it != se_it->second.end(), "Invalid SIMD: " << simd, return );
    auto slot_it = simd_it->second.find(sl);
    QWARNING(slot_it != simd_it->second.end(), "Invalid wave slot: " << sl, return );
    auto wid_it = slot_it->second.find(wid);
    QWARNING(wid_it != slot_it->second.end(), "Invalid WID: " << wid, return );

    const bool no_main_wave = WaveInstance::main_wave == nullptr;
    const auto& entry = wid_it->second;
    auto main_wave = data_store->getWave(entry);
    WaveInstance::main_wave = main_wave;

    QWARNING(main_wave && code_contents && code_contents->connector, "invalid code_contents", return );

    force_gather = current_wave_coord_se != se || no_main_wave;
    current_wave_coord_se = se;
    current_wave_coord_sm = simd;
    current_wave_coord_sl = sl;

    // Compute waitcnt on demand for the selected wave (decoder path)
    main_wave->buildWaitcnt(data_store->gfxip);

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

    // Hidden-latency traces already require all waves, so use the full selected SE
    // for Compute Unit, Utilization and Hotspot too.
    const bool load_all_waves = shouldAutoAnalyzeHiddenLatency();
    if (load_all_waves || !data_store->wave_records.empty())
    {
        int64_t se_min = INT64_MAX, se_max = INT64_MIN;
        size_t total_instructions = 0;

        data_store->forEachWave(
            [&](const DataStore::WaveCoordinate& coord, const WaveEntry& entry)
            {
                if (coord.hwid.se != se) return;
                if (!data_store->wave_records.empty())
                {
                    auto rec_it = data_store->wave_records.find(entry.id);
                    if (rec_it == data_store->wave_records.end() && !load_all_waves) return;
                    if (rec_it != data_store->wave_records.end())
                        total_instructions += rec_it->second.instructions.size();
                }
                se_min = std::min(se_min, entry.begin);
                se_max = std::max(se_max, entry.end);
            }
        );

        if ((load_all_waves || total_instructions <= GATHER_INSTRUCTION_BUDGET) && se_min < se_max)
        {
            ui->wview_range_min->setText(std::to_string(se_min).c_str());
            ui->wview_range_max->setText(std::to_string(se_max + WAVE_END_ROOM).c_str());
        }
    }

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
                RCV_LOG();
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
                RCV_LOG();
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

void MainWindow::OpenHiddenLatencyAnalysis() { runHiddenLatencyAnalysis(true); }

void MainWindow::refreshHiddenLatencyViews()
{
    if (!data_store || !data_store->hidden_latency_analyzed) return;

    HiddenLatencyAnalysis::applyToAsm(*data_store);
    if (source_filetab)
    {
        source_filetab->refreshHiddenLatencyFromAsm();
        source_filetab->refreshLatencyDisplay();
    }
    if (code_contents) code_contents->refreshLatencyAnnotations();
    if (flameGraph) flameGraph->rebuild();
}

bool MainWindow::shouldAutoAnalyzeHiddenLatency() const
{
    if (!data_store || !data_store->has_thread_trace) return false;
    if (data_store->gfxip >= 10) return true;

    std::string gfxv = data_store->gfxv;
    std::transform(gfxv.begin(), gfxv.end(), gfxv.begin(), [](unsigned char c) { return std::tolower(c); });
    return gfxv.find("navi") != std::string::npos;
}

bool MainWindow::runHiddenLatencyAnalysis(bool show_dialogs)
{
    if (!data_store)
    {
        if (show_dialogs)
            QMessageBox::warning(this, "Hidden Latency", "Load a trace before running hidden latency analysis.");
        return false;
    }

    if (!HiddenLatencyAnalysis::analyze(*data_store))
    {
        if (flameGraph) flameGraph->setHiddenLatencyAvailable(false);
        if (source_filetab)
        {
            source_filetab->refreshHiddenLatencyFromAsm();
            source_filetab->refreshLatencyDisplay();
        }
        if (code_contents) code_contents->refreshLatencyAnnotations();
        if (flameGraph) flameGraph->rebuild();
        if (show_dialogs) QMessageBox::warning(this, "Hidden Latency", "Hidden latency analysis failed.");
        return false;
    }

    if (flameGraph) flameGraph->setHiddenLatencyAvailable(true, true);
    refreshHiddenLatencyViews();
    if (code_contents) code_contents->selectAnnotation("inst_latency");

    if (show_dialogs) QMessageBox::information(this, "Hidden Latency", QString("Hidden latency analysis complete."));
    return true;
}

void MainWindow::SetJsonsFolder()
{
    std::string jsons_dir = QFileDialog::getExistingDirectory(this, "Select Dir", ui_dir.c_str()).toStdString();
    if (jsons_dir.empty()) return;
    current_path = jsons_dir;
    ResetSelector();
}

void MainWindow::OpenAttFiles()
{
#ifndef RCV_HAS_TRACE_DECODER
    QMessageBox::warning(
        this,
        "Unsupported",
        "This build was compiled without trace-decoder support.\n"
        "Rebuild with -DTRACE_DECODER_ROOT=... to load .att/.out files."
    );
    return;
#else
    QStringList picked = QFileDialog::getOpenFileNames(
        this, "Select ATT Trace Files", ui_dir.c_str(), "ATT Trace Files (*.att);;All Files (*)"
    );
    if (picked.isEmpty()) return;

    InputInfo info;
    info.type = InputType::ATT_FILES;
    info.att_files.reserve(picked.size());
    for (const QString& p : picked) info.att_files.push_back(p.toStdString());
    std::sort(info.att_files.begin(), info.att_files.end());

    // Keep att_file_info parallel to att_files (same order, same length) so the
    // dispatch selector and any future consumer can rely on the documented
    // invariant in input_detector.h:57. detectInput() does this for directory
    // inputs; the file-picker path was previously skipping it.
    info.att_file_info.reserve(info.att_files.size());
    for (const auto& p : info.att_files) info.att_file_info.push_back(parseAttFilename(p));

    // Common parent dir as base_path so the lazy .out scan inside
    // TraceDecoderEmitter can find sibling codeobj_<id>.out files. When the
    // user picks files from multiple directories we just use the first one's
    // parent; only that directory's .out siblings get scanned, which matches
    // the explicit-file-list intent.
    info.base_path = fs::path(info.att_files.front()).parent_path().string();

    // Capture display path BEFORE moving info — argument evaluation order is
    // unspecified, so reading info.att_files.front() after std::move(info) on
    // the same call is undefined behaviour (and segfaulted in practice).
    std::string display = info.att_files.front();
    LoadInput(std::move(info), display);
#endif
}

void MainWindow::OpenRocpd()
{
#ifndef RCV_HAS_TRACE_DECODER
    QMessageBox::warning(
        this,
        "Unsupported",
        "This build was compiled without trace-decoder support.\n"
        "Rebuild with -DTRACE_DECODER_ROOT=... to load .rocpd files."
    );
    return;
#else
    QString picked = QFileDialog::getOpenFileName(
        this, "Select ROCpd Database", ui_dir.c_str(), "ROCpd Database (*.rocpd);;All Files (*)"
    );
    if (picked.isEmpty()) return;

    InputInfo info;
    info.type = InputType::ROCPD;
    info.rocpd_path = picked.toStdString();
    info.base_path = picked.toStdString();
    std::string display = info.rocpd_path;
    LoadInput(std::move(info), display);
#endif
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

    const bool has_sources = data_store && !data_store->source_snapshots.empty();
    const bool has_markers = shaderdata_manager && shaderdata_manager->HasMarkers();

    if (has_sources)
    {
        for (auto& snap : data_store->source_snapshots)
        {
            source_filetab->addFile(snap.original_path, snap.snapshot_path);
        }
    }

    // The fine flamegraph is meaningful with either DWARF source rows or marker
    // scope rows — instruction costs roll up under whichever is present.
    ui->tabWidget_2->setTabEnabled(3, has_sources || has_markers);
    if (has_sources || has_markers) ensureFlameGraphWidget();

    // Only show raw source ref if no snapshot is found
    bool hassource = source_filetab->files.size();
    code_contents->elements.at(ASMCodeline::Element::ESOURCEREF)->setVisible(!hassource);
    source_filetab->setVisible(hassource);
}

void MainWindow::ResetSelector()
{
    if (current_path.empty()) return;
    const std::string input_path = QDir::cleanPath(QString::fromStdString(current_path)).toStdString();
    InputInfo input_info = detectInput(input_path);
    LoadInput(std::move(input_info), input_path);
}

MainWindow::LoadResult MainWindow::LoadInputForTests(InputInfo input_info, const std::string& input_path)
{
    return LoadInputImpl(std::move(input_info), input_path, false);
}

void MainWindow::LoadInput(InputInfo input_info, const std::string& input_path)
{
    (void) LoadInputImpl(std::move(input_info), input_path, true);
}

MainWindow::LoadResult MainWindow::LoadInputImpl(InputInfo input_info, const std::string& input_path, bool show_dialogs)
{
    LoadResult load_result;

    // Apply CLI overrides — preserved so the legacy `code.json` /
    // `snapshots.json` argv hack on main.cpp still works for every input type.
    if (!cli_code_json_override.empty()) input_info.code_json_override = cli_code_json_override;
    if (!cli_snapshots_json_override.empty()) input_info.snapshots_json_override = cli_snapshots_json_override;

#ifdef RCV_HAS_TRACE_DECODER
    // Multi-dispatch ATT inputs: each .att file is its own shader-clock domain
    // starting at sc=0, and REALTIME alignment cannot reconcile two domains on
    // the same SE. Pop the dispatch selector so the user picks exactly one
    // capture. The unique key is (agent, dispatch) — different agents (GPUs)
    // may reuse the same dispatch_id from independent processes, so dispatch
    // alone is not enough.
    if (input_info.type == InputType::ATT_FILES && !input_info.att_file_info.empty())
    {
        std::set<std::pair<int, int>> captures;
        for (const auto& f : input_info.att_file_info) captures.emplace(f.agent, f.dispatch);
        if (captures.size() > 1)
        {
            AttSelectorDialog dlg(this, input_info);
            if (dlg.exec() != QDialog::Accepted)
            {
                load_result.status = LoadStatus::LoadFailed;
                load_result.message = "ATT selection cancelled";
                return load_result;
            }
            input_info = dlg.selectedInfo();
            if (input_info.att_files.empty())
            {
                load_result.status = LoadStatus::LoadFailed;
                load_result.message = "No ATT files selected";
                return load_result;
            }
        }
    }
#endif

    // For JSON_DIR, use the existing filenames.json check as the change-detection key
    std::string newpath;
    if (input_info.type == InputType::JSON_DIR)
    {
        QDir dir(QString::fromStdString(input_path));
        newpath = dir.filePath("filenames.json").toStdString();
    }
    else if (input_info.type == InputType::ATT_FILES && !input_info.att_files.empty())
    {
        // Two distinct selections may share the input_path (one of the picked
        // .att files) and the file count, so include every picked path in the
        // key. Hash via std::hash<string> to keep the key bounded.
        std::string joined;
        for (const auto& p : input_info.att_files)
        {
            joined.push_back('\0');
            joined += p;
        }
        newpath = input_path + "#" + std::to_string(std::hash<std::string>{}(joined));
    }
    else { newpath = input_path; }

    if (newpath == lastPath) return load_result;

    if (input_info.type == InputType::UNKNOWN)
    {
        if (show_dialogs)
            QMessageBox::warning(
                this,
                "Path Not Found",
                QString("No recognized trace data found at: %1").arg(QString::fromStdString(input_path))
            );
        load_result.status = LoadStatus::InvalidInput;
        load_result.message = QString("No recognized trace data found at: %1").arg(QString::fromStdString(input_path));
        return load_result;
    }

    // Remember what we actually loaded so subsequent ResetSelector() calls
    // (e.g. theme reload) can re-detect against it.
    current_path = input_path;

#ifndef RCV_HAS_TRACE_DECODER
    if (input_info.type == InputType::ATT_FILES || input_info.type == InputType::ROCPD)
    {
        if (show_dialogs)
            QMessageBox::warning(
                this,
                "Unsupported Input",
                "This build does not include trace-decoder support.\n"
                "Please rebuild with -DRCV_ENABLE_TRACE_DECODER=ON or use a "
                "JSON ui_output_* directory."
            );
        load_result.status = LoadStatus::UnsupportedInput;
        load_result.message = "This build does not include trace-decoder support.";
        return load_result;
    }
#endif

    ui_dir = input_path;
    if (!ui_dir.empty() && ui_dir.back() != '/') ui_dir.push_back('/');

    // New traces can reuse the same generated wave filenames and code.json
    // path. Clear these process-wide caches before emitters resolve markers or
    // lazily load any waves for the incoming trace.
    WaveInstance::main_wave.reset();
    WaveInstance::InvalidadeCache();

    // Clear non-owning pointer before resetting data_store (which owns the memory)
    shaderdata_manager = nullptr;

    // Populate the DataStore via the appropriate emitter
    data_store = std::make_unique<DataStore>();
    {
        RecordDispatcher dispatcher;
        WaveHandler wave_handler(*data_store);
        OccupancyHandler occ_handler(*data_store);
        CounterHandler ctr_handler(*data_store);
        ShaderDataHandler shaderdata_handler(*data_store);
        OtherSimdHandler other_simd_handler(*data_store);
        RealtimeHandler rt_handler(*data_store);
        MetadataHandler meta_handler(*data_store);
        dispatcher.addHandler(&wave_handler);
        dispatcher.addHandler(&occ_handler);
        dispatcher.addHandler(&ctr_handler);
        dispatcher.addHandler(&shaderdata_handler);
        dispatcher.addHandler(&other_simd_handler);
        dispatcher.addHandler(&rt_handler);
        dispatcher.addHandler(&meta_handler);

        switch (input_info.type)
        {
            case InputType::JSON_DIR:
            {
                JsonRecordEmitter emitter(ui_dir, dispatcher, *data_store);
                emitter.run();
                break;
            }
#ifdef RCV_HAS_TRACE_DECODER
            case InputType::ATT_FILES:
            {
                TraceDecoderEmitter emitter(input_info, dispatcher, *data_store);
                emitter.run();
                // Surface decoder parse errors to the user. These are .att files
                // that the decoder failed to parse — without this popup the only
                // signal was a stderr line they'd never see.
                const auto& errs = emitter.parseErrors();
                if (!errs.empty())
                {
                    QString details;
                    for (const auto& e : errs) details += QString::fromStdString(e) + "\n";
                    if (show_dialogs)
                        QMessageBox::warning(
                            this,
                            "Trace decoder parse errors",
                            QString("%1 .att file(s) failed to parse:\n\n%2").arg(errs.size()).arg(details)
                        );
                }
                break;
            }
            case InputType::ROCPD:
            {
                RocpdEmitter emitter(input_info, dispatcher, *data_store);
                emitter.run();
                break;
            }
#endif
            default: break;
        }
    }

    // Wire up other_simd from DataStore
    if (utilization_content)
    {
        if (!data_store->other_simd_by_se.empty())
        {
            // Decoder path: convert records to OtherSimdInstruction
            std::map<int, std::vector<OtherSimdInstruction>> records;
            for (auto& [se, recs] : data_store->other_simd_by_se)
                for (auto& r : recs) records[se].push_back({r.time, (int) r.cycles, (int) r.category});
            utilization_content->SetOtherSimdRecords(std::move(records));
        }
        else { utilization_content->SetOtherSimdSources(data_store->other_simd_files); }
    }

    // Wire up shaderdata from DataStore
    if (data_store->shaderdata && data_store->shaderdata->HasData())
    {
        shaderdata_manager = data_store->shaderdata.get();
    }

    // Instantiate the global marker flamegraph tab on demand. The tab is
    // hidden entirely on traces without resolved markers (JSON path, ATT
    // traces with no funcmap section, etc.).
    {
        const bool show_markers = shaderdata_manager && shaderdata_manager->HasMarkers();
        if (show_markers)
        {
            if (ui->marker_fg_tab && !markerFlameGraph)
            {
                if (ui->marker_fg_tab->layout())
                {
                    QLayoutItem* item;
                    while ((item = ui->marker_fg_tab->layout()->takeAt(0)) != nullptr)
                    {
                        if (item->widget()) item->widget()->deleteLater();
                        delete item;
                    }
                }
                else { ui->marker_fg_tab->setLayout(new QVBox()); }
                markerFlameGraph = new MarkerFlameGraphWidget();
                QScrollArea* mScroll = new QScrollArea();
                mScroll->setWidget(markerFlameGraph);
                mScroll->setWidgetResizable(true);
                ui->marker_fg_tab->layout()->addWidget(mScroll);
            }
            if (markerFlameGraph) markerFlameGraph->rebuild();
            ui->tabWidget_2->setTabEnabled(4, true);
            ui->tabWidget_2->setTabVisible(4, true);
        }
        else
        {
            ui->tabWidget_2->setTabEnabled(4, false);
            ui->tabWidget_2->setTabVisible(4, false);
        }
    }

    // Surface marker decode diagnostics. Warnings logged to console; errors
    // also raise a non-modal dialog once. Per the user's requirement, orphan
    // exits, unmatched IDs, and any malformed sequences are never silently
    // dropped.
    if (shaderdata_manager)
    {
        const auto& diags = shaderdata_manager->GetMarkerDiagnostics();
        if (!diags.empty())
        {
            int n_warn = 0, n_err = 0, n_info = 0;
            QStringList lines;
            for (const auto& d : diags)
            {
                switch (d.severity)
                {
                    case MarkerDiagnostic::Severity::Error: n_err++; break;
                    case MarkerDiagnostic::Severity::Warning: n_warn++; break;
                    case MarkerDiagnostic::Severity::Info: n_info++; break;
                }
                qWarning() << "[markers]" << QString::fromStdString(d.message);
                if (lines.size() < 100) lines << QString::fromStdString(d.message);
            }
            if (n_err + n_warn > 0)
            {
                QString summary = QString("Marker decode: %1 warning(s), %2 error(s)").arg(n_warn).arg(n_err);
                if (statusBar()) statusBar()->showMessage(summary, 0);
                if (n_err > 0)
                {
                    QString body = lines.join('\n');
                    if (lines.size() < (int) diags.size())
                        body += QString("\n... (%1 more)").arg((int) diags.size() - lines.size());
                    QMessageBox* box = new QMessageBox(
                        QMessageBox::Warning,
                        "Marker Decode Diagnostics",
                        summary + "\n\n" + body,
                        QMessageBox::Ok,
                        this
                    );
                    box->setModal(false);
                    box->setAttribute(Qt::WA_DeleteOnClose);
                    box->show();
                }
            }
        }
    }

    lastPath = newpath;

    current_loaded_clk_start = -1;
    current_loaded_clk_end = -1;

    ASMCodeline::Clear();
    Annotation::Registry::instance().clearAll();
    Canvas::active_annotation_id.clear();
    if (code_contents) code_contents->refreshAnnotations();
    LoadSourceFiles();
    cuwaves_content->Clear();
    utilization_content->Clear();
    if (hotspot_view) hotspot_view->setVisible(false);
    updateFont(); // Fixme: This is being called to force redraw in case the incoming trace is empty

    HorizontalHotspot::is_sqtt_enabled = data_store->has_thread_trace;
    HorizontalHotspot::is_pcs_enabled = data_store->has_pc_sampling;
    if (label_minimap) label_minimap->Clear();
    if (HorizontalHotspot::is_pcs_enabled)
    {
        if (!data_store->code.empty()) code_contents->Populate(data_store->code);
        if (flameGraph) flameGraph->rebuild();
    }

    if (this->seSelector) delete this->seSelector;
    this->seSelector = nullptr;
    this->seSelector = new SESelector(*data_store);

    if (shouldAutoAnalyzeHiddenLatency()) runHiddenLatencyAnalysis(false);

    if (history_table) history_table->setRowCount(0);

    counter_values_tableitem.clear();
    occupancy_values_tableitem.clear();

    if (graph_info_table) graph_info_table->setRowCount(1);
    if (ui->occ_info_table) ui->occ_info_table->setRowCount(0);

    try
    {
        CreateOccupancyPlot(false);
        CreateOccupancyPlot(true);
        CreateCountersPlot();

        this->CreateGlobalView();
    }
    catch (std::exception& e)
    {
        RCV_LOG();
        load_result.status = LoadStatus::LoadFailed;
        load_result.message = QString("Unable to create plots: %1").arg(e.what());
        return load_result;
    }

    if (!data_store ||
        (data_store->wave_hierarchy.empty() && data_store->occupancy_by_se.empty() && data_store->code.empty()))
    {
        load_result.status = LoadStatus::LoadFailed;
        load_result.message = "Input did not produce usable viewer data.";
    }

    return load_result;
}

MainWindow::~MainWindow()
{
    if (code_scrollarea) delete code_scrollarea;

    if (cuwaves_content) delete cuwaves_content;
    if (cuwaves_v_scrollarea) delete cuwaves_v_scrollarea;

    if (counters_plot) delete counters_plot;
    if (counters_plot_layout) delete counters_plot_layout;

    if (dispatch_plot) delete dispatch_plot;
    if (occupancy_plot) delete occupancy_plot;
    if (dispatch_plot_layout) delete dispatch_plot_layout;
    if (occupancy_plot_layout) delete occupancy_plot_layout;

    // shaderdata_manager is a non-owning pointer into data_store; data_store handles cleanup.
    shaderdata_manager = nullptr;
    if (seSelector) delete seSelector;
    if (widSelector) delete widSelector;
    if (ui) delete ui;

    WaveInstance::InvalidadeCache();
    MainWindow::window = nullptr;
}

namespace
{

/// Build the bar-chart entries shown above the summary table, plus the
/// `util_names` map used to label per-CU rows. Returns the index of
/// `BUSY_CU_CYCLES` in `perfcounter_names`, or -1 if absent — callers use
/// that to decide whether the bar chart is meaningful (peaks need a busy
/// baseline to normalize against).
int buildSummaryBarChartData(
    const std::vector<std::string>& perfcounter_names,
    const DerivedCounter::Tensor& accumulated,
    const std::vector<double>& peak_rates,
    QList<std::tuple<QString, float, QColor>>& bar_chart_data_out,
    std::map<std::string, size_t>& util_names_out
)
{
    auto acc_index = [&accumulated](int index) -> double
    {
        double acc = 0;
        for (int se = 0; se < accumulated.shape().getSE(); se++)
            for (int cu = 0; cu < accumulated.shape().getCU(); cu++) acc += accumulated.at(0, se, cu, index);
        return acc;
    };

    const auto& tokenColors = Config::TokenColors();
    auto getColorByName = [&](const std::string& lookupName) -> QColor
    {
        for (const auto& style_color : tokenColors)
            if (style_color.name == lookupName) return style_color.qcolor;
        return tokenColors[0].qcolor;
    };

    int busy_cu_index = -1;
    for (int i = 0; i < (int) perfcounter_names.size(); i++)
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
            bar_chart_data_out.push_back({"MFMA", acc_index(i) / 4, color});
            bar_chart_data_out.push_back({"MFMA Peak", peak_rates.at(i) / 4, color});
            util_names_out["MFMA"] = i;
        }
        else if (name.find("ACTIVE_INST_") == 0)
        {
            name = name.substr(std::string("ACTIVE_INST_").size());
            QColor color = (name == "SCA") ? getColorByName("SMEM") : getColorByName(name);
            bar_chart_data_out.push_back({name.c_str(), acc_index(i), color});
            if (name == "VALU") bar_chart_data_out.push_back({"VALU Peak", peak_rates.at(i), color});
            util_names_out[name] = i;
        }
    }
    return busy_cu_index;
}

/// Populate the SummaryView's table: column headers from `util_names` +
/// `perfcounter_names`, a "Peak" row, then one row per (SE, CU) that saw
/// any counter increment. Pure UI fill — no business decisions live here.
void populateSummaryTable(
    SummaryView* summary_view,
    const std::vector<std::string>& perfcounter_names,
    const std::map<std::string, size_t>& util_names,
    const std::vector<double>& peak_rates,
    const DerivedCounter::Tensor& accumulated,
    int busy_cu_index
)
{
    QStringList q_perf_names;
    for (auto& [name, _] : util_names) q_perf_names.push_back((name + " Util").c_str());
    for (auto& name : perfcounter_names) q_perf_names.push_back(name.c_str());
    summary_view->setTableHeaders(q_perf_names);

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
        for (int cidx = 0; cidx < (int) perfcounter_names.size(); cidx++)
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
            for (int cidx = 0; cidx < (int) perfcounter_names.size(); cidx++)
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

} // anonymous namespace

void MainWindow::CreateCountersPlot()
{
    QWARNING(summary_view && ui->tabWidget_2, "No summary view!", return );

    summary_view->clearTableData();
    summary_view->clearBarChartData();
    ui->tabWidget_2->setTabEnabled(2, false);

    // Load counter names from DataStore
    auto perfcounter_names = data_store ? data_store->counter_names : std::vector<std::string>{};

    for (auto& name : perfcounter_names)
        if (name.size() > 5 && name.find("SQ_") == 0) name = name.substr(3);

    bool load_perf_counters = data_store && !perfcounter_names.empty() && !data_store->counters_by_se.empty();

    auto* traceplot = new TraceCounterPlotView(this);
    this->counters_plot = traceplot;

    if (load_perf_counters) traceplot->LoadCounterData(*data_store);

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

    ui->tabWidget_2->setTabEnabled(2, true);

    std::map<std::string, size_t> util_names{};
    QList<std::tuple<QString, float, QColor>> bar_chart_data;
    int busy_cu_index =
        buildSummaryBarChartData(perfcounter_names, accumulated, peak_rates, bar_chart_data, util_names);

    if (busy_cu_index >= 0)
    {
        // Normalize bar-chart values against the busy-CU baseline so
        // utilization reads as a percentage. Without a baseline, the
        // accumulated counts are unitless and the bar chart is hidden.
        double busy_cu_cycles = 0;
        for (int se = 0; se < accumulated.shape().getSE(); se++)
            for (int cu = 0; cu < accumulated.shape().getCU(); cu++)
                busy_cu_cycles += accumulated.at(0, se, cu, busy_cu_index);
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
    else { util_names.clear(); }

    populateSummaryTable(summary_view, perfcounter_names, util_names, peak_rates, accumulated, busy_cu_index);
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

    if (data_store)
    {
        if (bDispatch)
            dynamic_cast<DispatchPlotView*>(plot)->LoadOccupancyData(*data_store);
        else
            dynamic_cast<OccupancyPlotView*>(plot)->LoadOccupancyData(*data_store);
    }
    else { plot->LoadOccupancyData(GetUIDir() + "occupancy.json"); }
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
    auto* scrollbar = window->global_view_scrollarea->horizontalScrollBar();
    int old_scroll = scrollbar->value();
    int viewport_mouse_x = content_mouse_x - old_scroll;

    int new_scroll = QGlobalView::calcZoomScroll(old_mip, new_mip, old_scroll, viewport_mouse_x);

    // Update spinbox without triggering SetGlobalViewMipmap
    ui->global_spin->blockSignals(true);
    ui->global_spin->setValue(spinValue);
    ui->global_spin->blockSignals(false);

    // Set scroll before mipmap change to avoid flash
    if (new_mip < old_mip && new_scroll > scrollbar->maximum()) scrollbar->setMaximum(new_scroll);
    scrollbar->setValue(new_scroll);
    window->global_view_widget->SetMip(new_mip);

    // Set scroll again after layout update for zoom out
    if (new_mip > old_mip) scrollbar->setValue(new_scroll);
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

    if (data_store)
        global_view_widget = new QGlobalView(*data_store);
    else
        global_view_widget = new QGlobalView(GetUIDir() + "occupancy.json");

    // Load shaderdata from multiple threads before setting up the view
    // (shaderdata_manager is already loaded in ResetSelector, just pass to global view)
    if (shaderdata_manager && shaderdata_manager->HasData()) global_view_widget->SetShaderData(*shaderdata_manager);
    if (shaderdata_manager && shaderdata_manager->HasMarkers()) global_view_widget->SetMarkers(*shaderdata_manager);

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
    QWARNING(data_store, "No data store", return );

    current_loaded_clk_start = QCustomScroll::clock_cutoff_start;
    current_loaded_clk_end = QCustomScroll::clock_cutoff_end;

    cuwaves_content->Clear();
    utilization_content->Clear();

    const std::vector<trace_event_record_t>* trace_events = nullptr;
    const std::vector<dispatch_record_t>* dispatch_records = nullptr;
    auto trace_event_it = data_store->trace_events_by_se.find(current_wave_coord_se);
    if (trace_event_it != data_store->trace_events_by_se.end()) trace_events = &trace_event_it->second;
    auto dispatch_it = data_store->dispatch_records_by_se.find(current_wave_coord_se);
    if (dispatch_it != data_store->dispatch_records_by_se.end()) dispatch_records = &dispatch_it->second;
    utilization_content->SetDecoderEvents(trace_events, dispatch_records, current_wave_coord_se);

    if (utilization_content)
    {
        utilization_content->PopulateOtherSimdTokens(
            current_wave_coord_se, current_wave_coord_sm, current_loaded_clk_start, current_loaded_clk_end
        );
    }

    int vertical_size = WSTATE_HEIGHT() + WSTATE_POSY() + 4; // Padding

    auto se_it = data_store->wave_hierarchy.find(current_wave_coord_se);
    if (se_it == data_store->wave_hierarchy.end()) return;
    auto& se_data = se_it->second;

    if (hotspot_view) delete hotspot_view;

    int _end = std::min<int>(hotspot_end, WaveInstance::main_wave->code.size());
    hotspot_view = new HotspotView(hotspot_begin, _end, hotspot_n_bins, hotspot_max_value);

    auto preload_wave = [this](HotspotView* view, WaveEntry entry)
    {
        auto instance = data_store->getWave(entry);
        if (instance) view->Add(instance->tokens);
    };

    {
        std::array<std::future<void>, 16> threads;
        size_t count = 0;

        data_store->forEachWave(
            [&](const DataStore::WaveCoordinate& coord, const WaveEntry& entry)
            {
                if (coord.hwid.se != current_wave_coord_se) return;
                if (entry.end < current_loaded_clk_start || entry.begin > current_loaded_clk_end) return;

                threads.at(count % threads.size()) = std::async(std::launch::async, preload_wave, hotspot_view, entry);
                count++;
            }
        );
    }

    int gathered_cu = WaveInstance::main_wave ? WaveInstance::main_wave->cu : -1;
    if (gathered_cu < 0)
    {
        bool found_cu = false;
        for (auto& [_simd_id, simd_data] : se_data)
        {
            for (auto& [_slot_id, slot_data] : simd_data)
            {
                for (auto& [_wid, entry] : slot_data)
                {
                    if (entry.end < current_loaded_clk_start || entry.begin > current_loaded_clk_end) continue;

                    auto instance = data_store->getWave(entry);
                    if (!instance || instance->cu < 0) continue;

                    gathered_cu = instance->cu;
                    found_cu = true;
                    break;
                }
                if (found_cu) break;
            }
            if (found_cu) break;
        }
    }

    std::set<std::pair<int, int>> shaderdata_slots;
    bool shaderdata_slots_ready = false;

    auto load_shaderdata_slots = [&]()
    {
        if (shaderdata_slots_ready || !shaderdata_manager || !shaderdata_manager->HasData() || gathered_cu < 0) return;

        for (int sd_simd = 0; sd_simd < 4; sd_simd++)
        {
            for (int sd_slot = 0; sd_slot < 32; sd_slot++)
            {
                auto records = shaderdata_manager->GetRecords({current_wave_coord_se, gathered_cu, sd_simd, sd_slot});
                if (records && !records->empty()) shaderdata_slots.emplace(sd_simd, sd_slot);
            }
        }
        shaderdata_slots_ready = true;
    };

    auto add_shaderdata_slot = [&](int sd_simd, int sd_slot)
    {
        load_shaderdata_slots();
        auto key = std::make_pair(sd_simd, sd_slot);
        auto slot_it = shaderdata_slots.find(key);
        if (slot_it == shaderdata_slots.end()) return;
        shaderdata_slots.erase(slot_it);

        const HWID hwid{current_wave_coord_se, gathered_cu, sd_simd, sd_slot};
        auto sd_records = shaderdata_manager->GetRecords(hwid);
        if (!sd_records || sd_records->empty()) return;

        auto* sd_view = new QShaderDataView(cuwaves_h_scrollarea, std::move(sd_records));
        int sd_height = vertical_size / 2;
        if (shaderdata_manager->HasMarkers())
        {
            auto markers = shaderdata_manager->GetMarkers(hwid);
            if (markers && !markers->empty())
            {
                sd_view->SetMarkers(std::move(markers));
                sd_height = sd_view->suggestedHeight(vertical_size / 2);
            }
        }

        std::string filler = sd_slot < 10 ? "-0" : "-";
        cuwaves_content->AddSlot(
            sd_view, "SD" + std::to_string(sd_simd) + filler + std::to_string(sd_slot) + " ", sd_height
        );
    };

    load_shaderdata_slots();

    for (auto& [simd_id, simd_data] : se_data)
    {
        std::string simd_name = std::to_string(simd_id);
        std::set<int> slot_ids;
        for (auto& [slot_id, _] : simd_data) slot_ids.insert(slot_id);
        if (shaderdata_slots_ready)
            for (const auto& [sd_simd, sd_slot] : shaderdata_slots)
                if (sd_simd == simd_id) slot_ids.insert(sd_slot);

        for (int slot_id : slot_ids)
        {
            std::map<int64_t, std::shared_ptr<TokenGroup>> waves{};
            std::string slot_name = std::to_string(slot_id);

            auto slot_data_it = simd_data.find(slot_id);
            if (slot_data_it != simd_data.end())
            {
                for (auto& [wid, entry] : slot_data_it->second)
                {
                    if (entry.end < current_loaded_clk_start || entry.begin > current_loaded_clk_end) continue;

                    if (auto instance = data_store->getWave(entry))
                    {
                        utilization_content->AddTokens(simd_id, instance->tokens);
                        waves[instance->wave_begin] = instance;
                    }
                }
            }

            // Keep shaderdata adjacent to the matching wave slot instead of
            // appending all SD rows as a second list after the CU rows.
            add_shaderdata_slot(simd_id, slot_id);
            if (!waves.size()) continue;

            QWaveView* view = new QWaveView(cuwaves_h_scrollarea);
            view->SetDecoderEvents(trace_events, dispatch_records, current_wave_coord_se);
            view->waves = std::move(waves);
            std::string filler = slot_id < 10 ? "-0" : "-";
            cuwaves_content->AddSlot(view, "SM" + simd_name + filler + slot_name + " ", vertical_size);
        }
    }

    // If a shaderdata bucket has no matching visible wave row, still show it
    // after the integrated rows so no loaded SQTT data disappears.
    load_shaderdata_slots();
    while (shaderdata_slots_ready && !shaderdata_slots.empty())
    {
        const auto [sd_simd, sd_slot] = *shaderdata_slots.begin();
        add_shaderdata_slot(sd_simd, sd_slot);
    }

    if (utilization_content)
    {
        const auto& other_tokens = utilization_content->GetOtherSimdTokens();
        if (!other_tokens.empty())
        {
            auto* view = new QUtilView(cuwaves_h_scrollarea);
            view->SetDecoderEvents(trace_events, dispatch_records, current_wave_coord_se);
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

void MainWindow::ensureFlameGraphWidget()
{
    if (flameGraph) return;

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
    ui->source_include_hidden_latency_box->setChecked(config.getSourceIncludeHiddenLatency());

    // Display Options
    ui->fontedit->setText(QString::number(config.getFontSize()));
    ui->dark_theme_box->setChecked(config.getDarkTheme());
    ui->scale_edit->setChecked(config.getDisplayScaling());
    ui->separate_lds_pipe_box->setChecked(config.getSeparateLDSPipe());
    ui->show_idle_time_box->setChecked(config.getShowIdleTime());

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
    HorizontalHotspot::source_include_hidden_latency = config.getSourceIncludeHiddenLatency();
    HorizontalHotspot::show_idle_time = config.getShowIdleTime();
    QUtilization::bSeparateLDSPipe = config.getSeparateLDSPipe();
}

void MainWindow::setupConfigConnections()
{
    // Graph Options
    connect(ui->lod_checkBox, &QCheckBox::stateChanged, this, &MainWindow::saveLevelOfDetailSetting);

    // Source Options
    connect(ui->display_line_number, &QCheckBox::stateChanged, this, &MainWindow::saveDisplayLineNumberSetting);
    connect(ui->source_hotspot_size_edit, &QLineEdit::editingFinished, this, &MainWindow::saveSourceHotspotSizeSetting);
    connect(
        ui->source_include_hidden_latency_box,
        &QCheckBox::stateChanged,
        this,
        &MainWindow::saveSourceIncludeHiddenLatencySetting
    );

    // Display Options
    connect(ui->fontedit, &QLineEdit::editingFinished, this, &MainWindow::saveFontSizeSetting);
    connect(ui->dark_theme_box, &QCheckBox::stateChanged, this, &MainWindow::saveDarkThemeSetting);
    connect(ui->scale_edit, &QCheckBox::stateChanged, this, &MainWindow::saveDisplayScalingSetting);
    connect(ui->separate_lds_pipe_box, &QCheckBox::stateChanged, this, &MainWindow::saveSeparateLDSPipeSetting);
    connect(ui->show_idle_time_box, &QCheckBox::stateChanged, this, &MainWindow::saveShowIdleTimeSetting);

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

void MainWindow::saveSourceIncludeHiddenLatencySetting(int state)
{
    const bool enabled = state != 0;
    AppConfig::getInstance().setSourceIncludeHiddenLatency(enabled);
    HorizontalHotspot::source_include_hidden_latency = enabled;

    if (source_filetab) source_filetab->refreshLatencyDisplay();
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

void MainWindow::saveShowIdleTimeSetting(int state)
{
    const bool enabled = state != 0;
    AppConfig::getInstance().setShowIdleTime(enabled);
    HorizontalHotspot::show_idle_time = enabled;

    if (code_contents) code_contents->refreshLatencyAnnotations();
    if (source_filetab) source_filetab->refreshLatencyDisplay();
    if (flameGraph) flameGraph->rebuild();
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
