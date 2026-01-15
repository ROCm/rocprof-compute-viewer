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

#include "latency.hpp"

#include "../json/include/nlohmann/json.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <numeric>
#include <unordered_map>

constexpr size_t MAX_THREADS = 8;

namespace LatencyAnalysis
{

// Internal structs used only within this compilation unit
namespace
{

/**
 * @brief A single wave instruction with timing information
 */
struct WaveInstruction
{
    int64_t time{0};  ///< Computed time value
    int simd{0};      ///< SIMD unit number
    int codeIndex{0}; ///< Index into the code map

    WaveInstruction() = default;
    WaveInstruction(int s, int64_t t, int c) : time(t), simd(s), codeIndex(c) {}

    bool operator<(const WaveInstruction& other) const { return time < other.time; }
};

/**
 * @brief A performance counter sample
 */
struct PerfSample
{
    int64_t timestamp{0}; ///< Sample timestamp
    int levelCount{0};    ///< Level counter value

    PerfSample() = default;
    PerfSample(int64_t t, int l) : timestamp(t), levelCount(l) {}

    bool operator<(const PerfSample& other) const { return timestamp < other.timestamp; }
};

} // anonymous namespace

CounterType counterTypeFromString(const std::string& name)
{
    static const std::unordered_map<std::string, CounterType> map = {
        {"SQ_INST_LEVEL_VMEM", CounterType::VMEM},
        {              "VMEM", CounterType::VMEM},
        {"SQ_INST_LEVEL_SMEM", CounterType::SMEM},
        {              "SMEM", CounterType::SMEM},
        { "SQ_INST_LEVEL_LDS",  CounterType::LDS},
        {               "LDS",  CounterType::LDS},
    };
    return map.at(name);
}

std::string counterTypeToString(CounterType type)
{
    static const std::unordered_map<CounterType, std::string> map = {
        {CounterType::VMEM, "SQ_INST_LEVEL_VMEM"},
        {CounterType::SMEM, "SQ_INST_LEVEL_SMEM"},
        { CounterType::LDS,  "SQ_INST_LEVEL_LDS"},
    };
    return map.at(type);
}

bool isLevelCounter(CounterType type, int level)
{
    switch (type)
    {
        case CounterType::VMEM: return level == 3 || level == 4;
        case CounterType::LDS: return level == 5;
        case CounterType::SMEM: return level == 1;
        default: return false;
    }
}

int findCounterIndex(const std::vector<std::string>& counterNames, const std::string& targetCounter)
{
    for (size_t idx = 0; idx < counterNames.size(); ++idx)
        if (counterNames[idx] == targetCounter) return static_cast<int>(idx) + 1; // 1-based index
    return -1;
}

std::vector<PerfDataEntry> filterPerfData(const std::vector<PerfDataEntry>& perfData, int targetCu, int bankFilter = 0)
{
    std::vector<PerfDataEntry> result;
    result.reserve(perfData.size());

    for (const auto& p : perfData)
        if (p.cu == targetCu && p.bank == bankFilter) result.push_back(p);
    return result;
}

void fillPerfGaps(std::vector<PerfDataEntry>& perfData, int perfInterval)
{
    if (perfData.empty()) return;

    // First, collect entries to add
    std::vector<PerfDataEntry> toAdd;

    for (size_t k = 1; k < perfData.size(); ++k)
        if (perfData[k].timestamp - perfData[k - 1].timestamp > perfInterval)
        {
            PerfDataEntry newEntry;
            newEntry.timestamp = perfData[k - 1].timestamp + perfInterval;
            // counters are already zero-initialized
            toAdd.push_back(newEntry);
        }

    // Append new entries
    perfData.insert(perfData.end(), toAdd.begin(), toAdd.end());
}

void sortAndAppendFinal(std::vector<PerfDataEntry>& perfData, int perfInterval)
{
    if (perfData.empty()) return;

    // Sort by timestamp
    std::sort(
        perfData.begin(),
        perfData.end(),
        [](const PerfDataEntry& a, const PerfDataEntry& b) { return a.timestamp < b.timestamp; }
    );

    // Append final entry
    PerfDataEntry finalEntry;
    finalEntry.timestamp = perfData.back().timestamp + perfInterval;
    perfData.push_back(finalEntry);
}

std::vector<WaveInstruction> extractWaveInstructions(
    const std::vector<WaveInstructionData>& waveData, int simd, CounterType type
)
{
    std::vector<WaveInstruction> result;
    result.reserve(waveData.size());

    for (const auto& w : waveData)
        if (isLevelCounter(type, w.level))
        {
            // issue time = timestamp + stall
            // We need a consistent cross-simd timing. Any order works, easiest is to add the simd id.
            int64_t time = w.timestamp + w.stall + simd;
            result.emplace_back(simd, time, w.codeIndex);
        }
    return result;
}

std::map<int, std::vector<double>> computeLatencies(
    std::vector<WaveInstruction>& waves, const std::vector<PerfSample>& perfSamples, int perfInterval
)
{
    std::map<int, std::vector<double>> latencies;

    if (waves.empty() || perfSamples.empty()) return latencies;

    // Extract timestamps and active counts
    std::vector<int64_t> perfTime;
    std::vector<int> active;
    perfTime.reserve(perfSamples.size());
    active.reserve(perfSamples.size());

    for (const auto& p : perfSamples)
    {
        perfTime.push_back(p.timestamp);
        active.push_back(p.levelCount);
    }

    std::vector<int> completed(perfSamples.size(), 0);

    size_t piter = 0;
    size_t it = 0;

    for (size_t N = 0; N < waves.size(); ++N)
    {
        const auto& w = waves[N];

        while (piter < perfSamples.size() && perfTime[piter] < w.time)
        {
            completed[piter] = static_cast<int>(N) - active[piter]; // completed = started - level

            while (it < waves.size() && static_cast<int>(it) < completed[piter])
            {
                double delta = static_cast<double>(perfTime[piter] - waves[it].time);

                //  TODO: This isn't quite right.
                // Latencies are not evenly distributed at the sample interval. Can we use e.g. poisson?
                if (delta > perfInterval)
                    delta -= static_cast<double>(perfInterval) / 2.0; // Correct for counter sample delay
                else
                    delta /= 2.0; // If we are in first sample, estimate ~ 1/2 actual

                int codeIdx = waves[it].codeIndex;
                latencies[codeIdx].push_back(delta);
                ++it;
            }
            ++piter;
        }
    }

    // Process remaining samples
    while (piter < perfSamples.size())
    {
        completed[piter] = static_cast<int>(waves.size()) - active[piter];
        while (it < waves.size() && static_cast<int>(it) < completed[piter]) ++it;
        ++piter;
    }

    return latencies;
}

LatencyStats computeLatencyStats(const std::vector<double>& latencies, int perfInterval)
{
    if (latencies.empty()) return LatencyStats();

    int count = static_cast<int>(latencies.size());
    double mean = std::accumulate(latencies.begin(), latencies.end(), 0.0) / count;

    double stdDev = 0.0;
    if (count > 1)
    {
        double variance = 0.0;
        for (double lat : latencies) variance += (lat - mean) * (lat - mean);
        variance /= (count - 1); // Sample variance (n-1)
        stdDev = std::sqrt(variance);
    }

    // Unbias factor
    double unbias = 0.1;
    if (count > 1) unbias = std::sqrt(static_cast<double>(count) - 1.5);

    // Error estimation combining std dev and quantization error
    double perfIntervalD = static_cast<double>(perfInterval);
    double error = std::sqrt(stdDev * stdDev + (perfIntervalD * perfIntervalD) / 3.0) / unbias;

    return LatencyStats(mean, stdDev, error, count);
}

// ============================================================================
// LatencyAnalyzer implementation
// ============================================================================

LatencyAnalyzer::LatencyAnalyzer(
    const std::vector<std::string>& counterNames,
    const std::map<int, std::string>& codeMap,
    const std::string& counterName,
    int targetCu,
    int perfInterval
) :
m_counterNames(counterNames),
m_codeMap(codeMap),
m_counterType(counterTypeFromString(counterName)),
m_targetCu(targetCu),
m_perfInterval(perfInterval)
{
    // Cache the level counter index
    std::string counterStr = counterTypeToString(m_counterType);
    m_levelIndex = findCounterIndex(m_counterNames, counterStr);
    if (m_levelIndex == -1) throw std::invalid_argument("Counter " + counterStr + " not found in counter names");
}

void LatencyAnalyzer::analyze(
    const std::vector<PerfDataEntry>& perfData,
    const std::vector<std::pair<int, std::vector<WaveInstructionData>>>& waveFiles
)
{
    // Filter and prepare performance data for this SE
    std::vector<PerfDataEntry> filtered = filterPerfData(perfData, m_targetCu);
    if (filtered.empty()) return;

    fillPerfGaps(filtered, m_perfInterval);
    sortAndAppendFinal(filtered, m_perfInterval);

    // Convert to PerfSample objects
    std::vector<PerfSample> perfSamples;
    perfSamples.reserve(filtered.size());
    for (const auto& p : filtered)
    {
        // levelIdx is 1-based, counters array is 0-based
        int levelCount = (m_levelIndex >= 1 && m_levelIndex <= 4) ? p.counters[m_levelIndex - 1] : 0;
        perfSamples.emplace_back(p.timestamp, levelCount);
    }

    // Extract wave instructions from this SE's wave files
    std::vector<WaveInstruction> waves;
    for (const auto& [simd, waveData] : waveFiles)
    {
        auto extracted = extractWaveInstructions(waveData, simd, m_counterType);
        waves.insert(waves.end(), extracted.begin(), extracted.end());
    }

    // Sort by time
    std::sort(waves.begin(), waves.end());

    // Build code index mapping (accumulates across SEs)
    for (auto& wave : waves)
    {
        if (m_usedIndex.find(wave.codeIndex) == m_usedIndex.end())
        {
            m_usedIndex.insert(wave.codeIndex);

            auto it = m_codeMap.find(wave.codeIndex);
            if (it != m_codeMap.end())
                m_insts[wave.codeIndex] = InstructionLatency(it->second);
            else
                m_insts[wave.codeIndex] = InstructionLatency("unknown_" + std::to_string(wave.codeIndex));
        }
    }

    // Accumulate issue and stall for matching instructions (after code index mapping is built)
    for (const auto& [simd, waveData] : waveFiles)
    {
        for (const auto& w : waveData)
        {
            if (isLevelCounter(m_counterType, w.level))
            {
                // issue = cycles - stall (duration - stall)
                int issue = std::max(0, w.cycles - w.stall);
                m_insts[w.codeIndex].totalIssue += issue;
                m_insts[w.codeIndex].totalStall += w.stall;
            }
        }
    }

    // Compute latencies for this SE
    auto latencyMap = computeLatencies(waves, perfSamples, m_perfInterval);

    // Append latencies to instructions (accumulates across SEs)
    for (const auto& [origIdx, latList] : latencyMap)
        m_insts[origIdx].latencies.insert(m_insts[origIdx].latencies.end(), latList.begin(), latList.end());
}

std::map<int, InstructionLatency> LatencyAnalyzer::getResults() const { return m_insts; }

void LatencyAnalyzer::reset()
{
    m_usedIndex.clear();
    m_insts.clear();
}

// File-local helper function
static std::string getSourceKey(const std::string& code)
{
    // Extract source key from code string: "source.s:123: instruction" -> "source.s:123"
    size_t firstColon = code.find(':');
    if (firstColon == std::string::npos) return code;

    size_t secondColon = code.find(':', firstColon + 1);
    if (secondColon == std::string::npos) return code;

    return code.substr(0, secondColon);
}

std::vector<PerfDataEntry> LatencyAnalyzer::loadPerfData(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) throw std::runtime_error("Failed to open: " + path);

    nlohmann::json perfJson;
    file >> perfJson;

    std::vector<PerfDataEntry> entries;
    for (const auto& row : perfJson["data"])
    {
        PerfDataEntry entry;
        entry.timestamp = row[0].get<int64_t>();
        entry.counters[0] = row[1].get<int>();
        entry.counters[1] = row[2].get<int>();
        entry.counters[2] = row[3].get<int>();
        entry.counters[3] = row[4].get<int>();
        entry.cu = row[5].get<int>();
        entry.bank = row[6].get<int>();
        entries.push_back(entry);
    }

    return entries;
}

// File-local helper function for loading a single wave file
static std::pair<int, std::vector<WaveInstructionData>> loadWaveFile(const std::string& path, int simd)
{
    std::ifstream file(path);
    if (!file.is_open()) throw std::runtime_error("Failed to open: " + path);

    nlohmann::json waveJson;
    file >> waveJson;

    std::vector<WaveInstructionData> instructions;
    for (const auto& instr : waveJson["wave"]["instructions"])
    {
        WaveInstructionData data;
        data.timestamp = instr[0].get<int64_t>();
        data.level = instr[1].get<int>();
        data.stall = instr[2].get<int>();  // inst[2] is stall
        data.cycles = instr[3].get<int>(); // inst[3] is total cycles (duration)
        data.codeIndex = instr[4].get<int>();
        instructions.push_back(data);
    }

    return {simd, std::move(instructions)};
}

bool LatencyAnalyzer::hasCounter(const std::vector<std::string>& counterNames, CounterType type)
{
    return findCounterIndex(counterNames, counterTypeToString(type)) != -1;
}

std::vector<std::pair<int, std::vector<WaveInstructionData>>> LatencyAnalyzer::loadWaveFiles(
    const std::vector<std::pair<std::string, int>>& waveFiles, std::atomic<int>* progress
)
{
    std::vector<std::pair<int, std::vector<WaveInstructionData>>> waveData(waveFiles.size());

    std::vector<std::pair<size_t, std::future<std::pair<int, std::vector<WaveInstructionData>>>>> active;
    active.reserve(MAX_THREADS);

    size_t nextToLaunch = 0;

    // Launch initial batch
    while (nextToLaunch < waveFiles.size() && active.size() < MAX_THREADS)
    {
        size_t idx = nextToLaunch++;
        const auto& [path, simd] = waveFiles[idx];
        active.emplace_back(idx, std::async(std::launch::async, loadWaveFile, path, simd));
    }

    // Process until all done
    while (!active.empty())
    {
        for (auto it = active.begin(); it != active.end(); ++it)
        {
            if (it->second.wait_for(std::chrono::microseconds(100)) == std::future_status::ready)
            {
                waveData[it->first] = it->second.get();
                if (progress) ++(*progress);
                active.erase(it);

                if (nextToLaunch < waveFiles.size())
                {
                    size_t idx = nextToLaunch++;
                    const auto& [path, simd] = waveFiles[idx];
                    active.emplace_back(idx, std::async(std::launch::async, loadWaveFile, path, simd));
                }
                break;
            }
        }
    }

    return waveData;
}

void LatencyAnalyzer::analyzeFiles(
    const std::string& perfFile, const std::vector<std::pair<std::string, int>>& waveFiles, std::atomic<int>* progress
)
{
    // Load perf data (single file)
    auto perfFuture = std::async(std::launch::async, loadPerfData, perfFile);

    // Load wave files in parallel
    auto waveData = loadWaveFiles(waveFiles, progress);

    analyze(perfFuture.get(), waveData);
}

std::vector<std::pair<std::string, int>> LatencyAnalyzer::collectWaveFilePaths(const std::string& uidir, int se)
{
    namespace fs = std::filesystem;
    std::vector<std::pair<std::string, int>> files;
    std::string pattern = "se" + std::to_string(se) + "_sm";

    for (const auto& entry : fs::directory_iterator(uidir))
    {
        if (!entry.is_regular_file()) continue;

        std::string filename = entry.path().filename().string();
        if (filename.find(pattern) != 0) continue;
        if (filename.find(".json") == std::string::npos) continue;

        auto smPos = filename.find("_sm");
        std::string rest = filename.substr(smPos + 3);
        int simd = std::stoi(rest.substr(0, 1));

        files.emplace_back(entry.path().string(), simd);
    }

    return files;
}

} // namespace LatencyAnalysis
