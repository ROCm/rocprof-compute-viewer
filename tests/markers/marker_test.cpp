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

#include <gtest/gtest.h>

#include <climits>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "data/marker_walker.h"
#include "data/sqtt_funcmap_json.h"
#include "json/include/nlohmann/json.hpp"

namespace
{
// Token encoding: (id << 2) | (is_enter << 1) | exit_prev
constexpr uint32_t enterTok(uint32_t id) { return (id << 2) | 0x2u; }
constexpr uint32_t exitTok() { return 0x1u; }
constexpr uint32_t transitionTok(uint32_t id) { return (id << 2) | 0x3u; }
constexpr uint32_t pointTok(uint32_t id) { return (id << 2); }

// A simple table-driven resolver. Unknown ids return found=false.
struct FakeFuncmap
{
    std::map<uint32_t, ResolvedMarker> entries;

    void add(uint32_t id, MarkerKind k, const std::string& name, const std::string& src = "")
    {
        ResolvedMarker r;
        r.found = true;
        r.kind = k;
        r.name = name;
        r.source_loc = src;
        entries[id] = r;
    }

    MarkerResolveFn fn() const
    {
        return [this](uint32_t id, int64_t /*time*/) -> ResolvedMarker
        {
            auto it = entries.find(id);
            if (it != entries.end()) return it->second;
            ResolvedMarker out;
            out.metadata_available = true;
            return out;
        };
    }
};

// Convenience: walk and return spans/diags by value.
struct WalkResult
{
    std::vector<MarkerSpan>       spans;
    std::vector<MarkerDiagnostic> diags;
};

WalkResult run(const std::vector<MarkerInputRecord>& recs, const MarkerResolveFn& resolver)
{
    WalkResult r;
    walkMarkerStream(recs, HWID{0, 1, 2, 3}, resolver, &r.spans, &r.diags);
    return r;
}

int countDiags(const std::vector<MarkerDiagnostic>& diags, MarkerDiagnostic::Severity sev)
{
    int n = 0;
    for (const auto& d : diags)
        if (d.severity == sev) ++n;
    return n;
}

std::filesystem::path tempJsonPath(const std::string& name)
{
    return std::filesystem::temp_directory_path() / ("rcv_marker_test_" + name + ".json");
}

void writeFile(const std::filesystem::path& path, const std::string& contents)
{
    std::ofstream out(path);
    out << contents;
}
} // namespace

TEST(SqttFuncmapJson, ParsesScopedTableRows)
{
    nlohmann::json rows = nlohmann::json::array();
    rows.push_back(nlohmann::json::array({1, 7, "F", "co1_func", "kernel.hip:42"}));
    rows.push_back(nlohmann::json::array({2, 7, "U", "co2_scope"}));
    rows.push_back(nlohmann::json::array({1, 8, "point", "checkpoint"}));

    SqttFuncmapJson fm = SqttFuncmapJson::FromJson({{"sqtt_funcmap", rows}});

    EXPECT_TRUE(fm.diagnostics().empty());
    ASSERT_FALSE(fm.empty());

    ResolvedMarker co1 = fm.Resolve(7, 1);
    ASSERT_TRUE(co1.found);
    EXPECT_EQ(co1.kind, MarkerKind::Function);
    EXPECT_EQ(co1.name, "co1_func");
    EXPECT_EQ(co1.source_loc, "kernel.hip:42");

    ResolvedMarker co2 = fm.Resolve(7, 2);
    ASSERT_TRUE(co2.found);
    EXPECT_EQ(co2.kind, MarkerKind::UserScope);
    EXPECT_EQ(co2.name, "co2_scope");

    EXPECT_FALSE(fm.Resolve(7, 0).found);
    EXPECT_FALSE(fm.Resolve(7, 3).found);
}

TEST(SqttFuncmapJson, KernelRowsMapNamesToCodeObjects)
{
    nlohmann::json rows = nlohmann::json::array();
    rows.push_back(nlohmann::json::array({10, 0, "K", "kernel_a"}));
    rows.push_back(nlohmann::json::array({11, 0, "K", "kernel_b"}));
    rows.push_back(nlohmann::json::array({12, 0, "K", "ambiguous"}));
    rows.push_back(nlohmann::json::array({13, 0, "K", "ambiguous"}));

    SqttFuncmapJson fm = SqttFuncmapJson::FromJson({{"sqtt_funcmap", rows}});

    EXPECT_EQ(fm.CodeobjForKernelName("kernel_a"), 10u);
    EXPECT_EQ(fm.CodeobjForKernelName("kernel_b"), 11u);
    EXPECT_EQ(fm.CodeobjForKernelName("missing"), 0u);
    EXPECT_EQ(fm.CodeobjForKernelName("ambiguous"), 0u);
}

TEST(SqttFuncmapJson, SkipsBadRows)
{
    nlohmann::json rows = nlohmann::json::array();
    rows.push_back(nlohmann::json::array({1, "bad_id", "F", "bad"}));
    rows.push_back(nlohmann::json::array({-1, 2, "F", "bad"}));
    rows.push_back(nlohmann::json::array({1, 3, "unknown", "bad"}));
    rows.push_back(nlohmann::json::array({1, 4, "F", ""}));
    rows.push_back(nlohmann::json::array({1, 5, "F"}));
    rows.push_back({{"not", "a row"}});
    rows.push_back(nlohmann::json::array({1, 6, "P", "good"}));

    SqttFuncmapJson fm = SqttFuncmapJson::FromJson({{"sqtt_funcmap", rows}});

    EXPECT_GE(fm.diagnostics().size(), 6u);
    EXPECT_FALSE(fm.Resolve(2, 1).found);
    EXPECT_FALSE(fm.Resolve(3, 1).found);
    EXPECT_FALSE(fm.Resolve(4, 1).found);

    ResolvedMarker good = fm.Resolve(6, 1);
    ASSERT_TRUE(good.found);
    EXPECT_EQ(good.kind, MarkerKind::Point);
    EXPECT_EQ(good.name, "good");
}

TEST(SqttFuncmapJson, MissingAndMalformedFilesReturnEmpty)
{
    const auto missing = tempJsonPath("missing");
    std::filesystem::remove(missing);

    SqttFuncmapJson missing_map = SqttFuncmapJson::LoadFromCodeJson(missing.string());
    EXPECT_TRUE(missing_map.empty());
    EXPECT_TRUE(missing_map.diagnostics().empty());

    const auto malformed = tempJsonPath("malformed");
    writeFile(malformed, "{");

    SqttFuncmapJson malformed_map = SqttFuncmapJson::LoadFromCodeJson(malformed.string());
    EXPECT_TRUE(malformed_map.empty());
    EXPECT_FALSE(malformed_map.diagnostics().empty());

    std::filesystem::remove(malformed);
}

TEST(SqttFuncmapJson, FeedsMarkerWalkerWithScopedIds)
{
    nlohmann::json rows = nlohmann::json::array();
    rows.push_back(nlohmann::json::array({1, 7, "F", "first"}));
    rows.push_back(nlohmann::json::array({2, 7, "F", "second"}));

    SqttFuncmapJson fm = SqttFuncmapJson::FromJson({{"sqtt_funcmap", rows}});

    std::vector<MarkerInputRecord> recs = {
        {10, enterTok(7)},
        {20, exitTok()},
    };

    auto r = run(
        recs,
        [&](uint32_t id, int64_t /*time*/) -> ResolvedMarker { return fm.Resolve(id, 2); }
    );

    ASSERT_EQ(r.spans.size(), 1u);
    EXPECT_EQ(r.spans[0].name, "second");
    EXPECT_EQ(r.diags.size(), 0u);
}

// ============================================================================
// Well-formed enter/exit pair
// ============================================================================
TEST(MarkerWalker, EnterExitPairProducesClosedSpan)
{
    FakeFuncmap fm;
    fm.add(7, MarkerKind::Function, "foo", "foo.cpp:42");

    std::vector<MarkerInputRecord> recs = {
        {100, enterTok(7)},
        {200, exitTok()},
    };

    auto r = run(recs, fm.fn());
    ASSERT_EQ(r.spans.size(), 1u);
    EXPECT_EQ(r.spans[0].marker_id, 7u);
    EXPECT_EQ(r.spans[0].kind, MarkerKind::Function);
    EXPECT_EQ(r.spans[0].name, "foo");
    EXPECT_EQ(r.spans[0].source_loc, "foo.cpp:42");
    EXPECT_EQ(r.spans[0].enter_time, 100);
    EXPECT_EQ(r.spans[0].exit_time, 200);
    EXPECT_EQ(r.spans[0].depth, 0);
    EXPECT_FALSE(r.spans[0].is_point);
    EXPECT_FALSE(r.spans[0].is_open);
    EXPECT_EQ(r.diags.size(), 0u);
}

// ============================================================================
// Nested enter/exit — depth tracking
// ============================================================================
TEST(MarkerWalker, NestedEntersTrackDepth)
{
    FakeFuncmap fm;
    fm.add(1, MarkerKind::Function, "outer");
    fm.add(2, MarkerKind::UserScope, "inner");

    std::vector<MarkerInputRecord> recs = {
        {10, enterTok(1)},
        {20, enterTok(2)},
        {30, exitTok()}, // closes inner
        {40, exitTok()}, // closes outer
    };

    auto r = run(recs, fm.fn());
    ASSERT_EQ(r.spans.size(), 2u);

    // Inner span pops first.
    EXPECT_EQ(r.spans[0].name, "inner");
    EXPECT_EQ(r.spans[0].depth, 1);
    EXPECT_EQ(r.spans[0].enter_time, 20);
    EXPECT_EQ(r.spans[0].exit_time, 30);

    EXPECT_EQ(r.spans[1].name, "outer");
    EXPECT_EQ(r.spans[1].depth, 0);
    EXPECT_EQ(r.spans[1].enter_time, 10);
    EXPECT_EQ(r.spans[1].exit_time, 40);

    EXPECT_EQ(r.diags.size(), 0u);
}

// ============================================================================
// Transition (exit_prev | is_enter): close one, open another, same depth
// ============================================================================
TEST(MarkerWalker, TransitionClosesAndReopensAtSameDepth)
{
    FakeFuncmap fm;
    fm.add(1, MarkerKind::Function, "a");
    fm.add(2, MarkerKind::Function, "b");

    std::vector<MarkerInputRecord> recs = {
        {10, enterTok(1)},
        {20, transitionTok(2)},
        {30, exitTok()},
    };

    auto r = run(recs, fm.fn());
    ASSERT_EQ(r.spans.size(), 2u);

    // Closed by transition first.
    EXPECT_EQ(r.spans[0].name, "a");
    EXPECT_EQ(r.spans[0].enter_time, 10);
    EXPECT_EQ(r.spans[0].exit_time, 20);
    EXPECT_EQ(r.spans[0].depth, 0);

    // Reopened scope at same depth.
    EXPECT_EQ(r.spans[1].name, "b");
    EXPECT_EQ(r.spans[1].enter_time, 20);
    EXPECT_EQ(r.spans[1].exit_time, 30);
    EXPECT_EQ(r.spans[1].depth, 0);

    EXPECT_EQ(r.diags.size(), 0u);
}

// ============================================================================
// Point marker — instantaneous span at parent depth
// ============================================================================
TEST(MarkerWalker, PointMarkerAtParentDepth)
{
    FakeFuncmap fm;
    fm.add(1, MarkerKind::Function, "outer");
    fm.add(99, MarkerKind::Point, "checkpoint", "x.cpp:9");

    std::vector<MarkerInputRecord> recs = {
        {10, enterTok(1)},
        {15, pointTok(99)},
        {20, exitTok()},
    };

    auto r = run(recs, fm.fn());
    ASSERT_EQ(r.spans.size(), 2u);

    // Point appears in stream order: emitted as it's seen.
    EXPECT_EQ(r.spans[0].name, "checkpoint");
    EXPECT_TRUE(r.spans[0].is_point);
    EXPECT_EQ(r.spans[0].enter_time, 15);
    EXPECT_EQ(r.spans[0].exit_time, 15);
    EXPECT_EQ(r.spans[0].depth, 1); // parent stack depth
    EXPECT_EQ(r.spans[0].kind, MarkerKind::Point);

    EXPECT_EQ(r.spans[1].name, "outer");
    EXPECT_FALSE(r.spans[1].is_point);

    EXPECT_EQ(r.diags.size(), 0u);
}

// ============================================================================
// Orphan exit → Warning, no span
// ============================================================================
TEST(MarkerWalker, OrphanExitEmitsWarningAndNoSpan)
{
    FakeFuncmap fm;
    std::vector<MarkerInputRecord> recs = {
        {10, exitTok()},
    };

    auto r = run(recs, fm.fn());
    EXPECT_EQ(r.spans.size(), 0u);
    EXPECT_EQ(countDiags(r.diags, MarkerDiagnostic::Severity::Warning), 1);
    EXPECT_NE(r.diags[0].message.find("orphan exit"), std::string::npos);
}

// ============================================================================
// Open scope at end → Info diag + is_open span with INT64_MAX exit
// ============================================================================
TEST(MarkerWalker, OpenScopeAtEndOfStream)
{
    FakeFuncmap fm;
    fm.add(5, MarkerKind::Kernel, "k");

    std::vector<MarkerInputRecord> recs = {
        {100, enterTok(5)},
    };

    auto r = run(recs, fm.fn());
    ASSERT_EQ(r.spans.size(), 1u);
    EXPECT_TRUE(r.spans[0].is_open);
    EXPECT_EQ(r.spans[0].enter_time, 100);
    EXPECT_EQ(r.spans[0].exit_time, INT64_MAX);
    EXPECT_EQ(r.spans[0].name, "k");

    EXPECT_EQ(countDiags(r.diags, MarkerDiagnostic::Severity::Info), 1);
}

// ============================================================================
// Unknown ID → Warning + Unknown-kind span (still pushed/closed normally)
// ============================================================================
TEST(MarkerWalker, UnknownEnterIdWarningAndUnknownKind)
{
    FakeFuncmap fm; // empty funcmap

    std::vector<MarkerInputRecord> recs = {
        {10, enterTok(42)},
        {20, exitTok()},
    };

    auto r = run(recs, fm.fn());
    ASSERT_EQ(r.spans.size(), 1u);
    EXPECT_EQ(r.spans[0].kind, MarkerKind::Unknown);
    EXPECT_EQ(r.spans[0].name, "");
    EXPECT_EQ(r.spans[0].marker_id, 42u);
    EXPECT_EQ(r.spans[0].enter_time, 10);
    EXPECT_EQ(r.spans[0].exit_time, 20);

    EXPECT_EQ(countDiags(r.diags, MarkerDiagnostic::Severity::Warning), 1);
    EXPECT_NE(r.diags[0].message.find("unknown marker ID 42"), std::string::npos);
}

TEST(MarkerWalker, UnknownPointIdWarning)
{
    FakeFuncmap fm;
    std::vector<MarkerInputRecord> recs = {
        {7, pointTok(123)},
    };

    auto r = run(recs, fm.fn());
    ASSERT_EQ(r.spans.size(), 1u);
    EXPECT_TRUE(r.spans[0].is_point);
    EXPECT_EQ(r.spans[0].kind, MarkerKind::Unknown);
    EXPECT_EQ(r.spans[0].marker_id, 123u);
    EXPECT_EQ(countDiags(r.diags, MarkerDiagnostic::Severity::Warning), 1);
}

TEST(MarkerWalker, MissingMarkerMetadataSkipsShaderdataWithoutWarnings)
{
    std::vector<MarkerInputRecord> recs = {
        {5, pointTok(123)},
        {10, enterTok(42)},
        {20, exitTok()},
        {30, transitionTok(7)},
        {40, exitTok()},
    };

    auto r = run(
        recs,
        [](uint32_t /*id*/, int64_t /*time*/) -> ResolvedMarker
        {
            return {};
        }
    );

    EXPECT_TRUE(r.spans.empty());
    EXPECT_TRUE(r.diags.empty());
}

// ============================================================================
// Transition from empty stack → Warning, but still pushes the new entry
// ============================================================================
TEST(MarkerWalker, TransitionFromEmptyStackWarnsButPushes)
{
    FakeFuncmap fm;
    fm.add(2, MarkerKind::Function, "b");

    std::vector<MarkerInputRecord> recs = {
        {10, transitionTok(2)},
        {20, exitTok()},
    };

    auto r = run(recs, fm.fn());
    ASSERT_EQ(r.spans.size(), 1u);
    EXPECT_EQ(r.spans[0].name, "b");
    EXPECT_EQ(r.spans[0].enter_time, 10);
    EXPECT_EQ(r.spans[0].exit_time, 20);
    EXPECT_EQ(r.spans[0].depth, 0);

    EXPECT_EQ(countDiags(r.diags, MarkerDiagnostic::Severity::Warning), 1);
    EXPECT_NE(r.diags[0].message.find("transition marker with empty stack"), std::string::npos);
}

// ============================================================================
// Value 0 decodes as a point marker with marker_id 0.
// ============================================================================
TEST(MarkerWalker, ZeroValueIsMarkerIdZeroPoint)
{
    FakeFuncmap fm;
    fm.add(0, MarkerKind::Point, "zero");
    fm.add(1, MarkerKind::Function, "f");

    std::vector<MarkerInputRecord> recs = {
        {1, 0u},
        {10, enterTok(1)},
        {15, 0u},
        {20, exitTok()},
        {21, 0u},
    };

    auto r = run(recs, fm.fn());
    ASSERT_EQ(r.spans.size(), 4u);
    EXPECT_EQ(r.spans[0].marker_id, 0u);
    EXPECT_EQ(r.spans[0].name, "zero");
    EXPECT_TRUE(r.spans[0].is_point);
    EXPECT_EQ(r.spans[0].depth, 0);
    EXPECT_EQ(r.spans[1].marker_id, 0u);
    EXPECT_EQ(r.spans[1].name, "zero");
    EXPECT_TRUE(r.spans[1].is_point);
    EXPECT_EQ(r.spans[1].depth, 1);
    EXPECT_EQ(r.spans[2].name, "f");
    EXPECT_EQ(r.spans[3].marker_id, 0u);
    EXPECT_EQ(r.spans[3].name, "zero");
    EXPECT_TRUE(r.spans[3].is_point);
    EXPECT_EQ(r.diags.size(), 0u);
}

// ============================================================================
// Empty input — no work, no diagnostics
// ============================================================================
TEST(MarkerWalker, EmptyInput)
{
    FakeFuncmap fm;
    auto r = run({}, fm.fn());
    EXPECT_EQ(r.spans.size(), 0u);
    EXPECT_EQ(r.diags.size(), 0u);
}

// ============================================================================
// Multiple opens at end → one is_open span per remaining stack entry
// ============================================================================
TEST(MarkerWalker, MultipleOpenScopesAtEnd)
{
    FakeFuncmap fm;
    fm.add(1, MarkerKind::Function, "outer");
    fm.add(2, MarkerKind::Function, "inner");

    std::vector<MarkerInputRecord> recs = {
        {10, enterTok(1)},
        {20, enterTok(2)},
    };

    auto r = run(recs, fm.fn());
    ASSERT_EQ(r.spans.size(), 2u);
    // Both open; LIFO order — top of stack drained first.
    EXPECT_EQ(r.spans[0].name, "inner");
    EXPECT_TRUE(r.spans[0].is_open);
    EXPECT_EQ(r.spans[0].depth, 1);
    EXPECT_EQ(r.spans[1].name, "outer");
    EXPECT_TRUE(r.spans[1].is_open);
    EXPECT_EQ(r.spans[1].depth, 0);

    EXPECT_EQ(countDiags(r.diags, MarkerDiagnostic::Severity::Info), 2);
}
