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

#include "data/waitcnt/analysis.h"

#include <unordered_map>

#define CLASS_BITS (0xF)
#define LGK_BIT    (1 << 4)
#define VMEM_BIT   (1 << 5)

namespace
{
enum MemoryInstType
{
    TYPE_UNCLASSIFIED = 0,
    TYPE_NOT_MEM,
    TYPE_SCALAR,
    TYPE_LDS_MSG,
    TYPE_GLOBAL,
    TYPE_FLAT,
    TYPE_WAITCNT
};

struct CodeLineEntry
{
    const std::string* inst{nullptr};
    int type{0};
};

int classify(const std::string& inst)
{
    if (inst.find("s_waitcnt") == 0)
    {
        int type = TYPE_WAITCNT;
        if (inst.find("lgk") != std::string::npos) type |= LGK_BIT;
        if (inst.find("vmcnt") != std::string::npos) type |= VMEM_BIT;
        return type;
    }

    if (inst.find("v_") == 0) return TYPE_NOT_MEM;

    if (inst.find("s_") == 0)
    {
        if (inst.find("s_load") == 0 || inst.find("s_store") == 0)
            return TYPE_SCALAR;
        else if (inst.find("s_sendmsg") == 0)
            return TYPE_LDS_MSG;
        else
            return TYPE_NOT_MEM;
    }

    if (inst.find("global_") == 0 || inst.find("buffer_") <= 1 || inst.find("scratch_") == 0)
        return TYPE_GLOBAL;
    else if (inst.find("flat_") == 0)
        return TYPE_FLAT;
    else if (inst.find("ds_") == 0)
        return TYPE_LDS_MSG;
    else
        return TYPE_NOT_MEM;
}
} // namespace

std::vector<LineWaitcnt> waitcnt_gfx9(const TokenMap& tokens, const std::vector<CodeData>& code)
{
    std::unordered_map<int, CodeLineEntry> lookup;
    for (size_t i = 0; i < code.size(); i++) lookup[code[i].line->index] = CodeLineEntry{&code[i].line->inst, 0};

    std::vector<LineWaitcnt> mem_unroll;

    MemoryCounter lgkm("lgkmcnt");
    MemoryCounter vmem("vmcnt");
    std::vector<int> vflat_list;

    for (auto& token : tokens)
    {
        auto it = lookup.find(token.code_line);
        if (it == lookup.end() || !it->second.inst || it->second.inst->empty()) continue;

        int& type = it->second.type;
        const std::string& inst_str = *it->second.inst;

        if (type == TYPE_UNCLASSIFIED) type = classify(inst_str);

        int line_number = token.code_line;
        int typeclass = type & CLASS_BITS;

        if (typeclass == TYPE_NOT_MEM) continue;

        if (typeclass == TYPE_LDS_MSG) { lgkm.list.push_back(line_number); }
        else if (typeclass == TYPE_SCALAR)
        {
            lgkm.order = MemoryCounter::Ordering::MEMORY_PARALLEL;
            lgkm.list.push_back(line_number);
        }
        else if (typeclass == TYPE_GLOBAL) { vmem.list.push_back(line_number); }
        else if (typeclass == TYPE_FLAT)
        {
            lgkm.order = MemoryCounter::Ordering::MEMORY_PARALLEL;
            vmem.order = MemoryCounter::Ordering::MEMORY_PARALLEL;
            vflat_list.push_back(line_number);
        }
        else if (typeclass == TYPE_WAITCNT)
        {
            if ((type & LGK_BIT) != 0)
            {
                if (auto joined = lgkm.handle_mem_op(inst_str, vflat_list))
                    mem_unroll.emplace_back(LineWaitcnt{line_number, std::move(*joined)});
            }

            if ((type & VMEM_BIT) != 0)
            {
                if (auto joined = vmem.handle_mem_op(inst_str, vflat_list))
                    mem_unroll.emplace_back(LineWaitcnt{line_number, std::move(*joined)});
            }
        }
    }

    return mem_unroll;
}
