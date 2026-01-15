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

#pragma once

#include "../analysis/latency.hpp"

#include <QDialog>
#include <array>
#include <atomic>
#include <future>
#include <map>
#include <string>

class QProgressBar;
class QLabel;
class QTextEdit;
class QTimer;

class LatencyAnalysisDialog : public QDialog
{
    Q_OBJECT

public:
    static constexpr int kNumCounters = static_cast<int>(LatencyAnalysis::CounterType::COUNT);

    explicit LatencyAnalysisDialog(const std::string& uidir, QWidget* parent = nullptr);
    ~LatencyAnalysisDialog();

private slots:
    void updateProgress();
    void analysisFinished();

private:
    void startAnalysis();

    std::string m_uidir;
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;
    QLabel* m_disclaimerLabel;
    std::array<QTextEdit*, kNumCounters> m_resultsText;
    QTimer* m_timer;

    std::atomic<int> m_progress{0};
    int m_totalFiles{0};
    std::future<void> m_analysisFuture;

    // Results stored after analysis: [counterType][codeIndex] -> (code, stats)
    std::array<std::map<int, std::pair<std::string, LatencyAnalysis::LatencyStats>>, kNumCounters> m_results;
};
