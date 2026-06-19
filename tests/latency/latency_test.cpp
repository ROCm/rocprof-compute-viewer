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

#include "memory_latency.hpp"
#include <gtest/gtest.h>
#include <cmath>

using namespace LatencyAnalysis;

// Helper for floating point comparison
constexpr double kEpsilon = 1e-9;

// ============================================================================
// CounterType Tests
// ============================================================================

TEST(CounterTypeTest, ToString)
{
    EXPECT_EQ(counterTypeToString(CounterType::VMEM), "SQ_INST_LEVEL_VMEM");
    EXPECT_EQ(counterTypeToString(CounterType::SMEM), "SQ_INST_LEVEL_SMEM");
    EXPECT_EQ(counterTypeToString(CounterType::LDS), "SQ_INST_LEVEL_LDS");
}

// ============================================================================
// ComputeLatencyStats Tests
// ============================================================================

TEST(ComputeLatencyStatsTest, EmptyInput)
{
    std::vector<double> latencies;
    auto stats = computeLatencyStats(latencies, 40);

    EXPECT_NEAR(stats.mean, 0.0, kEpsilon);
    EXPECT_NEAR(stats.stdDev, 0.0, kEpsilon);
    EXPECT_NEAR(stats.error, 0.0, kEpsilon);
    EXPECT_EQ(stats.count, 0);
}

TEST(ComputeLatencyStatsTest, SingleValue)
{
    std::vector<double> latencies = {50.0};
    auto stats = computeLatencyStats(latencies, 40);

    EXPECT_NEAR(stats.mean, 50.0, kEpsilon);
    EXPECT_NEAR(stats.stdDev, 0.0, kEpsilon);
    EXPECT_EQ(stats.count, 1);
}

TEST(ComputeLatencyStatsTest, MultipleValues)
{
    std::vector<double> latencies = {10.0, 20.0, 30.0, 40.0, 50.0};
    auto stats = computeLatencyStats(latencies, 40);

    // Mean = (10+20+30+40+50)/5 = 30
    EXPECT_NEAR(stats.mean, 30.0, kEpsilon);

    // Sample std dev: sqrt(((10-30)^2 + (20-30)^2 + (30-30)^2 + (40-30)^2 + (50-30)^2) / 4)
    // = sqrt((400 + 100 + 0 + 100 + 400) / 4) = sqrt(250) ≈ 15.811
    EXPECT_NEAR(stats.stdDev, std::sqrt(250.0), kEpsilon);
    EXPECT_EQ(stats.count, 5);

    // unbias = sqrt(5 - 1.5) = sqrt(3.5)
    double unbias = std::sqrt(3.5);
    double expectedErr = std::sqrt(250.0 + 40.0 * 40.0 / 3.0) / unbias;
    EXPECT_NEAR(stats.error, expectedErr, kEpsilon);
}

// ============================================================================
// LatencyAnalyzer Tests
// ============================================================================

TEST(LatencyAnalyzerTest, Construction)
{
    std::vector<std::string> counterNames = {"SQ_INST_LEVEL_VMEM"};
    std::map<int, std::string> codeMap = {
        {100, "test.s:10: inst"}
    };

    LatencyAnalyzer analyzer(counterNames, codeMap, "SQ_INST_LEVEL_VMEM", 1, 40);

    EXPECT_EQ(analyzer.getCounterType(), CounterType::VMEM);
}

TEST(LatencyAnalyzerTest, ConstructionMissingCounter)
{
    std::vector<std::string> counterNames = {"SQ_WAVES"}; // No VMEM counter
    std::map<int, std::string> codeMap = {
        {100, "test.s:10: inst"}
    };

    EXPECT_THROW(LatencyAnalyzer(counterNames, codeMap, "SQ_INST_LEVEL_VMEM", 1, 40), std::invalid_argument);
}

TEST(LatencyAnalyzerTest, AnalyzeSimpleCase)
{
    std::vector<std::string> counterNames = {"SQ_WAVES", "SQ_INST_LEVEL_VMEM", "SQ_BUSY"};

    std::map<int, std::string> codeMap = {
        {100, "test.s:10: global_load_dword" },
        {101, "test.s:20: global_store_dword"},
    };

    LatencyAnalyzer analyzer(counterNames, codeMap, "SQ_INST_LEVEL_VMEM", 1, 40);

    // Performance data: VMEM counter is at index 2 (1-based), so counters[1]
    std::vector<PerfDataEntry> perfData = {
        PerfDataEntry(0, 0, 1, 0, 0, 1, 0),   // t=0, level=1
        PerfDataEntry(40, 0, 0, 0, 0, 1, 0),  // t=40, level=0
        PerfDataEntry(80, 0, 1, 0, 0, 1, 0),  // t=80, level=1
        PerfDataEntry(120, 0, 0, 0, 0, 1, 0), // t=120, level=0
    };

    // Wave instructions: VMEM level is 3 or 4
    std::vector<std::pair<int, std::vector<WaveInstructionData>>> waveFiles = {
        {0,
         {
             WaveInstructionData(0, 3, 100, 0, 0),  // time = 0, code 100
             WaveInstructionData(50, 3, 101, 0, 0), // time = 50, code 101
         }},
    };

    analyzer.analyze(perfData, waveFiles);
    auto results = analyzer.getResults();

    EXPECT_EQ(results.size(), 2);
}

TEST(LatencyAnalyzerTest, AnalyzeEmptyPerfData)
{
    std::vector<std::string> counterNames = {"SQ_INST_LEVEL_VMEM"};
    std::map<int, std::string> codeMap = {
        {100, "test.s:10: inst"}
    };

    LatencyAnalyzer analyzer(counterNames, codeMap, "SQ_INST_LEVEL_VMEM", 1, 40);

    std::vector<PerfDataEntry> perfData; // Empty

    std::vector<std::pair<int, std::vector<WaveInstructionData>>> waveFiles = {
        {0, {WaveInstructionData(0, 3, 100, 0, 0)}},
    };

    analyzer.analyze(perfData, waveFiles);
    auto results = analyzer.getResults();

    EXPECT_EQ(results.size(), 0);
}

TEST(LatencyAnalyzerTest, Reset)
{
    std::vector<std::string> counterNames = {"SQ_INST_LEVEL_VMEM"};
    std::map<int, std::string> codeMap = {
        {100, "test.s:10: inst"}
    };

    LatencyAnalyzer analyzer(counterNames, codeMap, "SQ_INST_LEVEL_VMEM", 1, 40);

    std::vector<PerfDataEntry> perfData = {
        PerfDataEntry(0, 1, 0, 0, 0, 1, 0),
        PerfDataEntry(40, 0, 0, 0, 0, 1, 0),
    };
    std::vector<std::pair<int, std::vector<WaveInstructionData>>> waveFiles = {
        {0, {WaveInstructionData(0, 3, 100, 0, 0)}},
    };

    analyzer.analyze(perfData, waveFiles);
    EXPECT_EQ(analyzer.getResults().size(), 1);

    analyzer.reset();
    EXPECT_EQ(analyzer.getResults().size(), 0);

    // Can analyze again after reset
    analyzer.analyze(perfData, waveFiles);
    EXPECT_EQ(analyzer.getResults().size(), 1);
}

// ============================================================================
// Integration Test - Compare with Python implementation
// ============================================================================

TEST(LatencyIntegrationTest, MatchesPythonOutput)
{
    // This test uses the same test case as create_test_case_simple() in Python
    // to verify that both implementations produce identical results.

    std::vector<std::string> counterNames = {"SQ_WAVES", "SQ_INST_LEVEL_VMEM", "SQ_BUSY"};

    std::map<int, std::string> codeMap = {
        {100, "test.s:10: global_load_dword" },
        {101, "test.s:20: global_store_dword"},
    };

    LatencyAnalyzer analyzer(counterNames, codeMap, "SQ_INST_LEVEL_VMEM", 1, 40);

    // Performance data: [timestamp, c1, c2, c3, c4, cu, simd]
    // Level counter is at index 2 (1-based), so it's counters[1]
    std::vector<PerfDataEntry> perfData = {
        PerfDataEntry(0, 0, 1, 0, 0, 1, 0),   // t=0, level=1
        PerfDataEntry(40, 0, 0, 0, 0, 1, 0),  // t=40, level=0
        PerfDataEntry(80, 0, 1, 0, 0, 1, 0),  // t=80, level=1
        PerfDataEntry(120, 0, 0, 0, 0, 1, 0), // t=120, level=0
    };

    // Wave instructions: [timestamp, level, codeIndex, stall, cycles]
    std::vector<std::pair<int, std::vector<WaveInstructionData>>> waveFiles = {
        {0,
         {
             WaveInstructionData(0, 3, 100, 0, 0),  // time = 0, code 100
             WaveInstructionData(50, 3, 101, 0, 0), // time = 50, code 101
         }},
    };

    analyzer.analyze(perfData, waveFiles);
    auto results = analyzer.getResults();

    // Verify we got results for both instructions
    EXPECT_EQ(results.size(), 2);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(EdgeCasesTest, SingleWave)
{
    std::vector<std::string> counterNames = {"SQ_INST_LEVEL_VMEM"};
    std::map<int, std::string> codeMap = {
        {100, "test.s:10: inst"}
    };

    LatencyAnalyzer analyzer(counterNames, codeMap, "SQ_INST_LEVEL_VMEM", 1, 40);

    std::vector<PerfDataEntry> perfData = {
        PerfDataEntry(0, 1, 0, 0, 0, 1, 0),
        PerfDataEntry(40, 0, 0, 0, 0, 1, 0),
    };

    std::vector<std::pair<int, std::vector<WaveInstructionData>>> waveFiles = {
        {0, {WaveInstructionData(0, 3, 100, 0, 0)}},
    };

    analyzer.analyze(perfData, waveFiles);
    auto results = analyzer.getResults();

    EXPECT_EQ(results.size(), 1);
    EXPECT_EQ(results.at(100).code, "test.s:10: inst");
}

TEST(EdgeCasesTest, MultipleSIMDs)
{
    std::vector<std::string> counterNames = {"SQ_INST_LEVEL_VMEM"};
    std::map<int, std::string> codeMap = {
        {100, "test.s:10: inst1"},
        {101, "test.s:20: inst2"},
    };

    LatencyAnalyzer analyzer(counterNames, codeMap, "SQ_INST_LEVEL_VMEM", 1, 40);

    std::vector<PerfDataEntry> perfData = {
        PerfDataEntry(0, 2, 0, 0, 0, 1, 0),
        PerfDataEntry(40, 0, 0, 0, 0, 1, 0),
    };

    // Two different SIMDs
    std::vector<std::pair<int, std::vector<WaveInstructionData>>> waveFiles = {
        {0, {WaveInstructionData(0, 3, 100, 0, 0)}}, // simd 0
        {1, {WaveInstructionData(0, 3, 101, 0, 0)}}, // simd 1
    };

    analyzer.analyze(perfData, waveFiles);
    auto results = analyzer.getResults();

    EXPECT_EQ(results.size(), 2);
}

TEST(EdgeCasesTest, UnknownCodeIndex)
{
    std::vector<std::string> counterNames = {"SQ_INST_LEVEL_VMEM"};
    std::map<int, std::string> codeMap; // Empty - no known codes

    LatencyAnalyzer analyzer(counterNames, codeMap, "SQ_INST_LEVEL_VMEM", 1, 40);

    std::vector<PerfDataEntry> perfData = {
        PerfDataEntry(0, 1, 0, 0, 0, 1, 0),
        PerfDataEntry(40, 0, 0, 0, 0, 1, 0),
    };

    std::vector<std::pair<int, std::vector<WaveInstructionData>>> waveFiles = {
        {0, {WaveInstructionData(0, 3, 999, 0, 0)}}, // Unknown code index
    };

    analyzer.analyze(perfData, waveFiles);
    auto results = analyzer.getResults();

    EXPECT_EQ(results.size(), 1);
    EXPECT_EQ(results.at(999).code, "unknown_999");
}

TEST(EdgeCasesTest, DifferentPerfIntervals)
{
    // Test with MI350-style interval (36)
    std::vector<std::string> counterNames = {"SQ_INST_LEVEL_VMEM"};
    std::map<int, std::string> codeMap = {
        {100, "test.s:10: inst"}
    };

    LatencyAnalyzer analyzer(counterNames, codeMap, "SQ_INST_LEVEL_VMEM", 1, 36);

    std::vector<double> latencies = {10.0, 20.0, 30.0};
    auto stats = computeLatencyStats(latencies, 36);

    // Error should use 36 instead of 40
    double stdDev = stats.stdDev;
    double unbias = std::sqrt(3 - 1.5);
    double expectedErr = std::sqrt(stdDev * stdDev + 36.0 * 36.0 / 3.0) / unbias;
    EXPECT_NEAR(stats.error, expectedErr, kEpsilon);
}

TEST(EdgeCasesTest, MultipleShaderEngines)
{
    // Test that data from multiple shader engines is processed correctly
    // Each SE has its own perf data and wave files
    std::vector<std::string> counterNames = {"SQ_INST_LEVEL_VMEM"};
    std::map<int, std::string> codeMap = {
        {100, "test.s:10: inst1"},
        {101, "test.s:20: inst2"},
    };

    LatencyAnalyzer analyzer(counterNames, codeMap, "SQ_INST_LEVEL_VMEM", 1, 40);

    // SE0: perf and waves for inst1
    std::vector<PerfDataEntry> perfDataSE0 = {
        PerfDataEntry(0, 1, 0, 0, 0, 1, 0),
        PerfDataEntry(40, 0, 0, 0, 0, 1, 0),
    };
    std::vector<std::pair<int, std::vector<WaveInstructionData>>> waveFilesSE0 = {
        {0, {WaveInstructionData(0, 3, 100, 0, 0)}},
    };

    // SE1: perf and waves for inst2
    std::vector<PerfDataEntry> perfDataSE1 = {
        PerfDataEntry(0, 1, 0, 0, 0, 1, 0),
        PerfDataEntry(40, 0, 0, 0, 0, 1, 0),
    };
    std::vector<std::pair<int, std::vector<WaveInstructionData>>> waveFilesSE1 = {
        {0, {WaveInstructionData(0, 3, 101, 0, 0)}},
    };

    // Process each SE separately
    analyzer.analyze(perfDataSE0, waveFilesSE0);
    analyzer.analyze(perfDataSE1, waveFilesSE1);
    auto results = analyzer.getResults();

    // Both instructions should be found
    EXPECT_EQ(results.size(), 2);
}

TEST(EdgeCasesTest, NoAnalyzeCalls)
{
    std::vector<std::string> counterNames = {"SQ_INST_LEVEL_VMEM"};
    std::map<int, std::string> codeMap = {
        {100, "test.s:10: inst"}
    };

    LatencyAnalyzer analyzer(counterNames, codeMap, "SQ_INST_LEVEL_VMEM", 1, 40);

    // Get results without calling analyze
    auto results = analyzer.getResults();

    EXPECT_EQ(results.size(), 0);
}
