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

#include "data/marker_walker.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <sstream>
#include <utility>

// <cxxabi.h> is provided by libstdc++ (Linux/MinGW) and libc++/libc++abi (macOS).
// MSVC has no equivalent demangler in its standard library; on that toolchain
// we fall through to returning the mangled name unchanged. AMD GPU kernel names
// are mangled regardless of the host OS, so demangling matters on Linux
// and macOS in particular.
#if defined(__GNUG__) || defined(__clang__)
#    define RCV_HAS_CXA_DEMANGLE 1
#    include <cxxabi.h>
#else
#    define RCV_HAS_CXA_DEMANGLE 0
#endif

namespace
{
struct StackEntry
{
    uint32_t marker_id;
    MarkerKind kind;
    std::string name;
    std::string source_loc;
    int64_t enter_time;
    int depth;
};

std::string locTag(int se, int cu, int simd, int slot, int64_t time)
{
    std::ostringstream ss;
    ss << "se=" << se << ",cu=" << cu << ",simd=" << simd << ",slot=" << slot << ",time=" << time;
    return ss.str();
}

/// Best-effort C++ demangle. Pass-through on:
///   - non-mangled names (no "_Z" prefix) — common case, allocation-free
///   - toolchains without <cxxabi.h> (MSVC)
///   - any libstdc++/libc++abi failure (status != 0)
/// Takes the string by value so a `maybeDemangle(std::move(x))` call site
/// is move-only in the pass-through path.
std::string maybeDemangle(std::string name)
{
#if RCV_HAS_CXA_DEMANGLE
    // Mangled symbols start with "_Z" (or "_GLOBAL_" for some
    // initializer/destructor names). Cheap two-byte check before we touch
    // the demangler — most marker names are plain identifiers.
    if (name.size() < 3) return name;
    const bool mangled = (name[0] == '_' && name[1] == 'Z');
    if (!mangled) return name;

    int status = 0;
    // length=nullptr, output_buffer=nullptr → demangler malloc's a fresh buffer.
    // On success the returned pointer must be free()'d (not delete'd, not
    // delete[]'d); on failure the spec says the return is NULL, but defensively
    // free anything non-NULL to survive an out-of-spec implementation.
    char* demangled = abi::__cxa_demangle(name.c_str(), nullptr, nullptr, &status);
    if (status == 0 && demangled != nullptr)
    {
        std::string out(demangled);
        std::free(demangled);
        return out;
    }
    if (demangled != nullptr) std::free(demangled);
#endif
    return name;
}
} // namespace

void walkMarkerStream(
    const std::vector<MarkerInputRecord>& records,
    int se,
    int cu,
    int simd,
    int slot,
    const MarkerResolveFn& resolver,
    std::vector<MarkerSpan>* out_spans,
    std::vector<MarkerDiagnostic>* out_diags
)
{
    if (!out_spans || !out_diags) return;
    if (records.empty()) return;

    out_spans->reserve(out_spans->size() + records.size());
    std::vector<StackEntry> stack;

    auto resolve_id = [&](uint32_t id, int64_t time) -> ResolvedMarker
    {
        if (!resolver) return {};
        ResolvedMarker out = resolver(id, time);
        if (out.found)
        {
            out.metadata_available = true;
            if (!out.name.empty()) out.name = maybeDemangle(std::move(out.name));
        }
        return out;
    };

    for (const auto& rec : records)
    {
        const uint32_t id = rec.value >> 2;
        const bool is_enter = (rec.value >> 1) & 1u;
        const bool exit_prev = rec.value & 1u;

        // Pure exit (0x1): exit_prev set, is_enter clear.
        if (exit_prev && !is_enter)
        {
            if (stack.empty())
            {
                if (!resolve_id(0, rec.time).metadata_available) continue;
                out_diags->push_back(
                    {MarkerDiagnostic::Severity::Warning,
                     "orphan exit marker at " + locTag(se, cu, simd, slot, rec.time)}
                );
                continue;
            }
            StackEntry& top = stack.back();
            MarkerSpan span;
            span.enter_time = top.enter_time;
            span.exit_time = rec.time;
            span.marker_id = top.marker_id;
            span.kind = top.kind;
            span.depth = top.depth;
            span.is_point = false;
            span.is_open = false;
            span.name = std::move(top.name);
            span.source_loc = std::move(top.source_loc);
            out_spans->push_back(std::move(span));
            stack.pop_back();
            continue;
        }

        // Transition (0x3): exit prior scope and immediately enter a new one.
        if (exit_prev && is_enter)
        {
            ResolvedMarker entry = resolve_id(id, rec.time);
            if (!entry.metadata_available && stack.empty()) continue;

            if (stack.empty())
            {
                out_diags->push_back(
                    {MarkerDiagnostic::Severity::Warning,
                     "transition marker with empty stack at " + locTag(se, cu, simd, slot, rec.time)}
                );
            }
            else
            {
                StackEntry& top = stack.back();
                MarkerSpan span;
                span.enter_time = top.enter_time;
                span.exit_time = rec.time;
                span.marker_id = top.marker_id;
                span.kind = top.kind;
                span.depth = top.depth;
                span.is_point = false;
                span.is_open = false;
                span.name = std::move(top.name);
                span.source_loc = std::move(top.source_loc);
                out_spans->push_back(std::move(span));
                stack.pop_back();
            }

            if (!entry.metadata_available) continue;

            StackEntry e;
            e.marker_id = id;
            e.enter_time = rec.time;
            e.depth = static_cast<int>(stack.size());
            if (entry.found)
            {
                e.kind = entry.kind;
                e.name = std::move(entry.name);
                e.source_loc = std::move(entry.source_loc);
            }
            else
            {
                e.kind = MarkerKind::Unknown;
                out_diags->push_back(
                    {MarkerDiagnostic::Severity::Warning,
                     "unknown marker ID " + std::to_string(id) + " (transition) at " +
                         locTag(se, cu, simd, slot, rec.time)}
                );
            }
            stack.push_back(std::move(e));
            continue;
        }

        // Pure enter (0x2).
        if (is_enter)
        {
            ResolvedMarker entry = resolve_id(id, rec.time);
            if (!entry.metadata_available) continue;

            StackEntry e;
            e.marker_id = id;
            e.enter_time = rec.time;
            e.depth = static_cast<int>(stack.size());
            if (entry.found)
            {
                e.kind = entry.kind;
                e.name = std::move(entry.name);
                e.source_loc = std::move(entry.source_loc);
            }
            else
            {
                e.kind = MarkerKind::Unknown;
                out_diags->push_back(
                    {MarkerDiagnostic::Severity::Warning,
                     "unknown marker ID " + std::to_string(id) + " (enter) at " + locTag(se, cu, simd, slot, rec.time)}
                );
            }
            stack.push_back(std::move(e));
            continue;
        }

        // Point marker (0x0 with id != 0).
        ResolvedMarker entry = resolve_id(id, rec.time);
        if (!entry.metadata_available) continue;

        MarkerSpan span;
        span.enter_time = rec.time;
        span.exit_time = rec.time;
        span.marker_id = id;
        span.depth = static_cast<int>(stack.size());
        span.is_point = true;
        span.is_open = false;
        if (entry.found)
        {
            span.kind = entry.kind;
            span.name = std::move(entry.name);
            span.source_loc = std::move(entry.source_loc);
        }
        else
        {
            span.kind = MarkerKind::Unknown;
            out_diags->push_back(
                {MarkerDiagnostic::Severity::Warning,
                 "unknown marker ID " + std::to_string(id) + " (point) at " + locTag(se, cu, simd, slot, rec.time)}
            );
        }
        out_spans->push_back(std::move(span));
    }

    // Trace ended with frames still open.
    while (!stack.empty())
    {
        StackEntry& top = stack.back();
        MarkerSpan span;
        span.enter_time = top.enter_time;
        span.exit_time = INT64_MAX;
        span.marker_id = top.marker_id;
        span.kind = top.kind;
        span.depth = top.depth;
        span.is_point = false;
        span.is_open = true;
        span.name = std::move(top.name);
        span.source_loc = std::move(top.source_loc);
        out_spans->push_back(std::move(span));
        out_diags->push_back(
            {MarkerDiagnostic::Severity::Info,
             "open scope at trace end: " +
                 (out_spans->back().name.empty() ? std::string{"<unknown>"} : out_spans->back().name) +
                 " at se=" + std::to_string(se) + ",cu=" + std::to_string(cu) + ",simd=" + std::to_string(simd) +
                 ",slot=" + std::to_string(slot)}
        );
        stack.pop_back();
    }
}
