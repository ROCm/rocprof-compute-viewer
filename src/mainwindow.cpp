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

#include "mainwindow.h"
#include <stdlib.h>
#include <sys/stat.h>
#include <QFileDialog>
#include <QFontDatabase>
#include <QPainterPath>
#include <QScrollArea>
#include <QSpinBox>
#include <fstream>
#include <vector>
#include "./ui_mainwindow.h"
#include "button/historyentry.h"
#include "button/jsonselector.h"
#include "code/qcodelist.h"
#include "code/sourcefile.h"
#include "collection/histogramdelegate.h"
#include "collection/options.h"
#include "config/appconfig.h"
#include "config/config.hpp"
#include "data/wavedata.h"
#include "graphics/canvas.h"
#include "graphics/container/counterdialog.h"
#include "graphics/hotspot.h"
#include "graphics/specialized_plots.h"
#include "wave/scroll.h"
#include "wave/waveglobal.h"
#include "wave/waveview.h"

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
    {  0, 255,   0},
    {  0,   0, 255},
    {255,   0,   0},

    {255, 160,   0},
    {255,   0, 160},
    {  0, 160, 255},
    {160,   0, 255},
    {  0, 255, 160},
    {160, 255,   0},

    {255,  16, 255},
    {255, 255,  16},
    { 16, 255, 255},
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

    for (int i = 0; i < 16; i++) cu_enable_list.push_back(true);

    this->history_table = ui->history_table;
    this->history_table->verticalHeader()->setDefaultSectionSize(16);

    this->graph_info_table = ui->graph_info_table;
    this->graph_info_table->setColumnCount(3);
    this->graph_info_table->setRowCount(4);
    this->graph_info_table->setHorizontalHeaderLabels(QList<QString>({"Counter", "Value", "%/View"}));
    this->graph_info_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    this->graph_info_table->setSelectionMode(QAbstractItemView::NoSelection);

    this->wave_info_table = ui->wave_info_table;
    this->wave_info_table->setColumnCount(3);
    this->wave_info_table->setHorizontalHeaderLabels(QList<QString>({"Type", "Hit", "Stalls"}));
    this->wave_info_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    this->wave_info_table->setSelectionMode(QAbstractItemView::NoSelection);

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
    this->global_view_tab = ui->globalview_tab;

    fileExplorer = ui->fileExplorer;

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
    connect(ui->actionCounters_filter, &QAction::triggered, this, &MainWindow::CountersFilter);

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

    connect(fileExplorer, &QTreeView::clicked, this, &MainWindow::onFileClicked);

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
            this->update();
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
    try
    {
        font() = std::min(std::max(5, std::stoi(ui->fontedit->displayText().toStdString())), 19);
    }
    catch (...)
    {}

    update();
    updateGeometry();

    if (ui->tabWidget) {
        int currentIndex = ui->tabWidget->currentIndex();
        ui->tabWidget->setCurrentIndex((!currentIndex ? 1 : currentIndex-1));
        ui->tabWidget->setCurrentIndex(currentIndex);
    }

    if (ui->tabWidget_2) {
        int currentIndex = ui->tabWidget_2->currentIndex();
        ui->tabWidget_2->setCurrentIndex((!currentIndex ? 1 : currentIndex-1));
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
    try
    {
        SourceLine::HISTOGRAM_WIDTH = std::max(0, std::stoi(ui->source_hotspot_size_edit->displayText().toStdString()));
    }
    catch (...)
    {}

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

std::string MainWindow::GetDisplayDir() { return ui->ConfigNameEdit->displayText().toStdString() + '/'; }
std::string MainWindow::GetUIDir()
{
    QASSERT(window, "");
    return window->ui_dir;
}

void MainWindow::UpdateWaveViewRange()
{
    try
    {
        std::string str_vmin = ui->wview_range_min->displayText().toStdString();
        std::string str_vmax = ui->wview_range_max->displayText().toStdString();

        int64_t vmin = stol(str_vmin);
        int64_t vmax = stol(str_vmax);

        QCustomScroll::clock_cutoff_start = vmin;
        QCustomScroll::clock_cutoff_end = std::max(vmax, vmin + 128);

        if (vmin < current_loaded_clk_start || vmax > current_loaded_clk_end) GatherWaves();

        cuwaves_h_scrollarea->updatebar(true);
        utilization_h_scrollarea->updatebar(true);
    }
    catch (std::exception& e)
    {
        QWARNING(false, "Could not set range values.", return );
    }
}

void MainWindow::SetMainWave(int se, int simd, int sl, int wid)
{
    constexpr int64_t WAVE_END_ROOM = 20000;
    current_wave_coord_se = se;
    current_wave_coord_sm = simd;
    current_wave_coord_sl = sl;

    std::stringstream wave_name;
    wave_name << GetUIDir() << "se" << se << "_sm" << simd << "_sl" << sl << "_wv" << wid << ".json";

    auto main_wave = WaveInstance::Get(wave_name.str());
    WaveInstance::main_wave = main_wave;

    auto thread =
        std::async(std::launch::async, &ArrowCanvas::buildConnections, code_contents->connector, main_wave->waitcnt);
    code_contents->Populate(*main_wave);

    ui->wview_range_min->setText(std::to_string(main_wave->wave_begin).c_str());
    ui->wview_range_max->setText(std::to_string(main_wave->wave_end + WAVE_END_ROOM).c_str());

    UpdateWaveViewRange();

    QWARNING(wave_info_table, "No wave info", return );
    int i = 0;
    for (auto& [name, value, stalls] : main_wave->wave_info)
    {
        wave_info_table->setCellWidget(i, 0, new QLabel(name.c_str()));
        wave_info_table->setCellWidget(i, 1, new QLabel(std::to_string(value).c_str()));
        wave_info_table->setCellWidget(i, 2, new QLabel(std::to_string(stalls).c_str()));
        i++;
    }

    thread.get();

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
}

void MainWindow::OpenOptionsDialog()
{
    OptionsDialogH dialog;
    dialog.exec();
}

void MainWindow::SetJsonsFolder()
{
    std::string jsons_dir = QFileDialog::getExistingDirectory(this, "Select Dir", ui_dir.c_str()).toStdString();
    ui->ConfigNameEdit->setText(jsons_dir.c_str());
    ResetSelector();
}

inline static std::string remove_slash(std::string in)
{
    while (in.size() && in.back() == '/') in = in.substr(0, in.size() - 1);
    return in;
}

void recursive_load(
    const std::string& path, nlohmann::json& data, std::vector<std::pair<std::string, std::string>>& widgets
)
{
    for (auto& [key, value] : data.items())
    {
        std::string newpath = path + '/' + key;
        if (newpath == "//") newpath = "";

        if (value.is_string()) { widgets.push_back({newpath, std::string(value)}); }
        else { recursive_load(newpath, value, widgets); }
    }
}

void MainWindow::clearFileExplorerTree()
{
    // Reset the hotspot summary if it exists
    if (hotspotSummary) {
        hotspotSummary->clear();
    }

    // Clear any existing layout in fileExplorer_tab
    if (ui->fileExplorer_tab->layout()) {
        // Clean up widgets properly
        if (fileExplorer) {
            fileExplorer->setParent(nullptr);
            fileExplorer = nullptr;
        }

        if (hotspotSummary) {
            hotspotSummary->setParent(nullptr);
            hotspotSummary = nullptr;
        }

        // Delete the layout (and its items)
        QLayout* oldLayout = ui->fileExplorer_tab->layout();
        delete oldLayout;
    }

    // Create a blank layout to prevent Qt warnings
    QVBoxLayout* layout = new QVBoxLayout();
    ui->fileExplorer_tab->setLayout(layout);

    // Reset file model pointer
    fileModel = nullptr;
}

void MainWindow::LoadSourceFiles()
{
    QWARNING(source_filetab, "No source file tab", return);

    clearFileExplorerTree();

    JsonRequest req(GetUIDir() + "/snapshots.json");
    source_filetab->clear();

    if (req.bValid)
    {
        std::vector<std::pair<std::string, std::string>> widgets{};
        recursive_load("", req.data, widgets);

        for (auto& [profilingPath, uiFilename] : widgets) {
            std::string fullUiPath = ui_dir + std::string(uiFilename);
            source_filetab->addFile(profilingPath, fullUiPath);
        }

        loadJsonFileTree(req.data.dump().c_str());
    }
    else { QWARNING(false, "Invalid snapshots.json", 0); }

    // Only show raw source ref if no snapshot is found
    bool hassource = source_filetab->files.size();
    code_contents->elements.at(ASMCodeline::Element::ESOURCEREF)->setVisible(!hassource);
    source_filetab->setVisible(hassource);
}

void MainWindow::ResetSelector()
{
    std::string newpath = remove_slash(GetDisplayDir()) + "/filenames.json";
    if (newpath == lastPath) return;

    std::shared_ptr<JsonRequest> request{nullptr};
    try
    {
        request = std::make_shared<JsonRequest>(newpath);
        if (!request || !request->bValid) throw std::exception();
    }
    catch (std::exception& e)
    {
        ui->ConfigNameEdit->setText(ui_dir.c_str());
        QWARNING(false, "Invalid filename " << newpath, return );
    }
    ui_dir = remove_slash(GetDisplayDir()) + '/';
    lastPath = newpath;

    current_loaded_clk_start = -1;
    current_loaded_clk_end = -1;

    ASMCodeline::Clear();
    LoadSourceFiles();

    if (this->seSelector) delete this->seSelector;
    this->seSelector = nullptr;
    this->seSelector = new SESelector(*request);

    updatePerfNames();
    CreateWavesPlot();
    CreateOccupancyPlot(false);
    CreateOccupancyPlot(true);
    CreateCountersPlot();
    if (this->counters_plot) this->counters_plot->setAutoLod(ui->lod_checkBox->isChecked());

    this->CreateGlobalView();
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

    if (seSelector) delete seSelector;
    if (widSelector) delete widSelector;
    if (ui) delete ui;

    WaveInstance::InvalidadeCache();
    MainWindow::window = nullptr;
}

void MainWindow::CreateCountersPlot()
{
    QWARNING(this->seSelector, "Selector missing", return );
    QWARNING(this->seSelector->names.size(), "Selector has no SE data", return;);

    if (this->counters_plot_layout) delete this->counters_plot_layout;

    se_enable_list = {};
    std::vector<CounterPlotView::CounterAccum> accumulated{};
    bool load_perf_counters = true;

    if (load_perf_counters)
    {
        auto* traceplot = new TraceCounterPlotView(this);
        this->counters_plot = traceplot;

        for (const auto& SE_name : this->seSelector->names)
        {
            std::string filename = GetUIDir() + "se" + SE_name + "_perfcounter.json";
            try
            {
                int se_num = std::stoi(SE_name);
                JsonRequest file(filename, false);

                while (accumulated.size() <= se_num) accumulated.push_back({});
                accumulated.at(se_num) = traceplot->LoadCounterData(file, se_num);
            }
            catch (...)
            {}
        }
    }
    se_enable_list = std::vector<bool>(accumulated.size(), true);

    this->counters_plot->setGeometry(0, 0, 300, this->counters_plot->size().width());

    counters_plot_layout = new QBox();
    ui->wv_counters_tab->setLayout(counters_plot_layout);
    counters_plot_layout->addWidget(this->counters_plot);

    UpdateCountersPlotSelection();
}

void MainWindow::updatePerfNames()
{
    perfcounter_names = {};
    try
    {
        JsonRequest file(MainWindow::GetUIDir() + "filenames.json", false);
        perfcounter_names = file.data["counter_names"];
    }
    catch (...)
    {}
}

void MainWindow::CountersFilter()
{
    QWARNING(this->counters_plot, "Counter plot missing", return);

    if (perfcounter_names.empty()) return;

    std::vector<std::pair<std::string, bool>> counters_disable;
    for (auto& name : perfcounter_names) counters_disable.push_back({name, false});

    CounterDialog dialog(cu_enable_list, se_enable_list, counters_disable);
    dialog.exec();

    disabled_counters = {};
    for (auto& [name, disable] : counters_disable)
        if (disable) disabled_counters.insert(name);

    UpdateCountersPlotSelection();
}

void MainWindow::UpdateCountersPlotSelection()
{
    QWARNING(this->counters_plot, "Plot missing", return );

    if (perfcounter_names.empty()) return;

    this->counters_plot->UpdateDataSelection(perfcounter_names, GetSEMask(), GetCUMask(), disabled_counters);
    graph_info_table->setRowCount(4 + perfcounter_names.size());

    int i = 4;
    for (auto& name : perfcounter_names)
    {
        class QLabel* v_label = new QLabel("");
        class QLabel* i_label = new QLabel("");
        counter_values_tableitem[name] = v_label;
        counter_integral_tableitem[name] = i_label;

        graph_info_table->setCellWidget(i, 0, new QLabel(name.c_str()));
        graph_info_table->setCellWidget(i, 1, v_label);
        graph_info_table->setCellWidget(i, 2, i_label);
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
    this->waves_plot->LoadWaveStateData(GetUIDir(), 0);
    this->waves_plot->setAutoLod(ui->lod_checkBox->isChecked());

    for (int i = 0; i < WavePlotView::state_names.size() - 1; i++)
    {
        auto& name = WavePlotView::state_names[i + 1];
        class QLabel* v_label = new QLabel("");
        class QLabel* i_label = new QLabel("");

        counter_values_tableitem[name] = v_label;
        counter_integral_tableitem[name] = i_label;

        graph_info_table->setCellWidget(i, 0, new QLabel(("Waves " + name).c_str()));
        graph_info_table->setCellWidget(i, 1, v_label);
        graph_info_table->setCellWidget(i, 2, i_label);
    }
    graph_info_table->setToolTip("Displays current values under the mouse pointer. "
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
    if (bDispatch)
    {
        if (ui->dispatch_tab->layout()) delete ui->dispatch_tab->layout();
        ui->dispatch_tab->setLayout(layout);
    }
    else
    {
        if (ui->occupancy_tab->layout()) delete ui->occupancy_tab->layout();
        ui->occupancy_tab->setLayout(layout);
    }
    layout->addWidget(plot);
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

void MainWindow::UpdateGraphInfo(const std::string& name, int value, float integral)
{
    QWARNING(graph_info_table, "No graph info", return );

    QLabel* v_label = counter_values_tableitem[name];
    if (v_label) v_label->setText(std::to_string(value).c_str());

    std::stringstream ss;
    ss << std::setprecision(4) << integral;
    QLabel* i_label = counter_integral_tableitem[name];
    if (i_label) i_label->setText(ss.str().c_str());
}

void MainWindow::UpdateOccupancyInfo(const std::vector<std::pair<std::string, int>>& values, float norm)
{
    QWARNING(ui->occ_info_table, "No graph info", return );
    ui->occ_info_table->setRowCount(values.size());
    int cnt = 0;
    for (auto& [k, v] : values)
    {
        std::stringstream ss;
        ss << std::setprecision(4) << v / norm;

        ui->occ_info_table->setCellWidget(cnt, 0, new QLabel(k.c_str()));
        ui->occ_info_table->setCellWidget(cnt, 1, new QLabel(std::to_string(v).c_str()));
        ui->occ_info_table->setCellWidget(cnt, 2, new QLabel(ss.str().c_str()));
        cnt++;
    }
}

void MainWindow::UpdateGraphAutoLod(int bAutoLod)
{
    if (waves_plot) waves_plot->setAutoLod((bool) bAutoLod);
    if (counters_plot) counters_plot->setAutoLod((bool) bAutoLod);
    if (occupancy_plot) occupancy_plot->setAutoLod((bool) bAutoLod);
    if (dispatch_plot) dispatch_plot->setAutoLod((bool) bAutoLod);
}

void MainWindow::incrementWaveViewMipmap(int inc)
{
    auto view = getCUScroll();
    QWARNING(view, "Widget not found", return );

    auto* ui = MainWindow::window->ui;

    int value = ui->waveview_spin->value() + inc;
    if (value > 10 || value < 0) return;

    if (inc < 0)
        view->start -= view->range / 2;
    else if (inc > 0)
        view->start += view->range / 4;

    view->notify();
    ui->waveview_spin->setValue(value);
}

void MainWindow::SetWaveViewMipmap(int value)
{
    value = std::min(std::max(10 - value, 0), 10);
    Token::mipmap_level = value;

    cuwaves_h_scrollarea->updatebar(true);
    utilization_h_scrollarea->updatebar(true);
}

void MainWindow::SetGlobalViewMipmap(int value)
{
    QWARNING(global_view_scrollarea, "No global_view scroll area", return );

    value = std::max(0, std::min(15 - value, 15));
    int vwidth = global_view_scrollarea->width() / 2;

    int old_value = QGlobalView::GetMip();
    int scroll_value = global_view_scrollarea->horizontalScrollBar()->value() + vwidth;

    if (global_view_widget) global_view_widget->SetMip(value);

    if (old_value > value)
        scroll_value = scroll_value << (old_value - value);
    else
        scroll_value = scroll_value >> (value - old_value);

    QTimer* timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(1);
    QObject::connect(
        timer,
        &QTimer::timeout,
        this,
        [this]()
        {
            if (auto* area = this->global_view_scrollarea)
                area->horizontalScrollBar()->setValue(this->slider_global - area->width() / 2);
        }
    );
    timer->start();

    slider_global = scroll_value;
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

    auto* layout = new QVBox();

    global_view_widget = new QGlobalView(GetUIDir() + "occupancy.json");
    global_view_scrollarea = new QScrollArea(this);
    global_view_scrollarea->setWidgetResizable(true);

    global_view_scrollarea->setWidget(global_view_widget);
    global_view_scrollarea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    layout->addWidget(global_view_scrollarea);
    this->global_view_tab->setLayout(layout);
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
    iteration_current.second = std::stoi(ui->iteration_edit->displayText().toStdString());
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

    for (auto& [simd_name, simd_data] : se_data.items())
    {
        std::map<int, std::pair<QWaveView*, std::string>> simd_views{};
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
            }
            if (!waves.size()) continue;

            QWaveView* view = new QWaveView(cuwaves_h_scrollarea);
            view->waves = std::move(waves);
            try
            {
                std::string filler = "-";
                if (slot_name.size() <= 1) filler = "-0";
                simd_views[std::stoi(slot_name)] = {view, "SM" + simd_name + filler + slot_name};
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

    utilization_content->Compile();

    hotspot_view->Compile();
    QWARNING(hotspot_tab && hotspot_tab->layout(), "No hotspot tab", return );
    hotspot_tab->layout()->setSpacing(0);
    hotspot_tab->layout()->setContentsMargins(0, 0, 0, 0);
    hotspot_tab->layout()->addWidget(hotspot_view);
}

void MainWindow::loadJsonFileTree(const char* streambytes)
{
    QJsonDocument jsonDoc = QJsonDocument::fromJson(QByteArray(streambytes));
    if (!jsonDoc.isObject()) {
        qWarning() << "Invalid JSON file";
        return;
    }

    // Clear the layout in a cleaner way
    if (ui->fileExplorer_tab->layout()) {
        if (fileExplorer) {
            fileExplorer->setParent(nullptr);
            fileExplorer = nullptr;
        }

        if (hotspotSummary) {
            hotspotSummary->setParent(nullptr);
            hotspotSummary = nullptr;
        }

        QLayout* oldLayout = ui->fileExplorer_tab->layout();
        delete oldLayout;
    }

    // Create horizontal splitter
    QSplitter* horizontalSplitter = new QSplitter(Qt::Horizontal);

    // Create new tree view
    fileExplorer = new QTreeView();
    fileExplorer->setHeaderHidden(true);
    fileExplorer->setUniformRowHeights(true);
    connect(fileExplorer, &QTreeView::clicked, this, &MainWindow::onFileClicked);

    // Set the fileExplorer to be more compact
    fileExplorer->setMaximumWidth(600);
    fileExplorer->setColumnWidth(0, 400);

    // Add fileExplorer to the left side of the splitter
    horizontalSplitter->addWidget(fileExplorer);

    // Create a placeholder for the hotspot summary
    hotspotSummary = new HotspotSummaryWidget();
    horizontalSplitter->addWidget(hotspotSummary);

    //make explorer about 40% of the width
    horizontalSplitter->setSizes(QList<int>({4, 6}));

    // Add the splitter to the layout
    QVBoxLayout* layout = new QVBoxLayout();
    ui->fileExplorer_tab->setLayout(layout);
    layout->addWidget(horizontalSplitter);

    // Create new model
    fileModel = new JsonFileModel(fileExplorer, fileExplorer);
    fileExplorer->setModel(fileModel);

    HistogramDelegate* delegate = new HistogramDelegate(this);
    fileExplorer->setItemDelegate(delegate);

    // Set the JSON data to the model
    fileModel->setJson(jsonDoc.object());
    // Expand all nodes
    QModelIndex rootIndex = fileExplorer->model()->index(0, 0);
    expandChildNodes(rootIndex);
}

void MainWindow::expandChildNodes(const QModelIndex& index)
{
    if (!index.isValid()) return;

    fileExplorer->expand(index);

    int rowCount = fileExplorer->model()->rowCount(index);
    for (int row = 0; row < rowCount; ++row)
    {
        QModelIndex childIndex = fileExplorer->model()->index(row, 0, index);
        expandChildNodes(childIndex);
    }
}

void MainWindow::onFileClicked(const QModelIndex& index)
{
    JsonFileModel::Node* node = static_cast<JsonFileModel::Node*>(index.internalPointer());
    if (!node || node->value.isObject()) return; // Only handle file nodes

    QString nodeName = node->value.toString();
    std::string fileName = nodeName.toStdString();

    // Find the profiling path using mapping
    std::string uiPath = ui_dir + fileName;
    std::string profilingPath = "";

    if(source_filetab) {
        auto it = source_filetab->snap_to_filename.find(uiPath);
        if (it != source_filetab->snap_to_filename.end()) {
            profilingPath = it->second;
        }
    }

    if (profilingPath.empty()) {
        qDebug() << "ERROR: No profiling path found for:" << nodeName;
        return;
    }

    // Find or create hotspot summary widget
    QSplitter* horizontalSplitter = nullptr;
    if (ui->fileExplorer_tab->layout()) {
        QLayoutItem* item = ui->fileExplorer_tab->layout()->itemAt(0);
        if (item && item->widget()) {
            horizontalSplitter = qobject_cast<QSplitter*>(item->widget());
        }
    }

    if (horizontalSplitter) {
        // If the second widget is not our hotspot summary, create it
        if (!hotspotSummary) {
            // Remove any existing second widget
            if (horizontalSplitter->count() > 1) {
                QWidget* oldWidget = horizontalSplitter->widget(1);
                oldWidget->setParent(nullptr);
                delete oldWidget;
            }

            // Create our hotspot summary widget
            hotspotSummary = new HotspotSummaryWidget();
            horizontalSplitter->addWidget(hotspotSummary);
            horizontalSplitter->setSizes(QList<int>({4, 6}));
        }

        // Update the hotspot summary
        if (hotspotSummary) {
            hotspotSummary->setFile(profilingPath, fileName);
        }
    }
}

void MainWindow::paintEvent(QPaintEvent* event)
{
    QMainWindow::paintEvent(event);
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);

    static bool first_time_init = true;
    if(first_time_init) {
        first_time_init = false;
        const double pixelRatio = 1.2;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        auto rect = QGuiApplication::primaryScreen()->availableGeometry();
#else
        auto rect = QApplication::desktop()->availableGeometry(this);
#endif
        int width = rect.width() / pixelRatio;
        int height = rect.height() / pixelRatio;
        this->setGeometry(width / 12, height / 12, width, height);
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

    // Apply loaded settings
    font() = config.getFontSize();
    WindowColors::setDark(config.getDarkTheme());
    _scaling_var = config.getDisplayScaling() ? 1 : 0;
    SourceLine::bDisplayLineNumber = config.getDisplayLineNumber();
    SourceLine::HISTOGRAM_WIDTH = config.getSourceHotspotSize();
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
}

void MainWindow::saveLevelOfDetailSetting(int state) { AppConfig::getInstance().setLevelOfDetail(state != 0); }

void MainWindow::saveDisplayLineNumberSetting(int state) { AppConfig::getInstance().setDisplayLineNumber(state != 0); }

void MainWindow::saveSourceHotspotSizeSetting()
{
    bool ok;
    int size = ui->source_hotspot_size_edit->text().toInt(&ok);
    if (ok) { AppConfig::getInstance().setSourceHotspotSize(size); }
}

void MainWindow::saveFontSizeSetting()
{
    bool ok;
    int size = ui->fontedit->text().toInt(&ok);
    if (ok) { AppConfig::getInstance().setFontSize(size); }
}

void MainWindow::saveDarkThemeSetting(int state) { AppConfig::getInstance().setDarkTheme(state != 0); }

void MainWindow::saveDisplayScalingSetting(int state) { AppConfig::getInstance().setDisplayScaling(state != 0); }
