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

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>

/// Single source of truth for dispatch (kernel) ID and name resolution.
///
/// Lives on DataStore and is populated by emitters (trace_decoder_emitter
/// buildDispatches, json_emitter emitOccupancy) via Register(), then queried
/// by consumers (QGlobalView, DispatchPlotView) via Resolve(). Replaces the
/// earlier dual-map design where buildDispatches wrote DataStore::dispatches
/// and a separately-constructed DispatchResolver tried to rebuild the same
/// PC->kid map from scratch — which it couldn't, because the (cobj_id, addr)
/// keys were thrown away. That bug surfaced as every wave getting a fresh
/// "kernel_N" name even when buildDispatches had already resolved the symbol.
///
class DispatchResolver
{
public:
    DispatchResolver() = default;
    static constexpr uint64_t JsonKernelCodeObjectId = ~uint64_t{0};

    /// Pre-populate a (cobj_id, addr) -> kid mapping with a known symbol name.
    /// Empty `name` triggers the synthetic "kernel_<kid>" fallback.
    int Register(uint64_t code_object_id, uint64_t address, const std::string& name);
    int RegisterJsonKernel(int kid, const std::string& name);

    /// Resolve a PC to a kid. Allocates a new id (with synthetic "kernel_N"
    /// name) on first sight when no Register call covered this PC.
    int Resolve(uint64_t code_object_id, uint64_t address);

    template <typename PcT> int Resolve(const PcT& pc) { return Resolve(pc.code_object_id, pc.address); }

    /// Look up the display name for a kid. Returns "kernel_<kid>" when
    /// nothing is recorded — keeps callers simple.
    std::string Name(int kid) const;

    /// Iterate all (kid, name) pairs. Used by plots that iterate by id range.
    const std::unordered_map<int, std::string>& Names() const { return names_; }

    int Count() const { return next_dispatch_; }

    /// Wipe state. Called from DataStore::clear() so a new load starts fresh.
    void Clear();

private:
    std::map<std::pair<uint64_t, uint64_t>, int> pc_to_dispatch_;
    std::unordered_map<int, std::string> names_;
    int next_dispatch_ = 0;
};
