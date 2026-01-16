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

#include "../../src/json/include/nlohmann/json.hpp"
#include "latency.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using json = nlohmann::json;

void printUsage(const char* program)
{
    std::cerr << "Usage: " << program << " <uidir> [options]\n"
              << "Options:\n"
              << "  --counter, -c <name>       Counter name (default: SQ_INST_LEVEL_VMEM)\n"
              << "                             Options: SQ_INST_LEVEL_VMEM, SQ_INST_LEVEL_SMEM, SQ_INST_LEVEL_LDS\n"
              << "  --target-cu, -t <cu>       Target CU (default: 1)\n"
              << "  --perf-interval, -p <n>    Performance interval (default: 40)\n"
              << "  --shader-engines, -s <n,m> Shader engines, comma-separated (default: 0)\n"
              << "  --output, -o <file>        Output JSON file\n"
              << "  --help, -h                 Show this help\n";
}

std::vector<int> parseShaderEngines(const std::string& arg)
{
    // Strip brackets if present
    std::string cleaned = arg;
    if (!cleaned.empty() && cleaned.front() == '[') cleaned = cleaned.substr(1);
    if (!cleaned.empty() && cleaned.back() == ']') cleaned.pop_back();

    std::vector<int> engines;
    std::stringstream ss(cleaned);
    std::string item;
    while (std::getline(ss, item, ',')) engines.push_back(std::stoi(item));
    return engines;
}

json loadJson(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) throw std::runtime_error("Failed to open: " + path);

    json j;
    file >> j;
    return j;
}

std::map<int, std::string> loadCodeMap(const std::string& uidir)
{
    json codeJson = loadJson(uidir + "/code.json");
    std::map<int, std::string> codeMap;

    for (const auto& entry : codeJson["code"])
    {
        int codeIndex = entry[2].get<int>();
        std::string filename = entry[3].get<std::string>();
        std::string instruction = entry[0].get<std::string>();

        // Extract just the filename from path
        auto pos = filename.rfind('/');
        if (pos != std::string::npos) filename = filename.substr(pos + 1);

        codeMap[codeIndex] = filename + ": " + instruction;
    }

    return codeMap;
}

std::vector<std::string> loadCounterNames(const std::string& uidir)
{
    json filenames = loadJson(uidir + "/filenames.json");
    std::vector<std::string> names;

    for (const auto& name : filenames["counter_names"]) names.push_back(name.get<std::string>());

    return names;
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        printUsage(argv[0]);
        return 1;
    }

    // Default options
    std::string uidir;
    std::string counterName = "SQ_INST_LEVEL_VMEM";
    int targetCu = 1;
    int perfInterval = 40;
    std::vector<int> shaderEngines{0};
    std::string outputFile;

    // Parse arguments
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h")
        {
            printUsage(argv[0]);
            return 0;
        }
        else if (arg == "--counter" || arg == "-c")
        {
            if (++i >= argc)
            {
                std::cerr << "Missing argument for " << arg << "\n";
                return 1;
            }
            counterName = argv[i];
        }
        else if (arg == "--target-cu" || arg == "-t")
        {
            if (++i >= argc)
            {
                std::cerr << "Missing argument for " << arg << "\n";
                return 1;
            }
            targetCu = std::stoi(argv[i]);
        }
        else if (arg == "--perf-interval" || arg == "-p")
        {
            if (++i >= argc)
            {
                std::cerr << "Missing argument for " << arg << "\n";
                return 1;
            }
            perfInterval = std::stoi(argv[i]);
        }
        else if (arg == "--shader-engines" || arg == "-s")
        {
            if (++i >= argc)
            {
                std::cerr << "Missing argument for " << arg << "\n";
                return 1;
            }
            shaderEngines = parseShaderEngines(argv[i]);
        }
        else if (arg == "--output" || arg == "-o")
        {
            if (++i >= argc)
            {
                std::cerr << "Missing argument for " << arg << "\n";
                return 1;
            }
            outputFile = argv[i];
        }
        else if (arg[0] != '-')
            uidir = arg;
        else
        {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    if (uidir.empty())
    {
        std::cerr << "Error: uidir is required\n";
        printUsage(argv[0]);
        return 1;
    }

    try
    {
        // Load shared data
        auto counterNames = loadCounterNames(uidir);
        auto codeMap = loadCodeMap(uidir);

        // Create analyzer
        LatencyAnalysis::LatencyAnalyzer analyzer(counterNames, codeMap, counterName, targetCu, perfInterval);

        // Process each shader engine using analyzeFiles
        for (int se : shaderEngines)
        {
            std::string perfFile = uidir + "/se" + std::to_string(se) + "_perfcounter.json";
            auto waveFilePaths = LatencyAnalysis::LatencyAnalyzer::collectWaveFilePaths(uidir, se);

            if (waveFilePaths.empty())
            {
                std::cerr << "Warning: No wave files for SE " << se << "\n";
                continue;
            }

            analyzer.analyzeFiles(perfFile, waveFilePaths);
            std::cerr << "SE " << se << " analyzed\n";
        }

        // Get results
        auto results = analyzer.getResults();
        std::cerr << "Analysis complete\n";

        // Output results
        if (!outputFile.empty())
        {
            json output;
            output["instructions"] = json::array();

            for (const auto& [codeIndex, instr] : results)
            {
                auto stats = LatencyAnalysis::computeLatencyStats(instr.latencies, perfInterval);
                json instrJson;
                instrJson["codeIndex"] = codeIndex;
                instrJson["code"] = instr.code;
                instrJson["stats"]["mean"] = stats.mean;
                instrJson["stats"]["stddev"] = stats.stdDev;
                instrJson["stats"]["error"] = stats.error;
                instrJson["stats"]["count"] = stats.count;
                output["instructions"].push_back(instrJson);
            }

            std::ofstream outFile(outputFile);
            if (!outFile.is_open()) throw std::runtime_error("Failed to open output file: " + outputFile);

            outFile << output.dump(2);
            std::cout << "Results saved to " << outputFile << "\n";
        }
        else
        {
            // Print to console
            for (const auto& [codeIndex, instr] : results)
            {
                auto stats = LatencyAnalysis::computeLatencyStats(instr.latencies, perfInterval);
                std::cout << "\n\033[94m[" << codeIndex << "] " << instr.code << "\033[00m\n";
                std::cout << "\t Mean latency  : \033[92m" << std::fixed << std::setprecision(1) << stats.mean
                          << "\033[00m  (\033[91m" << stats.error << "\033[00m) +- \033[93m" << stats.stdDev
                          << "\033[00m\n";
            }
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
