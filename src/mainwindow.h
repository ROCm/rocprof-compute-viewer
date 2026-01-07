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

#if _WIN32
#    include <QtCore>
#    include <QtWidgets/QMainWindow>
#else
#    include <QMainWindow>
#endif
#include <QApplication>
#include <QLineEdit>
#include <QMetaObject>
#include <QStyleFactory>
#include <QTreeView>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <vector>
#include "collection/hotspotsummarywidget.h"
#include "graphics/hotspot.h"
#include "util/custom_layouts.h"

QT_BEGIN_NAMESPACE
namespace Ui
{
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT
    friend class OptionsDialogH;

public:
    MainWindow(std::string uidir);
    virtual ~MainWindow();
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void showEvent(QShowEvent* event) override;

    static std::string GetUIDir();
    std::string GetDisplayDir();
    void ResetSelector();

    void CreateCountersPlot();
    void UpdateCountersPlotSelection();
    void CreateOccupancyPlot(bool bDispatch);
    void CreateWavesPlot();
    void setPlotBarPos(float x);
    void UpdateGraphInfo(const std::string& name, int value, float integral);
    void UpdateGraphAutoLod(int bAutoLod);
    void ToggleDisplayLineNumber(int display);
    void SetJsonsFolder();
    void CountersFilter();
    void OpenOptionsDialog();
    void SetWaveViewMipmap(int value);
    void SetGlobalViewMipmap(int value);
    int GetHotspotBins() const { return hotspot_n_bins; }
    void UpdateWaveViewRange();
    void SetMainWave(int se, int simd, int sl, int wid);
    void CreateGlobalView();
    void SetIteration(int code_line, int iteration);
    void SetIterationCallback();
    void AddHistoryEntry(int64_t cycle, std::string_view type, std::string_view asmline);
    void ScrollViewsTo(int64_t cycle);
    void GatherWaves();
    void UpdateOccupancyInfo(const std::vector<std::pair<std::string, int>>& values, float norm);

    uint64_t GetSEMask() { return ToMask(se_enable_list); }
    uint64_t GetCUMask() { return ToMask(cu_enable_list); }
    static uint64_t ToMask(const std::vector<bool>& list);

    void PrevSearch();
    void NextSearch();
    void SetSearchText(const std::string& text);
    void SourceHotspotSizeEdited();

    void LoadSourceFiles();

    class QLayout* widSel = nullptr;
    class QLayout* wslSel = nullptr;
    class QLayout* simdSel = nullptr;
    class QLayout* shaderSel = nullptr;
    class QCodelist* code_contents = nullptr;
    class QWaveSlots* cuwaves_content = nullptr;
    class QUtilization* utilization_content = nullptr;
    class QScrollArea* utilization_v_scrollarea = nullptr;
    class QScrollArea* code_scrollarea = nullptr;
    class QWidget* wv_states = nullptr;
    class CounterPlotView* counters_plot = nullptr;
    class WavePlotView* waves_plot = nullptr;
    class OccupancyPlotView* occupancy_plot = nullptr;
    class OccupancyPlotView* dispatch_plot = nullptr;
    class HotspotView* hotspot_view = nullptr;
    class QScrollArea* cuwaves_v_scrollarea = nullptr;
    class QCustomScroll* cuwaves_h_scrollarea = nullptr;
    class QCustomScroll* utilization_h_scrollarea = nullptr;
    class QScrollArea* global_view_scrollarea = nullptr;
    class SummaryView* summary_view = nullptr;

    class QGridLayout* waves_plot_layout = nullptr;
    class QGridLayout* counters_plot_layout = nullptr;
    class QGridLayout* occupancy_plot_layout = nullptr;
    class QGridLayout* dispatch_plot_layout = nullptr;
    class QTableWidget* wave_info_table = nullptr;
    class QTableWidget* graph_info_table = nullptr;
    class QTableWidget* history_table = nullptr;
    class QWidget* hotspot_tab = nullptr;
    class SourceFileTab* source_filetab = nullptr;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    class AccordionWidget* accordion = nullptr;
#endif

    class WaveIDSelector* widSelector = nullptr;
    std::pair<int, int> iteration_current{};

    class QWidget* global_view_tab = nullptr;
    class QGlobalView* global_view_widget = nullptr;
    static MainWindow* window;

    static std::vector<QColor> dispatchcolors;
    static void getScaling(class QPainter& painter);
    static double getScaling();
    void setScaling(int scale);

    static void incrementWaveViewMipmap(int value, float position);
    static void incrementGlobalViewMipmap(int inc, int content_mouse_x);
    static std::shared_ptr<class ScrollValue> getCUScroll();

    static int& font();
    void updateFont();

    static QString default_font;

private:
    void updatePerfNames();
    std::vector<std::string> perfcounter_names{};

    static double _paint_scale;
    static int _scaling_var;

    Ui::MainWindow* ui = nullptr;
    class SESelector* seSelector = nullptr;
    std::string lastPath = "";

    std::unordered_map<std::string, std::pair<class QLabel*, class QLabel*>> counter_values_tableitem;
    std::unordered_map<std::string, std::pair<class QLabel*, class QLabel*>> occupancy_values_tableitem;

    std::string ui_dir;
    int hotspot_n_bins = 32;
    int hotspot_begin = 0;
    int hotspot_end = 1000000;
    double hotspot_max_value = -1;

    std::vector<bool> cu_enable_list = {};
    std::vector<bool> se_enable_list = {};

    int slider_scrollbar;
    int slider_cuwaves;
    int slider_global;
    int current_search_pos = 0;

    std::unordered_set<std::string> disabled_counters{};

    int current_wave_coord_se = 0;
    int current_wave_coord_sm = 0;
    int current_wave_coord_sl = 0;
    bool force_gather = false;

    int64_t current_loaded_clk_start = 0;
    int64_t current_loaded_clk_end = 0;

    QTreeView* fileExplorer;
    void loadJsonFileTree(const char* streambytes);
    void expandChildNodes(const QModelIndex& index);

    void loadConfigSettings();
    void setupConfigConnections();

    // Config save slots
    void saveLevelOfDetailSetting(int state);
    void saveDisplayLineNumberSetting(int state);
    void saveSourceHotspotSizeSetting();
    void saveFontSizeSetting();
    void saveDarkThemeSetting(int state);
    void saveDisplayScalingSetting(int state);
    void saveSeparateLDSPipeSetting(int state);

    class HotspotSummaryWidget* hotspotSummary = nullptr;

    static std::optional<int> parseLineEditInt(const QLineEdit* edit);
    static std::optional<int64_t> parseLineEditInt64(const QLineEdit* edit);

private slots:
    void onFileClicked(const QModelIndex& index);
};
