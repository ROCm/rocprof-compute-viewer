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

#include "data/dispatch_resolver.h"

#include <cstdio>

int DispatchResolver::Register(uint64_t code_object_id, uint64_t address, const std::string& name)
{
    auto pc_key = std::make_pair(code_object_id, address);
    auto it = pc_to_dispatch_.find(pc_key);
    int kid;
    if (it != pc_to_dispatch_.end()) { kid = it->second; }
    else
    {
        kid = next_dispatch_++;
        pc_to_dispatch_[pc_key] = kid;
    }

    if (!name.empty())
        names_[kid] = name;
    else if (!names_.count(kid))
        names_[kid] = "kernel_" + std::to_string(kid);
    return kid;
}

int DispatchResolver::RegisterJsonKernel(int kid, const std::string& name)
{
    pc_to_dispatch_[std::make_pair(JsonKernelCodeObjectId, static_cast<uint64_t>(kid))] = kid;
    if (!name.empty())
        names_[kid] = name;
    else if (!names_.count(kid))
        names_[kid] = "kernel_" + std::to_string(kid);
    if (kid + 1 > next_dispatch_) next_dispatch_ = kid + 1;
    return kid;
}

int DispatchResolver::Resolve(uint64_t code_object_id, uint64_t address)
{
    auto pc_key = std::make_pair(code_object_id, address);
    auto it = pc_to_dispatch_.find(pc_key);
    if (it != pc_to_dispatch_.end()) return it->second;

    // First sight without a prior Register — manufacture a synthetic name.
    int kid = next_dispatch_++;
    pc_to_dispatch_[pc_key] = kid;
    names_[kid] = "kernel_" + std::to_string(kid);
    return kid;
}

std::string DispatchResolver::Name(int kid) const
{
    auto it = names_.find(kid);
    if (it != names_.end()) return it->second;
    return "kernel_" + std::to_string(kid);
}

void DispatchResolver::Clear()
{
    pc_to_dispatch_.clear();
    names_.clear();
    next_dispatch_ = 1;
}
