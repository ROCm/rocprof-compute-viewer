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

#include "wavemanager.h"
#include <QPainter>
#include <set>
#include <shared_mutex>
#include "json/include/nlohmann/json.hpp"
#include "util/version.h"
#include "wave/scroll.h"
#include "wavedata.h"

std::shared_mutex wave_mutex;
std::mutex code_mutex;
std::unordered_map<std::string, std::shared_ptr<WaveInstance>> reader_cache;
std::pair<std::string, std::vector<CodeData>> code_cache;

std::shared_ptr<WaveInstance> WaveInstance::main_wave{nullptr};

std::shared_ptr<WaveInstance> WaveInstance::Get(const std::string& path)
{
    {
        std::shared_lock<std::shared_mutex> lk(wave_mutex);
        if (reader_cache.find(path) != reader_cache.end()) return reader_cache.at(path);
    }

    auto loaded_wave = std::make_shared<WaveInstance>(path);

    std::unique_lock<std::shared_mutex> lk(wave_mutex);
    reader_cache[path] = loaded_wave;
    return loaded_wave;
}

void WaveInstance::InvalidadeCache()
{
    std::unique_lock<std::shared_mutex> lk1(wave_mutex);
    std::unique_lock<std::mutex> lk2(code_mutex);
    reader_cache.clear();
    code_cache.first = "";
    code_cache.second.clear();
}

static int gettype(const std::array<int, 16>& partial_cycles)
{
    int64_t max_cycles = -1;
    int type = 0;

    for (size_t i = 0; i < partial_cycles.size(); i++)
        if (partial_cycles.at(i) >= max_cycles)
        {
            max_cycles = partial_cycles.at(i);
            type = i;
        }

    return type;
}

Token TokenGroup::TokenArray::finalize(int64_t res)
{
    token.type = gettype(cycles);
    token.clock = (token.clock + res / 2) & ~(res - 1);
    token.cycles = (token.cycles + res / 2) & ~(res - 1);
    return token;
}

void TokenGroup::SetMipN(const std::vector<TokenArray>& previous, size_t M)
{
    const int64_t res = (1 << M) * WaveInstance::BaseClock();

    if (M >= token_mip.size()) return;

    if (previous.size() <= 16)
    {
        for (auto current : previous)
        {
            auto token = current.finalize(res);
            if (token.type > 0)
                token_mip.at(M).emplace_back(token);
        }
        token_mip.at(M).Compile();
        return;
    }

    std::vector<TokenArray> next{};

    TokenArray current{};
    int64_t current_cycle = current.token.clock = previous.at(0).token.clock;

    for (auto& prev : previous)
    {
        if (current_cycle + res/2 <= prev.token.clock)
        {
            if (current.token.cycles > 0)
            {
                auto finalized = current.finalize(res);
                next.emplace_back(std::move(current));

                if (finalized.type > 0) token_mip.at(M).emplace_back(finalized);
            }

            current = {};
            current_cycle = current.token.clock = prev.token.clock;
        }
        else if (current_cycle != prev.token.clock)
        {
            // add IDLE time
            current.cycles.at(0) += prev.token.clock - current_cycle;
            current.token.cycles += prev.token.clock - current_cycle;
        }

        current += prev;
        current_cycle = std::max(current_cycle, prev.token.clock + prev.token.cycles);
    }

    auto finalized = current.finalize(res);
    if (finalized.type > 0)
        token_mip.at(M).emplace_back(finalized);
    next.emplace_back(std::move(current));

    token_mip.at(M).Compile();
    SetMipN(next, M + 1);
}

void TokenGroup::SetMipN()
{
    bInitialized = true;
    for (auto& mip : token_mip) mip.clear();

    std::vector<TokenArray> next{};
    next.reserve(tokens.size());

    for (auto& token : tokens) try
        {
            TokenArray newtoken{token, {}};
            newtoken.cycles.at(token.type) = token.cycles;
            next.emplace_back(std::move(newtoken));
        }
        catch (std::out_of_range& e)
        {
            QWARNING(false, "Invalid token type " << token.type, continue);
        }

    SetMipN(next, 0);
}

void TokenGroup::Draw(class QPainter& painter, int64_t viewstart, int64_t viewend)
{
    if (viewstart > wave_end || viewend < wave_begin) return;

    if (!bInitialized) SetMipN();

    int blank_space_thresh = (WaveInstance::BaseClock() << Token::mipmap_level) / 3;

    painter.setPen(QPen(Qt::black, 0.9));

    if (Token::mipmap_level <= 1)
    {
        auto it = timeline.upper_bound(viewstart);
        if (it != timeline.begin()) it = std::prev(it);

        while (it != timeline.end() && it->second.clock + it->second.duration < viewstart) it++;

        while (it != timeline.end() && it->second.clock < viewend)
        {
            it->second.DrawState(painter, viewstart, viewend);
            it++;
        }
    }

    auto pen = painter.pen();
    {
        Token blankToken{};
        blankToken.clock = wave_begin;
        blankToken.cycles = wave_end - wave_begin;
        blankToken.DrawToken(painter, viewstart, viewend, 1.0f);
    }

    // Lower bound would only sometimes put us past this value, while upper bound does it consistently
    auto& selected_mip = (Token::mipmap_level <= 1) ? tokens : token_mip.at(Token::mipmap_level - 2);
    auto it = selected_mip.upper_bound(viewstart);
    while (it != selected_mip.begin())
    {
        it = std::prev(it);
        // Search for the first token outside the view begin range to draw
        if (it->clock + it->cycles <= viewstart && !it->overlapped()) break;
    }

    const float penwidth = 0.5f*std::max(3 - Token::mipmap_level, 1);

    while (it != selected_mip.end() && it->clock < viewend)
    {
        it->DrawToken(painter, viewstart, viewend, penwidth);
        it++;
    }

    painter.setPen(pen);
}

WaveInstance::WaveInstance(const std::string& _path) : path(_path)
{
    JsonRequest json(path);
    nlohmann::json& data = json.data;

    auto& instructions = data["wave"]["instructions"];
    int wave_id = data["wave"]["id"];

    std::string version = "";
    try
    {
        version = std::string(data["version"]);
    }
    catch (...)
    {}

    bool bIsV2 = Version::Get().tool_major == 0;
    bool isIdleInfo = true;

    {
        std::string code_path = path.substr(0, path.rfind("se")) + "code.json";
        std::unique_lock<std::mutex> lk(code_mutex);
        if (code_cache.first != code_path)
        {
            JsonRequest coderequest(code_path);
            try
            {
                if (coderequest.fail() || coderequest.bad()) throw std::exception{};
                auto& code_cache_json = coderequest.data["code"];
                code_cache.first = code_path;
                code_cache.second.clear();

                int i = 0;
                for (auto& c : code_cache_json)
                {
                    std::string cppline;
                    try
                    {
                        cppline = c[3].is_null() ? "" : std::string(c[3]);
                    }
                    catch (...)
                    {}

                    int64_t idle = 0;
                    int64_t stall = 0;
                    if (isIdleInfo) try
                        {
                            idle = int64_t(c[9]);
                            stall = int64_t(c[8]);
                        }
                        catch (...)
                        {
                            isIdleInfo = false;
                        }
                    code_cache.second.push_back(
                        {bIsV2 ? i : int(c[2]),
                         int(c[6]),
                         int64_t(c[5]),
                         int64_t(c[4]),
                         int64_t(c[7]),
                         idle,
                         stall,
                         std::string(c[0]),
                         cppline}
                    );

                    for (auto& [custom_token, custom_type] : Config::CustomTokens())
                        if (code_cache.second.back().line->inst.find(custom_token) == 0)
                            code_cache.second.back().line->custom_type = custom_type;
                    i += 1;
                }
            }
            catch (std::exception& e)
            {
                QWARNING(false, "Could not parse " << code_path, (void) 0);
            }
        }
        code = code_cache.second;
    }

    std::array<int64_t, 4> prev_clock{};
    std::array<int64_t, 4> last_clock{};

    for (auto& inst : instructions)
    {
        int stall = int(inst[2]);

        Token token{};
        token.clock = int64_t(inst[0]);
        token.cycles = std::max(stall, int(inst[3]));
        token.stall = (int16_t) stall;
        token.type = (int16_t) inst[1];
        token.code_line = (int) inst[4];

        while (prev_clock.at(token.slot) == token.clock && last_clock.at(token.slot) > token.clock && token.slot < 3)
            token.slot++;

        prev_clock.at(token.slot) = token.clock;
        last_clock.at(token.slot) = std::max(last_clock.at(token.slot), token.clock + token.cycles);

        tokens.emplace_back(std::move(token));
    }
    tokens.Compile();

    std::vector<int> code_line_map;
    for (size_t i = 0; i < code.size(); i++)
    {
        int index = code.at(i).line->index;

        if (code_line_map.size() <= index) code_line_map.resize(index + 1);
        code_line_map.at(index) = i;
    }

    this->wave_begin = int64_t(data["wave"]["begin"]);
    this->wave_end = int64_t(data["wave"]["end"]);

    int thrownLine = 0;
    int64_t maxtime = 0;
    int64_t prev_token_clock = wave_begin;

    for (auto& token : tokens)
    {
        token.setOverlapped(token.clock < maxtime);
        maxtime = std::max(maxtime, token.clock + token.cycles);

        line_to_clock[token.code_line].push_back(token.clock);
        try
        {
            CodeData& _code = code.at(code_line_map.at(token.code_line));
            if (_code.exec == nullptr) _code.exec = std::make_unique<CodeData::Exec>(wave_id);
            token.setIteration(_code.exec->latency.size());
            _code.exec->clock.push_back(token.clock);
            _code.exec->latency.push_back(token.cycles);
            if (isIdleInfo && token.clock > prev_token_clock)
                _code.exec->idle.push_back(token.clock - prev_token_clock);

            if (_code.line->custom_type > 0) token.type = _code.line->custom_type;

            auto exchange = [&](int exp)
            { return _code.line->type.compare_exchange_strong(exp, token.type, std::memory_order_relaxed); };
            if (!exchange(0) && _code.line->type == 9 && token.type != 9) exchange(9);
        }
        catch (std::out_of_range& e)
        {
            thrownLine = token.code_line;
        }

        prev_token_clock = token.clock + token.cycles;
    }

    for (auto& _code : code)
        if (_code.exec)
        {
            _code.exec->clock.shrink_to_fit();
            _code.exec->latency.shrink_to_fit();
            _code.exec->idle.shrink_to_fit();
        }

    for (auto& [_, line] : line_to_clock) line.shrink_to_fit();

    QWARNING(thrownLine == 0, "Token referenced invalid code line: " << thrownLine, (void) 0);

    {
        int64_t _clock = wave_begin;
        for (auto& time : data["wave"]["timeline"])
        {
            timeline[_clock] = WaveState{_clock, int(time[1]), int(time[0])};
            _clock += int(time[1]);
        }
    }

    for (auto& array : data["wave"]["waitcnt"])
    {
        WaitList list = {array[0], {}};
        for (auto& pair : array[1]) list.sources.push_back({int(pair[0]), int(pair[1])});
        waitcnt.push_back(std::move(list));
    }

    auto& json_wave_info = data["wave"]["info"];

    std::vector<std::string> info_params;
    for (auto& [param, value] : json_wave_info.items())
        if (param.find("_stall") == std::string::npos) info_params.push_back(param);

    for (auto& param : info_params)
    {
        int stall_cnt = 0;
        try
        {
            stall_cnt = int(json_wave_info[param + "_stall"]);
        }
        catch (...)
        {}

        try
        {
            wave_info.push_back({param, int(json_wave_info[param]), stall_cnt});
        }
        catch (std::exception& e)
        {
            std::cout << "Warning: Invalid param " << param << std::endl;
        }
    }

    SetMipN();
}

int64_t WaveInstance::GetMainClock(int code_line, int iteration)
{
    QWARNING(main_wave.get(), "Invalid main wave", return -1);

    try
    {
        auto& clock_array = main_wave->line_to_clock.at(code_line);
        if (iteration > 0 && iteration < clock_array.size()) return clock_array.at(iteration);

        for (int64_t clock : clock_array)
            if (clock >= QCustomScroll::clock_cutoff_start) return clock;
    }
    catch (std::exception& e)
    {}

    return -1;
}

WaveInstance::~WaveInstance() {}
