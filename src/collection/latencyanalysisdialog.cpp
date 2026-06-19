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

#include "latencyanalysisdialog.h"

#include "../analysis/annotation.h"
#include "../analysis/memory_latency.hpp"
#include "../code/asmcode.h"
#include "../code/qcodelist.h"
#include "../config/config.hpp"
#include "../graphics/canvas.h"
#include "../json/include/nlohmann/json.hpp"
#include "../util/diagnostic_log.h"

#include <iomanip>
#include <memory>
#include <sstream>

#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <array>
#include <fstream>

using json = nlohmann::json;

namespace
{
const char* counterLabel(LatencyAnalysis::CounterType type)
{
    switch (type)
    {
        case LatencyAnalysis::CounterType::VMEM: return "VMEM";
        case LatencyAnalysis::CounterType::SMEM: return "SMEM";
        case LatencyAnalysis::CounterType::LDS: return "LDS";
        default: return "";
    }
}
} // namespace

LatencyAnalysisDialog::LatencyAnalysisDialog(const std::string& uidir, QWidget* parent) :
QDialog(parent), m_uidir(uidir)
{
    setWindowTitle("Memory Latency Analysis");
    setMinimumSize(900, 600);

    auto* mainLayout = new QVBoxLayout(this);

    // Disclaimer
    m_disclaimerLabel = new QLabel(
        "<b>Note:</b> These results are approximate and may be unreliable. "
        "Use for general guidance only.",
        this
    );
    m_disclaimerLabel->setStyleSheet("color: orange; padding: 5px;");
    m_disclaimerLabel->setWordWrap(true);
    mainLayout->addWidget(m_disclaimerLabel);

    m_statusLabel = new QLabel("Initializing...", this);
    mainLayout->addWidget(m_statusLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setMinimum(0);
    m_progressBar->setValue(0);
    mainLayout->addWidget(m_progressBar);

    // Horizontal layout for result panels
    auto* resultsLayout = new QHBoxLayout();

    for (int i = 0; i < kNumCounters; ++i)
    {
        auto type = static_cast<LatencyAnalysis::CounterType>(i);
        auto* groupBox = new QGroupBox(counterLabel(type), this);
        auto* groupLayout = new QVBoxLayout(groupBox);

        m_resultsText[i] = new QTextEdit(this);
        m_resultsText[i]->setReadOnly(true);
        m_resultsText[i]->setFont(QFont("monospace"));
        groupLayout->addWidget(m_resultsText[i]);

        resultsLayout->addWidget(groupBox);
    }

    mainLayout->addLayout(resultsLayout, 1); // stretch factor 1

    auto* closeButton = new QPushButton("Close", this);
    closeButton->setEnabled(false);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    mainLayout->addWidget(closeButton);

    // Timer for progress updates
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &LatencyAnalysisDialog::updateProgress);

    // Start analysis after dialog is shown
    QTimer::singleShot(100, this, &LatencyAnalysisDialog::startAnalysis);
}

LatencyAnalysisDialog::~LatencyAnalysisDialog()
{
    if (m_analysisFuture.valid()) m_analysisFuture.wait();
}

void LatencyAnalysisDialog::startAnalysis()
{
    // Check required files exist
    std::string filenamesPath = m_uidir + "/filenames.json";
    std::string codePath = m_uidir + "/code.json";

    std::ifstream filenamesFile(filenamesPath);
    if (!filenamesFile.is_open())
    {
        QMessageBox::critical(this, "Error", QString("Cannot open %1").arg(filenamesPath.c_str()));
        reject();
        return;
    }

    std::ifstream codeFile(codePath);
    if (!codeFile.is_open())
    {
        QMessageBox::critical(this, "Error", QString("Cannot open %1").arg(codePath.c_str()));
        reject();
        return;
    }

    // Load counter names from filenames.json
    json filenamesJson;
    filenamesFile >> filenamesJson;

    std::vector<std::string> counterNames;
    for (const auto& name : filenamesJson["counter_names"]) counterNames.push_back(name.get<std::string>());

    // Get list of available SEs from wave_filenames (same approach as CreateCountersPlot)
    std::vector<int> availableSEs;
    if (filenamesJson.contains("wave_filenames") && filenamesJson["wave_filenames"].is_object())
    {
        for (const auto& [seKey, seData] : filenamesJson["wave_filenames"].items())
        {
            try
            {
                int seNum = std::stoi(seKey);
                // Check if corresponding perfcounter file exists
                std::string perfPath = m_uidir + "/se" + std::to_string(seNum) + "_perfcounter.json";
                std::ifstream perfFile(perfPath);
                if (perfFile.is_open()) availableSEs.push_back(seNum);
            }
            catch (...)
            {
                RCV_LOG();
                // Skip invalid SE keys
            }
        }
    }

    if (availableSEs.empty())
    {
        QMessageBox::critical(this, "Error", "No shader engines with perfcounter data found");
        reject();
        return;
    }

    std::sort(availableSEs.begin(), availableSEs.end());

    // Load code map from code.json
    json codeJson;
    codeFile >> codeJson;

    std::map<int, std::string> codeMap;
    for (const auto& entry : codeJson["code"])
    {
        int codeIndex = entry[2].get<int>();
        std::string filename = entry[3].get<std::string>();
        std::string instruction = entry[0].get<std::string>();

        auto pos = filename.rfind('/');
        if (pos != std::string::npos) filename = filename.substr(pos + 1);

        codeMap[codeIndex] = filename + ": " + instruction;
    }

    // Collect wave files for each SE separately - must not mix SEs
    std::map<int, std::vector<std::pair<std::string, int>>> waveFilesBySE;
    int totalWaveFiles = 0;
    for (int se : availableSEs)
    {
        auto seWaveFiles = LatencyAnalysis::LatencyAnalyzer::collectWaveFilePaths(m_uidir, se);
        if (!seWaveFiles.empty())
        {
            totalWaveFiles += seWaveFiles.size();
            waveFilesBySE[se] = std::move(seWaveFiles);
        }
    }

    if (waveFilesBySE.empty())
    {
        QMessageBox::critical(this, "Error", "No wave files found for any shader engine");
        reject();
        return;
    }

    // Count available counters
    int availableCounters = 0;
    for (int i = 0; i < kNumCounters; ++i)
    {
        auto type = static_cast<LatencyAnalysis::CounterType>(i);
        if (LatencyAnalysis::LatencyAnalyzer::hasCounter(counterNames, type)) ++availableCounters;
    }

    if (availableCounters == 0)
    {
        QMessageBox::critical(
            this,
            "Error",
            "No latency counters found in data. Re-run rocprofv3 with:<br>--att-perfcounter-ctrl 1 --att-perfcounters "
            "\"<span style='color: #FFB366;'>SQ_INST_LEVEL_VMEM</span> <span style='color: "
            "#FF8C00;'>SQ_INST_LEVEL_LDS</span>\""
        );
        reject();
        return;
    }

    // Total files = wave files (loaded once, not per counter)
    m_totalFiles = totalWaveFiles;
    m_progressBar->setMaximum(m_totalFiles);
    m_statusLabel->setText(QString("Analyzing %1 wave files across %2 SE(s) for %3 counter(s)...")
                               .arg(totalWaveFiles)
                               .arg(waveFilesBySE.size())
                               .arg(availableCounters));

    // Start timer for progress updates
    m_timer->start(100);

    // Run analysis in background thread
    m_analysisFuture = std::async(
        std::launch::async,
        [this,
         counterNames = std::move(counterNames),
         codeMap = std::move(codeMap),
         waveFilesBySE = std::move(waveFilesBySE)]() mutable
        {
            try
            {
                // Accumulate results across all SEs for each counter type
                std::array<std::map<int, std::vector<double>>, kNumCounters> accumulatedLatencies;
                std::array<std::map<int, std::string>, kNumCounters> codeForIndex;
                std::array<std::map<int, int64_t>, kNumCounters> accumulatedIssue;
                std::array<std::map<int, int64_t>, kNumCounters> accumulatedStall;

                // Process each SE: load wave files once, then run all counter types
                for (const auto& [se, waveFilePaths] : waveFilesBySE)
                {
                    // Load perf data asynchronously for this SE
                    std::string perfPath = m_uidir + "/se" + std::to_string(se) + "_perfcounter.json";
                    auto perfFuture =
                        std::async(std::launch::async, LatencyAnalysis::LatencyAnalyzer::loadPerfData, perfPath);

                    // Load wave files in parallel
                    auto loaded = LatencyAnalysis::LatencyAnalyzer::loadWaveFiles(waveFilePaths, &m_progress);

                    // Get perf data (should be ready by now)
                    auto perfData = perfFuture.get();

                    // Run analysis for each counter type
                    for (int i = 0; i < kNumCounters; ++i)
                    {
                        auto type = static_cast<LatencyAnalysis::CounterType>(i);

                        // Skip counters that aren't available in this data
                        if (!LatencyAnalysis::LatencyAnalyzer::hasCounter(counterNames, type)) continue;

                        LatencyAnalysis::LatencyAnalyzer analyzer(
                            counterNames, codeMap, LatencyAnalysis::counterTypeToString(type), loaded.cu
                        );
                        analyzer.analyze(perfData, loaded.waves);

                        auto results = analyzer.getResults();

                        // Merge results from this SE
                        for (const auto& [codeIndex, instr] : results)
                        {
                            accumulatedLatencies[i][codeIndex].insert(
                                accumulatedLatencies[i][codeIndex].end(), instr.latencies.begin(), instr.latencies.end()
                            );
                            codeForIndex[i][codeIndex] = instr.code;
                            accumulatedIssue[i][codeIndex] += instr.totalIssue;
                            accumulatedStall[i][codeIndex] += instr.totalStall;
                        }
                    }
                }

                // Compute final stats from accumulated data for each counter type
                for (int i = 0; i < kNumCounters; ++i)
                {
                    for (const auto& [codeIndex, latencies] : accumulatedLatencies[i])
                    {
                        auto stats = LatencyAnalysis::computeLatencyStats(latencies, 40);
                        int sampleCount = static_cast<int>(latencies.size());
                        if (sampleCount > 0)
                        {
                            stats.meanIssue = static_cast<double>(accumulatedIssue[i][codeIndex]) / sampleCount;
                            stats.meanStall = static_cast<double>(accumulatedStall[i][codeIndex]) / sampleCount;
                        }
                        m_results[i][codeIndex] = {codeForIndex[i][codeIndex], stats};
                    }
                }

                // Signal completion on main thread
                QMetaObject::invokeMethod(this, &LatencyAnalysisDialog::analysisFinished, Qt::QueuedConnection);
            }
            catch (const std::exception& e)
            {
                RCV_LOG();
                QString errorMsg = QString::fromStdString(e.what());
                QMetaObject::invokeMethod(
                    this,
                    [this, errorMsg]()
                    {
                        m_timer->stop();
                        QMessageBox::critical(this, "Error", errorMsg);
                        reject();
                    },
                    Qt::QueuedConnection
                );
            }
        }
    );
}

void LatencyAnalysisDialog::updateProgress()
{
    int current = m_progress.load();
    m_progressBar->setValue(current);
    m_statusLabel->setText(QString("Loading wave files... %1/%2").arg(current).arg(m_totalFiles));
}

void LatencyAnalysisDialog::analysisFinished()
{
    m_timer->stop();
    m_progressBar->setValue(m_totalFiles);
    m_statusLabel->setText("Analysis complete!");

    // One Memory Latency category covering both VMEM and LDS. Each ASM line
    // has at most one counter type; on collision the later write wins (kept
    // for parity with prior behaviour). VMEM/LDS only differ in the "Mean"
    // segment colour.
    auto cat = std::make_unique<Annotation::Category>();
    cat->id = "memory_latency";
    cat->display_name = "Memory Latency";
    cat->row_count = 1;

    const QColor issueColor = Config::IssueColor();
    const QColor stallColor = Config::StallColor();

    for (int i = 0; i < kNumCounters; ++i)
    {
        // Mean-bar colour: VMEM light orange, LDS dark orange (matches prior UX).
        const QColor meanColor = (static_cast<LatencyAnalysis::CounterType>(i) == LatencyAnalysis::CounterType::LDS)
                                   ? QColor(0xff, 0x70, 0x00)
                                   : QColor(0xff, 0xc4, 0x32);

        for (const auto& [codeIndex, data] : m_results[i])
        {
            const auto& s = data.second;
            if (s.count <= 0) continue;
            auto it = ASMCodeline::line_map.find(codeIndex);
            if (it == ASMCodeline::line_map.end() || !it->second) continue;

            const int idx = it->second->line_index;
            Annotation::LineData ld;
            // Draw order: Issue, Mean, Stall, ± StdDev whisker.
            ld.rows[0] = {
                {s.meanIssue, issueColor, Annotation::Component::Kind::Segment},
                {s.mean,      meanColor,  Annotation::Component::Kind::Segment},
                {s.meanStall, stallColor, Annotation::Component::Kind::Segment},
                {s.stdDev,    meanColor,  Annotation::Component::Kind::Whisker},
            };

            std::ostringstream ss;
            ss << "<b>Average memory Latency:</b><br><table>"
               << "<tr><td>Exec to data:</td><td>" << std::fixed << std::setprecision(1) << s.mean
               << " cycles</td></tr>"
               << "<tr><td>Issue to Exec:</td><td>" << s.meanIssue << " cycles</td></tr>"
               << "<tr><td>Stall to Issue:</td><td>" << s.meanStall << " cycles</td></tr>"
               << "<tr><td>StdDev:</td><td>" << s.stdDev << "</td></tr>"
               << "<tr><td>Error:</td><td>" << s.error << "</td></tr>"
               << "<tr><td>Samples:</td><td>" << s.count << "</td></tr>"
               << "</table>";
            ld.tooltip = QString::fromStdString(ss.str());

            cat->per_line[idx] = std::move(ld);
        }
    }

    if (cat->per_line.empty())
    {
        Annotation::Registry::instance().clear("memory_latency");
        if (QCodelist::singleton) QCodelist::singleton->refreshAnnotations();
    }
    else
    {
        Annotation::Registry::instance().publish(std::move(cat));
        if (QCodelist::singleton)
        {
            QCodelist::singleton->refreshAnnotations();
            QCodelist::singleton->selectAnnotation("memory_latency");
        }
    }

    // Update status to indicate Memory Latency view is now available
    m_statusLabel->setText("<span style='color: #00ff00; font-weight: bold;'>Analysis complete! "
                           "Memory Latency view has been enabled in the dropdown.</span>");

    // Display results for each counter type
    for (int i = 0; i < kNumCounters; ++i)
    {
        QString resultsStr;

        if (m_results[i].empty())
        {
            resultsStr = "Counter not available in data";
            m_resultsText[i]->setText(resultsStr);
            continue;
        }

        resultsStr += QString("Found %1 instructions:\n\n").arg(m_results[i].size());

        // Sort by mean latency (descending)
        std::vector<std::tuple<int, std::string, LatencyAnalysis::LatencyStats>> sorted;
        for (const auto& [idx, data] : m_results[i]) sorted.emplace_back(idx, data.first, data.second);

        std::sort(
            sorted.begin(),
            sorted.end(),
            [](const auto& a, const auto& b) { return std::get<2>(a).mean > std::get<2>(b).mean; }
        );

        for (const auto& [idx, code, stats] : sorted)
        {
            resultsStr += QString("[%1] %2\n").arg(idx).arg(code.c_str());
            resultsStr +=
                QString("    Mean: %1 cycles, StdDev: %2\n").arg(stats.mean, 0, 'f', 1).arg(stats.stdDev, 0, 'f', 1);
            resultsStr += QString("    Error: %1, Samples: %2\n\n").arg(stats.error, 0, 'f', 1).arg(stats.count);
        }

        m_resultsText[i]->setText(resultsStr);
    }

    // Enable close button
    auto* closeButton = findChild<QPushButton*>();
    if (closeButton) closeButton->setEnabled(true);
}
