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

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace LatencyAnalysis
{

/**
 * @brief Enum for supported performance counter types
 */
enum class CounterType
{
    VMEM,  // SQ_INST_LEVEL_VMEM
    LDS,   // SQ_INST_LEVEL_LDS
    COUNT, // Number of displayed counter types (VMEM, LDS)
    SMEM   // SQ_INST_LEVEL_SMEM (not displayed in UI)
};

/**
 * @brief Convert CounterType to string
 * @param type Counter type
 * @return String representation
 */
std::string counterTypeToString(CounterType type);

/**
 * @brief Statistics for latency measurements
 */
struct LatencyStats
{
    double mean{0};      ///< Mean latency
    double stdDev{0};    ///< Standard deviation
    double error{0};     ///< Error estimate
    int count{0};        ///< Number of measurements
    double meanIssue{0}; ///< Mean issue cycles (duration - stall)
    double meanStall{0}; ///< Mean stall cycles

    LatencyStats() = default;
    LatencyStats(double m, double s, double e, int c) : mean(m), stdDev(s), error(e), count(c) {}
};

/**
 * @brief Latency data for a single instruction
 */
struct InstructionLatency
{
    std::string code;              ///< Code string (e.g., "test.s:10: global_load_dword")
    std::vector<double> latencies; ///< Individual latency measurements
    int64_t totalIssue{0};         ///< Accumulated issue cycles (duration - stall)
    int64_t totalStall{0};         ///< Accumulated stall cycles

    InstructionLatency() = default;
    explicit InstructionLatency(const std::string& c) : code(c) {}
};

/**
 * @brief Raw performance data entry
 *
 * Represents a row from the perfcounter JSON: [timestamp, c1, c2, c3, c4, cu, simd]
 */
struct PerfDataEntry
{
    int64_t timestamp;
    int counters[4]; ///< c1-c4 counter values
    int cu;
    int bank;

    PerfDataEntry() : timestamp(0), counters{0, 0, 0, 0}, cu(0), bank(0) {}
    PerfDataEntry(int64_t ts, int c1, int c2, int c3, int c4, int _cu, int _bank) :
    timestamp(ts), counters{c1, c2, c3, c4}, cu(_cu), bank(_bank)
    {}
};

/**
 * @brief Raw wave instruction data
 *
 * Represents an instruction entry from wave JSON
 */
struct WaveInstructionData
{
    int64_t timestamp{0};
    int level{0};
    int codeIndex{0};
    int stall{0};
    int cycles{0};

    WaveInstructionData() = default;
    WaveInstructionData(int64_t ts, int lv, int ci, int st, int cy) :
    timestamp(ts), level(lv), codeIndex(ci), stall(st), cycles(cy)
    {}
};

/**
 * @brief Compute statistics for a list of latency measurements
 * @param latencies Latency values
 * @param perfInterval Performance counter interval (for error estimation)
 * @return LatencyStats with computed statistics
 */
LatencyStats computeLatencyStats(const std::vector<double>& latencies, int perfInterval);

/**
 * @brief Main latency analyzer class
 *
 * Encapsulates the latency analysis algorithm for easy testing and use.
 * Uses a stateful API to minimize memory usage when processing multiple shader engines:
 * 1. Construct with counter names and code map (shared across all SEs)
 * 2. Call analyze() for each shader engine's data
 * 3. Call getResults() to get final latencies after all SEs are processed
 * 4. Call reset() to clear state for reuse
 */
class LatencyAnalyzer
{
public:
    /**
     * @brief Construct a new Latency Analyzer
     * @param counterNames List of counter names from filenames.json
     * @param codeMap Mapping from code index to code string
     * @param counterName Performance counter name (default: SQ_INST_LEVEL_VMEM)
     * @param targetCu CU to filter by (default: 1)
     * @param perfInterval Sampling interval (default: 40 for MI300)
     */
    explicit LatencyAnalyzer(
        const std::vector<std::string>& counterNames,
        const std::map<int, std::string>& codeMap,
        const std::string& counterName = "SQ_INST_LEVEL_VMEM",
        int targetCu = 1,
        int perfInterval = 40
    );

    /**
     * @brief Process one shader engine's data and accumulate results
     *
     * Call this once per shader engine. Results are accumulated internally.
     *
     * @param perfData Performance counter data for this SE
     * @param waveFiles Wave instruction data for this SE: (simd, wave_data) pairs
     */
    void analyze(
        const std::vector<PerfDataEntry>& perfData,
        const std::vector<std::pair<int, std::vector<WaveInstructionData>>>& waveFiles
    );

    /**
     * @brief Get accumulated results after processing all shader engines
     * @return Map from code index to InstructionLatency with all accumulated latencies
     */
    std::map<int, InstructionLatency> getResults() const;

    /**
     * @brief Clear accumulated state for reuse
     */
    void reset();

    /**
     * @brief Load and analyze from file paths
     *
     * Convenience method that loads perf data and wave files, then runs analysis.
     *
     * @param perfFile Path to perfcounter JSON file
     * @param waveFiles Vector of (path, simd) pairs for wave files
     * @param progress Optional atomic counter incremented after each wave file is loaded
     */
    void analyzeFiles(
        const std::string& perfFile,
        const std::vector<std::pair<std::string, int>>& waveFiles,
        std::atomic<int>* progress = nullptr
    );

    /**
     * @brief Collect wave file paths for a shader engine
     * @param uidir Directory containing the JSON files
     * @param se Shader engine number
     * @return Vector of (path, simd) pairs
     */
    static std::vector<std::pair<std::string, int>> collectWaveFilePaths(const std::string& uidir, int se);

    /**
     * @brief Check if a counter type is available in the counter names
     * @param counterNames List of counter names from filenames.json
     * @param type Counter type to check
     * @return True if the counter is available
     */
    static bool hasCounter(const std::vector<std::string>& counterNames, CounterType type);

    /**
     * @brief Load wave files in parallel
     * @param waveFiles Vector of (path, simd) pairs for wave files
     * @param progress Optional atomic counter incremented after each wave file is loaded
     * @return Vector of (simd, wave_data) pairs
     */
    static std::vector<std::pair<int, std::vector<WaveInstructionData>>> loadWaveFiles(
        const std::vector<std::pair<std::string, int>>& waveFiles, std::atomic<int>* progress = nullptr
    );

    /**
     * @brief Load performance counter data from a JSON file
     * @param path Path to the perfcounter JSON file
     * @return Vector of PerfDataEntry
     */
    static std::vector<PerfDataEntry> loadPerfData(const std::string& path);

    // Getters
    CounterType getCounterType() const { return m_counterType; }
    int getTargetCu() const { return m_targetCu; }
    int getPerfInterval() const { return m_perfInterval; }

private:
    // Configuration (set in constructor, shared across all SEs)
    std::vector<std::string> m_counterNames;
    std::map<int, std::string> m_codeMap;
    CounterType m_counterType;
    int m_targetCu;
    int m_perfInterval;
    int m_levelIndex; ///< Cached counter index

    // Accumulated state across analyze() calls
    std::set<int> m_usedIndex;                 ///< Code indices already seen
    std::map<int, InstructionLatency> m_insts; ///< Accumulated latencies by code index
};

} // namespace LatencyAnalysis
